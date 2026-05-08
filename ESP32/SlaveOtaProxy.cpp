// SlaveOtaProxy.cpp — Implementare proxy OTA Slave (Master side).
#include "SlaveOtaProxy.h"
#include "CrcUtil.h"
#include "HiveMqCert.h"

#include <esp_task_wdt.h>
#include <Ethernet.h>
#include <WiFiClient.h>
#include <memory>
#include "Config.h"

bool SlaveOtaProxy::_isUrlAllowed(const char* url) {
    return (strstr(url, OTA_URL_WHITELIST1) == url ||
            strstr(url, OTA_URL_WHITELIST2) == url);
}

// Trimite linia "<cmd>*<crc>\n" si asteapta "OK*<crc>\n" inapoi.
bool SlaveOtaProxy::_waitOk(HardwareSerial& s, uint32_t timeoutMs) {
    char buf[64];
    size_t n = 0;
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (s.available()) {
            char c = (char)s.read();
            if (c == '\n') {
                buf[n] = '\0';
                if (Crc::validate(buf)) {
                    Crc::stripCrc(buf);
                    return strncmp(buf, "OK", 2) == 0;
                }
                return false;
            }
            if (c != '\r' && n < sizeof(buf) - 1) buf[n++] = c;
        } else {
            yield();
        }
    }
    return false;
}

// Trimite cmd UART_BAUD_HIGH/LOW + CRC, asteapta OK, schimba baud.
bool SlaveOtaProxy::_switchBaud(HardwareSerial& s, uint32_t /*currentBaud*/, uint32_t newBaud) {
    const char* cmd = (newBaud > SLAVE_UART_BAUD) ? "UART_BAUD_HIGH" : "UART_BAUD_LOW";
    char buf[32];
    strncpy(buf, cmd, sizeof(buf) - 6);
    buf[sizeof(buf) - 6] = '\0';
    Crc::appendCrc(buf, sizeof(buf));
    s.print(buf);
    s.print('\n');
    s.flush();

    if (!_waitOk(s, 500)) return false;
    delay(60);   // Slave avea delay(50) dupa OK; un mic margin
    s.flush();
    s.updateBaudRate(newBaud);
    return true;
}

SlaveOtaResult SlaveOtaProxy::perform(const char* url, const char* sha256,
                                      HardwareSerial& slaveSerial,
                                      SlaveOtaProgressCb progress) {
    Serial.printf("[SlaveOTA] Start: %s\n", url);

    if (!_isUrlAllowed(url)) {
        Serial.println("[SlaveOTA] URL not whitelisted");
        return SOTA_ERR_URL_INVALID;
    }
    if (!sha256 || strlen(sha256) != 64) {
        Serial.println("[SlaveOTA] SHA invalid");
        return SOTA_ERR_URL_INVALID;
    }
    if (strncmp(url, "https://", 8) != 0) {
        return SOTA_ERR_URL_INVALID;
    }

    // Parse host + path
    const char* hostStart = url + 8;
    const char* pathStart = strchr(hostStart, '/');
    if (!pathStart) return SOTA_ERR_URL_INVALID;

    char host[128];
    size_t hostLen = (size_t)(pathStart - hostStart);
    if (hostLen >= sizeof(host)) return SOTA_ERR_URL_INVALID;
    memcpy(host, hostStart, hostLen);
    host[hostLen] = '\0';

    // HTTPS GET via dynamic client + SSLClient (re-foloseste TrustAnchors HiveMQ)
    std::unique_ptr<Client> base;
    if (g_wifiAvailable) {
        base.reset(new WiFiClient());
    } else {
        base.reset(new EthernetClient());
    }
    SSLClient ssl(*base, TrustAnchors, TrustAnchors_NUM, A0);
    HttpClient http(ssl, host, 443);
    http.setHttpResponseTimeout(30000);

    int err = http.get(pathStart);
    if (err != 0) {
        Serial.printf("[SlaveOTA] HTTP GET err=%d\n", err);
        return SOTA_ERR_HTTP;
    }
    int status = http.responseStatusCode();

    // Manual redirect handling (max 5 hops)
    int hops = 0;
    while ((status == 301 || status == 302) && hops++ < 5) {
        String location;
        while (http.headerAvailable()) {
            String hn = http.readHeaderName();
            String hv = http.readHeaderValue();
            if (hn.equalsIgnoreCase("Location")) { location = hv; break; }
        }
        http.stop();
        if (location.length() == 0) return SOTA_ERR_HTTP;

        // Re-parse host + path din noul URL
        if (!location.startsWith("https://")) return SOTA_ERR_HTTP;
        if (!_isUrlAllowed(location.c_str())) return SOTA_ERR_URL_INVALID;
        const char* h2 = location.c_str() + 8;
        const char* p2 = strchr(h2, '/');
        if (!p2) return SOTA_ERR_URL_INVALID;
        size_t hl2 = (size_t)(p2 - h2);
        if (hl2 >= sizeof(host)) return SOTA_ERR_URL_INVALID;
        memcpy(host, h2, hl2); host[hl2] = '\0';

        if (http.get(p2) != 0) return SOTA_ERR_HTTP;
        status = http.responseStatusCode();
    }
    if (status != 200) {
        Serial.printf("[SlaveOTA] HTTP %d\n", status);
        http.stop();
        return SOTA_ERR_HTTP;
    }

    long contentLength = http.contentLength();
    if (contentLength < 100 * 1024 || contentLength > 1500 * 1024) {
        Serial.printf("[SlaveOTA] size %ld out of range\n", contentLength);
        http.stop();
        return SOTA_ERR_SIZE;
    }
    Serial.printf("[SlaveOTA] FW size: %ld bytes\n", contentLength);

    // Switch baud rate inalt
    if (!_switchBaud(slaveSerial, SLAVE_UART_BAUD, OTA_UART_BAUD)) {
        Serial.println("[SlaveOTA] Baud switch HIGH failed");
        http.stop();
        return SOTA_ERR_BAUD_SWITCH;
    }

    // OTA_BEGIN <size> <sha>
    char beginCmd[128];
    snprintf(beginCmd, sizeof(beginCmd) - 6, "OTA_BEGIN %ld %s", contentLength, sha256);
    Crc::appendCrc(beginCmd, sizeof(beginCmd));
    slaveSerial.print(beginCmd);
    slaveSerial.print('\n');
    slaveSerial.flush();
    if (!_waitOk(slaveSerial, 5000)) {
        Serial.println("[SlaveOTA] OTA_BEGIN rejected");
        _switchBaud(slaveSerial, OTA_UART_BAUD, SLAVE_UART_BAUD);
        http.stop();
        return SOTA_ERR_BEGIN_REJECT;
    }

    // Stream chunks
    uint8_t chunk[1024];
    long sent = 0;
    while (sent < contentLength) {
        esp_task_wdt_reset();
        long want = min((long)sizeof(chunk), contentLength - sent);

        // Wait until enough bytes available (or timeout)
        const uint32_t startWait = millis();
        long avail = 0;
        while ((avail = http.available()) < want) {
            if (millis() - startWait > 5000) break;
            if (!http.connected() && avail < want) break;
            delay(2);
        }
        if (avail < 1) {
            Serial.println("[SlaveOTA] HTTP stream stalled");
            slaveSerial.print("OTA_ABORT");
            char abortCmd[16] = "OTA_ABORT";
            Crc::appendCrc(abortCmd, sizeof(abortCmd));
            slaveSerial.print(abortCmd); slaveSerial.print('\n');
            _switchBaud(slaveSerial, OTA_UART_BAUD, SLAVE_UART_BAUD);
            http.stop();
            return SOTA_ERR_HTTP;
        }
        long thisChunk = min(want, avail);
        long got = http.read(chunk, thisChunk);
        if (got <= 0) break;

        // OTA_CHUNK <len>\n
        char hdr[32];
        snprintf(hdr, sizeof(hdr) - 6, "OTA_CHUNK %ld", got);
        Crc::appendCrc(hdr, sizeof(hdr));
        slaveSerial.print(hdr);
        slaveSerial.print('\n');
        slaveSerial.write(chunk, got);
        slaveSerial.flush();

        // Asteapta "OK <len>*XXXX\n"
        if (!_waitOk(slaveSerial, 3000)) {
            Serial.printf("[SlaveOTA] CHUNK reject at %ld\n", sent);
            _switchBaud(slaveSerial, OTA_UART_BAUD, SLAVE_UART_BAUD);
            http.stop();
            return SOTA_ERR_CHUNK_REJECT;
        }
        sent += got;
        if (progress) progress((uint32_t)sent, (uint32_t)contentLength);
    }
    http.stop();

    // OTA_END — Slave reseteaza la succes, deci nu mai vine OK
    char endCmd[16] = "OTA_END";
    Crc::appendCrc(endCmd, sizeof(endCmd));
    slaveSerial.print(endCmd);
    slaveSerial.print('\n');
    slaveSerial.flush();

    // Asteptam orice raspuns scurt — daca e ERR_END inseamna fail; lipsa raspuns = succes (Slave reset)
    const uint32_t waitEnd = millis();
    char endResp[64]; size_t en = 0;
    bool sawErr = false;
    while (millis() - waitEnd < 5000) {
        if (slaveSerial.available()) {
            char c = (char)slaveSerial.read();
            if (c == '\n') {
                endResp[en] = '\0';
                if (Crc::validate(endResp)) {
                    Crc::stripCrc(endResp);
                    if (strncmp(endResp, "ERR", 3) == 0) {
                        sawErr = true;
                    }
                }
                break;
            }
            if (c != '\r' && en < sizeof(endResp) - 1) endResp[en++] = c;
        } else {
            yield();
        }
    }
    if (sawErr) {
        Serial.printf("[SlaveOTA] OTA_END rejected: %s\n", endResp);
        _switchBaud(slaveSerial, OTA_UART_BAUD, SLAVE_UART_BAUD);
        return SOTA_ERR_END_REJECT;
    }

    // Dupa Slave reboot, baud-ul e implicit 115200 din nou.
    // Master resetam si noi explicit.
    slaveSerial.flush();
    slaveSerial.updateBaudRate(SLAVE_UART_BAUD);
    Serial.println("[SlaveOTA] OK — Slave should be rebooting");
    return SOTA_OK;
}

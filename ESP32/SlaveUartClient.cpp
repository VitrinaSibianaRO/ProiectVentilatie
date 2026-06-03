// SlaveUartClient.cpp — Implementare client UART cu CRC-16/Modbus.
// Fiecare comanda trimisa are CRC atasat (*XXXX).
// Fiecare raspuns primit e validat CRC. La fail → retry (max 2).
#include "SlaveUartClient.h"
#include "CrcUtil.h"

SlaveUartClient::SlaveUartClient()
    : _serial(nullptr), _consecutiveErrors(0), _lastSuccessMs(0) {}

void SlaveUartClient::begin(HardwareSerial& serial) {
    _serial = &serial;
}

void SlaveUartClient::_flushInput() {
    if (!_serial) return;
    while (_serial->available()) _serial->read();
}

String SlaveUartClient::_readLine(uint32_t timeoutMs) {
    if (!_serial) return String();
    String line;
    line.reserve(256);
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            char c = (char)_serial->read();
            if (c == '\n') return line;
            if (c != '\r') line += c;
        } else {
            yield();
        }
    }
    return String();   // timeout
}

void SlaveUartClient::_sendCmd(const char* cmd) {
    if (!_serial) return;
    // Construieste mesaj cu CRC: <cmd>*XXXX\n
    char buf[128];
    strncpy(buf, cmd, sizeof(buf) - 6);
    buf[sizeof(buf) - 6] = '\0';
    Crc::appendCrc(buf, sizeof(buf));
    _serial->print(buf);
    _serial->print('\n');
}

bool SlaveUartClient::fetch(float& temp, float& hum, uint32_t& slaveTs,
                            uint32_t timeoutMs) {
    if (!_serial) { _consecutiveErrors++; return false; }

    for (int retry = 0; retry < SLAVE_RETRY_PER_FETCH + 1; retry++) {
        _flushInput();
        _sendCmd("GET_SENSOR");

        String line = _readLine(timeoutMs);
        if (line.isEmpty()) continue;

        // Validare CRC pe raspuns
        char buf[256];
        size_t copyLen = (line.length() < sizeof(buf) - 1) ? line.length() : sizeof(buf) - 1;
        memcpy(buf, line.c_str(), copyLen);
        buf[copyLen] = '\0';

        if (!Crc::validate(buf)) {
            Serial.println("[Slave] response CRC fail, retry");
            continue;
        }
        Crc::stripCrc(buf);   // ramane doar JSON-ul curat

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, buf);
        if (err) continue;

        if (!doc["ok"].as<bool>()) {
            // Slave a raspuns dar senzor failing — propagam catre apelant
            temp = 0; hum = 0; slaveTs = 0;
            _consecutiveErrors++;
            return false;
        }

        temp     = doc["temp"].as<float>();
        hum      = doc["hum"].as<float>();
        slaveTs  = doc["ts"].as<uint32_t>();
        _consecutiveErrors = 0;
        _lastSuccessMs = millis();
        return true;
    }

    _consecutiveErrors++;
    return false;
}

bool SlaveUartClient::sendReboot(uint32_t timeoutMs) {
    if (!_serial) return false;
    _flushInput();
    _sendCmd("REBOOT");
    String resp = _readLine(timeoutMs);

    // Validare CRC pe raspuns
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t copyLen = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), copyLen);
    buf[copyLen] = '\0';

    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

bool SlaveUartClient::ping(uint32_t& latencyMs, uint32_t timeoutMs) {
    if (!_serial) return false;
    _flushInput();
    unsigned long start = millis();
    _sendCmd("PING");
    String resp = _readLine(timeoutMs);
    latencyMs = millis() - start;

    if (resp.isEmpty()) return false;
    char buf[64];
    size_t copyLen = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), copyLen);
    buf[copyLen] = '\0';

    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "PONG") == 0;
}

// ============================================================
//  LED Control — forward to Slave
// ============================================================

bool SlaveUartClient::sendLedSet(uint8_t percent, uint32_t timeoutMs) {
    if (!_serial) return false;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "LED_SET %u", percent);
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

bool SlaveUartClient::sendLedSchedule(uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                                       uint8_t maxI, bool enabled, uint32_t timeoutMs) {
    if (!_serial) return false;
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "LED_SCHEDULE %u %u %u %u %u %d",
             onH, onM, offH, offM, maxI, enabled ? 1 : 0);
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

bool SlaveUartClient::fetchLedStatus(uint8_t& intensity, bool& schedEnabled,
                                      uint8_t& onH, uint8_t& onM, uint8_t& offH, uint8_t& offM, uint8_t& maxI,
                                      uint32_t timeoutMs) {
    if (!_serial) return false;
    _flushInput();
    _sendCmd("LED_STATUS");
    String line = _readLine(timeoutMs);
    if (line.isEmpty()) return false;

    char buf[256];
    size_t n = (line.length() < sizeof(buf) - 1) ? line.length() : sizeof(buf) - 1;
    memcpy(buf, line.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);

    JsonDocument doc;
    if (deserializeJson(doc, buf)) return false;
    intensity    = doc["intensity"] | 0;
    schedEnabled = doc["enabled"]   | false;
    
    JsonObject s = doc["sched"];
    onH  = s["onH"] | 0;
    onM  = s["onM"] | 0;
    offH = s["offH"] | 0;
    offM = s["offM"] | 0;
    maxI = s["maxI"] | 0;
    
    return true;
}

bool SlaveUartClient::sendLedMode(uint8_t id, uint16_t p1, uint16_t p2,
                                   uint16_t p3, uint16_t p4, uint32_t timeoutMs) {
    if (!_serial) return false;
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "LED_MODE %u %u %u %u %u", id, p1, p2, p3, p4);
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

// ============================================================
//  LED_FOLLOW_TV + LED_TV_CAP — follow TV brightness feature
// ============================================================

bool SlaveUartClient::sendLedFollowTv(bool enabled, uint32_t timeoutMs) {
    if (!_serial) return false;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "LED_FOLLOW_TV %d", enabled ? 1 : 0);
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

bool SlaveUartClient::sendLedTvCap(uint8_t pct, uint32_t timeoutMs) {
    if (!_serial) return false;
    if (pct > 100) pct = 100;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "LED_TV_CAP %u", pct);
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

bool SlaveUartClient::sendLedMorseText(const char* text, uint32_t timeoutMs) {
    if (!_serial) return false;
    char cmd[80];  // "LED_MORSE_TEXT " (15) + max 50 chars + null
    snprintf(cmd, sizeof(cmd), "LED_MORSE_TEXT %.50s", text ? text : "");
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

// ============================================================
//  TIME_SYNC — trimite epoch Slave-ului
// ============================================================

bool SlaveUartClient::sendTimeSync(uint32_t epochSec, uint32_t timeoutMs) {
    if (!_serial) return false;
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "TIME_SYNC %lu", (unsigned long)epochSec);
    _flushInput();
    _sendCmd(cmd);
    String resp = _readLine(timeoutMs);
    if (resp.isEmpty()) return false;
    char buf[64];
    size_t n = (resp.length() < sizeof(buf) - 1) ? resp.length() : sizeof(buf) - 1;
    memcpy(buf, resp.c_str(), n);
    buf[n] = '\0';
    if (!Crc::validate(buf)) return false;
    Crc::stripCrc(buf);
    return strcmp(buf, "OK") == 0;
}

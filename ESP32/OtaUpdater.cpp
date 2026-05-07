// ============================================================
//  OtaUpdater.cpp
//  OTA via HTTPS + SHA-256 streaming verification.
//  Transport: EthernetClient + SSLClient (W5500).
// ============================================================

#include "OtaUpdater.h"
#include "Config.h"
#include "HiveMqCert.h"

#include <SPI.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoHttpClient.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>

bool OtaUpdater::_isUrlAllowed(const char* url) {
    return (strstr(url, OTA_URL_WHITELIST1) == url ||
            strstr(url, OTA_URL_WHITELIST2) == url);
}

OtaResult OtaUpdater::start(const char* url, const char* expectedSha256,
                             OtaProgressCallback progressCb) {
    Serial.printf("[OTA] Start: %s\n", url);

    // 1. Whitelist check
    if (!_isUrlAllowed(url)) {
        Serial.println("[OTA] URL rejected: not on whitelist.");
        return OTA_ERR_URL_INVALID;
    }

    // 2. Validare SHA-256 format (64 hex chars)
    if (!expectedSha256 || strlen(expectedSha256) != 64) {
        Serial.println("[OTA] SHA-256 invalid (expected 64 hex chars).");
        return OTA_ERR_URL_INVALID;
    }

    // 3. Parse URL — extragem host și path
    // Format: https://host/path
    const char* hostStart = url + 8; // skip "https://"
    if (strncmp(url, "https://", 8) != 0) {
        Serial.println("[OTA] URL must start with https://");
        return OTA_ERR_URL_INVALID;
    }
    const char* pathStart = strchr(hostStart, '/');
    if (!pathStart) {
        Serial.println("[OTA] URL has no path.");
        return OTA_ERR_URL_INVALID;
    }

    char host[128];
    size_t hostLen = (size_t)(pathStart - hostStart);
    if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
    memcpy(host, hostStart, hostLen);
    host[hostLen] = '\0';

    // 4. Connect via EthernetClient + SSLClient
    EthernetClient baseClient;
    SSLClient sslClient(baseClient, TrustAnchors, TrustAnchors_NUM, A0);
    HttpClient httpClient(sslClient, host, 443);
    httpClient.setHttpResponseTimeout(30000);

    Serial.printf("[OTA] Connecting to %s...\n", host);
    int err = httpClient.get(pathStart);
    if (err != 0) {
        Serial.printf("[OTA] HTTP GET failed: %d\n", err);
        return OTA_ERR_CONNECT;
    }

    int statusCode = httpClient.responseStatusCode();
    // Handle redirects (GitHub releases redirect 302)
    if (statusCode == 301 || statusCode == 302) {
        String location;
        // ArduinoHttpClient: iterate headers to find Location
        while (httpClient.headerAvailable()) {
            String headerName = httpClient.readHeaderName();
            String headerValue = httpClient.readHeaderValue();
            if (headerName.equalsIgnoreCase("Location")) {
                location = headerValue;
                break;
            }
        }
        if (location.length() > 0) {
            Serial.printf("[OTA] Redirect to: %s\n", location.c_str());
            httpClient.stop();
            // Recursive call with redirected URL (one level max)
            return start(location.c_str(), expectedSha256, progressCb);
        }
    }

    if (statusCode != 200) {
        Serial.printf("[OTA] HTTP error: %d\n", statusCode);
        httpClient.stop();
        return OTA_ERR_HTTP;
    }

    long contentLength = httpClient.contentLength();
    if (contentLength <= 0) {
        Serial.println("[OTA] Content-Length invalid.");
        httpClient.stop();
        return OTA_ERR_HTTP;
    }

    Serial.printf("[OTA] Firmware size: %ld bytes\n", contentLength);

    // 5. Begin update
    if (!Update.begin(contentLength)) {
        Serial.printf("[OTA] Update.begin failed: %s\n",
            Update.errorString());
        httpClient.stop();
        return OTA_ERR_BEGIN;
    }

    // 6. SHA-256 context
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);  // 0 = SHA-256

    // 7. Download + write chunks
    uint8_t buf[4096];
    long totalRead = 0;
    int lastPct = -1;

    while (totalRead < contentLength) {
        // WDT feed — download-ul poate dura minute
        esp_task_wdt_reset();

        int available = httpClient.available();
        if (available <= 0) {
            if (!httpClient.connected()) {
                Serial.println("[OTA] Connection lost during download.");
                Update.abort();
                mbedtls_sha256_free(&sha);
                return OTA_ERR_WRITE;
            }
            delay(10);
            continue;
        }

        int toRead = min((int)sizeof(buf), available);
        toRead = min((long)toRead, contentLength - totalRead);
        int bytesRead = httpClient.read(buf, toRead);

        if (bytesRead <= 0) {
            Serial.println("[OTA] Read error during download.");
            Update.abort();
            httpClient.stop();
            mbedtls_sha256_free(&sha);
            return OTA_ERR_WRITE;
        }

        // SHA-256 update
        mbedtls_sha256_update(&sha, buf, bytesRead);

        // Write to flash
        size_t written = Update.write(buf, bytesRead);
        if (written != (size_t)bytesRead) {
            Serial.printf("[OTA] Write failed: %d/%d\n", (int)written, bytesRead);
            Update.abort();
            httpClient.stop();
            mbedtls_sha256_free(&sha);
            return OTA_ERR_WRITE;
        }

        totalRead += bytesRead;

        // Progress reporting la fiecare 10%
        int pct = (int)((totalRead * 100L) / contentLength);
        int pct10 = (pct / 10) * 10;
        if (pct10 > lastPct) {
            lastPct = pct10;
            Serial.printf("[OTA] Progress: %d%%\n", pct10);
            if (progressCb) progressCb(pct10);
        }
    }

    httpClient.stop();

    // 8. Finalizare SHA-256
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);

    // Convertim hash-ul la hex string
    char computedSha[65];
    for (int i = 0; i < 32; i++) {
        sprintf(computedSha + i * 2, "%02x", hash[i]);
    }
    computedSha[64] = '\0';

    Serial.printf("[OTA] SHA-256 computed: %s\n", computedSha);
    Serial.printf("[OTA] SHA-256 expected: %s\n", expectedSha256);

    // 9. Verificare SHA-256
    if (strcasecmp(computedSha, expectedSha256) != 0) {
        Serial.println("[OTA] SHA-256 MISMATCH! Aborting.");
        Update.abort();
        return OTA_ERR_SHA_MISMATCH;
    }

    // 10. Finalizare Update
    if (!Update.end(true)) {
        Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        return OTA_ERR_END;
    }

    Serial.println("[OTA] Update SUCCESS. Rebooting...");
    // Callback final 100%
    if (progressCb) progressCb(100);

    return OTA_OK;
}

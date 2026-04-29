// ============================================================
//  OtaUpdater.cpp
//  OTA via HTTPS + SHA-256 streaming verification.
// ============================================================

#include "OtaUpdater.h"
#include "Config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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

    // 3. HTTPS connect (setInsecure — SHA-256 garantează integritatea)
    WiFiClientSecure secClient;
    secClient.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);

    if (!http.begin(secClient, url)) {
        Serial.println("[OTA] HTTP begin failed.");
        return OTA_ERR_CONNECT;
    }

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP error: %d\n", httpCode);
        http.end();
        return OTA_ERR_HTTP;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("[OTA] Content-Length invalid.");
        http.end();
        return OTA_ERR_HTTP;
    }

    Serial.printf("[OTA] Firmware size: %d bytes\n", contentLength);

    // 4. Begin update
    if (!Update.begin(contentLength)) {
        Serial.printf("[OTA] Update.begin failed: %s\n",
            Update.errorString());
        http.end();
        return OTA_ERR_BEGIN;
    }

    // 5. SHA-256 context
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);  // 0 = SHA-256

    // 6. Download + write chunks
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[4096];
    int totalRead = 0;
    int lastPct = -1;

    while (totalRead < contentLength) {
        // WDT feed — download-ul poate dura minute
        esp_task_wdt_reset();

        int available = stream->available();
        if (available <= 0) {
            delay(10);
            continue;
        }

        int toRead = min((int)sizeof(buf), available);
        toRead = min(toRead, contentLength - totalRead);
        int bytesRead = stream->readBytes(buf, toRead);

        if (bytesRead <= 0) {
            Serial.println("[OTA] Read error during download.");
            Update.abort();
            http.end();
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
            http.end();
            mbedtls_sha256_free(&sha);
            return OTA_ERR_WRITE;
        }

        totalRead += bytesRead;

        // Progress reporting la fiecare 10%
        int pct = (totalRead * 100) / contentLength;
        int pct10 = (pct / 10) * 10;
        if (pct10 > lastPct) {
            lastPct = pct10;
            Serial.printf("[OTA] Progress: %d%%\n", pct10);
            if (progressCb) progressCb(pct10);
        }
    }

    http.end();

    // 7. Finalizare SHA-256
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

    // 8. Verificare SHA-256
    if (strcasecmp(computedSha, expectedSha256) != 0) {
        Serial.println("[OTA] SHA-256 MISMATCH! Aborting.");
        Update.abort();
        return OTA_ERR_SHA_MISMATCH;
    }

    // 9. Finalizare Update
    if (!Update.end(true)) {
        Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
        return OTA_ERR_END;
    }

    Serial.println("[OTA] Update SUCCESS. Rebooting...");
    // Callback final 100%
    if (progressCb) progressCb(100);

    return OTA_OK;
}

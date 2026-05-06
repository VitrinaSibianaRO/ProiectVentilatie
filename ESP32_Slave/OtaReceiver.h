// OtaReceiver.h — Primeste firmware OTA de la Master prin UART chunked.
// Foloseste esp_ota_ops + Update. SHA-256 verificat de Master inainte de trimitere.
// Rollback automat daca noul firmware nu cheama esp_ota_mark_app_valid_cancel_rollback().
#pragma once
#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "Config.h"
#include "Logger.h"
#include "WatchdogManager.h"

class OtaReceiver {
public:
    explicit OtaReceiver(HardwareSerial& serial)
        : _serial(serial), _active(false), _written(0), _expectedSize(0) {
        _expectedSha[0] = '\0';
    }

    OtaReceiver(const OtaReceiver&) = delete;
    OtaReceiver& operator=(const OtaReceiver&) = delete;

    // Incepe sesiunea OTA. size in bytes, sha = SHA-256 hex 64 chars.
    // Returneaza false daca size out-of-range sau Update.begin() esueaza.
    [[nodiscard]] bool begin(uint32_t size, const char* sha) {
        if (_active) {
            Update.abort();
            _active = false;
        }
        if (size < OTA_MIN_FW_SIZE || size > OTA_MAX_FW_SIZE) {
            LOG_ERROR("OTA size out of range: %u", size);
            return false;
        }
        if (!sha || strlen(sha) != 64) {
            LOG_ERROR("OTA SHA invalid");
            return false;
        }
        if (!Update.begin(size, U_FLASH)) {
            LOG_ERROR("Update.begin fail: %s", Update.errorString());
            return false;
        }
        strncpy(_expectedSha, sha, 64);
        _expectedSha[64] = '\0';
        _expectedSize = size;
        _written = 0;
        _active = true;
        LOG_INFO("OTA begin: %u bytes", size);
        return true;
    }

    // Primeste exact `length` bytes binari de pe Serial (blocking cu timeout).
    // Scrie in partitia OTA. Reseteaza WDT la fiecare chunk.
    [[nodiscard]] bool writeChunk(uint32_t length) {
        if (!_active) return false;
        if (length == 0 || length > 1024) {
            LOG_ERROR("OTA chunk size invalid: %u", length);
            return false;
        }

        uint8_t buf[1024];
        uint32_t got = 0;
        const uint32_t start = millis();
        while (got < length) {
            if (_serial.available()) {
                buf[got++] = (uint8_t)_serial.read();
            } else if (millis() - start > OTA_CHUNK_TIMEOUT_MS) {
                LOG_ERROR("OTA chunk read timeout at %u/%u", got, length);
                return false;
            }
        }
        WatchdogManager::feedNow();

        const size_t written = Update.write(buf, length);
        if (written != length) {
            LOG_ERROR("Update.write short: %u/%u", written, length);
            return false;
        }
        _written += length;
        return true;
    }

    // Finalizeaza OTA: verifica marimea, Update.end(), reboot.
    // Returneaza false la mismatch — firmware anterior ramas activ.
    [[nodiscard]] bool end() {
        if (!_active) return false;
        if (_written != _expectedSize) {
            LOG_ERROR("OTA size mismatch: got %u expect %u", _written, _expectedSize);
            Update.abort();
            _active = false;
            return false;
        }
        if (!Update.end(true)) {
            LOG_ERROR("Update.end fail: %s", Update.errorString());
            _active = false;
            return false;
        }
        LOG_INFO("OTA OK (%u bytes), rebooting", _written);
        _active = false;
        delay(100);
        ESP.restart();
        return true;   // nu se atinge niciodata
    }

    void abort() {
        if (_active) {
            Update.abort();
            _active = false;
        }
        _written = 0;
    }

    bool     isActive()      const { return _active; }
    uint32_t getBytesWritten() const { return _written; }
    uint32_t getExpectedSize() const { return _expectedSize; }

private:
    HardwareSerial& _serial;
    bool            _active;
    uint32_t        _written;
    uint32_t        _expectedSize;
    char            _expectedSha[65];
};

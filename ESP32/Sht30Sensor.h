// Sht30Sensor.h — Wrapper Adafruit_SHT31 cu cooldown, retry si cache.
// Header-only pentru a putea fi copiat identic in ESP32_Slave/.
// Compatibil cu senzori GY-SHT30-D (addr 0x44 default, 0x45 cu ADDR=HIGH).
#pragma once
#include <Arduino.h>
#include <Adafruit_SHT31.h>
#include "Config.h"

class Sht30Sensor {
public:
    Sht30Sensor()
        : _addr(0x44), _initialized(false),
          _consecutiveErrors(0), _lastReadMs(0),
          _cachedTemp(NAN), _cachedHum(NAN) {}

    // Wire.begin(SDA,SCL) trebuie apelat inainte in setup().
    bool begin(uint8_t addr = 0x44) {
        _addr = addr;
        _initialized = _sht.begin(addr);
        return _initialized;
    }

    // Citire cu cooldown intern (SHT30_MIN_READ_MS) si retry (SHT30_RETRY_COUNT).
    // force=true sare peste cooldown (folosit la refresh explicit din MAUI).
    // La succes: temp/hum actualizate si returneaza true.
    // La esec dupa toate retry-urile: _consecutiveErrors++ si returneaza false.
    // Sub cooldown cu cache valid: returneaza cache-ul si true.
    bool read(float& temp, float& hum, bool force = false) {
        if (!_initialized) return false;

        const uint32_t now = millis();
        const bool inCooldown = (now - _lastReadMs) < SHT30_MIN_READ_MS;

        if (!force && inCooldown && !isnan(_cachedTemp)) {
            temp = _cachedTemp;
            hum  = _cachedHum;
            return true;
        }

        for (uint8_t retry = 0; retry < SHT30_RETRY_COUNT; retry++) {
            const float t = _sht.readTemperature();
            const float h = _sht.readHumidity();
            if (_isValid(t, h)) {
                _cachedTemp = t;
                _cachedHum  = h;
                _lastReadMs = now;
                _consecutiveErrors = 0;
                temp = t;
                hum  = h;
                return true;
            }
            if (retry < SHT30_RETRY_COUNT - 1) delay(20);
        }

        _consecutiveErrors++;
        return false;
    }

    int      getConsecutiveErrors() const { return _consecutiveErrors; }
    bool     isInitialized()        const { return _initialized; }
    uint32_t getLastReadMs()        const { return _lastReadMs; }
    float    getCachedTemp()        const { return _cachedTemp; }
    float    getCachedHum()         const { return _cachedHum; }
    bool     hasCachedValue()       const { return !isnan(_cachedTemp); }

private:
    Adafruit_SHT31 _sht;
    uint8_t        _addr;
    bool           _initialized;
    int            _consecutiveErrors;
    uint32_t       _lastReadMs;
    float          _cachedTemp;
    float          _cachedHum;

    static bool _isValid(float t, float h) {
        return !isnan(t) && !isnan(h) &&
               t > -20.0f && t < 80.0f &&
               h >= 0.0f  && h <= 100.0f;
    }
};

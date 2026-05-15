// Resilience.h — Always-on hardening mechanisms for 3-year autonomous operation.
// Brownout config, heap monitoring, boot-loop guard, I2C recovery,
// Ethernet link monitor, preventive reboot, NVS validation.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_system.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Config.h"
#include "TimeSync.h"

// ============================================================
//  BROWNOUT — disable brown-out reset la threshold prea mic
//  (ESP32 default e 2.43V — la marginea surselor 3.3V ieftine)
// ============================================================
namespace Brownout {
    inline void configure() {
        // Dezactivăm brownout detector — sursele Mean Well sunt stabile.
        // Altfel, ESP32 se reseta la micro-drops de tensiune în sarcină grea (TLS).
        WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
        Serial.println("[Resilience] Brownout detector disabled");
    }
}

// ============================================================
//  HEAP MONITOR — tracked in loop(), restart dacă heap < threshold
// ============================================================
class HeapMonitor {
public:
    static constexpr uint32_t CRITICAL_HEAP   = 30000;  // 30KB
    static constexpr uint32_t CHECK_INTERVAL  = 30000;  // 30s

    static void check() {
        unsigned long now = millis();
        if (now - _lastCheckMs < CHECK_INTERVAL && _lastCheckMs > 0) return;
        _lastCheckMs = now;

        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t minHeap  = ESP.getMinFreeHeap();

        if (freeHeap < CRITICAL_HEAP) {
            Serial.printf("[HeapMonitor] CRITICAL: free=%u min=%u — RESTARTING\n",
                freeHeap, minHeap);
            delay(100);
            ESP.restart();
        }

        // Log la fiecare 5 minute dacă sub 50KB
        if (freeHeap < 50000 && (now - _lastWarnMs > 300000 || _lastWarnMs == 0)) {
            Serial.printf("[HeapMonitor] WARNING: free=%u min=%u\n", freeHeap, minHeap);
            _lastWarnMs = now;
        }
    }

private:
    static inline unsigned long _lastCheckMs = 0;
    static inline unsigned long _lastWarnMs  = 0;
};

// ============================================================
//  BOOT LOOP GUARD — detectează reboot-uri repetate rapide
//  NVS counter reset dacă uptime > 120s. Dacă 6× rapid → safe mode.
// ============================================================
class BootLoopGuard {
public:
    static constexpr int  MAX_RAPID_BOOTS   = 6;
    static constexpr uint32_t STABLE_AFTER_MS = 120000;  // 2 min

    // Apelat la inceputul setup(). Returneaza true daca sistem e in safe mode.
    static bool check() {
        Preferences p;
        p.begin("blg", false);
        int count = p.getInt("cnt", 0);
        count++;
        p.putInt("cnt", count);
        p.end();

#if BOOTGUARD_SAFE_MODE_ENABLED
        if (count >= MAX_RAPID_BOOTS) {
            Serial.printf("[BootGuard] SAFE MODE! %d rapid reboots\n", count);
            _safeMode = true;
            return true;
        }
#else
        if (count >= MAX_RAPID_BOOTS) {
            Serial.printf("[BootGuard] Safe mode bypassed for hardware debug (%d rapid reboots)\n", count);
        }
#endif
        Serial.printf("[BootGuard] Boot count: %d/%d\n", count, MAX_RAPID_BOOTS);
        return false;
    }

    // Apelat din loop() — dupa STABLE_AFTER_MS, reseteaza counter.
    static void tick() {
        if (_cleared || _safeMode) return;
        if (millis() > STABLE_AFTER_MS) {
            Preferences p;
            p.begin("blg", false);
            p.putInt("cnt", 0);
            p.end();
            _cleared = true;
            Serial.println("[BootGuard] Stable — counter reset");
        }
    }

    static bool isSafeMode() { return _safeMode; }

private:
    static inline bool _safeMode = false;
    static inline bool _cleared  = false;
};

// ============================================================
//  I2C RECOVERY — toggle SCL 9× pentru a debloca SDA stuck low
// ============================================================
namespace I2CRecovery {
    inline void recoverBus(uint8_t sdaPin, uint8_t sclPin) {
        Serial.println("[I2C] Bus recovery...");

        // Setăm SCL ca output, SDA ca input (monitorizăm)
        pinMode(sclPin, OUTPUT);
        pinMode(sdaPin, INPUT_PULLUP);

        // Toggle SCL de 9 ori — orice slave stuck va elibera SDA
        for (int i = 0; i < 9; i++) {
            digitalWrite(sclPin, LOW);
            delayMicroseconds(5);
            digitalWrite(sclPin, HIGH);
            delayMicroseconds(5);
            if (digitalRead(sdaPin) == HIGH) {
                Serial.printf("[I2C] Bus free after %d clocks\n", i + 1);
                break;
            }
        }

        // Re-init Wire cu frecvența originală
        Wire.begin(sdaPin, sclPin);
        Wire.setClock(I2C_FREQ_HZ);
    }
}

// ============================================================
//  PREVENTIVE REBOOT — Master: duminică 03:00–03:04 UTC, weekly
//  Fereastra 5min ca sa nu ratam minutul exact (verificam la 60s).
//  Uptime > 6 zile evita loop de restart si protejeaza la deploy.
// ============================================================
namespace PreventiveReboot {
    inline void checkWeekly() {
        // Throttle: verifica la fiecare 60s (intern in functie)
        static unsigned long lastCheckMs = 0;
        if (lastCheckMs != 0 && millis() - lastCheckMs < 60000UL) return;
        lastCheckMs = millis();

        // Skip primele 6 zile (fereastra deploy + boot recovery)
        if (millis() < 6UL * 24UL * 3600UL * 1000UL) return;

        uint32_t epoch = TimeSync::getEpochSec();
        if (epoch < 1700000000UL) return;   // timp nesincronizat

        struct tm timeinfo;
        time_t t = (time_t)epoch;
        if (!gmtime_r(&t, &timeinfo)) return;

        // Duminica (wday==0), ora 03:00–03:04 UTC
        if (timeinfo.tm_wday == 0 &&
            timeinfo.tm_hour == 3 &&
            timeinfo.tm_min  < 5) {
            Serial.println("[PreventiveReboot] Weekly maintenance reboot (Sun 03:00 UTC)");
            delay(200);
            ESP.restart();
        }
    }
}

// ============================================================
//  MQTT RECONNECT GUARD — restart după N eșecuri consecutive
// ============================================================
class MqttReconnectGuard {
public:
    static constexpr int MAX_FAILURES = 20;

    static void onConnectFail() {
        _failures++;
        Serial.printf("[MQTT Guard] Fail #%d/%d\n", _failures, MAX_FAILURES);
        if (_failures >= MAX_FAILURES) {
            Serial.println("[MQTT Guard] Too many failures — RESTART");
            delay(200);
            ESP.restart();
        }
    }

    static void onConnectSuccess() {
        if (_failures > 0) {
            Serial.printf("[MQTT Guard] Connected after %d fails\n", _failures);
        }
        _failures = 0;
    }

private:
    static inline int _failures = 0;
};

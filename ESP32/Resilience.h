// Resilience.h — Always-on hardening mechanisms for 3-year autonomous operation.
// Brownout config, heap monitoring, boot-loop guard, I2C recovery,
// Ethernet link monitor, preventive reboot, NVS validation.
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
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
//  MQTT RECONNECT GUARD — logging esecuri, FARA reboot
//  MQTT picat NU reporneste placa: releele si senzorii functioneaza
//  offline la nesfarsit. Singurul reboot de retea e in WifiWatchdog
//  (last-resort dupa 6h continuu fara WiFi).
// ============================================================
class MqttReconnectGuard {
public:
    static void onConnectFail() {
        _failures++;
        // Log la primul esec si la fiecare 10 (evita spam la backoff lung).
        if (_failures == 1 || _failures % 10 == 0) {
            Serial.printf("[MQTT Guard] %d esecuri consecutive (offline OK, fara reboot)\n",
                          _failures);
        }
    }

    static void onConnectSuccess() {
        if (_failures > 0) {
            Serial.printf("[MQTT Guard] Reconectat dupa %d esecuri\n", _failures);
        }
        _failures = 0;
    }

private:
    static inline int _failures = 0;
};

// ============================================================
//  WIFI WATCHDOG — reconectare periodica, FARA blocare
//
//  State machine ne-blocanta: tick() returneaza in microsecunde la
//  fiecare apel din loop(), fara while/delay. Releele si senzorii
//  ruleaza continuu chiar in timpul celor 3 incercari de reconectare.
//
//  Tranzitii:
//    IDLE ──(interval 5min expirat, WiFi jos)──> ATTEMPTING (incercare 1)
//    ATTEMPTING ──(timeout 15s, mai sunt incercari)──> ATTEMPTING (urm.)
//    ATTEMPTING ──(timeout 15s, epuizat)──> IDLE (retry dupa 5 min)
//    orice stare ──(WiFi conectat)──> IDLE (reset complet)
//
//  Last-resort: dupa WIFI_DOWN_REBOOT_MS (6h) continuu fara WiFi → reboot.
//  MQTT picat singur nu declanseaza reboot (gestionat de MqttReconnectGuard).
//  g_wifiAvailable sincronizat la fiecare tick → mqtt.loop() il citeste intern.
// ============================================================
class WifiWatchdog {
public:
    static void tick() {
        const bool connected = (WiFi.status() == WL_CONNECTED);
        g_wifiAvailable = connected;
        const unsigned long now = millis();
        if (_lastCheckMs == 0) _lastCheckMs = now;   // lazy-init la boot

        if (connected) {
            // Conectat: reset complet; reporneste fereastra de 5 min
            if (_attempting) {
                Serial.printf("[WifiWatchdog] Reconectat: %s\n",
                              WiFi.localIP().toString().c_str());
            }
            _attempting  = false;
            _attempt     = 0;
            _lastCheckMs = now;
            _downSinceMs = 0;   // reset contor last-resort
            return;
        }

        // Contor last-resort: incepe de la primul moment de deconectare.
        if (_downSinceMs == 0) _downSinceMs = now;

        // Last-resort: 6h continuu fara WiFi → reboot (recuperare stack TCP/DHCP).
        if (now - _downSinceMs >= WIFI_DOWN_REBOOT_MS) {
            Serial.println("[WifiWatchdog] 6h fara WiFi — reboot last-resort.");
            delay(200);
            ESP.restart();
        }

        if (!_attempting) {
            // IDLE — asteptam urmatoarea fereastra de 5 min
            if (now - _lastCheckMs < WIFI_WATCHDOG_INTERVAL_MS) return;
            _beginAttempt(now);
            return;
        }

        // ATTEMPTING — verificam daca a trecut timeout-ul incercarii curente
        if (now - _attemptStart < WIFI_WATCHDOG_ATTEMPT_TIMEOUT_MS) return;

        if (_attempt < WIFI_WATCHDOG_MAX_ATTEMPTS) {
            _beginAttempt(now);   // urmatoarea incercare din burst
        } else {
            Serial.println("[WifiWatchdog] Burst esuat — retry in 5 min");
            _attempting  = false;
            _attempt     = 0;
            _lastCheckMs = now;   // reia dupa un nou INTERVAL
        }
    }

private:
    static void _beginAttempt(unsigned long now) {
        _attempt++;
        Serial.printf("[WifiWatchdog] WiFi down — reconnect %d/%d\n",
                      _attempt, WIFI_WATCHDOG_MAX_ATTEMPTS);
        WiFi.mode(WIFI_STA);
        WiFi.reconnect();   // asincron — returneaza imediat
        _attempting   = true;
        _attemptStart = now;
    }

    static inline unsigned long _lastCheckMs  = 0;
    static inline unsigned long _attemptStart = 0;
    static inline unsigned long _downSinceMs  = 0;   // pentru last-resort 6h
    static inline int           _attempt      = 0;
    static inline bool          _attempting   = false;
};

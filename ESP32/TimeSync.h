#pragma once

// ============================================================
//  TimeSync.h
//  Wrapper NTP pentru ESP32 — sincronizare automată cu
//  pool.ntp.org / time.google.com.
//
//  Timezone: Europa/București (EET-2EEST)
//  Re-sync periodic la 24h.
//  Fallback: dacă NTP nu e disponibil, timestamp = uptime.
// ============================================================

#include <Arduino.h>
#include <time.h>
#include "Config.h"

class TimeSync {
public:
    // Inițializare NTP. Apelat o singură dată după WiFi connect.
    static void begin() {
        configTzTime(NTP_TIMEZONE,
                     NTP_SERVER1, NTP_SERVER2);
        _lastSyncMs = millis();
        _synced = false;

        // Așteptăm maxim 5 secunde pentru prima sincronizare
        Serial.print("[NTP] Sincronizare...");
        for (int i = 0; i < 50; i++) {
            delay(100);
            time_t now = time(nullptr);
            if (now > NTP_EPOCH_VALID_AFTER) {   // NTP sincronizat
                _synced = true;
                break;
            }
        }

        if (_synced) {
            struct tm tInfo;
            getLocalTime(&tInfo);
            Serial.printf(" OK %04d-%02d-%02d %02d:%02d\n",
                tInfo.tm_year + 1900, tInfo.tm_mon + 1, tInfo.tm_mday,
                tInfo.tm_hour, tInfo.tm_min);
        } else {
            Serial.println(" FAIL (se va reîncerca).");
        }
    }

    // Apelat periodic din loop() — re-sync la 24h.
    static void loop() {
        if (millis() - _lastSyncMs >= RESYNC_INTERVAL_MS) {
            _lastSyncMs = millis();
            // configTzTime re-triggerează SNTP intern
            configTzTime(NTP_TIMEZONE,
                         NTP_SERVER1, NTP_SERVER2);
            Serial.println("[NTP] Re-sync declanșat.");

            // Verificăm dacă s-a sincronizat
            delay(500);
            time_t now = time(nullptr);
            _synced = (now > NTP_EPOCH_VALID_AFTER);
        }
    }

    // Returnează epoch seconds curent (UTC).
    // Dacă NTP nu e sincronizat, returnează uptime ca fallback.
    static uint32_t getEpochSec() {
        time_t now = time(nullptr);
        if (now > NTP_EPOCH_VALID_AFTER) {
            _synced = true;
            return (uint32_t)now;
        }
        // Fallback: uptime în secunde
        return (uint32_t)(millis() / 1000);
    }

    // Formatează timestamp ISO 8601 UTC în buffer-ul furnizat.
    // Buffer minim 25 bytes: "2026-04-29T14:32:15Z\0"
    // Returnează true dacă NTP e sincronizat, false = uptime fallback.
    static bool formatISO(uint32_t epochSec, char* buf, size_t bufLen) {
        if (epochSec > NTP_EPOCH_VALID_AFTER && _synced) {
            time_t t = (time_t)epochSec;
            struct tm tInfo;
            gmtime_r(&t, &tInfo);
            snprintf(buf, bufLen, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tInfo.tm_year + 1900, tInfo.tm_mon + 1, tInfo.tm_mday,
                tInfo.tm_hour, tInfo.tm_min, tInfo.tm_sec);
            return true;
        }
        // Fallback: "uptime:NNNNs"
        snprintf(buf, bufLen, "uptime:%us", epochSec);
        return false;
    }

    static bool isSynced() { return _synced; }

private:
    static constexpr unsigned long RESYNC_INTERVAL_MS = NTP_RESYNC_MS;
    static unsigned long _lastSyncMs;
    static bool _synced;
};

// Definiții variabile statice
// (într-un header-only: inline pentru a evita multiple definition)
inline unsigned long TimeSync::_lastSyncMs = 0;
inline bool TimeSync::_synced = false;

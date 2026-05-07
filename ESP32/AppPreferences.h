#pragma once

// ============================================================
//  AppPreferences.h
//  Wrapper peste ESP32 Preferences (NVS flash).
//  Toți parametrii configurabili sunt persistați aici.
//  La boot se încarcă automat; la fiecare modificare se salvează.
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class AppPreferences {
public:
    float   tempThresh;
    float   humThresh;
    float   tempHyst;
    float   humHyst;
    int     intervalSec;
    bool    overrideLeft;
    bool    overrideRight;
    unsigned long overrideLeftSetAt;   // millis() când a fost setat
    unsigned long overrideRightSetAt;
    int     overrideTimeoutMin;

    void begin() {
        _prefs.begin(NVS_PREFS_NAMESPACE, false);
        load();
    }

    void load() {
        tempThresh          = _prefs.getFloat("tempThresh",   DEFAULT_TEMP_THRESH);
        humThresh           = _prefs.getFloat("humThresh",    DEFAULT_HUM_THRESH);
        tempHyst            = _prefs.getFloat("tempHyst",     DEFAULT_TEMP_HYST);
        humHyst             = _prefs.getFloat("humHyst",      DEFAULT_HUM_HYST);
        intervalSec         = _prefs.getInt  ("intervalSec",  DEFAULT_INTERVAL_SEC);
        overrideLeft        = _prefs.getBool ("ovrLeft",      false);
        overrideRight       = _prefs.getBool ("ovrRight",     false);
        overrideTimeoutMin  = _prefs.getInt  ("ovrTimeout",   DEFAULT_OVERRIDE_TIMEOUT_MIN);
        // millis() nu supraviețuiește reboot-ului, deci resetăm timestamp-urile
        overrideLeftSetAt   = overrideLeft  ? millis() : 0;
        overrideRightSetAt  = overrideRight ? millis() : 0;
        _validateOrFallback();
    }

private:
    void _validateOrFallback() {
        bool bad = false;
        if (tempThresh < 0.0f || tempThresh > 50.0f) bad = true;
        if (humThresh < 0.0f || humThresh > 100.0f) bad = true;
        if (intervalSec < MIN_INTERVAL_SEC || intervalSec > MAX_INTERVAL_SEC) bad = true;
        if (tempHyst < MIN_TEMP_HYST || tempHyst > MAX_TEMP_HYST) bad = true;
        if (humHyst < MIN_HUM_HYST || humHyst > MAX_HUM_HYST) bad = true;

        if (bad) {
            Serial.println("[Prefs] NVS CORRUPTION DETECTED — falling back to defaults!");
            resetToDefaults();
        }
    }

public:
    void saveTempThresh(float v) {
        tempThresh = v;
        _prefs.putFloat("tempThresh", v);
    }

    void saveHumThresh(float v) {
        humThresh = v;
        _prefs.putFloat("humThresh", v);
    }

    void saveTempHyst(float v) {
        tempHyst = constrain(v, MIN_TEMP_HYST, MAX_TEMP_HYST);
        _prefs.putFloat("tempHyst", tempHyst);
    }

    void saveHumHyst(float v) {
        humHyst = constrain(v, MIN_HUM_HYST, MAX_HUM_HYST);
        _prefs.putFloat("humHyst", humHyst);
    }

    void saveIntervalSec(int v) {
        intervalSec = constrain(v, MIN_INTERVAL_SEC, MAX_INTERVAL_SEC);
        _prefs.putInt("intervalSec", intervalSec);
    }

    void saveOverrideLeft(bool v) {
        overrideLeft = v;
        overrideLeftSetAt = v ? millis() : 0;
        _prefs.putBool("ovrLeft", v);
    }

    void saveOverrideRight(bool v) {
        overrideRight = v;
        overrideRightSetAt = v ? millis() : 0;
        _prefs.putBool("ovrRight", v);
    }

    void saveOverrideTimeout(int minutes) {
        overrideTimeoutMin = minutes;
        _prefs.putInt("ovrTimeout", minutes);
    }

    // Verifică dacă un override a expirat și îl șterge dacă da.
    // Returnează true dacă starea s-a schimbat (pentru a triggera un update Blynk).
    bool tickOverrideExpiry() {
        bool changed = false;
        unsigned long nowMs = millis();
        unsigned long limitMs = (unsigned long)overrideTimeoutMin * 60UL * 1000UL;

        if (overrideLeft && overrideLeftSetAt > 0) {
            if (nowMs - overrideLeftSetAt >= limitMs) {
                saveOverrideLeft(false);
                Serial.println("[Override] Zona STANGA: timeout expirat, override anulat.");
                changed = true;
            }
        }
        if (overrideRight && overrideRightSetAt > 0) {
            if (nowMs - overrideRightSetAt >= limitMs) {
                saveOverrideRight(false);
                Serial.println("[Override] Zona DREAPTA: timeout expirat, override anulat.");
                changed = true;
            }
        }
        return changed;
    }

    void resetToDefaults() {
        _prefs.clear();
        tempThresh         = DEFAULT_TEMP_THRESH;
        humThresh          = DEFAULT_HUM_THRESH;
        tempHyst           = DEFAULT_TEMP_HYST;
        humHyst            = DEFAULT_HUM_HYST;
        intervalSec        = DEFAULT_INTERVAL_SEC;
        overrideLeft       = false;
        overrideRight      = false;
        overrideLeftSetAt  = 0;
        overrideRightSetAt = 0;
        overrideTimeoutMin = DEFAULT_OVERRIDE_TIMEOUT_MIN;
        // Re-salvăm explicit ca NVS să fie consistent
        _prefs.putFloat("tempThresh",  tempThresh);
        _prefs.putFloat("humThresh",   humThresh);
        _prefs.putFloat("tempHyst",    tempHyst);
        _prefs.putFloat("humHyst",     humHyst);
        _prefs.putInt  ("intervalSec", intervalSec);
        _prefs.putBool ("ovrLeft",     false);
        _prefs.putBool ("ovrRight",    false);
        _prefs.putInt  ("ovrTimeout",  overrideTimeoutMin);
        Serial.println("[Prefs] Reset la valorile default.");
    }

private:
    Preferences _prefs;
};

// LedConfigStorage.h — Master-side NVS persist for LED settings.
// Mirrors the Slave schedule so it survives Slave factory resets.
#pragma once

#include <Preferences.h>
#include "Config.h"

class LedConfigStorage {
public:
    uint8_t onH;
    uint8_t onM;
    uint8_t offH;
    uint8_t offM;
    uint8_t maxI;
    bool    enabled;

    void begin() {
        _prefs.begin(NVS_PREFS_NAMESPACE, false);
        load();
    }

    void load() {
        onH     = _prefs.getUChar("ledOnH", 8);     // Default 08:00
        onM     = _prefs.getUChar("ledOnM", 0);
        offH    = _prefs.getUChar("ledOffH", 22);   // Default 22:00
        offM    = _prefs.getUChar("ledOffM", 0);
        maxI    = _prefs.getUChar("ledMaxI", 80);   // Default 80%
        enabled = _prefs.getBool ("ledEn", false);  // Default off
    }

    void save(uint8_t _onH, uint8_t _onM, uint8_t _offH, uint8_t _offM, uint8_t _maxI, bool _en) {
        onH = _onH;
        onM = _onM;
        offH = _offH;
        offM = _offM;
        maxI = _maxI;
        enabled = _en;
        
        _prefs.putUChar("ledOnH", onH);
        _prefs.putUChar("ledOnM", onM);
        _prefs.putUChar("ledOffH", offH);
        _prefs.putUChar("ledOffM", offM);
        _prefs.putUChar("ledMaxI", maxI);
        _prefs.putBool ("ledEn", enabled);
    }

    // Sync la boot: verifica daca Slave are alta configuratie
    // false daca s-a detectat divergenta si e nevoie de re-sync.
    bool compare(uint8_t s_onH, uint8_t s_onM, uint8_t s_offH, uint8_t s_offM, uint8_t s_maxI, bool s_en) {
        return (onH == s_onH && onM == s_onM &&
                offH == s_offH && offM == s_offM &&
                maxI == s_maxI && enabled == s_en);
    }

private:
    Preferences _prefs;
};

// LedConfigStorage.h — Master-side NVS persist for LED settings.
// Mirrors the Slave schedule and pattern mode so they survive Slave factory resets.
#pragma once

#include <Preferences.h>
#include "Config.h"

class LedConfigStorage {
public:
    static constexpr uint8_t PATTERN_COUNT = 12;

    // Schedule
    uint8_t onH;
    uint8_t onM;
    uint8_t offH;
    uint8_t offM;
    uint8_t maxI;
    bool    enabled;

    // Pattern mode
    uint8_t modeId;
    struct PatternParams { uint16_t p1, p2, p3, p4; };
    PatternParams patterns[PATTERN_COUNT];

    void begin() {
        _prefs.begin(NVS_PREFS_NAMESPACE, false);
        load();
    }

    void load() {
        onH     = _prefs.getUChar("ledOnH", 18);
        onM     = _prefs.getUChar("ledOnM", 0);
        offH    = _prefs.getUChar("ledOffH", 23);
        offM    = _prefs.getUChar("ledOffM", 30);
        maxI    = _prefs.getUChar("ledMaxI", 80);
        enabled = _prefs.getBool ("ledEn", false);
        modeId  = _prefs.getUChar("ledModeId", 0);
        if (modeId >= PATTERN_COUNT) modeId = 0;
        size_t r = _prefs.getBytes("ledModePall", patterns, sizeof(patterns));
        if (r != sizeof(patterns)) _initPatternDefaults();
    }

    void save(uint8_t _onH, uint8_t _onM, uint8_t _offH, uint8_t _offM,
              uint8_t _maxI, bool _en) {
        onH = _onH; onM = _onM; offH = _offH; offM = _offM;
        maxI = _maxI; enabled = _en;
        _prefs.putUChar("ledOnH", onH);
        _prefs.putUChar("ledOnM", onM);
        _prefs.putUChar("ledOffH", offH);
        _prefs.putUChar("ledOffM", offM);
        _prefs.putUChar("ledMaxI", maxI);
        _prefs.putBool ("ledEn", enabled);
    }

    void saveMode(uint8_t id, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4) {
        if (id >= PATTERN_COUNT) return;
        modeId = id;
        patterns[id] = {p1, p2, p3, p4};
        _prefs.putUChar("ledModeId", modeId);
        _prefs.putBytes("ledModePall", patterns, sizeof(patterns));
    }

    bool compare(uint8_t s_onH, uint8_t s_onM, uint8_t s_offH, uint8_t s_offM,
                 uint8_t s_maxI, bool s_en) const {
        return (onH == s_onH && onM == s_onM &&
                offH == s_offH && offM == s_offM &&
                maxI == s_maxI && enabled == s_en);
    }

private:
    Preferences _prefs;

    void _initPatternDefaults() {
        patterns[0]  = {0,   0,   0,  0};
        patterns[1]  = {10,  4,   0,  0};
        patterns[2]  = {10,  4,   0,  0};
        patterns[3]  = {10,  4,   0,  0};
        patterns[4]  = {50,  20,  0,  0};
        patterns[5]  = {65,  80,  0,  0};
        patterns[6]  = {50,  0,   0,  0};
        patterns[7]  = {5,   20,  0,  0};
        patterns[8]  = {200, 0,   0,  0};
        patterns[9]  = {30,  100, 0,  0};
        patterns[10] = {30,  100, 0,  0};
        patterns[11] = {10,  100, 50, 0};
    }
};

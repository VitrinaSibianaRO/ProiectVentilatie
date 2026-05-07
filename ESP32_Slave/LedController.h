// LedController.h — PWM LED 24V 36W prin NCEP01T18, cu schedule NVS persistent.
// PWM LEDC 12-bit la 5kHz pe GPIO 25. Schedule: fereastra orara + intensitate max.
// Persist NVS namespace "led". Sync time via TIME_SYNC de la Master (NTP-ul masterului).
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"
#include "Logger.h"

class LedController {
public:
    static constexpr uint16_t PWM_MAX = (1u << LED_PWM_BITS) - 1;  // 4095

    // Schedule snapshot — pentru status report si comparatii.
    struct Schedule {
        uint8_t onHour;
        uint8_t onMin;
        uint8_t offHour;
        uint8_t offMin;
        uint8_t maxIntensity;   // 0-100%
        bool    enabled;
    };

    LedController()
        : _currentPercent(0),
          _schedule{18, 0, 23, 30, 80, false},
          _syncedEpoch(0),
          _manualOverride(false),
          _manualOverrideUntilSec(0) {}

    LedController(const LedController&) = delete;
    LedController& operator=(const LedController&) = delete;

    void begin() {
        // esp32 core v3.x API: ledcAttach combina setup + attach pe pin
        ledcAttach(LED_PWM_PIN, LED_PWM_FREQ_HZ, LED_PWM_BITS);
        _loadFromNvs();
        _applyPwm(0);   // start OFF
        LOG_INFO("LedController init: schedule %02u:%02u->%02u:%02u @%u%% en=%d",
                 _schedule.onHour, _schedule.onMin,
                 _schedule.offHour, _schedule.offMin,
                 _schedule.maxIntensity, _schedule.enabled);
    }

    // Setare imediata intensitate (0-100%). Override schedule 1 ora.
    // Daca timpul nu e sincronizat, override-ul tine pana la primul TIME_SYNC,
    // moment in care se reseteaza la _now+3600s.
    void setIntensity(uint8_t percent) {
        if (percent > 100) percent = 100;
        _manualOverride = true;
        if (_syncedEpoch > 0) {
            _manualOverrideUntilSec = _syncedEpoch + 3600;
        } else {
            // Timp ne-sincronizat → override expira la primul TIME_SYNC.
            // Marker special: 1 (impossibil ca real epoch).
            _manualOverrideUntilSec = 1;
        }
        _applyPwm(percent);
        LOG_INFO("LED manual: %u%% (override until %lu)", percent, _manualOverrideUntilSec);
    }

    // Configurare schedule. Persistent in NVS. Canceleaza orice manual override.
    void setSchedule(uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                     uint8_t maxIntensity, bool enabled) {
        if (onH > 23 || offH > 23 || onM > 59 || offM > 59 || maxIntensity > 100) {
            LOG_WARN("LED schedule values out of range, rejected");
            return;
        }
        _schedule = {onH, onM, offH, offM, maxIntensity, enabled};
        _manualOverride = false;
        _saveToNvs();
        LOG_INFO("LED schedule saved: %02u:%02u->%02u:%02u @%u%% en=%d",
                 onH, onM, offH, offM, maxIntensity, enabled);
    }

    // Sincronizare timp de la Master (epoch seconds).
    void syncTime(uint32_t epochSec) {
        if (epochSec < 1700000000UL) return;
        struct timeval tv = { .tv_sec = (time_t)epochSec, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        _syncedEpoch = epochSec;
        // Daca aveam override pending (marker=1), il consolidam acum la +1h.
        if (_manualOverride && _manualOverrideUntilSec == 1) {
            _manualOverrideUntilSec = epochSec + 3600;
            LOG_INFO("TIME_SYNC: override consolidat pana la %lu", _manualOverrideUntilSec);
        }
        LOG_INFO("TIME_SYNC: epoch=%lu", epochSec);
    }

    // Apelat din loop() — evalueaza schedule si aplica PWM daca e nevoie.
    void tick() {
        const uint32_t now = (uint32_t)time(nullptr);
        if (now < 1700000000UL) return;   // timp nesincronizat

        // Verifica expirare override manual
        if (_manualOverride && _manualOverrideUntilSec > 0 && now > _manualOverrideUntilSec) {
            _manualOverride = false;
            LOG_INFO("LED manual override expired");
        }
        if (_manualOverride || !_schedule.enabled) return;

        struct tm timeinfo;
        time_t t = (time_t)now;
        if (!localtime_r(&t, &timeinfo)) return;

        const bool inWindow = _isInWindow(
            (uint8_t)timeinfo.tm_hour, (uint8_t)timeinfo.tm_min,
            _schedule.onHour, _schedule.onMin,
            _schedule.offHour, _schedule.offMin);

        const uint8_t target = inWindow ? _schedule.maxIntensity : 0;
        if (target != _currentPercent) {
            _applyPwm(target);
            LOG_INFO("LED schedule: %u%% (window=%d)", target, inWindow);
        }
        // NU actualizam _syncedEpoch aici — _syncedEpoch e timestamp ULTIMA SYNC,
        // nu time-of-day curent. Drift verificat doar la TIME_SYNC explicit.
    }

    uint8_t  getCurrentIntensity() const { return _currentPercent; }
    Schedule getSchedule()          const { return _schedule; }
    bool     hasTimeSync()          const { return _syncedEpoch > 1700000000UL; }

private:
    uint8_t   _currentPercent;
    Schedule  _schedule;
    uint32_t  _syncedEpoch;
    bool      _manualOverride;
    uint32_t  _manualOverrideUntilSec;

    void _applyPwm(uint8_t percent) {
        _currentPercent = percent;
        const uint32_t duty = (uint32_t)percent * PWM_MAX / 100;
        ledcWriteChannel(LED_PWM_CHANNEL, duty);
    }

    // Fereastra poate trece miezul noptii (ex. 22:00 → 06:00).
    static bool _isInWindow(uint8_t curH, uint8_t curM,
                             uint8_t onH,  uint8_t onM,
                             uint8_t offH, uint8_t offM) {
        const int cur = curH * 60 + curM;
        const int on  = onH  * 60 + onM;
        const int off = offH * 60 + offM;
        if (on <= off) {
            return cur >= on && cur < off;
        } else {
            // trece miezul noptii
            return cur >= on || cur < off;
        }
    }

    void _loadFromNvs() {
        Preferences p;
        p.begin("led", true);
        _schedule.onHour       = p.getUChar("oh", 18);
        _schedule.onMin        = p.getUChar("om", 0);
        _schedule.offHour      = p.getUChar("fh", 23);
        _schedule.offMin       = p.getUChar("fm", 30);
        _schedule.maxIntensity = p.getUChar("mi", 80);
        _schedule.enabled      = p.getBool ("en", false);
        p.end();
    }

    void _saveToNvs() const {
        Preferences p;
        p.begin("led", false);
        p.putUChar("oh", _schedule.onHour);
        p.putUChar("om", _schedule.onMin);
        p.putUChar("fh", _schedule.offHour);
        p.putUChar("fm", _schedule.offMin);
        p.putUChar("mi", _schedule.maxIntensity);
        p.putBool ("en", _schedule.enabled);
        p.end();
    }
};

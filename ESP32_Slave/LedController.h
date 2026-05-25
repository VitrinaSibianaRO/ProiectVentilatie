// LedController.h — PWM LED 24V 36W prin NCEP01T18, cu schedule NVS persistent
// și 12 moduri de efect (pattern state machine).
// effectTick() apelat la 10ms din loop() aplică waveform-ul activ.
#pragma once
#include "Config.h"
#include "Logger.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

class LedController {
public:
  static constexpr uint16_t PWM_MAX = (1u << LED_PWM_BITS) - 1; // 4095
  static constexpr uint8_t PATTERN_COUNT = 12;

  struct Schedule {
    uint8_t onHour;
    uint8_t onMin;
    uint8_t offHour;
    uint8_t offMin;
    uint8_t maxIntensity; // 0-100%
    bool enabled;
  };

  struct PatternConfig {
    uint16_t p1, p2, p3, p4;
  };

  LedController()
      : _targetPercent(0), _schedule{18, 0, 23, 30, 80, false},
        _syncedEpoch(0), _manualOverride(false), _manualIntensity(0),
        _wasInWindow(false),
        _patternMode(0), _patternStateMs(0),
        _nextLightningMs(0), _lightningFlashing(false),
        _candleLastChangeMs(0), _candlePrevTarget(0), _candleCurTarget(0),
        _randomLastChangeMs(0), _randomPrevTarget(0), _randomCurTarget(0) {
    _initPatternDefaults();
  }

  LedController(const LedController &) = delete;
  LedController &operator=(const LedController &) = delete;

  void begin() {
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    ledcAttach(LED_PWM_PIN, LED_PWM_FREQ_HZ, LED_PWM_BITS);
    _loadFromNvs();

    if (_manualIntensity > 0) {
      _manualOverride = true;
      _targetPercent = _manualIntensity;
    }

    _patternStateMs = millis();

    LOG_INFO("LedController init: schedule %02u:%02u->%02u:%02u @%u%% en=%d mode=%u",
             _schedule.onHour, _schedule.onMin, _schedule.offHour,
             _schedule.offMin, _schedule.maxIntensity, _schedule.enabled,
             _patternMode);
  }

  void setIntensity(uint8_t percent) {
    if (percent > 100) percent = 100;
    _manualOverride = true;
    _manualIntensity = percent;
    _saveManToNvs();
    _applyPwm(percent);
    LOG_INFO("LED manual: %u%% (persistent)", percent);
  }

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

  void setMode(uint8_t mode, uint16_t p1, uint16_t p2, uint16_t p3, uint16_t p4) {
    if (mode >= PATTERN_COUNT) {
      LOG_WARN("setMode: mode %u out of range", mode);
      return;
    }
    _patterns[mode] = {p1, p2, p3, p4};
    _patternMode = mode;
    _patternStateMs = millis();
    // Reset animation state on mode switch
    _nextLightningMs = 0;
    _lightningFlashing = false;
    _candleLastChangeMs = 0;
    _randomLastChangeMs = 0;
    _saveModeToNvs();
    LOG_INFO("LED mode=%u p1=%u p2=%u p3=%u p4=%u", mode, p1, p2, p3, p4);
  }

  void syncTime(uint32_t epochSec) {
    if (epochSec < 1700000000UL) return;
    struct timeval tv = {.tv_sec = (time_t)epochSec, .tv_usec = 0};
    settimeofday(&tv, nullptr);
    _syncedEpoch = epochSec;

    struct tm timeinfo;
    time_t t = (time_t)epochSec;
    localtime_r(&t, &timeinfo);
    LOG_INFO("TIME_SYNC: %02d:%02d:%02d (epoch=%lu)",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, epochSec);
  }

  // Evaluează schedule-ul și actualizează _targetPercent. Apelat din loop().
  void tick() {
    const uint32_t now = (uint32_t)time(nullptr);
    if (now < 1700000000UL) return;

    struct tm timeinfo;
    time_t t = (time_t)now;
    if (!localtime_r(&t, &timeinfo)) return;

    const bool inWindow = _isInWindow(
        (uint8_t)timeinfo.tm_hour, (uint8_t)timeinfo.tm_min,
        _schedule.onHour, _schedule.onMin,
        _schedule.offHour, _schedule.offMin);

    if (inWindow != _wasInWindow) {
      _wasInWindow = inWindow;
      if (_manualOverride) {
        _manualOverride = false;
        LOG_INFO("LED override anulat de tranzitia schedule-ului");
      }
    }

    if (_manualOverride || !_schedule.enabled) return;

    const uint8_t target = inWindow ? _schedule.maxIntensity : 0;
    if (target != _targetPercent) {
      _applyPwm(target);
      LOG_INFO("LED schedule: %u%% (window=%d)", target, inWindow);
    }
  }

  // Calculează și aplică waveform-ul activ pe PWM. Apelat la 10ms din loop().
  void effectTick() {
    if (_targetPercent == 0) {
      _applyPwmRaw(0);
      return;
    }
    uint8_t b;
    switch (_patternMode) {
      case 0:  b = _targetPercent;         break;
      case 1:  b = _computeBreathing();    break;
      case 2:  b = _computeTriangle();     break;
      case 3:  b = _computeSawtooth();     break;
      case 4:  b = _computeStrobe();       break;
      case 5:  b = _computeHeartbeat();    break;
      case 6:  b = _computeCandle();       break;
      case 7:  b = _computeLightning();    break;
      case 8:  b = _computeMorseSos();     break;
      case 9:  b = _computeSunrise();      break;
      case 10: b = _computeSunset();       break;
      case 11: b = _computeRandom();       break;
      default: b = _targetPercent;         break;
    }
    _applyPwmRaw(b);
  }

  uint8_t getCurrentIntensity() const { return _targetPercent; }
  uint8_t getPatternMode() const { return _patternMode; }
  PatternConfig getPatternConfig(uint8_t mode) const {
    if (mode >= PATTERN_COUNT) return {0, 0, 0, 0};
    return _patterns[mode];
  }
  Schedule getSchedule() const { return _schedule; }
  bool hasTimeSync() const { return _syncedEpoch > 1700000000UL; }

private:
  uint8_t  _targetPercent;  // cap curent (setat de schedule sau manual)
  Schedule _schedule;
  uint32_t _syncedEpoch;
  bool     _manualOverride;
  uint8_t  _manualIntensity;
  bool     _wasInWindow;

  uint8_t       _patternMode;
  PatternConfig _patterns[PATTERN_COUNT];
  uint32_t      _patternStateMs;

  // Animation state per pattern
  uint32_t _nextLightningMs;
  bool     _lightningFlashing;
  uint32_t _candleLastChangeMs;
  uint8_t  _candlePrevTarget;
  uint8_t  _candleCurTarget;
  uint32_t _randomLastChangeMs;
  uint8_t  _randomPrevTarget;
  uint8_t  _randomCurTarget;

  // Setează _targetPercent fără a scrie hardware
  void _applyPwm(uint8_t percent) {
    _targetPercent = percent;
  }

  // Scrie direct pe hardware PWM
  void _applyPwmRaw(uint8_t percent) {
    if (percent > 100) percent = 100;
    const uint32_t duty = (uint32_t)percent * PWM_MAX / 100;
    ledcWrite(LED_PWM_PIN, duty);
  }

  static bool _isInWindow(uint8_t curH, uint8_t curM,
                           uint8_t onH, uint8_t onM,
                           uint8_t offH, uint8_t offM) {
    const int cur = curH * 60 + curM;
    const int on  = onH  * 60 + onM;
    const int off = offH * 60 + offM;
    if (on <= off) return cur >= on && cur < off;
    return cur >= on || cur < off;
  }

  // ============================================================
  //  Compute functions (returnează 0-100)
  // ============================================================

  uint8_t _computeBreathing() {
    const auto& cfg = _patterns[1];
    uint8_t minP = (uint8_t)constrain((int)cfg.p1, 0, 100);
    if (minP >= _targetPercent || cfg.p2 == 0) return _targetPercent;
    float t = (float)(millis() - _patternStateMs) / 1000.0f;
    float phase = fmodf(t, (float)cfg.p2) / (float)cfg.p2;
    float f = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * phase);
    return (uint8_t)((float)minP + (float)(_targetPercent - minP) * f);
  }

  uint8_t _computeTriangle() {
    const auto& cfg = _patterns[2];
    uint8_t minP = (uint8_t)constrain((int)cfg.p1, 0, 100);
    if (minP >= _targetPercent || cfg.p2 == 0) return _targetPercent;
    float t = (float)(millis() - _patternStateMs) / 1000.0f;
    float phase = fmodf(t, (float)cfg.p2) / (float)cfg.p2;
    float f = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f);
    return (uint8_t)((float)minP + (float)(_targetPercent - minP) * f);
  }

  uint8_t _computeSawtooth() {
    const auto& cfg = _patterns[3];
    uint8_t minP = (uint8_t)constrain((int)cfg.p1, 0, 100);
    if (minP >= _targetPercent || cfg.p2 == 0) return _targetPercent;
    float t = (float)(millis() - _patternStateMs) / 1000.0f;
    float phase = fmodf(t, (float)cfg.p2) / (float)cfg.p2;
    float f = (cfg.p3 == 0) ? phase : (1.0f - phase);
    return (uint8_t)((float)minP + (float)(_targetPercent - minP) * f);
  }

  uint8_t _computeStrobe() {
    const auto& cfg = _patterns[4];
    if (cfg.p2 == 0) return _targetPercent;
    float freqHz = (float)cfg.p2 / 10.0f;
    if (freqHz <= 0.0f) return _targetPercent;
    float periodMs = 1000.0f / freqHz;
    float elapsed = (float)(millis() - _patternStateMs);
    float phase = fmodf(elapsed, periodMs) / periodMs;
    float duty = (float)cfg.p1 / 100.0f;
    return (phase < duty) ? _targetPercent : 0;
  }

  uint8_t _computeHeartbeat() {
    const auto& cfg = _patterns[5];
    if (cfg.p1 == 0) return 0;
    float periodMs = 60000.0f / (float)cfg.p1;
    if (periodMs <= 0.0f) return 0;
    float elapsed = (float)(millis() - _patternStateMs);
    float phase = fmodf(elapsed, periodMs) / periodMs;
    uint8_t maxB = (uint8_t)constrain((int)cfg.p2, 0, 100);
    uint8_t cap = (maxB > _targetPercent) ? _targetPercent : maxB;
    if (phase < 0.10f)      return (uint8_t)(cap * (phase / 0.10f));
    if (phase < 0.15f)      return (uint8_t)(cap * (1.0f - (phase - 0.10f) / 0.05f));
    if (phase < 0.20f)      return (uint8_t)(cap * 0.7f * ((phase - 0.15f) / 0.05f));
    if (phase < 0.27f)      return (uint8_t)(cap * 0.7f * (1.0f - (phase - 0.20f) / 0.07f));
    return 0;
  }

  uint8_t _computeCandle() {
    const auto& cfg = _patterns[6];
    const uint32_t now = millis();
    if (_candleLastChangeMs == 0) {
      _candlePrevTarget = _targetPercent;
      _candleCurTarget  = _targetPercent;
      _candleLastChangeMs = now;
    }
    uint32_t intervalMs = 80 + (uint32_t)random(40);
    if (now - _candleLastChangeMs >= intervalMs) {
      _candlePrevTarget = _candleCurTarget;
      int variation = constrain((int)cfg.p1, 0, 100);
      int delta = (int)random(-variation, variation + 1) * (int)_targetPercent / 100;
      int newT = constrain((int)_targetPercent + delta, 0, (int)_targetPercent);
      _candleCurTarget = (uint8_t)newT;
      _candleLastChangeMs = now;
      return _candleCurTarget;
    }
    float t = (float)(now - _candleLastChangeMs) / (float)intervalMs;
    if (t > 1.0f) t = 1.0f;
    int16_t diff = (int16_t)_candleCurTarget - (int16_t)_candlePrevTarget;
    return (uint8_t)((int16_t)_candlePrevTarget + (int16_t)((float)diff * t));
  }

  uint8_t _computeLightning() {
    const auto& cfg = _patterns[7];
    const uint32_t now = millis();
    uint16_t flashesPerMin = cfg.p1 < 1 ? 1 : cfg.p1;
    uint32_t avgIntervalMs = 60000UL / flashesPerMin;

    if (_nextLightningMs == 0) {
      _nextLightningMs = now + (uint32_t)random(avgIntervalMs / 2, avgIntervalMs);
      _lightningFlashing = false;
    }

    if (_lightningFlashing) {
      if (now >= _nextLightningMs) {
        _lightningFlashing = false;
        _nextLightningMs = now + (uint32_t)random(avgIntervalMs / 2, avgIntervalMs);
      } else {
        return _targetPercent;
      }
    } else if (now >= _nextLightningMs) {
      _lightningFlashing = true;
      _nextLightningMs = now + 200 + (uint32_t)random(50);
      return _targetPercent;
    }

    uint8_t baseline = (uint8_t)constrain((int)cfg.p2, 0, 100);
    return (baseline > _targetPercent) ? _targetPercent : baseline;
  }

  uint8_t _computeMorseSos() {
    const auto& cfg = _patterns[8];
    static const uint8_t SOS[] = {
      1,0,1,0,1,0,0,0,3,0,3,0,3,0,0,0,1,0,1,0,1,0,0,0,0,0,0,0
    };
    static constexpr uint8_t SOS_LEN = sizeof(SOS);
    uint16_t dit = cfg.p1 < 50 ? 50 : cfg.p1;
    uint32_t elapsed = millis() - _patternStateMs;
    uint32_t idx = (elapsed / dit) % SOS_LEN;
    return (SOS[idx] != 0) ? _targetPercent : 0;
  }

  uint8_t _computeSunrise() {
    const auto& cfg = _patterns[9];
    if (cfg.p1 == 0) return _targetPercent;
    uint32_t durMs = (uint32_t)cfg.p1 * 60000UL;
    uint32_t elapsed = (millis() - _patternStateMs) % durMs;
    float t = (float)elapsed / (float)durMs;
    uint8_t finalP = (uint8_t)constrain((int)cfg.p2, 0, 100);
    uint8_t cap = (finalP > _targetPercent) ? _targetPercent : finalP;
    return (uint8_t)((float)cap * t);
  }

  uint8_t _computeSunset() {
    const auto& cfg = _patterns[10];
    if (cfg.p1 == 0) return _targetPercent;
    uint32_t durMs = (uint32_t)cfg.p1 * 60000UL;
    uint32_t elapsed = (millis() - _patternStateMs) % durMs;
    float t = (float)elapsed / (float)durMs;
    uint8_t startP = (uint8_t)constrain((int)cfg.p2, 0, 100);
    uint8_t cap = (startP > _targetPercent) ? _targetPercent : startP;
    return (uint8_t)((float)cap * (1.0f - t));
  }

  uint8_t _computeRandom() {
    const auto& cfg = _patterns[11];
    const uint32_t now = millis();
    uint8_t minP = (uint8_t)constrain((int)cfg.p1, 0, 100);
    uint8_t maxP = (uint8_t)constrain((int)cfg.p2, 0, 100);
    if (minP > maxP) maxP = minP;
    uint8_t speed = (uint8_t)constrain((int)cfg.p3, 1, 100);

    if (_randomLastChangeMs == 0) {
      _randomPrevTarget = minP;
      _randomCurTarget  = minP;
      _randomLastChangeMs = now;
    }

    uint32_t intervalMs = 200 + (uint32_t)(100 - speed) * 30;
    if (now - _randomLastChangeMs >= intervalMs) {
      _randomPrevTarget = _randomCurTarget;
      uint8_t newT = (uint8_t)random(minP, (long)maxP + 1);
      if (newT > _targetPercent) newT = _targetPercent;
      _randomCurTarget = newT;
      _randomLastChangeMs = now;
      return _randomCurTarget;
    }
    float t = (float)(now - _randomLastChangeMs) / (float)intervalMs;
    if (t > 1.0f) t = 1.0f;
    int16_t diff = (int16_t)_randomCurTarget - (int16_t)_randomPrevTarget;
    return (uint8_t)((int16_t)_randomPrevTarget + (int16_t)((float)diff * t));
  }

  // ============================================================
  //  NVS
  // ============================================================

  void _initPatternDefaults() {
    _patterns[0]  = {0,   0,   0,  0};
    _patterns[1]  = {10,  4,   0,  0};   // BREATHING: min=10%, dur=4s
    _patterns[2]  = {10,  4,   0,  0};   // TRIANGLE
    _patterns[3]  = {10,  4,   0,  0};   // SAWTOOTH
    _patterns[4]  = {50,  20,  0,  0};   // STROBE: duty=50%, 2.0Hz
    _patterns[5]  = {65,  80,  0,  0};   // HEARTBEAT: 65bpm, 80%
    _patterns[6]  = {50,  0,   0,  0};   // CANDLE: variation=50%
    _patterns[7]  = {5,   20,  0,  0};   // LIGHTNING: 5/min, baseline=20%
    _patterns[8]  = {200, 0,   0,  0};   // SOS_MORSE: 200ms/dit
    _patterns[9]  = {30,  100, 0,  0};   // SUNRISE: 30min, final=100%
    _patterns[10] = {30,  100, 0,  0};   // SUNSET: 30min, start=100%
    _patterns[11] = {10,  100, 50, 0};   // RANDOM: 10-100%, speed=50
  }

  void _loadFromNvs() {
    Preferences p;
    p.begin("led", true);
    _schedule.onHour       = p.getUChar("oh",   18);
    _schedule.onMin        = p.getUChar("om",    0);
    _schedule.offHour      = p.getUChar("fh",   23);
    _schedule.offMin       = p.getUChar("fm",   30);
    _schedule.maxIntensity = p.getUChar("mi",   80);
    _schedule.enabled      = p.getBool ("en",   false);
    _manualIntensity       = p.getUChar("manI",  0);
    _patternMode           = p.getUChar("pmode", 0);
    if (_patternMode >= PATTERN_COUNT) _patternMode = 0;
    size_t bytesRead = p.getBytes("pall", &_patterns, sizeof(_patterns));
    if (bytesRead != sizeof(_patterns)) {
      _initPatternDefaults();
      LOG_WARN("LED patterns NVS blob corrupt, using defaults");
    }
    p.end();
  }

  void _saveManToNvs() const {
    Preferences p;
    p.begin("led", false);
    p.putUChar("manI", _manualIntensity);
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

  void _saveModeToNvs() const {
    Preferences p;
    p.begin("led", false);
    p.putUChar("pmode", _patternMode);
    p.putBytes("pall", &_patterns, sizeof(_patterns));
    p.end();
  }
};

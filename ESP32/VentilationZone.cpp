// ============================================================
//  VentilationZone.cpp
// ============================================================

#include "VentilationZone.h"
#include "Config.h"

VentilationZone::VentilationZone(int dhtPin, int relPin, const char* name)
    : _dht(dhtPin, DHT22),
      _relayPin(relPin),
      _name(name),
      _currentTemp(0.0f),
      _currentHum(0.0f),
      _manualOverride(false),
      _firstReadDone(false),
      _relayState(false),
      _lastReadMs(0),
      _consecutiveErrors(0) {}

void VentilationZone::begin() {
    // Ordinea corectă: mai întâi modul pinului, abia apoi scriem starea.
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, HIGH);   // releu Active-LOW → oprit la boot
    _dht.begin();
}

void VentilationZone::setManualOverride(bool state) {
    _manualOverride = state;
}

bool VentilationZone::getManualOverride() const {
    return _manualOverride;
}

void VentilationZone::readSensor() {
    unsigned long nowMs = millis();

    // Respectăm cooldown-ul minim al DHT22 (2100ms).
    // Metoda este sigură de apelat oricând — returnează silențios dacă e prea devreme.
    if (nowMs - _lastReadMs < DHT_MIN_READ_MS) return;

    float rawT = _dht.readTemperature();
    float rawH = _dht.readHumidity();

    bool valid = !isnan(rawT) && !isnan(rawH)
                 && rawT > -20.0f && rawT < 80.0f
                 && rawH >= 0.0f  && rawH <= 100.0f;

    // Actualizăm timestamp-ul indiferent de rezultat — evităm să bombardăm
    // senzorul cu cereri repetate în caz de eroare.
    _lastReadMs = nowMs;

    if (valid) {
        _currentTemp       = rawT;
        _currentHum        = rawH;
        _firstReadDone     = true;
        _consecutiveErrors = 0;
    } else {
        _consecutiveErrors++;
        // Păstrăm ultima valoare validă — nu resetăm la 0.
        Serial.printf("[!] %s: eroare citire #%d. Se păstrează: T:%.1f°C H:%.1f%%\n",
                      _name, _consecutiveErrors, _currentTemp, _currentHum);
    }
}

void VentilationZone::updateLogic(float threshTemp, float threshHum,
                                   float hystTemp,  float hystHum) {
    bool autoOn;
    if (!_firstReadDone) {
        autoOn = false;
    } else if (_relayState) {
        // Releu ON: rămâne ON cât timp cel puțin o valoare e deasupra (prag − hyst).
        // Se oprește doar când AMBELE coboară sub banda de histerezis.
        autoOn = (_currentTemp >= (threshTemp - hystTemp))
              || (_currentHum  >= (threshHum  - hystHum));
    } else {
        // Releu OFF: pornește dacă ORICARE depășește pragul.
        autoOn = (_currentTemp >= threshTemp) || (_currentHum >= threshHum);
    }
    _relayState = autoOn || _manualOverride;
    digitalWrite(_relayPin, _relayState ? LOW : HIGH);  // Active-LOW
}

void VentilationZone::emergencyOff() {
    _relayState = false;
    _manualOverride = false;
    digitalWrite(_relayPin, HIGH);
}

float       VentilationZone::getTemp()         const { return _currentTemp; }
float       VentilationZone::getHum()          const { return _currentHum; }
bool        VentilationZone::getRelayState()   const { return _relayState; }
bool        VentilationZone::isFirstReadDone() const { return _firstReadDone; }
int         VentilationZone::getConsecErrors() const { return _consecutiveErrors; }
const char* VentilationZone::getName()         const { return _name; }

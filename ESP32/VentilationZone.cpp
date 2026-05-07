// ============================================================
//  VentilationZone.cpp
//  Implementare dual-mode: senzor local (Sht30Sensor) sau
//  senzor remote (valori de la Slave prin UART).
// ============================================================

#include "VentilationZone.h"
#include "Config.h"

// Constructor LOCAL — zona cu senzor Sht30 conectat direct
VentilationZone::VentilationZone(Sht30Sensor* localSensor, int relayPin, const char* name)
    : _localSensor(localSensor),
      _relayPin(relayPin),
      _name(name),
      _currentTemp(0.0f),
      _currentHum(0.0f),
      _lastExternalTs(0),
      _manualOverride(false),
      _firstReadDone(false),
      _relayState(false),
      _failsafe(false),
      _consecutiveErrors(0) {}

// Constructor REMOTE — zona cu senzor pe Slave (fără local sensor)
VentilationZone::VentilationZone(int relayPin, const char* name)
    : _localSensor(nullptr),
      _relayPin(relayPin),
      _name(name),
      _currentTemp(0.0f),
      _currentHum(0.0f),
      _lastExternalTs(0),
      _manualOverride(false),
      _firstReadDone(false),
      _relayState(false),
      _failsafe(false),
      _consecutiveErrors(0) {}

void VentilationZone::begin() {
    // Ordinea corectă: mai întâi modul pinului, abia apoi scriem starea.
    pinMode(_relayPin, OUTPUT);
    digitalWrite(_relayPin, HIGH);   // releu Active-LOW → oprit la boot

    // Senzor local: inițializare (Wire.begin() trebuie apelat înainte)
    if (_localSensor) {
        if (!_localSensor->begin(SHT30_ADDR)) {
            Serial.printf("[%s] SHT30 init FAIL la 0x%02X\n", _name, SHT30_ADDR);
        } else {
            Serial.printf("[%s] SHT30 OK la 0x%02X\n", _name, SHT30_ADDR);
        }
    }
}

void VentilationZone::setManualOverride(bool state) {
    _manualOverride = state;
}

bool VentilationZone::getManualOverride() const {
    return _manualOverride;
}

void VentilationZone::readSensor(bool force) {
    // Doar pentru zone cu senzor local. Zone remote primesc date prin setExternalSensorValues().
    if (!_localSensor) return;

    float temp, hum;
    if (_localSensor->read(temp, hum, force)) {
        _currentTemp       = temp;
        _currentHum        = hum;
        _firstReadDone     = true;
        _consecutiveErrors = 0;
    } else {
        _consecutiveErrors++;
        // Păstrăm ultima valoare validă — nu resetăm la 0.
        Serial.printf("[!] %s: eroare citire SHT30 #%d. Se păstrează: T:%.1f°C H:%.1f%%\n",
                      _name, _consecutiveErrors, _currentTemp, _currentHum);
    }
}

void VentilationZone::setExternalSensorValues(float temp, float hum, uint32_t ts) {
    _currentTemp   = temp;
    _currentHum    = hum;
    _lastExternalTs = ts;
    _firstReadDone  = true;
    _consecutiveErrors = 0;
}

void VentilationZone::updateLogic(float threshTemp, float threshHum,
                                   float hystTemp,  float hystHum) {
    // Failsafe: releul rămâne OFF forțat
    if (_failsafe) {
        _relayState = false;
        digitalWrite(_relayPin, HIGH);   // Active-LOW → OFF
        return;
    }

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
    _failsafe = false;
    digitalWrite(_relayPin, HIGH);
}

void VentilationZone::enterFailsafe() {
    _failsafe = true;
    _relayState = false;
    digitalWrite(_relayPin, HIGH);   // Active-LOW → OFF
    Serial.printf("[%s] FAILSAFE: relay forced OFF\n", _name);
}

void VentilationZone::exitFailsafe() {
    _failsafe = false;
    Serial.printf("[%s] FAILSAFE: exited, normal operation resumed\n", _name);
}

bool VentilationZone::isInFailsafe() const { return _failsafe; }

float       VentilationZone::getTemp()         const { return _currentTemp; }
float       VentilationZone::getHum()          const { return _currentHum; }
bool        VentilationZone::getRelayState()   const { return _relayState; }
bool        VentilationZone::isFirstReadDone() const { return _firstReadDone; }
int         VentilationZone::getConsecErrors() const { return _consecutiveErrors; }
const char* VentilationZone::getName()         const { return _name; }

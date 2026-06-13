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
      _forceOff(false),
      _firstReadDone(false),
      _relayState(false),
      _failsafe(false),
      _consecutiveErrors(0),
      _stuckCount(0),
      _lastSwitchMs(0) {}

// Constructor REMOTE — zona cu senzor pe Slave (fără local sensor)
VentilationZone::VentilationZone(int relayPin, const char* name)
    : _localSensor(nullptr),
      _relayPin(relayPin),
      _name(name),
      _currentTemp(0.0f),
      _currentHum(0.0f),
      _lastExternalTs(0),
      _manualOverride(false),
      _forceOff(false),
      _firstReadDone(false),
      _relayState(false),
      _failsafe(false),
      _consecutiveErrors(0),
      _stuckCount(0),
      _lastSwitchMs(0) {}

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

void VentilationZone::setForceOff(bool state) { _forceOff = state; }
bool VentilationZone::getForceOff() const { return _forceOff; }

void VentilationZone::updateLogic(float threshTemp, float threshHum,
                                   float hystTemp,  float hystHum,
                                   bool forceImmediate) {
    // Prioritate: failsafe > forceOff > manualOverride > autoOn
    if (_failsafe) {
        _relayState = false;
        digitalWrite(_relayPin, HIGH);   // Active-LOW → OFF
        return;
    }

    // forceOff: oprire manuala explicita — ignora senzorii (v=2 MQTT)
    if (_forceOff) {
        if (_relayState) {
            Serial.printf("[%s] Relay OFF (forceOff manual activ)\n", _name);
        }
        _relayState = false;
        digitalWrite(_relayPin, HIGH);
        return;
    }

    // Anti-chatter #1: histerezis minim EFECTIV impus — banda moarta sub prag
    // nu dispare niciodata, chiar daca userul seteaza H=0 din MAUI.
    if (hystTemp < MIN_EFFECTIVE_TEMP_HYST) hystTemp = MIN_EFFECTIVE_TEMP_HYST;
    if (hystHum  < MIN_EFFECTIVE_HUM_HYST)  hystHum  = MIN_EFFECTIVE_HUM_HYST;

    const bool prevState = _relayState;
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

    const bool desired = autoOn || _manualOverride;

    if (desired != prevState) {
        // Anti-chatter #2: timp minim intre comutari pentru tranzitiile AUTO.
        // Comuta IMEDIAT la actiuni explicite ale userului: override manual SAU
        // o comanda (forceImmediate, ex. Save din Settings). Anti-chatter-ul ramane
        // doar pentru oscilatia automata periodica (zgomot senzor). Safety OFF
        // (failsafe/forceOff) e tratat in early-return de mai sus.
        const unsigned long now = millis();
        const bool immediate = _manualOverride || forceImmediate;
        if (!immediate && _lastSwitchMs != 0 &&
            (now - _lastSwitchMs < RELAY_MIN_SWITCH_MS)) {
            // Suprima comutarea auto — pastreaza starea, reevalueaza la ciclul urmator.
            Serial.printf("[%s] Comutare auto suprimata (anti-chatter, %lus ramase)\n",
                          _name,
                          (RELAY_MIN_SWITCH_MS - (now - _lastSwitchMs)) / 1000UL);
        } else {
            _relayState   = desired;
            _lastSwitchMs = now;
        }
    }

    const int wantLevel = _relayState ? LOW : HIGH;     // Active-LOW
    digitalWrite(_relayPin, wantLevel);

    // Log doar la schimbare de stare — arata factorii deciziei (debug relee).
    if (_relayState != prevState) {
        Serial.printf("[%s] Relay %s | T=%.1f(>=%.1f) H=%.1f(>=%.1f) auto=%d manual=%d\n",
                      _name, _relayState ? "ON" : "OFF",
                      _currentTemp, threshTemp, _currentHum, threshHum,
                      autoOn ? 1 : 0, _manualOverride ? 1 : 0);
    }

    // Stuck relay detection — citim pinul inapoi (output mode permite digitalRead).
    // Pe ESP32 functioneaza fara pinMode INPUT (open-drain config-uiri ok).
    delayMicroseconds(50);
    const int actual = digitalRead(_relayPin);
    if (actual != wantLevel) {
        _stuckCount++;
        if (_stuckCount == 3) {   // raporteaza la pragul 3 (evita false-positive)
            Serial.printf("[%s] RELAY STUCK: want=%d actual=%d (count=%d)\n",
                          _name, wantLevel, actual, _stuckCount);
        }
    } else if (_stuckCount > 0) {
        _stuckCount = 0;
    }
}

void VentilationZone::emergencyOff() {
    _relayState = false;
    _manualOverride = false;
    _forceOff = false;
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

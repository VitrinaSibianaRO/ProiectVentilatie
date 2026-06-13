#pragma once

// ============================================================
//  VentilationZone.h
//  Encapsulează un senzor + un releu (local sau remote).
//
//  Reguli de design:
//  - Constructor LOCAL: primește un Sht30Sensor* (ownership extern).
//    readSensor() apelează _localSensor->read().
//  - Constructor REMOTE: fără senzor local. Valorile vin din
//    setExternalSensorValues() (de la Slave prin UART).
//  - updateLogic() calculează starea releului exclusiv local.
//    Nu știe nimic de MQTT sau rețea.
//  - Failsafe: la pierderea comunicării cu Slave (5 cicluri),
//    zona remote intră în failsafe (releu OFF forțat).
// ============================================================

#include <Arduino.h>
#include "Sht30Sensor.h"

class VentilationZone {
public:
    // Constructor pentru zona cu senzor LOCAL (Master stânga)
    VentilationZone(Sht30Sensor* localSensor, int relayPin, const char* name);

    // Constructor pentru zona cu senzor REMOTE (dreapta, de la Slave UART)
    VentilationZone(int relayPin, const char* name);

    void begin();

    // Setează override manual. Persistența este responsabilitatea AppPreferences.
    void setManualOverride(bool state);
    bool getManualOverride() const;

    // Forțează releul OFF indiferent de senzori (comanda v=2 din MQTT).
    // NU persistat în NVS — se resetează la reboot (auto-mode la power cycle).
    // Prioritate: failsafe > forceOff > manualOverride > autoOn.
    void setForceOff(bool state);
    bool getForceOff() const;

    // Încearcă o citire de la senzorul local. Respectă cooldown-ul intern.
    // Pentru zone remote, nu face nimic (valorile vin din setExternalSensorValues).
    void readSensor(bool force = false);

    // Setează valori senzor primite din exterior (Slave UART).
    // Doar pentru zone remote.
    void setExternalSensorValues(float temp, float hum, uint32_t ts);

    // Calculează și aplică starea releului — logică 100% locală.
    // hystTemp/hystHum: banda de histerezis (releul se oprește la prag−hyst).
    // forceImmediate: comutare imediată (acțiune user/comandă) — ignoră timpul
    // minim între comutări (anti-chatter rămâne doar pentru evaluarea periodică).
    void updateLogic(float threshTemp, float threshHum, float hystTemp, float hystHum,
                     bool forceImmediate = false);

    // Oprire de urgență (restart, heap critic etc.).
    void emergencyOff();

    // Failsafe — forțează relay OFF când Slave e offline
    void enterFailsafe();
    void exitFailsafe();
    bool isInFailsafe() const;

    float       getTemp()          const;
    float       getHum()           const;
    bool        getRelayState()    const;
    bool        isFirstReadDone()  const;
    int         getConsecErrors()  const;
    const char* getName()          const;

    // Stuck relay detection (citeste pinul GPIO inapoi dupa write).
    // Returneaza count de fail-uri consecutive; 0 daca OK.
    int         getStuckCount()    const { return _stuckCount; }

private:
    Sht30Sensor*  _localSensor;     // nullptr dacă remote
    int           _relayPin;
    const char*   _name;
    float         _currentTemp;
    float         _currentHum;
    uint32_t      _lastExternalTs;
    bool          _manualOverride;
    bool          _forceOff;
    bool          _firstReadDone;
    bool          _relayState;
    bool          _failsafe;
    int           _consecutiveErrors;
    int           _stuckCount;
    unsigned long _lastSwitchMs;   // anti-chatter: ultimul moment de comutare
};

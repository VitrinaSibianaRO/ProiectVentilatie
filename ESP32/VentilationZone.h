#pragma once

// ============================================================
//  VentilationZone.h
//  Encapsulează un senzor DHT22 + un releu.
//
//  Reguli de design:
//  - readSensor() este idempotent: dacă nu au trecut ≥2100ms
//    de la ultima citire, returnează imediat. Sigur de apelat oricând.
//  - updateLogic() calculează starea releului exclusiv local.
//    Nu știe nimic de Blynk sau WiFi.
//  - manualOverride este un flag simplu; AppPreferences
//    gestionează persistența și timeout-ul lui.
// ============================================================

#include <Arduino.h>
#include <DHT.h>

class VentilationZone {
public:
    VentilationZone(int dhtPin, int relPin, const char* name);

    void begin();

    // Setează override manual. Persistența este responsabilitatea AppPreferences.
    void setManualOverride(bool state);
    bool getManualOverride() const;

    // Încearcă o citire. Respectă cooldown-ul intern de 2100ms.
    void readSensor();

    // Calculează și aplică starea releului — logică 100% locală.
    void updateLogic(float threshTemp, float threshHum);

    // Oprire de urgență (restart, heap critic etc.).
    void emergencyOff();

    float       getTemp()          const;
    float       getHum()           const;
    bool        getRelayState()    const;
    bool        isFirstReadDone()  const;
    int         getConsecErrors()  const;
    const char* getName()          const;

private:
    DHT           _dht;
    int           _relayPin;
    const char*   _name;
    float         _currentTemp;
    float         _currentHum;
    bool          _manualOverride;
    bool          _firstReadDone;
    bool          _relayState;
    unsigned long _lastReadMs;
    int           _consecutiveErrors;
};

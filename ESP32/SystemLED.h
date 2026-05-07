#pragma once

// ============================================================
//  SystemLED.h
//  LED NeoPixel de status — reflectă starea reală a sistemului.
//
//  Stări:
//    Albastru  — boot / inițializare
//    Verde     — Ethernet + MQTT conectate, sistem OK
//    Galben    — Ethernet OK, MQTT deconectat
//    Roșu      — fără Ethernet link
//    Alb       — reset NVS în curs
//    Mov       — Slave offline (failsafe activ)
// ============================================================

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class SystemLED {
public:
    SystemLED(uint16_t count, uint8_t pin, uint8_t enPin);

    void begin();
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    // Apelat periodic din loop() — actualizează culoarea
    // în funcție de starea curentă a conexiunii Ethernet + MQTT.
    void updateStatus(bool ethOk, bool mqttOk);

    // Slave offline (mov)
    void setSlaveOffline();

    void setWhite();
    void setBlue();

private:
    Adafruit_NeoPixel _strip;
    uint8_t _enablePin;
    uint8_t _r, _g, _b;
};

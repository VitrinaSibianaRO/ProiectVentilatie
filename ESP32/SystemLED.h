#pragma once

// ============================================================
//  SystemLED.h
//  LED NeoPixel de status — reflectă starea reală a sistemului.
//
//  Stări:
//    Albastru  — boot / inițializare
//    Verde     — WiFi + Blynk conectate, sistem OK
//    Galben    — WiFi conectat, Blynk deconectat
//    Roșu      — fără WiFi
//    Alb       — reset WiFiManager în curs
// ============================================================

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class SystemLED {
public:
    SystemLED(uint16_t count, uint8_t pin, uint8_t enPin);

    void begin();
    void setColor(uint8_t r, uint8_t g, uint8_t b);

    // Apelat periodic din loop() — actualizează culoarea
    // în funcție de starea curentă a conexiunii.
    void updateStatus(bool wifiOk, bool blynkOk);

    void setWhite();
    void setBlue();

private:
    Adafruit_NeoPixel _strip;
    uint8_t _enablePin;
    uint8_t _r, _g, _b;
};

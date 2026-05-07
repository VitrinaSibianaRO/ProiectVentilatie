// ============================================================
//  SystemLED.cpp
//  Stări: Albastru=boot, Verde=OK, Galben=noMQTT, Roșu=noEth,
//         Alb=reset, Mov=slaveOffline
// ============================================================

#include "SystemLED.h"

SystemLED::SystemLED(uint16_t count, uint8_t pin, uint8_t enPin)
    : _strip(count, pin, NEO_GRB + NEO_KHZ800),
      _enablePin(enPin),
      _r(0), _g(0), _b(200) {}

void SystemLED::begin() {
    pinMode(_enablePin, OUTPUT);
    digitalWrite(_enablePin, LOW);   // activează tensiunea pe Carbon V3
    _strip.begin();
    _strip.setBrightness(40);
    setColor(0, 0, 200);             // albastru la boot
}

void SystemLED::setColor(uint8_t r, uint8_t g, uint8_t b) {
    _r = r; _g = g; _b = b;
    _strip.setPixelColor(0, _strip.Color(r, g, b));
    _strip.show();
}

void SystemLED::updateStatus(bool ethOk, bool mqttOk) {
    if (!ethOk) {
        if (_r != 200 || _g != 0 || _b != 0)
            setColor(200, 0, 0);          // roșu — fără Ethernet
    } else if (!mqttOk) {
        if (_r != 180 || _g != 100 || _b != 0)
            setColor(180, 100, 0);        // galben — Eth ok, MQTT deconectat
    } else {
        if (_r != 0 || _g != 180 || _b != 0)
            setColor(0, 180, 0);          // verde — totul funcționează
    }
}

void SystemLED::setSlaveOffline() {
    setColor(128, 0, 128);               // mov — Slave offline
}

void SystemLED::setWhite() { setColor(200, 200, 200); }
void SystemLED::setBlue()  { setColor(0, 0, 200); }

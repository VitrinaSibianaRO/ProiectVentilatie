// SystemLED.h — Status LED RGB WS2812B cu enum Status.
// Afiseaza starea operationala a Slave-ului prin culori.
#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "Config.h"

class SystemLED {
public:
    enum class Status : uint8_t {
        Off         = 0,
        Booting     = 1,   // oranj     — setup() in curs
        Ready       = 2,   // cyan      — idle, asteapta comenzi
        Active      = 3,   // verde     — request reusit recent
        Idle        = 4,   // oranj     — >2min fara request
        SensorFail  = 5,   // rosu      — SHT30 nu raspunde
        OtaProgress = 6,   // albastru  — OTA in desfasurare
    };

    SystemLED(uint8_t dataPin, uint8_t enablePin, uint8_t count)
        : _strip(count, dataPin, NEO_GRB + NEO_KHZ800),
          _enablePin(enablePin),
          _status(Status::Off) {}

    void begin() {
        pinMode(_enablePin, OUTPUT);
        digitalWrite(_enablePin, HIGH);
        _strip.begin();
        _strip.setBrightness(40);
        setStatus(Status::Off);
    }

    void setStatus(Status s) {
        _status = s;
        _strip.setPixelColor(0, _statusToColor(s));
        _strip.show();
    }

    Status getStatus() const { return _status; }

private:
    Adafruit_NeoPixel _strip;
    uint8_t           _enablePin;
    Status            _status;

    static uint32_t _statusToColor(Status s) {
        switch (s) {
            case Status::Booting:     return 0xFF8000;  // oranj
            case Status::Ready:       return 0x00E0FF;  // cyan
            case Status::Active:      return 0x00FF00;  // verde
            case Status::Idle:        return 0xFF6000;  // oranj inchis
            case Status::SensorFail:  return 0xFF0000;  // rosu
            case Status::OtaProgress: return 0x0000FF;  // albastru
            case Status::Off:
            default:                  return 0x000000;
        }
    }
};

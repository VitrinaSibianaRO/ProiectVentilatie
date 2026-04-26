#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class SystemLED {
private:
  Adafruit_NeoPixel strip;
  uint8_t enablePin;

public:
  SystemLED(uint16_t count, uint8_t pin, uint8_t enPin);
  void begin();
  void setColor(uint8_t r, uint8_t g, uint8_t b);
};
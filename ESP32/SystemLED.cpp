#include "SystemLED.h"

SystemLED::SystemLED(uint16_t count, uint8_t pin, uint8_t enPin) 
  : strip(count, pin, NEO_GRB + NEO_KHZ800), enablePin(enPin) {}

void SystemLED::begin() {
  pinMode(enablePin, OUTPUT);
  digitalWrite(enablePin, LOW); // Activeaza tensiunea catre LED pe placa Carbon V3
  strip.begin();
  strip.setBrightness(50);
}

void SystemLED::setColor(uint8_t r, uint8_t g, uint8_t b) {
  strip.setPixelColor(0, strip.Color(r, g, b));
  strip.show();
}
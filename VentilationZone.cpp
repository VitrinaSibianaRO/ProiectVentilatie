#include "VentilationZone.h"

VentilationZone::VentilationZone(int dhtPin, int relPin, String name) 
  : dht(dhtPin, DHT22), relayPin(relPin), zoneName(name) {
  smoothTemp = 0.0;
  smoothHum = 0.0;
  manualOverride = false;
  firstReadDone = false;
  relayState = false;
}

void VentilationZone::begin() {
  digitalWrite(relayPin, HIGH); // Releu Active-LOW -> Oprit la boot
  pinMode(relayPin, OUTPUT);
  dht.begin();
}

void VentilationZone::setManualOverride(bool state) {
  manualOverride = state;
}

void VentilationZone::readSensor() {
  float rawT = dht.readTemperature();
  float rawH = dht.readHumidity();

  bool isValid = !isnan(rawT) && !isnan(rawH) && (rawT > -20.0 && rawT < 80.0) && (rawH >= 0.0 && rawH <= 100.0);

  if (isValid) {
    if (!firstReadDone) {
      smoothTemp = rawT;
      smoothHum = rawH;
      firstReadDone = true;
    } else {
      smoothTemp = (smoothTemp * 0.7) + (rawT * 0.3);
      smoothHum = (smoothHum * 0.7) + (rawH * 0.3);
    }
  } else {
    Serial.printf("[!] Eroare senzor zona: %s\n", zoneName.c_str());
  }
}

void VentilationZone::updateLogic(float threshTemp, float threshHum) {
  bool autoOn = (smoothTemp >= threshTemp || smoothHum >= threshHum);
  relayState = autoOn || manualOverride;

  digitalWrite(relayPin, relayState ? LOW : HIGH); // LOW porneste releul
}

// Returnam valorile stocate
float VentilationZone::getTemp() { return smoothTemp; }
float VentilationZone::getHum() { return smoothHum; }
bool VentilationZone::getRelayState() { return relayState; }
bool VentilationZone::isFirstReadDone() { return firstReadDone; }
String VentilationZone::getName() { return zoneName; }
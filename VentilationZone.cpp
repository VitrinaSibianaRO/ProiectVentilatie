#include "VentilationZone.h"

VentilationZone::VentilationZone(int dhtPin, int relPin, String name) 
  : dht(dhtPin, DHT22), relayPin(relPin), zoneName(name) {
  smoothTemp = 0.0;
  smoothHum = 0.0;
  manualOverride = false;
  firstReadDone = false;
  relayState = false;
  autoState = false; // Initializat ca oprit
  consecutiveErrors = 0;
}

void VentilationZone::begin() {
  digitalWrite(relayPin, HIGH); 
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
    consecutiveErrors = 0; // Resetam erorile daca citirea e buna
  } else {
    consecutiveErrors++;
    Serial.printf("[!] Eroare senzor zona: %s (Erori consecutive: %d)\n", zoneName.c_str(), consecutiveErrors);
  }
}

void VentilationZone::updateLogic(float threshTemp, float threshHum, float marginTemp, float marginHum) {
  // 1. REGULA DE PORNIRE (Daca oricare parametru depaseste pragul setat)
  if (smoothTemp >= threshTemp || smoothHum >= threshHum) {
    autoState = true;
  }
  // 2. REGULA DE OPRIRE (Doar daca AMBII parametri au scazut sub "Prag - Marja")
  else if (smoothTemp <= (threshTemp - marginTemp) && smoothHum <= (threshHum - marginHum)) {
    autoState = false;
  }

  // Starea finala ia in calcul si butonul manual
  relayState = autoState || manualOverride;

  digitalWrite(relayPin, relayState ? LOW : HIGH); 
}

float VentilationZone::getTemp() { return smoothTemp; }
float VentilationZone::getHum() { return smoothHum; }
bool VentilationZone::getRelayState() { return relayState; }
bool VentilationZone::isFirstReadDone() { return firstReadDone; }
int VentilationZone::getErrors() { return consecutiveErrors; }
String VentilationZone::getName() { return zoneName; }
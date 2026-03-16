#pragma once
#include <Arduino.h>
#include <DHT.h>

class VentilationZone {
private:
  DHT dht;
  int relayPin;
  String zoneName;
  
  float smoothTemp;
  float smoothHum;
  bool manualOverride;
  bool firstReadDone;
  bool relayState;

public:
  VentilationZone(int dhtPin, int relPin, String name);
  void begin();
  void setManualOverride(bool state);
  void readSensor();
  void updateLogic(float threshTemp, float threshHum);
  
  // Metode "Getters" pentru a putea "extrage" valorile in fisierul principal
  float getTemp();
  float getHum();
  bool getRelayState();
  bool isFirstReadDone();
  String getName();
};
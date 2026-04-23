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
  
  bool autoState; // Memoria pentru histerezis (stie daca a pornit automat)
  int consecutiveErrors; // Contor erori senzor pentru detectie blocaj

public:
  VentilationZone(int dhtPin, int relPin, String name);
  void begin();
  void setManualOverride(bool state);
  void readSensor();
  
  // Am modificat functia sa primeasca si marjele setate din Blynk
  void updateLogic(float threshTemp, float threshHum, float marginTemp, float marginHum);
  
  float getTemp();
  float getHum();
  bool getRelayState();
  bool isFirstReadDone();
  int getErrors();
  String getName();
};
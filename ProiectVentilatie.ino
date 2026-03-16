#include "Config.h" 
#include "SystemLED.h"
#include "VentilationZone.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp32.h>

// --- INSTANTIERE OBIECTE GLOBALE ---
SystemLED statusLed(LED_COUNT, LED_PIN, LED_ENABLE_PIN);
VentilationZone leftZone(DHT_LEFT_PIN, RELAY_LEFT_PIN, "STANGA");
VentilationZone rightZone(DHT_RIGHT_PIN, RELAY_RIGHT_PIN, "DREAPTA");

BlynkTimer timer;

// --- VARIABILE STARE GENERALE ---
float globalTempThresh = 45.0;
float globalHumThresh = 60.0;
int timerID;
long currentInterval = 300000L; // Setează direct 300 secunde pentru economie la boot

// --- VARIABILE PENTRU ECONOMIE CREDITE (Last Sent) ---
float lastSentTempL = -99.0, lastSentHumL = -99.0;
float lastSentTempR = -99.0, lastSentHumR = -99.0;
float tempDiffThreshold = 2.0; // Pragul de 2 grade cerut de tine
float humDiffThreshold = 5.0;  // Prag de 5% pentru umiditate (recomandat)

unsigned long buttonPressTime = 0;
bool isButtonPressed = false;
unsigned long lastWiFiCheck = 0;

void processZones() {
  Serial.println("\n--- Citire si Procesare (Mod Econom) ---");
  
  leftZone.readSensor();
  rightZone.readSensor();

  leftZone.updateLogic(globalTempThresh, globalHumThresh);
  rightZone.updateLogic(globalTempThresh, globalHumThresh);

  if (Blynk.connected()) {
    // --- LOGICA PENTRU ZONA STANGA ---
    if (leftZone.isFirstReadDone()) {
      float currentTL = leftZone.getTemp();
      float currentHL = leftZone.getHum();

      // Trimitem DOAR daca s-a schimbat temperatura cu 2 grade SAU umiditatea cu 5%
      if (abs(currentTL - lastSentTempL) >= tempDiffThreshold || 
          abs(currentHL - lastSentHumL) >= humDiffThreshold) {
        
        Blynk.virtualWrite(V1, currentTL);
        Blynk.virtualWrite(V2, currentHL);
        
        lastSentTempL = currentTL;
        lastSentHumL = currentHL;
        Serial.println("[Blynk] Date trimise pentru zona STANGA (schimbare detectata).");
      }
    }

    // --- LOGICA PENTRU ZONA DREAPTA ---
    if (rightZone.isFirstReadDone()) {
      float currentTR = rightZone.getTemp();
      float currentHR = rightZone.getHum();

      if (abs(currentTR - lastSentTempR) >= tempDiffThreshold || 
          abs(currentHR - lastSentHumR) >= humDiffThreshold) {
        
        Blynk.virtualWrite(V3, currentTR);
        Blynk.virtualWrite(V4, currentHR);
        
        lastSentTempR = currentTR;
        lastSentHumR = currentHR;
        Serial.println("[Blynk] Date trimise pentru zona DREAPTA (schimbare detectata).");
      }
    }

    // Starile releelor se trimit mereu pentru a fi siguri ca butonul din app e corect
    Blynk.virtualWrite(V5, leftZone.getRelayState() ? 1 : 0);
    Blynk.virtualWrite(V6, rightZone.getRelayState() ? 1 : 0);
  }
}

void triggerImmediateUpdate() {
  leftZone.updateLogic(globalTempThresh, globalHumThresh);
  rightZone.updateLogic(globalTempThresh, globalHumThresh);
  
  if (Blynk.connected()) {
    Blynk.virtualWrite(V5, leftZone.getRelayState() ? 1 : 0);
    Blynk.virtualWrite(V6, rightZone.getRelayState() ? 1 : 0);
  }
}

// Handler-ul de reset (V10) trebuie si el sa reseteze memoria de trimitere
BLYNK_WRITE(V10) {
  if (param.asInt() == 1) { 
    globalTempThresh = 45.0;
    globalHumThresh = 60.0;
    leftZone.setManualOverride(false);
    rightZone.setManualOverride(false);

    currentInterval = 300000L; 
    timer.deleteTimer(timerID);
    timerID = timer.setInterval(currentInterval, processZones);

    // Resetam si valorile "Last Sent" ca sa forțăm o trimitere proaspătă după reset
    lastSentTempL = -99.0; lastSentHumL = -99.0;
    lastSentTempR = -99.0; lastSentHumR = -99.0;

    Blynk.virtualWrite(V7, 45.0);
    Blynk.virtualWrite(V8, 60.0);
    Blynk.virtualWrite(V9, 300);
    
    triggerImmediateUpdate();
    Blynk.virtualWrite(V10, 0); 
    Serial.println("[Sistem] Reset complet. Urmatoarea citire va fi trimisa obligatoriu.");
  }
}

// BLYNK HANDLERS RESTANTE (V5, V6, V7, V8, V9) ramân identice...
BLYNK_CONNECTED() { 
  Blynk.syncVirtual(V5, V6, V7, V8, V9, V10); 
}
BLYNK_WRITE(V5) { leftZone.setManualOverride(param.asInt() == 1); triggerImmediateUpdate(); }
BLYNK_WRITE(V6) { rightZone.setManualOverride(param.asInt() == 1); triggerImmediateUpdate(); }
BLYNK_WRITE(V7) { globalTempThresh = param.asFloat(); triggerImmediateUpdate(); }
BLYNK_WRITE(V8) { globalHumThresh = param.asFloat(); triggerImmediateUpdate(); }
BLYNK_WRITE(V9) {
  long val = param.asInt();
  if (val < 5) val = 5; 
  currentInterval = val * 1000L;
  timer.deleteTimer(timerID);
  timerID = timer.setInterval(currentInterval, processZones);
}

void setup() {
  Serial.begin(115200);
  delay(2000); 
  statusLed.begin();
  statusLed.setColor(0, 0, 255);
  leftZone.begin();
  rightZone.begin();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  WiFiManager wm;
  if (!wm.autoConnect("ESP32_Setare_Ventilatie")) {
    delay(3000); ESP.restart();
  }
  
  statusLed.setColor(0, 255, 0);
  Blynk.config(BLYNK_AUTH_TOKEN);
  timerID = timer.setInterval(currentInterval, processZones);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) { Blynk.run(); }
  timer.run();
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!isButtonPressed) { buttonPressTime = millis(); isButtonPressed = true; statusLed.setColor(255, 255, 0); }
    else if (millis() - buttonPressTime > 3000) { 
      statusLed.setColor(255, 255, 255); delay(500); 
      WiFiManager wm; wm.resetSettings(); ESP.restart(); 
    }
  } else { isButtonPressed = false; }
}
#include "Config.h" 
#include "SystemLED.h"
#include "VentilationZone.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>
#include <WiFiManager.h>
#include <Preferences.h>

// --- CERTIFICAT ROOT PENTRU HIVEMQ CLOUD (ISRG Root X1) ---
const char* root_ca = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIFazCCA1OgAwIBAgIRAANYZIjzCG0K03PqQzI4QjIwDQYJKoZIhvcNAQELBQAw\n" \
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n" \
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n" \
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n" \
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n" \
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJ1yOObpPeYa0MBX0\n" \
"aP4C29J2nL80s3B5hB4A5C10s8E2wL312P6A/bA+aE0X3qKz/ZpZ138Yt0K+y+eE\n" \
"7Bf5f3e7A+vA1p2+mP4G0Q3pT9hYh4R+y2G1t8X21n6d4+t7q2w9k/H3qP2D+6y5\n" \
"G/rA/9y+h3t2w8n0o22D+q2E3m8Z6B4J3P3H9Q3D/0c3D9xX4x0Z8F+v6C3O6r+v\n" \
"yY8f/7T8oH7v3eYw8E4P+Y6u8eQ4G0T2sW5D/6+B/z8X9E/o1E3O3t3o4F3r+O8r\n" \
"P6E2w+u8Y2+u/2M3r3+T4P+M8O+X9T3A/7t3E3+3e3/6/5e7+6T+R6n8tZ2B+B5e\n" \
"Z8k4G+7y+T3Z7+m8+v/8r3u+m4k+2s/4y2u+k4+z7t5+2p+1t/1o9m+3q4O9v/r+\n" \
"s+2x+2l/8t3X6w4X+6M6m2g/3f7e5t5y7n/6u4e+1i5v+8w9Y+7a+1z7D9n/6t5Y\n" \
"B3A+8g7s8E7w/5h9M9X9Q5T9v8h9w9b7c/7v8r5r7E9n9c9E8m8E/7b7d7n7K9l7\n" \
"s9B8A9X8Z6n8e6A9b7E9D9h9X8Z6a9w8B7a9X9X9M8h9c8A9m8c9e8D9c9k9Q8E9\n" \
"h9F8a9S8W9A9X9c/v9E8h9n/k9E9m9R9n/E/x9c8D9b8Z8s9t/E9m8B8X9n9E9M\n" \
"A0GCSqGSIb3DQEBCwUAA4ICAQCW1r/8M9c9W9D9k9M8E9n9B8A9h9X9F9X9c/w9\n" \
"v/B9c8B/r9W8s9H9E8b8A9X9M8E/t8a9T9F8A9k/R8B9m8t9F8k/r9t8c9c/u9E\n" \
"d9b8E9R9k9M8t9X8E/r9n/T9W9E9X9B9k8A9h/R9v/F9k9c9t8X9c9E8r/v8t/F\n" \
"v9m8d9X9E9k/c9A/D8k/u8s9F9n9X/v9A9k9c9A9n/W8t9X8A9n9c8E9r9W9D9k\n" \
"h9F8a9S8W9A9X9c/v9E8h9n/k9E9m9R9n/E/x9c8D9b8Z8s9t/E9m8B8X9n9E9M\n" \
"c9t8X9c9E8r/v8t/Fv9m8d9X9E9k/c9A/D8k/u8s9F9n9X/v9A9k9c9A9n/W8t9\n" \
"X8A9n9c8E9r9W9D9kh9F8a9S8W9A9X9c/v9E8h9n/k9E9m9R9n/E/x9c8D9b8Z8\n" \
"-----END CERTIFICATE-----\n";

// --- INSTANTIERE OBIECTE GLOBALE ---
SystemLED statusLed(LED_COUNT, LED_PIN, LED_ENABLE_PIN);
VentilationZone leftZone(DHT_LEFT_PIN, RELAY_LEFT_PIN, "STANGA");
VentilationZone rightZone(DHT_RIGHT_PIN, RELAY_RIGHT_PIN, "DREAPTA");

WiFiClientSecure secureClient;
PubSubClient mqttClient(secureClient);
Preferences preferences;

// --- VARIABILE STARE GENERALE ---
float globalTempThresh = 45.0;
float globalHumThresh = 60.0;
float globalTempMargin = 2.0; 
float globalHumMargin = 5.0;  
long currentInterval = 300000L; // 5 minute

unsigned long lastSensorRead = 0;
unsigned long lastPublish = 0;
unsigned long buttonPressTime = 0;
bool isButtonPressed = false;

// 15 sec watchdog
#define WDT_TIMEOUT_SEC 15 

// Variabile MQTT salvate in NVS
char mqtt_server[60];
char mqtt_port[6];
char mqtt_user[60];
char mqtt_pass[60];
bool shouldSaveConfig = false;

void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void loadCustomParams() {
  preferences.begin("mqtt_config", false);
  String server = preferences.getString("server", DEFAULT_MQTT_SERVER);
  String port = preferences.getString("port", DEFAULT_MQTT_PORT);
  String user = preferences.getString("user", "");
  String pass = preferences.getString("pass", "");
  
  strlcpy(mqtt_server, server.c_str(), sizeof(mqtt_server));
  strlcpy(mqtt_port, port.c_str(), sizeof(mqtt_port));
  strlcpy(mqtt_user, user.c_str(), sizeof(mqtt_user));
  strlcpy(mqtt_pass, pass.c_str(), sizeof(mqtt_pass));
  preferences.end();
}

void saveCustomParams() {
  preferences.begin("mqtt_config", false);
  preferences.putString("server", mqtt_server);
  preferences.putString("port", mqtt_port);
  preferences.putString("user", mqtt_user);
  preferences.putString("pass", mqtt_pass);
  preferences.end();
}

void setupWiFiManager() {
  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);
  
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 60);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "MQTT Username", mqtt_user, 60);
  WiFiManagerParameter custom_mqtt_pass("pass", "MQTT Password", mqtt_pass, 60);
  
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  
  if (!wm.autoConnect("ESP32_Setare_Ventilatie")) {
    Serial.println("Eroare conectare. Timeout -> Auto-Restart");
    delay(3000);
    ESP.restart();
  }
  
  if (shouldSaveConfig) {
    strlcpy(mqtt_server, custom_mqtt_server.getValue(), sizeof(mqtt_server));
    strlcpy(mqtt_port, custom_mqtt_port.getValue(), sizeof(mqtt_port));
    strlcpy(mqtt_user, custom_mqtt_user.getValue(), sizeof(mqtt_user));
    strlcpy(mqtt_pass, custom_mqtt_pass.getValue(), sizeof(mqtt_pass));
    saveCustomParams();
  }
  Serial.println("WiFi conectat!");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);

  if (error) return;

  const char* cmd = doc["cmd"];
  if (!cmd) return;

  if (strcmp(cmd, "refresh") == 0) {
    lastPublish = 0; 
  }
  else if (strcmp(cmd, "reboot") == 0) {
    delay(1000);
    ESP.restart();
  }
  else if (strcmp(cmd, "setRelay") == 0) {
    const char* zone = doc["zone"];
    bool state = doc["state"];
    if (zone && strcmp(zone, "left") == 0) leftZone.setManualOverride(state);
    if (zone && strcmp(zone, "right") == 0) rightZone.setManualOverride(state);
    leftZone.updateLogic(globalTempThresh, globalHumThresh, globalTempMargin, globalHumMargin);
    rightZone.updateLogic(globalTempThresh, globalHumThresh, globalTempMargin, globalHumMargin);
    lastPublish = 0; 
  }
  else if (strcmp(cmd, "setConfig") == 0) {
    if (doc.containsKey("threshT")) globalTempThresh = doc["threshT"];
    if (doc.containsKey("threshH")) globalHumThresh = doc["threshH"];
    if (doc.containsKey("marginT")) globalTempMargin = doc["marginT"];
    if (doc.containsKey("marginH")) globalHumMargin = doc["marginH"];
    if (doc.containsKey("interval")) currentInterval = doc["interval"];
    leftZone.updateLogic(globalTempThresh, globalHumThresh, globalTempMargin, globalHumMargin);
    rightZone.updateLogic(globalTempThresh, globalHumThresh, globalTempMargin, globalHumMargin);
    lastPublish = 0; 
  }
}

void connectMQTT() {
  if (mqttClient.connected() || WiFi.status() != WL_CONNECTED) return;

  String clientId = "ESP32-Vent-" + String(random(0xffff), HEX);
  mqttClient.setBufferSize(512);

  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    mqttClient.subscribe("ventilation/command");
    statusLed.setColor(0, 255, 0); // Verde = OK
  } else {
    statusLed.setColor(255, 0, 0); // Rosu = Eroare MQTT
  }
}

void publishState() {
  if (!mqttClient.connected()) return;

  StaticJsonDocument<512> doc;

  JsonObject left = doc.createNestedObject("left");
  left["temp"] = leftZone.getTemp();
  left["hum"] = leftZone.getHum();
  left["relay"] = leftZone.getRelayState();
  left["err"] = leftZone.getErrors();

  JsonObject right = doc.createNestedObject("right");
  right["temp"] = rightZone.getTemp();
  right["hum"] = rightZone.getHum();
  right["relay"] = rightZone.getRelayState();
  right["err"] = rightZone.getErrors();

  JsonObject config = doc.createNestedObject("config");
  config["threshT"] = globalTempThresh;
  config["threshH"] = globalHumThresh;
  config["marginT"] = globalTempMargin;
  config["marginH"] = globalHumMargin;
  config["interval"] = currentInterval;

  char buffer[512];
  serializeJson(doc, buffer);
  mqttClient.publish("ventilation/state", buffer);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  esp_task_wdt_init(WDT_TIMEOUT_SEC, true); 
  esp_task_wdt_add(NULL);

  statusLed.begin();
  statusLed.setColor(0, 0, 255); // Albastru = Boot

  leftZone.begin();
  rightZone.begin();
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  loadCustomParams();
  setupWiFiManager();

  secureClient.setCACert(root_ca);
  mqttClient.setServer(mqtt_server, atoi(mqtt_port));
  mqttClient.setCallback(mqttCallback);
}

void loop() {
  esp_task_wdt_reset(); 
  unsigned long currentMillis = millis();

  // --- RECONNECT NON-BLOCANT ---
  static unsigned long lastReconnectAttempt = 0;
  if (!mqttClient.connected()) {
    if (currentMillis - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = currentMillis;
      if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
      } else {
        connectMQTT();
      }
    }
  } else {
    mqttClient.loop();
  }

  // --- CITIRE SENZORI (minim 2.5 secunde) ---
  static unsigned long lastSensorPoll = 0;
  if (currentMillis - lastSensorPoll >= 5000) {
    lastSensorPoll = currentMillis;
    leftZone.readSensor();
    rightZone.readSensor();

    leftZone.updateLogic(globalTempThresh, globalHumThresh, globalTempMargin, globalHumMargin);
    rightZone.updateLogic(globalTempThresh, globalHumThresh, globalTempMargin, globalHumMargin);
    
    if (leftZone.getErrors() > 10 && rightZone.getErrors() > 10) {
        delay(1000);
        ESP.restart();
    }
  }

  // --- PUBLICARE DATE ---
  if (currentMillis - lastPublish >= currentInterval || lastPublish == 0) {
    lastPublish = currentMillis;
    publishState();
  }

  // --- BUTON RESET WIFIMANAGER ---
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (!isButtonPressed) {
      buttonPressTime = currentMillis;
      isButtonPressed = true;
      statusLed.setColor(255, 255, 0); // Galben = Buton apasat
    } else if (currentMillis - buttonPressTime > 3000) {
      statusLed.setColor(255, 255, 255); // Alb = Se da reset
      delay(500);
      WiFiManager wm;
      wm.resetSettings(); // Sterge credențialele WiFi
      preferences.begin("mqtt_config", false);
      preferences.clear(); // Sterge setările MQTT
      preferences.end();
      ESP.restart(); 
    }
  } else {
    isButtonPressed = false;
  }
}
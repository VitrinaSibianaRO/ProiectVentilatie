// ProiectVentilatie.ino — Master ESP32 (Carbon V3 #1)
// Arhitectură: Ethernet W5500 + SSLClient → HiveMQ Cloud
//              UART2 → Slave ESP32 (senzor SHT30 dreapta)
//              SHT30 local I2C (senzor stânga)
//              Relee x2 pentru ventilație
// FĂRĂ WiFi, FĂRĂ Blynk, FĂRĂ DHT22.

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <WiFiManager.h>

#include "AppPreferences.h"
#include "Config.h"
#include "DiagnosticLogger.h"
#include "EventLog.h"
#include "LedConfigStorage.h"
#include "MqttBridge.h"
#include "Resilience.h"
#include "SharedState.h"
#include "Sht30Sensor.h"
#include "SlaveCommTask.h"
#include "SlaveUartClient.h"
#include "SystemLED.h"
#include "TimeSync.h"
#include "VentilationZone.h"

// ============================================================
//  GLOBALE — ownership clar, toate în acest fișier
// ============================================================

AppPreferences prefs;
LedConfigStorage ledPrefs;
Sht30Sensor sensorLocal;     // SHT30 stânga (I2C pe Master)
SlaveUartClient slaveClient; // UART2 → Slave
SystemLED statusLed(LED_COUNT, LED_PIN, LED_ENABLE_PIN);
MqttBridge mqtt;
EventLog eventLog;

// Zone: stânga = senzor local, dreapta = senzor remote (Slave)
VentilationZone leftZone(&sensorLocal, RELAY_LEFT_PIN, "STANGA");
VentilationZone rightZone(RELAY_RIGHT_PIN, "DREAPTA");

// ============================================================
//  TIMER simplu (înlocuiește SimpleTimer cu o logică minimală)
// ============================================================
unsigned long lastProcessMs = 0;
unsigned long lastTimeSyncMs = 0;

// LED status cache (fetched from Slave)
uint8_t g_ledIntensity = 0;
bool g_ledSchedEnabled = false;

// Buffer PSRAM pentru dump log JSON (4KB pe stack ar fragmenta heap).
// Alocat o singura data in setup() cu fallback pe heap intern.
char *g_logBuf = nullptr;

// ============================================================
//  FREERTOS SHARED STATE (Core 0 SlaveCommTask ↔ Core 1 loopTask)
// ============================================================
SemaphoreHandle_t g_slaveDataMutex = nullptr;
QueueHandle_t g_slaveCommandQueue = nullptr;
SlaveData *g_slaveData = nullptr; // alocat in PSRAM
bool g_wifiAvailable = false;

// ============================================================
//  PROCESS ZONES — logica principală (apelată la interval)
// ============================================================
void processZones() {
  lastProcessMs = millis();

  // 1. Citeste snapshot Slave din SharedState (Core 0 actualizeaza la 500ms).
  //    Non-blocant: <1ms (vs pana la 3s blocant anterior pe
  //    slaveClient.fetch()).
  SlaveData snap{};
  bool snapOk = slaveDataRead(snap);

  bool slaveOnline;
  if (snapOk && snap.fetchOk) {
    rightZone.setExternalSensorValues(snap.temp, snap.hum, snap.ts);
    if (rightZone.isInFailsafe()) {
      rightZone.exitFailsafe();
      Serial.println("[Slave] Recovery: failsafe exited");
    }
    slaveOnline = true;

    // Sync LED status cache din SharedState (actualizat orar de SlaveCommTask)
    if (snap.ledScheduleFetched) {
      g_ledIntensity = snap.ledIntensity;
      g_ledSchedEnabled = snap.ledSchedEnabled;

      // Daca Slave a fost resetat si a pierdut schedule-ul, re-trimitem din NVS
      // Master
      if (!ledPrefs.compare(snap.ledOnH, snap.ledOnM, snap.ledOffH,
                            snap.ledOffM, snap.ledMaxI, snap.ledSchedEnabled)) {
        Serial.println("[LED] Slave schedule diverged, resyncing from NVS");
        SlaveCommand ledCmd{};
        ledCmd.type = SLAVE_CMD_LED_SCHEDULE;
        ledCmd.ledOnH = ledPrefs.onH;
        ledCmd.ledOnM = ledPrefs.onM;
        ledCmd.ledOffH = ledPrefs.offH;
        ledCmd.ledOffM = ledPrefs.offM;
        ledCmd.ledMaxI = ledPrefs.maxI;
        ledCmd.ledSchedEn = ledPrefs.enabled;
        slaveCommandSend(ledCmd);
      }
      
      // IMPORTANT: Resetează flag-ul pentru a evita re-verificarea la fiecare secundă!
      // Altfel, orice eroare de comparație ar duce la scrieri infinite în Flash-ul Slave-ului.
      snap.ledScheduleFetched = false;
      slaveDataWrite(snap);
    }
  } else {
    const int slaveErrors = snapOk ? snap.consecutiveErrors : 0;
    if (slaveErrors >= SLAVE_FAILSAFE_AFTER_FAILS &&
        !rightZone.isInFailsafe()) {
      rightZone.enterFailsafe();
      eventLog.append(EVT_SENSOR_ERR, ZONE_RIGHT, "slave_offline");
      Serial.printf("[Slave] FAILSAFE after %d errors\n", slaveErrors);
    }
    slaveOnline = snapOk && snap.consecutiveErrors < SLAVE_FAILSAFE_AFTER_FAILS;
  }
  const int slaveErrors = snapOk ? snap.consecutiveErrors : -1;

  // 3. Procesare comenzi MQTT pending — TREBUIE să fie înainte de updateLogic!
  MqttPending &pending = mqtt.getPending();

  if (pending.refresh) {
    leftZone.readSensor(true);
    SlaveCommTask::forceRead();
    mqtt.requestPublishNow();
    pending.refresh = false;
  }

  if (pending.setOverrideL) {
    int v = pending.overrideLVal;
    if (v == 0)
      prefs.saveOverrideLeft(false);
    else if (v == 1)
      prefs.saveOverrideLeft(true);
    else if (v == 2)
      prefs.saveOverrideLeft(false);
    leftZone.setManualOverride(prefs.overrideLeft);
    mqtt.requestPublishNow();
    pending.setOverrideL = false;
  }

  if (pending.setOverrideR) {
    int v = pending.overrideRVal;
    if (v == 0)
      prefs.saveOverrideRight(false);
    else if (v == 1)
      prefs.saveOverrideRight(true);
    else if (v == 2)
      prefs.saveOverrideRight(false);
    rightZone.setManualOverride(prefs.overrideRight);
    mqtt.requestPublishNow();
    pending.setOverrideR = false;
  }

  if (pending.setConfig) {
    if (pending.threshT > 0)
      prefs.saveTempThresh(pending.threshT);
    if (pending.threshH > 0)
      prefs.saveHumThresh(pending.threshH);
    if (pending.interval > 0)
      prefs.saveIntervalSec(pending.interval);
    if (pending.hystT >= 0)
      prefs.saveTempHyst(pending.hystT);
    if (pending.hystH >= 0)
      prefs.saveHumHyst(pending.hystH);
    mqtt.requestPublishNow();
    pending.setConfig = false;
  }

  if (pending.resetDefaults) {
    prefs.resetToDefaults();
    leftZone.setManualOverride(false);
    rightZone.setManualOverride(false);
    mqtt.requestPublishNow();
    pending.resetDefaults = false;
  }

  if (pending.rebootSlave) {
    Serial.println("[Slave] Reboot requested from MQTT");
    SlaveCommand cmd{};
    cmd.type = SLAVE_CMD_REBOOT;
    slaveCommandSend(cmd);
    pending.rebootSlave = false;
  }

  if (pending.getLog) {
    if (g_logBuf) {
      size_t n = eventLog.dumpJson(g_logBuf, PSRAM_LOG_BUF_SIZE);
      if (n > 0)
        mqtt.publishLog(g_logBuf, n);
    }
    pending.getLog = false;
  }

  if (pending.setLedNow) {
    SlaveCommand cmd{};
    cmd.type = SLAVE_CMD_LED_SET;
    cmd.ledPercent = pending.ledPercent;
    slaveCommandSend(cmd);
    g_ledIntensity = pending.ledPercent;
    mqtt.requestPublishNow();
    pending.setLedNow = false;
  }

  if (pending.setLedSched) {
    SlaveCommand cmd{};
    cmd.type = SLAVE_CMD_LED_SCHEDULE;
    cmd.ledOnH = pending.ledOnH;
    cmd.ledOnM = pending.ledOnM;
    cmd.ledOffH = pending.ledOffH;
    cmd.ledOffM = pending.ledOffM;
    cmd.ledMaxI = pending.ledMaxI;
    cmd.ledSchedEn = pending.ledSchedEn;
    slaveCommandSend(cmd);
    ledPrefs.save(pending.ledOnH, pending.ledOnM, pending.ledOffH,
                  pending.ledOffM, pending.ledMaxI, pending.ledSchedEn);
    g_ledSchedEnabled = pending.ledSchedEn;
    mqtt.requestPublishNow();
    pending.setLedSched = false;
  }

  if (pending.setLedMode) {
    SlaveCommand cmd{};
    cmd.type      = SLAVE_CMD_LED_MODE;
    cmd.ledModeId = pending.ledModeId;
    cmd.ledModeP1 = pending.ledModeP1;
    cmd.ledModeP2 = pending.ledModeP2;
    cmd.ledModeP3 = pending.ledModeP3;
    cmd.ledModeP4 = pending.ledModeP4;
    slaveCommandSend(cmd);
    ledPrefs.saveMode(pending.ledModeId, pending.ledModeP1, pending.ledModeP2,
                      pending.ledModeP3, pending.ledModeP4);
    mqtt.requestPublishNow();
    pending.setLedMode = false;
  }

  if (pending.reboot) {
    mqtt.publishOnline(false);
    delay(200);
    ESP.restart();
  }

  // 4. Citire senzor local (stânga) — Wire e pe Core 1, OK
  leftZone.readSensor(false);
  if (leftZone.getConsecErrors() >= 5 && leftZone.getConsecErrors() % 5 == 0) {
    I2CRecovery::recoverBus(I2C_SDA_PIN, I2C_SCL_PIN);
  }

  // 4. Sync override state din prefs → zone
  leftZone.setManualOverride(prefs.overrideLeft);
  rightZone.setManualOverride(prefs.overrideRight);

  // 5. updateLogic — decide starea releului
  leftZone.updateLogic(prefs.tempThresh, prefs.humThresh, prefs.tempHyst,
                       prefs.humHyst);
  rightZone.updateLogic(prefs.tempThresh, prefs.humThresh, prefs.tempHyst,
                        prefs.humHyst);

  // 6. MQTT publish state (incluzând LED status)
  // Forțează publicare la fiecare ciclu — senzori se actualizează la intervalul din Settings
  mqtt.requestPublishNow();
  mqtt.publishStateIfNeeded(leftZone, rightZone, slaveOnline, slaveErrors,
                            snap.lastSuccessMs, g_ledIntensity,
                            g_ledSchedEnabled);

  if (pending.reboot) {
    mqtt.publishOnline(false);
    delay(200);
    ESP.restart();
  }

  // 8. Status LED update
  bool mqttOk = mqtt.connected();
  if (!slaveOnline) {
    statusLed.setSlaveOffline();
  } else {
    statusLed.updateStatus(g_wifiAvailable, mqttOk);
  }
}

// ============================================================
//  BUTON RESET — factory reset NVS
// ============================================================
unsigned long btnPressStart = 0;
bool btnHandled = false;

void handleResetButton() {
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (btnPressStart == 0) {
      btnPressStart = millis();
      btnHandled = false;
    }
    // Apăsare > 3 secunde → factory reset NVS
    if (!btnHandled && millis() - btnPressStart > 3000) {
      btnHandled = true;
      Serial.println("[Reset] Factory reset NVS...");
      statusLed.setWhite();
      prefs.resetToDefaults();
      leftZone.setManualOverride(false);
      rightZone.setManualOverride(false);
      mqtt.requestPublishNow();
      delay(1000);
      statusLed.setBlue();
    }
  } else {
    btnPressStart = 0;
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Brownout::configure();

  Serial.begin(115200);
  Serial.printf("\n=== Master ESP32 boot — fw build %d ===\n", FW_BUILD_NUMBER);

  if (BootLoopGuard::check()) {
    // Safe mode (6 rapid reboots)
    pinMode(LED_ENABLE_PIN, OUTPUT);
    digitalWrite(LED_ENABLE_PIN, HIGH);
    Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
    strip.begin();
    strip.setBrightness(40);
    strip.setPixelColor(0, 0xFF0000); // Roșu
    strip.show();

    Serial.println(
        "[BootGuard] Halted in Safe Mode. Waiting 5min then restart.");
    delay(300000);
    ESP.restart();
  }

  // 1. Oprim Bluetooth
  btStop();

  // 2. Status LED — albastru la boot
  statusLed.begin();
  statusLed.setBlue();

  // 3. I2C bus pentru SHT30 local (stânga)
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_FREQ_HZ);

  // 4. Inițializare zone
  leftZone.begin();  // init SHT30 + releu stânga
  rightZone.begin(); // doar releu dreapta (senzor vine de la Slave)

  // 5. Buton reset — pull-up intern
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  // 6. NVS preferences
  prefs.begin();
  ledPrefs.begin();
  leftZone.setManualOverride(prefs.overrideLeft);
  rightZone.setManualOverride(prefs.overrideRight);
  Serial.printf("[Prefs] T:%.1f H:%.1f int:%ds hyst:T%.1f/H%.1f\n",
                prefs.tempThresh, prefs.humThresh, prefs.intervalSec,
                prefs.tempHyst, prefs.humHyst);

  // 7. Event log
  eventLog.begin();

  // 8. UART2 → Slave
  Serial2.begin(SLAVE_UART_BAUD, SERIAL_8N1, SLAVE_UART_RX_PIN,
                SLAVE_UART_TX_PIN);
  slaveClient.begin(Serial2);
  Serial.printf("[UART] Serial2 ready: baud=%u TX=%d RX=%d\n", SLAVE_UART_BAUD,
                SLAVE_UART_TX_PIN, SLAVE_UART_RX_PIN);

  // 9. PSRAM + FreeRTOS primitives pentru shared Slave state (Core 0 ↔ Core 1)
  g_slaveData = (SlaveData *)ps_malloc(sizeof(SlaveData));
  if (!g_slaveData)
    g_slaveData = new SlaveData();
  memset(g_slaveData, 0, sizeof(SlaveData));

  g_slaveDataMutex = xSemaphoreCreateMutex();
  g_slaveCommandQueue =
      xQueueCreate(SLAVE_CMD_QUEUE_DEPTH, sizeof(SlaveCommand));

  if (!g_slaveDataMutex || !g_slaveCommandQueue) {
    Serial.println("[FATAL] FreeRTOS primitive create failed");
    delay(500);
    ESP.restart();
  }
  Serial.printf("[PSRAM] g_slaveData @ %p  psramFree=%u KB\n", g_slaveData,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

  // 10. Watchdog hardware 60s — configurat INAINTE de SlaveCommTask.
  // SlaveCommTask apeleaza esp_task_wdt_add() imediat la start; daca WDT-ul
  // ramane la default-ul de 5s al sistemului, task-ul depaseste timeout-ul
  // in prima iteratie (PING+fetch+fetchLedStatus ~4s) si provoaca reboot.
  {
    const esp_task_wdt_config_t wdtConfig = {.timeout_ms = WDT_TIMEOUT_SEC * 1000,
                                             .idle_core_mask = 0,
                                             .trigger_panic = true};
    esp_err_t wdtErr = esp_task_wdt_reconfigure(&wdtConfig);
    if (wdtErr == ESP_ERR_INVALID_STATE) {
      wdtErr = esp_task_wdt_init(&wdtConfig);
    }
    if (wdtErr != ESP_OK && wdtErr != ESP_ERR_INVALID_STATE) {
      Serial.printf("[WDT] configure failed: %s\n", esp_err_to_name(wdtErr));
    }
    esp_task_wdt_add(NULL);  // subscrie loopTask (Core 1)
    Serial.printf("[WDT] Timeout: %us\n", WDT_TIMEOUT_SEC);
  }

  // 11. Pornire SlaveCommTask pe Core 0 (detine Serial2 / SlaveUartClient)
  SlaveCommTask::start(slaveClient);

  // 12. WiFiManager — singura cale de rețea
  {
    WiFiManager wm;
    wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_MS / 1000);
    wm.setConfigPortalTimeout(180);

    wm.setAPCallback([](WiFiManager* wm) {
        Serial.println("[WiFi] Config portal activ.");
        Serial.printf("[WiFi] SSID: %s\n", wm->getConfigPortalSSID().c_str());
        statusLed.setColor(0, 0, 200);
    });

    wm.setSaveConfigCallback([]() {
        Serial.println("[WiFi] Credentiale salvate.");
    });

    wm.setConnectRetries(WIFI_MAX_ATTEMPTS);

    if (wm.autoConnect("CarbonV3-Setup")) {
        Serial.printf("[WiFi] Connected: %s\n",
                      WiFi.localIP().toString().c_str());
        g_wifiAvailable = true;
        TimeSync::begin();
        mqtt.begin(&prefs);
    } else {
        Serial.println("[WiFi] Portal timeout — running offline.");
        WiFi.mode(WIFI_OFF);
    }
  }

  // 14. PSRAM buffer pentru log dump (4KB — prea mare pentru stack)
  g_logBuf = (char *)ps_malloc(PSRAM_LOG_BUF_SIZE);
  if (!g_logBuf)
    g_logBuf = new char[PSRAM_LOG_BUF_SIZE];
  Serial.printf("[PSRAM] g_logBuf @ %p  psramFree=%u KB\n", g_logBuf,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

  // WDT configurat si loopTask subscris anterior (pas 10).

  // 14. Prima citire senzori
  leftZone.readSensor(true); // force = true la boot

  statusLed.setColor(0, 180, 0); // verde = boot OK
  Serial.println("=== Boot complete ===\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // Watchdog feed
  esp_task_wdt_reset();

  // Resilience monitoring
  HeapMonitor::check();
  BootLoopGuard::tick();

  // Network-dependent operations
  if (g_wifiAvailable) {
    PreventiveReboot::checkWeekly();
    mqtt.loop();
    TimeSync::loop();
  }

  // Buton reset
  handleResetButton();

  // Process zones la intervalul configurat SAU imediat cand exista comenzi pending
  unsigned long now = millis();
  unsigned long intervalMs = (unsigned long)prefs.intervalSec * 1000UL;

  if ((now - lastProcessMs >= intervalMs) ||
      (g_wifiAvailable && mqtt.hasPendingCommands())) {
    processZones();
  }

  // Diagnostic logging
  DiagnosticLogger::printPeriodicLog();
}

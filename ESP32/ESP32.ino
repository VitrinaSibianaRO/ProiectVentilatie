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
#include "TvCommTask.h"
#include "TimeSync.h"
#include "TvController.h"
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
TvController tvCtrl;

// Zone: stânga = senzor local, dreapta = senzor remote (Slave)
VentilationZone leftZone(&sensorLocal, RELAY_LEFT_PIN, "STANGA");
VentilationZone rightZone(RELAY_RIGHT_PIN, "DREAPTA");

// ============================================================
//  TIMER simplu (înlocuiește SimpleTimer cu o logică minimală)
// ============================================================
unsigned long lastProcessMs  = 0;
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

// TvCommTask (Core 0) ↔ loopTask (Core 1)
SemaphoreHandle_t g_tvDataMutex = nullptr;
TvData *g_tvData = nullptr;       // alocat in PSRAM

bool g_wifiAvailable = false;

// ============================================================
//  CONTROL (loopTask, Core 1) ↔ NETWORK (taskNetwork, Core 0)
//  Config + zone + GPIO + NVS = owned EXCLUSIV de control.
//  Comenzile care ating NVS vin network→control prin g_controlCommandQueue.
//  Telemetria merge control→network prin g_controlState (mutex).
// ============================================================
QueueHandle_t     g_controlCommandQueue = nullptr;  // network → control
SemaphoreHandle_t g_controlStateMutex   = nullptr;
ControlState*     g_controlState        = nullptr;  // alocat in PSRAM
QueueHandle_t     g_tvCommandQueue      = nullptr;  // network → TvCommTask

// Status MQTT publicat de taskNetwork (owner mqtt), citit de control pentru LED.
volatile bool g_mqttConnected = false;

// Seq incrementat de control la fiecare snapshot publicabil (detectie schimbare).
static uint32_t g_controlSeq = 0;

// ============================================================
//  SNAPSHOT TELEMETRIE (control → network) — network publica DOAR de aici.
//  Citeste zone + config (owned de control) → fara acces cross-task la zone.
// ============================================================
void publishControlSnapshot(bool slaveOnline, int slaveErrors,
                            unsigned long slaveLastSuccessMs, bool forcePublish) {
  // Detectie schimbare relevanta → bumpam seq DOAR atunci (event-driven).
  // Jitter-ul senzorilor singur NU declanseaza publish; acela vine pe heartbeat 4min.
  static bool lastLR = false, lastRR = false, lastLO = false, lastRO = false,
              lastFS = false, init = false;

  ControlState st{};
  st.leftTemp      = leftZone.getTemp();
  st.leftHum       = leftZone.getHum();
  st.leftRelay     = leftZone.getRelayState();
  st.leftOverride  = leftZone.getManualOverride();
  st.leftErrs      = leftZone.getConsecErrors();
  st.rightTemp     = rightZone.getTemp();
  st.rightHum      = rightZone.getHum();
  st.rightRelay    = rightZone.getRelayState();
  st.rightOverride = rightZone.getManualOverride();
  st.rightErrs     = rightZone.getConsecErrors();
  st.rightFailsafe = rightZone.isInFailsafe();
  st.slaveOnline   = slaveOnline;
  st.slaveErrors   = slaveErrors;
  st.slaveLastSeenSec   = (uint32_t)(slaveLastSuccessMs / 1000UL);
  st.ledIntensity       = g_ledIntensity;
  st.followTvBrightness = prefs.followTvBrightness;
  strlcpy(st.morseText, prefs.morseText, sizeof(st.morseText));

  const bool changed = !init ||
                       st.leftRelay     != lastLR || st.rightRelay    != lastRR ||
                       st.leftOverride  != lastLO || st.rightOverride != lastRO ||
                       st.rightFailsafe != lastFS;
  // seq creste doar la schimbare/forta → network publica atunci; altfel doar heartbeat.
  g_controlSeq += (forcePublish || changed) ? 1u : 0u;
  st.seq = g_controlSeq;
  controlStateWrite(st);

  lastLR = st.leftRelay;  lastRR = st.rightRelay;
  lastLO = st.leftOverride; lastRO = st.rightOverride;
  lastFS = st.rightFailsafe; init = true;
}

// ============================================================
//  APLICARE COMENZI CONTROL (network → control prin queue).
//  Ruleaza pe taskControl (loopTask) — singurul care atinge NVS/config/zone.
// ============================================================
void applyControlCommand(const ControlCommand& cc) {
  switch (cc.type) {
    case CTRL_SET_CONFIG:
      if (cc.threshT > 0)  prefs.saveTempThresh(cc.threshT);
      if (cc.threshH > 0)  prefs.saveHumThresh(cc.threshH);
      if (cc.interval > 0) prefs.saveIntervalSec(cc.interval);
      if (cc.hystT >= 0)   prefs.saveTempHyst(cc.hystT);
      if (cc.hystH >= 0)   prefs.saveHumHyst(cc.hystH);
      // Save = "reia controlul automat cu pragurile astea" → stergem AMBELE
      // moduri manuale (override ON + forceOff), pe ambele zone. Butonul MAUI
      // comuta doar ON↔OFF (nu trimite "auto"), deci Save e calea catre AUTO.
      prefs.saveOverrideLeft(false);
      prefs.saveOverrideRight(false);
      leftZone.setManualOverride(false);
      rightZone.setManualOverride(false);
      leftZone.setForceOff(false);
      rightZone.setForceOff(false);
      Serial.printf("[Cfg] setConfig: T>=%.1f H>=%.1f int=%ds hystT=%.1f hystH=%.1f (override+forceOff sterse, AUTO)\n",
                    prefs.tempThresh, prefs.humThresh, prefs.intervalSec,
                    prefs.tempHyst, prefs.humHyst);
      break;

    case CTRL_SET_OVERRIDE_L: {
      const int v = cc.overrideVal;
      if (v == 0)      { prefs.saveOverrideLeft(false); leftZone.setForceOff(false); }
      else if (v == 1) { prefs.saveOverrideLeft(true);  leftZone.setForceOff(false); }
      else if (v == 2) { prefs.saveOverrideLeft(false); leftZone.setForceOff(true);  }
      leftZone.setManualOverride(prefs.overrideLeft);
      break;
    }

    case CTRL_SET_OVERRIDE_R: {
      const int v = cc.overrideVal;
      if (v == 0)      { prefs.saveOverrideRight(false); rightZone.setForceOff(false); }
      else if (v == 1) { prefs.saveOverrideRight(true);  rightZone.setForceOff(false); }
      else if (v == 2) { prefs.saveOverrideRight(false); rightZone.setForceOff(true);  }
      rightZone.setManualOverride(prefs.overrideRight);
      break;
    }

    case CTRL_RESET:
      prefs.resetToDefaults();
      leftZone.setManualOverride(false);
      rightZone.setManualOverride(false);
      leftZone.setForceOff(false);
      rightZone.setForceOff(false);
      break;

    case CTRL_REFRESH:
      // Citire fortata senzori — processZones (declansat de gotCmd) republica.
      leftZone.readSensor(true);
      SlaveCommTask::forceRead();
      break;

    case CTRL_LED_SET: {
      SlaveCommand cmd{};
      cmd.type       = SLAVE_CMD_LED_SET;
      cmd.ledPercent = cc.ledPercent;
      slaveCommandSend(cmd);
      g_ledIntensity = cc.ledPercent;
      break;
    }

    case CTRL_LED_SCHEDULE: {
      SlaveCommand cmd{};
      cmd.type    = SLAVE_CMD_LED_SCHEDULE;
      cmd.ledOnH  = cc.ledOnH;  cmd.ledOnM  = cc.ledOnM;
      cmd.ledOffH = cc.ledOffH; cmd.ledOffM = cc.ledOffM;
      cmd.ledMaxI = cc.ledMaxI; cmd.ledSchedEn = cc.ledSchedEn;
      slaveCommandSend(cmd);
      ledPrefs.save(cc.ledOnH, cc.ledOnM, cc.ledOffH, cc.ledOffM,
                    cc.ledMaxI, cc.ledSchedEn);
      g_ledSchedEnabled = cc.ledSchedEn;
      break;
    }

    case CTRL_LED_MODE: {
      SlaveCommand cmd{};
      cmd.type      = SLAVE_CMD_LED_MODE;
      cmd.ledModeId = cc.ledModeId;
      cmd.ledModeP1 = cc.ledModeP1; cmd.ledModeP2 = cc.ledModeP2;
      cmd.ledModeP3 = cc.ledModeP3; cmd.ledModeP4 = cc.ledModeP4;
      slaveCommandSend(cmd);
      ledPrefs.saveMode(cc.ledModeId, cc.ledModeP1, cc.ledModeP2,
                        cc.ledModeP3, cc.ledModeP4);
      break;
    }

    case CTRL_LED_FOLLOW_TV: {
      prefs.saveFollowTvBrightness(cc.followTvValue);
      SlaveCommand ftvCmd{};
      ftvCmd.type            = SLAVE_CMD_LED_FOLLOW_TV;
      ftvCmd.followTvEnabled = cc.followTvValue;
      slaveCommandSend(ftvCmd);
      if (cc.followTvValue) {
        TvData tv{};
        if (tvDataRead(tv) && tv.reachable) {
          SlaveCommand capCmd{};
          capCmd.type         = SLAVE_CMD_LED_TV_CAP;
          capCmd.tvCapPercent = tv.backlight;
          slaveCommandSend(capCmd);
        }
      }
      break;
    }

    case CTRL_LED_MORSE: {
      prefs.saveMorseText(cc.morseText);
      SlaveCommand cmd{};
      cmd.type = SLAVE_CMD_LED_MORSE_TEXT;
      strlcpy(cmd.morseText, cc.morseText, sizeof(cmd.morseText));
      slaveCommandSend(cmd);
      break;
    }
  }
}

// ============================================================
//  CONTROL RELEE DIN SENZORI — nucleu minimal, fara MQTT/WiFi/TV
//  Apelat devreme in setup() INAINTE de blocul WiFiManager, ca
//  releele sa aiba starea corecta din primele secunde de la boot.
//  Zona dreapta (Slave UART) poate sa nu aiba date inca → ramane
//  OFF (safe default); converge dupa 1-2 cicluri de loop.
//  Idempotent: reutilizat si in processZones() implicit prin updateLogic.
// ============================================================
void controlReleeFromSensors() {
  // Senzor local stanga — disponibil instant pe I2C
  leftZone.readSensor(true);

  // Senzor dreapta (Slave UART) — daca SlaveCommTask a apucat sa citeasca
  SlaveData snap{};
  if (slaveDataRead(snap) && snap.fetchOk) {
    rightZone.setExternalSensorValues(snap.temp, snap.hum, snap.ts);
  }

  // Override-uri persistate in NVS
  leftZone.setManualOverride(prefs.overrideLeft);
  rightZone.setManualOverride(prefs.overrideRight);

  // Decizie + actionare pini — 100% locala, fara retea. forceImmediate la boot.
  leftZone.updateLogic(prefs.tempThresh, prefs.humThresh,
                       prefs.tempHyst,   prefs.humHyst, true);
  rightZone.updateLogic(prefs.tempThresh, prefs.humThresh,
                        prefs.tempHyst,   prefs.humHyst, true);

  Serial.printf("[Boot] Relee initiale: STANGA=%s DREAPTA=%s\n",
                leftZone.getRelayState()  ? "ON" : "OFF",
                rightZone.getRelayState() ? "ON" : "OFF");

  // Snapshot initial — taskNetwork publica imediat ce se conecteaza la MQTT.
  publishControlSnapshot(snap.fetchOk, snap.consecutiveErrors, snap.lastSuccessMs, true);
}

// ============================================================
//  PROCESS ZONES — logica principală (apelată la interval / comandă)
//  forcePublish=true → publica snapshot chiar fara schimbare de releu
//  (comenzi: setConfig/LED/refresh trebuie sa actualizeze dashboard-ul).
// ============================================================
void processZones(bool forcePublish) {
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

  // 3. Citire senzor local (stânga) — Wire pe Core 1 (control), OK.
  //    Comenzile MQTT (config/override/LED) sunt aplicate in loop() ÎNAINTE de
  //    processZones (via applyControlCommand), deci config-ul e deja actualizat aici.
  leftZone.readSensor(false);
  if (leftZone.getConsecErrors() >= 5 && leftZone.getConsecErrors() % 5 == 0) {
    I2CRecovery::recoverBus(I2C_SDA_PIN, I2C_SCL_PIN);
  }

  // 4. Sync override state din prefs → zone
  leftZone.setManualOverride(prefs.overrideLeft);
  rightZone.setManualOverride(prefs.overrideRight);

  // 5. updateLogic — decide starea releului (histerezis + anti-chatter).
  //    forcePublish (comanda) → comutare imediata; periodic → anti-chatter activ.
  leftZone.updateLogic(prefs.tempThresh, prefs.humThresh, prefs.tempHyst,
                       prefs.humHyst, forcePublish);
  rightZone.updateLogic(prefs.tempThresh, prefs.humThresh, prefs.tempHyst,
                        prefs.humHyst, forcePublish);

  // 6. Publica snapshot telemetrie → taskNetwork (Core 0) il trimite pe MQTT.
  publishControlSnapshot(slaveOnline, slaveErrors, snap.lastSuccessMs, forcePublish);

  // 7. Status LED — g_mqttConnected scris de taskNetwork (owner mqtt).
  if (!slaveOnline) {
    statusLed.setSlaveOffline();
  } else {
    statusLed.updateStatus(g_wifiAvailable, g_mqttConnected);
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
      leftZone.setForceOff(false);
      rightZone.setForceOff(false);
      // Republicarea se face automat: urmatorul processZones bumpeaza seq.
      delay(1000);
      statusLed.setBlue();
    }
  } else {
    btnPressStart = 0;
  }
}

// ============================================================
//  RUTARE COMENZI MQTT (taskNetwork) — pending → cozile potrivite.
//  Config/override/reset/LED → control (NVS owned de control);
//  TV → TvCommTask; getLog/rebootSlave/reboot/refresh tratate aici.
// ============================================================
void routeMqttCommands() {
  MqttPending &p = mqtt.getPending();

  if (p.refresh) {
    // App-ul trimite refresh imediat dupa ce s-a abonat → publicam "online"
    // (non-retained) ca sa-l prinda instant. Vezi MqttService.cs (CleanStart).
    mqtt.publishOnline(true);
    ControlCommand c{}; c.type = CTRL_REFRESH;
    controlCommandSend(c); p.refresh = false;
  }
  if (p.setOverrideL) {
    ControlCommand c{}; c.type = CTRL_SET_OVERRIDE_L; c.overrideVal = p.overrideLVal;
    controlCommandSend(c); p.setOverrideL = false;
  }
  if (p.setOverrideR) {
    ControlCommand c{}; c.type = CTRL_SET_OVERRIDE_R; c.overrideVal = p.overrideRVal;
    controlCommandSend(c); p.setOverrideR = false;
  }
  if (p.setConfig) {
    ControlCommand c{}; c.type = CTRL_SET_CONFIG;
    c.threshT = p.threshT; c.threshH = p.threshH; c.interval = p.interval;
    c.hystT = p.hystT;     c.hystH = p.hystH;
    controlCommandSend(c); p.setConfig = false;
  }
  if (p.resetDefaults) {
    ControlCommand c{}; c.type = CTRL_RESET;
    controlCommandSend(c); p.resetDefaults = false;
  }
  if (p.setLedNow) {
    ControlCommand c{}; c.type = CTRL_LED_SET; c.ledPercent = p.ledPercent;
    controlCommandSend(c); p.setLedNow = false;
  }
  if (p.setLedSched) {
    ControlCommand c{}; c.type = CTRL_LED_SCHEDULE;
    c.ledOnH = p.ledOnH; c.ledOnM = p.ledOnM;
    c.ledOffH = p.ledOffH; c.ledOffM = p.ledOffM;
    c.ledMaxI = p.ledMaxI; c.ledSchedEn = p.ledSchedEn;
    controlCommandSend(c); p.setLedSched = false;
  }
  if (p.setLedMode) {
    ControlCommand c{}; c.type = CTRL_LED_MODE;
    c.ledModeId = p.ledModeId;
    c.ledModeP1 = p.ledModeP1; c.ledModeP2 = p.ledModeP2;
    c.ledModeP3 = p.ledModeP3; c.ledModeP4 = p.ledModeP4;
    controlCommandSend(c); p.setLedMode = false;
  }
  if (p.setFollowTvBrightness) {
    ControlCommand c{}; c.type = CTRL_LED_FOLLOW_TV; c.followTvValue = p.followTvValue;
    controlCommandSend(c); p.setFollowTvBrightness = false;
  }
  if (p.setLedMorseText) {
    ControlCommand c{}; c.type = CTRL_LED_MORSE;
    strlcpy(c.morseText, p.ledMorseText, sizeof(c.morseText));
    controlCommandSend(c); p.setLedMorseText = false;
  }
  if (p.rebootSlave) {
    SlaveCommand cmd{}; cmd.type = SLAVE_CMD_REBOOT;
    slaveCommandSend(cmd); p.rebootSlave = false;
  }
  if (p.getLog) {
    if (g_logBuf) {
      size_t n = eventLog.dumpJson(g_logBuf, PSRAM_LOG_BUF_SIZE);
      if (n > 0) mqtt.publishLog(g_logBuf, n);
    }
    p.getLog = false;
  }
  if (p.setTvConfig) {
    TvCommand t{}; t.type = TV_CMD_CONFIG;
    strlcpy(t.ip, p.tvConfigIp, sizeof(t.ip));
    memcpy(t.mac, p.tvConfigMac, 6);
    tvCommandSend(t); TvCommTask::forcePoll();
    p.setTvConfig = false;
  }
  if (p.setTv) {
    TvCommand t{}; t.type = TV_CMD_ACTION;
    strlcpy(t.action, p.tvAction, sizeof(t.action));
    t.value = p.tvValue;
    tvCommandSend(t); TvCommTask::forcePoll();
    p.setTv = false;
  }
  if (p.reboot) {
    mqtt.publishOnline(false);
    delay(200);
    ESP.restart();
  }
}

// ============================================================
//  CONSUM REZULTAT POLL TV (taskNetwork) — publica + LED cap follow-TV.
// ============================================================
void consumeTvPollResult() {
  TvData tv{};
  if (!tvDataRead(tv) || !tv.newValueReady) return;

  mqtt.publishTvState(tvCtrl.state());   // citire tvCtrl (Core 0) — cosmetic, acceptat

  ControlState st{};
  if (controlStateRead(st) && st.followTvBrightness && tv.reachable) {
    SlaveCommand capCmd{};
    capCmd.type         = SLAVE_CMD_LED_TV_CAP;
    capCmd.tvCapPercent = tv.backlight;
    slaveCommandSend(capCmd);
  }
  tv.newValueReady = false;
  tvDataWrite(tv);
}

// ============================================================
//  TASK NETWORK (Core 0) — WiFi + MQTT + watchdog WiFi + housekeeping.
//  NU atinge niciodata zone/NVS/config (owned de control). Comenzile care
//  modifica config sunt rutate catre control prin g_controlCommandQueue.
// ============================================================
void taskNetwork(void* pv) {
  (void)pv;
  esp_task_wdt_add(NULL);   // subscrie acest task la WDT (60s)
  Serial.printf("[NetTask] Core %d started (prio=%d)\n",
                xPortGetCoreID(), NET_TASK_PRIORITY);

  for (;;) {
    esp_task_wdt_reset();

    WifiWatchdog::tick();   // sync g_wifiAvailable + reconectare 5min/3×15s + reboot 6h
    mqtt.loop();            // connect/backoff/keepalive + cleanup TLS la pierdere WiFi
    g_mqttConnected = mqtt.connected();

    routeMqttCommands();    // pending MQTT → cozile potrivite

    if (g_wifiAvailable) {
      ControlState st{};
      if (controlStateRead(st)) mqtt.publishFromSnapshot(st);
      TimeSync::loop();
      PreventiveReboot::checkWeekly();
    }

    consumeTvPollResult();

    HeapMonitor::check();
    BootLoopGuard::tick();
    DiagnosticLogger::printPeriodicLog();

    vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_POLL_MS));
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

  // 9b. Primitive CONTROL ↔ NETWORK — create INAINTE de TvCommTask si de
  //     controlReleeFromSensors (ambele le folosesc).
  g_controlState = (ControlState *)ps_malloc(sizeof(ControlState));
  if (!g_controlState) g_controlState = new ControlState();
  memset(g_controlState, 0, sizeof(ControlState));
  g_controlStateMutex   = xSemaphoreCreateMutex();
  g_controlCommandQueue = xQueueCreate(CONTROL_CMD_QUEUE_DEPTH, sizeof(ControlCommand));
  g_tvCommandQueue      = xQueueCreate(TV_CMD_QUEUE_DEPTH, sizeof(TvCommand));
  if (!g_controlStateMutex || !g_controlCommandQueue || !g_tvCommandQueue) {
    Serial.println("[FATAL] control/tv primitive create failed");
    delay(500);
    ESP.restart();
  }

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

  // 11b. PSRAM + mutex pentru TvData (TvCommTask Core 0 ↔ loopTask Core 1)
  g_tvData = (TvData *)ps_malloc(sizeof(TvData));
  if (!g_tvData) g_tvData = new TvData();
  memset(g_tvData, 0, sizeof(TvData));
  g_tvDataMutex = xSemaphoreCreateMutex();
  if (!g_tvDataMutex) {
    Serial.println("[FATAL] g_tvDataMutex create failed");
    delay(500);
    ESP.restart();
  }
  TvCommTask::start(tvCtrl);

  // 11c. Prima decizie relee — INAINTE de blocul WiFiManager (care poate bloca
  // 1-180s). Senzorii sunt disponibili (Wire init la pasul 3, SHT30 init la pasul 4).
  // Zona dreapta poate sa nu aiba date Slave inca (UART abia a pornit) → OFF safe.
  // Reevaluare completa are loc in primul loop() via lastProcessMs==0.
  controlReleeFromSensors();

  // 12. WiFiManager — singura cale de rețea
  // Portal de configurare doar daca nu exista credentiale salvate (first-run).
  // La reboot dupa pica de curent, portalul e dezactivat (setConfigPortalTimeout=1)
  // ca sa nu blocheze automatizarea zeci de secunde.
  {
    WiFiManager wm;
    wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_MS / 1000);

    // Daca exista credentiale salvate, nu deschidem portal — incercam direct si
    // daca nu reusim in timeout, mergem offline. Automatizarea porneste imediat.
    bool hasSavedCredentials = (strlen(WiFi.SSID().c_str()) > 0);
    wm.setConfigPortalTimeout(hasSavedCredentials ? 1 : 180);

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
    } else {
        Serial.println("[WiFi] No WiFi la boot — WifiWatchdog reincearca la 5 min.");
        g_wifiAvailable = false;
        // NU oprim WiFi (fara WIFI_OFF): pastram modul STA ca WiFi.reconnect()
        // din WifiWatchdog sa functioneze ulterior.
    }

    // Auto-reconnect nativ al stack-ului — acopera caderile scurte intre
    // ferestrele de 10 min; WifiWatchdog ramane backstop-ul garantat.
    WiFi.setAutoReconnect(true);

    // MQTT + NTP se initializeaza NECONDITIONAT (recuperare completa la boot):
    // begin() nu necesita WiFi activ. Cand WifiWatchdog ridica WiFi-ul, loop()
    // (gate-uit de g_wifiAvailable) le conecteaza singur; SNTP se sincronizeaza
    // in fundal odata ce apare reteaua.
    TimeSync::begin();
    mqtt.begin(&prefs);
  }

  // 14. PSRAM buffer pentru log dump (4KB — prea mare pentru stack)
  g_logBuf = (char *)ps_malloc(PSRAM_LOG_BUF_SIZE);
  if (!g_logBuf)
    g_logBuf = new char[PSRAM_LOG_BUF_SIZE];
  Serial.printf("[PSRAM] g_logBuf @ %p  psramFree=%u KB\n", g_logBuf,
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

  // WDT configurat si loopTask subscris anterior (pas 10).

  // 15. TV controller — incarca IP/MAC din NVS, citeste device info o data
  tvCtrl.begin();
  if (tvCtrl.state().configured && g_wifiAvailable) {
    tvCtrl.readDeviceInfo();
    Serial.printf("[TV] Device info: serial=%s sw=%s\n",
                  tvCtrl.state().serial, tvCtrl.state().swVersion);
  }

  // Nota: prima citire senzori + decizie relee s-a facut la pasul 11c
  // (controlReleeFromSensors()), inainte de blocul WiFi. Nu mai e necesar repetat.

  // 16. taskNetwork pe Core 0 — preia WiFi + MQTT (mqtt.begin facut la pasul 12).
  //     Pornit ULTIMUL: toate dependentele (mqtt, tvCtrl, TimeSync) sunt gata.
  //     loopTask (Core 1) ramane CONTROL pur.
  xTaskCreatePinnedToCore(taskNetwork, "NetTask", NET_TASK_STACK, nullptr,
                          NET_TASK_PRIORITY, nullptr, 0);
  Serial.println("[NetTask] creat pe Core 0.");

  statusLed.setColor(0, 180, 0); // verde = boot OK
  Serial.println("=== Boot complete ===\n");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  // loopTask (Core 1) = CONTROL pur. WiFi/MQTT/housekeeping ruleaza pe
  // taskNetwork (Core 0). Releele functioneaza INDIFERENT de starea retelei.
  esp_task_wdt_reset();

  // Buton reset (factory) — atinge prefs + zone (control domain).
  handleResetButton();

  // Aplica comenzile primite de la network (config/override/LED/reset/refresh).
  // Sunt aplicate ÎNAINTE de processZones → config-ul e actualizat la updateLogic.
  ControlCommand cc;
  bool gotCmd = false;
  while (g_controlCommandQueue &&
         xQueueReceive(g_controlCommandQueue, &cc, 0) == pdTRUE) {
    applyControlCommand(cc);
    gotCmd = true;
  }

  // Evaluare relee la interval SAU imediat la comanda. lastProcessMs==0 → foc
  // imediat la boot (nu asteptam 5 min). Senzorii sunt cititi deja in setup().
  const unsigned long now = millis();
  const unsigned long intervalMs = (unsigned long)prefs.intervalSec * 1000UL;
  const bool firstRun = (lastProcessMs == 0);
  if (firstRun || (now - lastProcessMs >= intervalMs) || gotCmd) {
    processZones(gotCmd || firstRun);   // forta publish la comanda / primul ciclu
  }

  // Poll comenzi la 50ms — fara busy-spin, hraneste WDT, latenta mica la comenzi.
  vTaskDelay(pdMS_TO_TICKS(CONTROL_LOOP_POLL_MS));
}

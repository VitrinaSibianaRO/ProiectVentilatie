// ProiectVentilatie.ino — Master ESP32 (Carbon V3 #1)
// Arhitectură: Ethernet W5500 + SSLClient → HiveMQ Cloud
//              UART2 → Slave ESP32 (senzor SHT30 dreapta)
//              SHT30 local I2C (senzor stânga)
//              Relee x2 pentru ventilație
// FĂRĂ WiFi, FĂRĂ Blynk, FĂRĂ DHT22.

#include <Arduino.h>
#include <Ethernet.h>
#include <SPI.h>
#include <WiFi.h> // doar pentru WiFi.mode(WIFI_OFF)
#include <WiFiManager.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <utility/w5100.h>

#include "AppPreferences.h"
#include "Config.h"
#include "DiagnosticLogger.h"
#include "EventLog.h"
#include "LedConfigStorage.h"
#include "MqttBridge.h"
#include "OtaUpdater.h"
#include "Resilience.h"
#include "SharedState.h"
#include "Sht30Sensor.h"
#include "SlaveCommTask.h"
#include "SlaveOtaProxy.h"
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
volatile bool g_otaInProgress = false;
bool g_ethAvailable = false; // true doar dacă W5500 este detectat fizic
bool g_wifiAvailable = false;

// ============================================================
//  ETHERNET MAC derivat din eFuse (unicat per chip)
// ============================================================
void getEthernetMac(byte mac[6]) {
  uint64_t chipId = ESP.getEfuseMac();
  mac[0] = 0xDE; // prefix local (bit unicast + locally administered)
  mac[1] = (chipId >> 8) & 0xFF;
  mac[2] = (chipId >> 16) & 0xFF;
  mac[3] = (chipId >> 24) & 0xFF;
  mac[4] = (chipId >> 32) & 0xFF;
  mac[5] = (chipId >> 40) & 0xFF;
}

// ============================================================
//  PROCESS ZONES — logica principală (apelată la interval)
// ============================================================
void processZones() {
  lastProcessMs = millis();

  // 1. Override expiry check
  bool overrideChanged = prefs.tickOverrideExpiry();

  // 2. Citeste snapshot Slave din SharedState (Core 0 actualizeaza la 500ms).
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

  // 3. Citire senzor local (stânga) — Wire e pe Core 1, OK
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
  mqtt.publishStateIfNeeded(leftZone, rightZone, slaveOnline, slaveErrors,
                            snap.lastSuccessMs, g_ledIntensity,
                            g_ledSchedEnabled);

  // 7. Procesare comenzi MQTT pending
  MqttPending &pending = mqtt.getPending();

  if (pending.refresh) {
    leftZone.readSensor(true);
    // Trezeste SlaveCommTask (Core 0) pentru fetch imediat.
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
      prefs.saveOverrideLeft(false); // clear
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
      prefs.saveOverrideRight(false); // clear
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
      if (n > 0) {
        mqtt.publishLog(g_logBuf, n);
      }
    }
    pending.getLog = false;
  }

  // LED commands — trimise catre Slave prin command queue (Core 0 le executa)
  if (pending.setLedNow) {
    SlaveCommand cmd{};
    cmd.type = SLAVE_CMD_LED_SET;
    cmd.ledPercent = pending.ledPercent;
    slaveCommandSend(cmd);
    Serial.printf("[LED] Set %u%% queued\n", pending.ledPercent);
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
    Serial.printf("[LED] Schedule %02u:%02u→%02u:%02u @%u%% en=%d queued\n",
                  pending.ledOnH, pending.ledOnM, pending.ledOffH,
                  pending.ledOffM, pending.ledMaxI, pending.ledSchedEn);
    // Persist in NVS Master (mirror) — non-blocking, Core 1 OK
    ledPrefs.save(pending.ledOnH, pending.ledOnM, pending.ledOffH,
                  pending.ledOffM, pending.ledMaxI, pending.ledSchedEn);
    g_ledSchedEnabled = pending.ledSchedEn;
    mqtt.requestPublishNow();
    pending.setLedSched = false;
  }

  if (pending.update) {
    Serial.println("[OTA] Master OTA triggered from MQTT");
    mqtt.publishOnline(false);
    delay(200);
    OtaResult result =
        OtaUpdater::start(pending.otaUrl, pending.otaSha,
                          [](int pct) { Serial.printf("[OTA] %d%%\n", pct); });
    if (result == OTA_OK) {
      delay(500);
      ESP.restart();
    } else {
      Serial.printf("[OTA] FAILED: %d\n", result);
      mqtt.publishOnline(true);
    }
    pending.update = false;
    memset(pending.otaUrl, 0, sizeof(pending.otaUrl));
    memset(pending.otaSha, 0, sizeof(pending.otaSha));
  }

  if (pending.updateSlave) {
    Serial.println("[SlaveOTA] Triggered from MQTT");
    // Suspend SlaveCommTask (Core 0) — SlaveOtaProxy detine exclusiv
    // Serial2/SlaveUartClient
    g_otaInProgress = true;
    SlaveCommTask::suspend();
    // Failsafe RIGHT pe durata OTA (Slave indisponibil ~30s)
    rightZone.enterFailsafe();
    SlaveOtaResult sres = SlaveOtaProxy::perform(
        pending.slaveOtaUrl, pending.slaveOtaSha, Serial2,
        [](uint32_t sent, uint32_t total) {
          static uint32_t lastPct = 0;
          uint32_t pct = (sent * 100UL) / (total ? total : 1);
          if (pct - lastPct >= 5 || pct == 100) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "{\"type\":\"slave_ota_progress\",\"sent\":%u,\"total\":%"
                     "u,\"percent\":%u}",
                     sent, total, pct);
            mqtt.publishEventJson(buf);
            lastPct = pct;
          }
        });
    // Redă Serial2 catre SlaveCommTask
    SlaveCommTask::resume();
    g_otaInProgress = false;
    char done[128];
    if (sres == SOTA_OK) {
      snprintf(done, sizeof(done),
               "{\"type\":\"slave_ota_done\",\"result\":\"ok\"}");
    } else {
      snprintf(done, sizeof(done),
               "{\"type\":\"slave_ota_done\",\"result\":\"fail\",\"code\":%d}",
               (int)sres);
    }
    mqtt.publishEventJson(done);
    // Lasam failsafe in vigoare — exit-ul se face automat la primul fetch
    // reusit.
    pending.updateSlave = false;
    memset(pending.slaveOtaUrl, 0, sizeof(pending.slaveOtaUrl));
    memset(pending.slaveOtaSha, 0, sizeof(pending.slaveOtaSha));
  }

  if (pending.reboot) {
    Serial.println("[System] Reboot requested from MQTT");
    mqtt.publishOnline(false);
    delay(200);
    ESP.restart();
  }

  // 8. Status LED update
  bool ethOk = (Ethernet.linkStatus() == LinkON);
  bool mqttOk = mqtt.connected();
  if (!slaveOnline) {
    statusLed.setSlaveOffline();
  } else {
    statusLed.updateStatus(ethOk, mqttOk);
  }
}

// ============================================================
//  W5500 HOT-PLUG — detectare automată la conectare ulterioară
// ============================================================
static unsigned long _lastHotPlugCheckMs = 0;

const char *ethernetHardwareName(EthernetHardwareStatus status) {
  switch (status) {
  case EthernetW5100:
    return "W5100";
  case EthernetW5200:
    return "W5200";
  case EthernetW5500:
    return "W5500";
  default:
    return "NO_HARDWARE";
  }
}

uint8_t readW5500VersionRegister(uint32_t spiFreqHz = W5500_SPI_FREQ_HZ,
                                 uint8_t spiMode = SPI_MODE1) {
  SPI.beginTransaction(SPISettings(spiFreqHz, MSBFIRST, spiMode));
  digitalWrite(W5500_CS_PIN, LOW);
  SPI.transfer(0x00); // VERSIONR address high byte
  SPI.transfer(0x39); // VERSIONR address low byte
  SPI.transfer(0x00); // Common register block, read, variable data length
  uint8_t version = SPI.transfer(0x00);
  digitalWrite(W5500_CS_PIN, HIGH);
  SPI.endTransaction();
  return version;
}

void probeW5500AlternateSpiModes() {
  uint8_t m0 = readW5500VersionRegister(W5500_PROBE_SPI_FREQ_HZ, SPI_MODE0);
  uint8_t m1 = readW5500VersionRegister(W5500_PROBE_SPI_FREQ_HZ, SPI_MODE1);
  uint8_t m2 = readW5500VersionRegister(W5500_PROBE_SPI_FREQ_HZ, SPI_MODE2);
  uint8_t m3 = readW5500VersionRegister(W5500_PROBE_SPI_FREQ_HZ, SPI_MODE3);
  Serial.printf(
      "[Eth] W5500 probe: M0=0x%02X M1=0x%02X M2=0x%02X M3=0x%02X @ %lu Hz\n",
      m0, m1, m2, m3, (unsigned long)W5500_PROBE_SPI_FREQ_HZ);
}

// Scriere directa registru COMMON (BSB=0, RWB=1, OM=00 var-len). Replica
// protocolul lib pentru chip 55, dar sub controlul nostru: putem testa pas
// cu pas write+read MR ca sa identificam unde clona pierde sincronul.
void writeW5500Common(uint16_t addr, uint8_t value,
                      uint32_t spiFreqHz = W5500_SPI_FREQ_HZ) {
  SPI.beginTransaction(SPISettings(spiFreqHz, MSBFIRST, SPI_MODE1));
  digitalWrite(W5500_CS_PIN, LOW);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(0x04); // common reg, write, var-len
  SPI.transfer(value);
  digitalWrite(W5500_CS_PIN, HIGH);
  SPI.endTransaction();
}

uint8_t readW5500Common(uint16_t addr,
                       uint32_t spiFreqHz = W5500_SPI_FREQ_HZ) {
  SPI.beginTransaction(SPISettings(spiFreqHz, MSBFIRST, SPI_MODE1));
  digitalWrite(W5500_CS_PIN, LOW);
  SPI.transfer(addr >> 8);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(0x00); // common reg, read, var-len
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(W5500_CS_PIN, HIGH);
  SPI.endTransaction();
  return v;
}

// Diagnostic: replica isW5500() din libraria Arduino Ethernet, dar sub
// controlul nostru (frecventa configurabila, log la fiecare pas). Returneaza
// true daca toate scrierile/citirile MR se confirma.
bool diagnoseW5500Writes(uint32_t spiFreqHz) {
  Serial.printf("[Eth] DIAG @ %lu Hz: ", (unsigned long)spiFreqHz);

  // Soft reset: scriem MR=0x80, asteptam pana citim MR=0x00.
  writeW5500Common(0x0000, 0x80, spiFreqHz);
  uint8_t mr = 0xFF;
  for (int i = 0; i < 100; i++) {
    delay(2);
    mr = readW5500Common(0x0000, spiFreqHz);
    if (mr == 0x00)
      break;
  }
  Serial.printf("softReset MR=0x%02X ", mr);
  if (mr != 0x00) {
    Serial.println("FAIL (soft reset nu se confirma)");
    return false;
  }

  // Test write+read pe MR cu valori distincte.
  const uint8_t patterns[] = {0x08, 0x10, 0x00};
  for (uint8_t p : patterns) {
    writeW5500Common(0x0000, p, spiFreqHz);
    delay(1);
    uint8_t back = readW5500Common(0x0000, spiFreqHz);
    Serial.printf("[w=0x%02X r=0x%02X%s] ", p, back, (back == p) ? "" : "!");
    if (back != p) {
      Serial.println("FAIL");
      return false;
    }
  }

  uint8_t ver = readW5500Common(0x0039, spiFreqHz);
  Serial.printf("VERSIONR=0x%02X ", ver);
  Serial.println(ver == 0x04 ? "OK" : "FAIL (VERSIONR final)");
  return ver == 0x04;
}

void resetW5500Hardware() {
  digitalWrite(W5500_CS_PIN, HIGH);
  pinMode(W5500_RST_PIN, OUTPUT);
  digitalWrite(W5500_RST_PIN, LOW);
  delay(W5500_RESET_LOW_MS);
  digitalWrite(W5500_RST_PIN, HIGH);
  delay(W5500_RESET_READY_MS);
  digitalWrite(W5500_CS_PIN, HIGH);
}

void printW5500ProbeHint(uint8_t rawVersion) {
  if (rawVersion == 0x04)
    return;

  Serial.printf("[Eth] SPI pins: SCK=%d MISO=%d MOSI=%d CS=%d RST=%d\n",
                W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN,
                W5500_RST_PIN);
  if (rawVersion == 0x00 || rawVersion == 0xFF) {
    Serial.println("[Eth] VERSIONR is bus-idle value: check 3V3/GND, CS, MISO, "
                   "reset and wiring.");
  } else if (rawVersion == 0x02) {
    Serial.println(
        "[Eth] VERSIONR=0x02 is not a W5500 signature. Check that the module "
        "is W5500 and the firmware pinout matches the wiring.");
  } else {
    Serial.println("[Eth] Unexpected VERSIONR value. Check SPI mode/pinout or "
                   "possible non-W5500 hardware.");
  }
}

bool initNetwork(bool isHotPlug) {
  // 1. CS trebuie sa stea inactiv inainte de reset si inainte de SPI.begin().
  // Pe boot/reboot rapid, un CS flotant poate lasa W5500 intr-o stare SPI
  // invalida.
  pinMode(W5500_CS_PIN, OUTPUT);
  digitalWrite(W5500_CS_PIN, HIGH);
  delay(5);

  // 2. Reset hardware W5500 obligatoriu la fiecare incercare.
  // Resetul este deliberat lung: unele module raman blocate dupa reseturi
  // rapide ESP32.
  resetW5500Hardware();

  // 3. Initializare magistrala SPI - O SINGURA DATA
  // Apeluri multiple de SPI.begin pe ESP32 pot corupe matricea GPIO.
  static bool _spiInitialized = false;
  if (!_spiInitialized) {
    SPI.begin(W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
    SPI.setFrequency(W5500_SPI_FREQ_HZ);
    _spiInitialized = true;
    Serial.printf("[Eth] SPI Bus initialized (HSPI) @ %lu Hz\n",
                  (unsigned long)W5500_SPI_FREQ_HZ);
  }

  // 4. Spunem librariei Ethernet ce pin folosim pentru CS
  Ethernet.init(W5500_CS_PIN);

  // 5. Proba SPI directa: W5500 VERSIONR trebuie sa fie 0x04.
  // Daca aici vezi 0x00/0xFF, problema e electrica/pinout/reset, nu DHCP/MQTT.
  uint8_t rawVersion = readW5500VersionRegister();
  Serial.printf("[Eth] W5500 VERSIONR raw=0x%02X (expected 0x04)\n",
                rawVersion);
  printW5500ProbeHint(rawVersion);
  if (rawVersion != 0x04) {
    probeW5500AlternateSpiModes();
  } else {
    // Diagnostic write+read MR la 2 viteze ca sa stim ce viteza sustine clona
    // pentru librarie (W5100.init scrie MR repetat la SPI_ETHERNET_SETTINGS).
    diagnoseW5500Writes(W5500_SPI_FREQ_HZ);
    diagnoseW5500Writes(500000UL);
  }

  // 6. Verificam prezenta chipului pe magistrala (3 incercari cu re-reset).
  // Ethernet.hardwareStatus() raporteaza doar dupa W5100.init(); nu declanseaza
  // detectia singur.
  bool hwFound = false;
  for (int attempt = 1; attempt <= 3 && !hwFound; attempt++) {
    if (W5100.init() != 0) {
      EthernetHardwareStatus status = Ethernet.hardwareStatus();
      Serial.printf("[Eth] Hardware detected by Ethernet lib: %s\n",
                    ethernetHardwareName(status));
      hwFound = (status != EthernetNoHardware);
      break;
    }
    if (attempt < 3) {
      Serial.printf(
          "[Eth] W5500 not detected (attempt %d/3), re-resetting...\n",
          attempt);
      resetW5500Hardware();
      Ethernet.init(W5500_CS_PIN);
      rawVersion = readW5500VersionRegister();
      Serial.printf("[Eth] W5500 VERSIONR raw=0x%02X (expected 0x04)\n",
                    rawVersion);
      printW5500ProbeHint(rawVersion);
      if (rawVersion != 0x04) {
        probeW5500AlternateSpiModes();
      }
    }
  }
  if (!hwFound) {
    if (!isHotPlug)
      Serial.println("[Eth] W5500 hardware NOT FOUND after 3 attempts! Running "
                     "in NO-NETWORK mode.");
    return false;
  }

  if (isHotPlug) {
    Serial.println("[Eth] W5500 detected (hot-plug)! Initializing network...");
  }

  byte mac[6];
  getEthernetMac(mac);
  EthLinkMonitor::setMac(mac);
  Serial.printf("[Eth] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1],
                mac[2], mac[3], mac[4], mac[5]);

  // 7. Daca PHY-ul nu vede link, nu blocam boot-ul 15s in DHCP/NTP.
  // Lasam hot-plug check sa reincerce init-ul complet dupa conectarea cablului.
  EthernetLinkStatus link = Ethernet.linkStatus();
  Serial.printf("[Eth] Link status before DHCP: %s\n",
                link == LinkON ? "ON" : (link == LinkOFF ? "OFF" : "UNKNOWN"));
  if (link != LinkON) {
    Serial.println("[Eth] W5500 hardware OK, but Ethernet link is DOWN. "
                   "Skipping DHCP/NTP/MQTT for now.");
    statusLed.setColor(200, 80, 0);
    return false;
  }

  // 8. Incercam DHCP
  if (Ethernet.begin(mac, ETH_DHCP_TIMEOUT_MS) == 0) {
    Serial.println("[Eth] DHCP FAILED. Continuing in OFFLINE mode (LinkMonitor "
                   "will retry).");
    statusLed.setColor(200, 0, 0);
    if (!isHotPlug)
      delay(2000);
  } else {
    Serial.print("[Eth] IP: ");
    Serial.println(Ethernet.localIP());
    if (isHotPlug) {
      statusLed.setColor(0, 180, 0);
      Serial.println(
          "[Eth] Hot-plug init complete — full network mode active.");
    }
  }

  TimeSync::begin();
  mqtt.begin(&prefs);

  return true;
}

void checkEthernetHotPlug() {
  unsigned long now = millis();
  if (now - _lastHotPlugCheckMs < ETH_HOTPLUG_CHECK_MS)
    return;
  _lastHotPlugCheckMs = now;

  bool wasEthAvailable = g_ethAvailable;
  // initNetwork(true) apeleaza mqtt.begin() intern daca detecteaza hardware.
  // Daca Ethernet devine disponibil si WiFi era activ, initNetwork comuta
  // transportul MQTT la Ethernet si dezactiveaza WiFi.
  g_ethAvailable = initNetwork(true);

  if (g_ethAvailable && !wasEthAvailable && g_wifiAvailable) {
    // Ethernet tocmai a aparut — Ethernet are prioritate, oprim WiFi.
    Serial.println("[Net] Ethernet hot-plug: comutare de la WiFi la Ethernet.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    g_wifiAvailable = false;
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

  // 0. OTA rollback protection — marcăm firmware-ul curent ca valid.
  //    Fără asta, bootloader-ul va reveni la versiunea anterioară la reboot.
  esp_ota_mark_app_valid_cancel_rollback();

  // 1. Oprim radio WiFi + Bluetooth — economie ~80mA + zero interferență
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.println("[Boot] WiFi + BT OFF");

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

  // 12. Ethernet W5500
  Serial.println("[Eth] Initializing W5500...");
  g_ethAvailable = initNetwork(false);

  // 12. WiFiManager Fallback
  if (!g_ethAvailable) {
    Serial.println("[WiFi] Ethernet not available. Starting WiFiManager fallback...");
    statusLed.setColor(0, 0, 200); // Albastru
    WiFiManager wm;
    wm.setAPCallback([](WiFiManager* myWiFiManager) {
        Serial.println("[WiFi] Entered config mode");
        Serial.println(WiFi.softAPIP());
        Serial.println(myWiFiManager->getConfigPortalSSID());
    });
    
    // autoConnect blocant infinit. Sistemul ramane aici pana se configureaza WiFi-ul.
    if (wm.autoConnect("CarbonV3-AP")) {
        Serial.println("[WiFi] Conectat cu succes la WiFi!");
        g_wifiAvailable = true;
        
        // Setup NTP si MQTT deoarece initNetwork() nu le-a rulat
        TimeSync::begin();
        mqtt.begin(&prefs);
    } else {
        Serial.println("[WiFi] Failed to connect.");
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

  // Network-dependent operations — skip complet dacă W5500 și WiFi lipsesc
  if (g_ethAvailable || g_wifiAvailable) {
    if (g_ethAvailable) {
      EthLinkMonitor::check(W5500_RST_PIN);
      Ethernet.maintain();
    }
    PreventiveReboot::checkWeekly();
    mqtt.loop();
    TimeSync::loop();
  }

  // Buton reset
  handleResetButton();

  // Verificare hot-plug W5500 (doar daca niciuna nu e valabila)
  if (!g_ethAvailable && !g_wifiAvailable) {
    checkEthernetHotPlug();
  }

  // Process zones la intervalul configurat
  unsigned long now = millis();
  unsigned long intervalMs = (unsigned long)prefs.intervalSec * 1000UL;

  if (now - lastProcessMs >= intervalMs) {
    processZones();
  }

  // Procesare imediată la comenzi MQTT pending
  if ((g_ethAvailable || g_wifiAvailable) && mqtt.hasPendingCommands()) {
    processZones();
  }

  // Diagnostic logging
  DiagnosticLogger::printPeriodicLog();
}

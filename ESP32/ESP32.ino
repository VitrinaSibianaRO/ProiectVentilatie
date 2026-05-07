// ProiectVentilatie.ino — Master ESP32 (Carbon V3 #1)
// Arhitectură: Ethernet W5500 + SSLClient → HiveMQ Cloud
//              UART2 → Slave ESP32 (senzor SHT30 dreapta)
//              SHT30 local I2C (senzor stânga)
//              Relee x2 pentru ventilație
// FĂRĂ WiFi, FĂRĂ Blynk, FĂRĂ DHT22.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>           // doar pentru WiFi.mode(WIFI_OFF)
#include <Ethernet.h>
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

#include "Config.h"
#include "Resilience.h"
#include "AppPreferences.h"
#include "LedConfigStorage.h"
#include "Sht30Sensor.h"
#include "SlaveUartClient.h"
#include "SharedState.h"
#include "SlaveCommTask.h"
#include "VentilationZone.h"
#include "MqttBridge.h"
#include "OtaUpdater.h"
#include "SlaveOtaProxy.h"
#include "TimeSync.h"
#include "EventLog.h"
#include "SystemLED.h"

// ============================================================
//  GLOBALE — ownership clar, toate în acest fișier
// ============================================================

AppPreferences  prefs;
LedConfigStorage ledPrefs;
Sht30Sensor     sensorLocal;         // SHT30 stânga (I2C pe Master)
SlaveUartClient slaveClient;         // UART2 → Slave
SystemLED       statusLed(LED_COUNT, LED_PIN, LED_ENABLE_PIN);
MqttBridge      mqtt;
EventLog        eventLog;

// Zone: stânga = senzor local, dreapta = senzor remote (Slave)
VentilationZone leftZone (&sensorLocal, RELAY_LEFT_PIN,  "STANGA");
VentilationZone rightZone(              RELAY_RIGHT_PIN, "DREAPTA");

// ============================================================
//  TIMER simplu (înlocuiește SimpleTimer cu o logică minimală)
// ============================================================
unsigned long lastProcessMs = 0;
unsigned long lastTimeSyncMs = 0;

// LED status cache (fetched from Slave)
uint8_t g_ledIntensity = 0;
bool    g_ledSchedEnabled = false;

// Buffer PSRAM pentru dump log JSON (4KB pe stack ar fragmenta heap).
// Alocat o singura data in setup() cu fallback pe heap intern.
char* g_logBuf = nullptr;

// ============================================================
//  FREERTOS SHARED STATE (Core 0 SlaveCommTask ↔ Core 1 loopTask)
// ============================================================
SemaphoreHandle_t g_slaveDataMutex    = nullptr;
QueueHandle_t     g_slaveCommandQueue = nullptr;
SlaveData*        g_slaveData         = nullptr;    // alocat in PSRAM
volatile bool     g_otaInProgress     = false;
bool              g_ethAvailable      = false;   // true doar dacă W5500 este detectat fizic

// ============================================================
//  ETHERNET MAC derivat din eFuse (unicat per chip)
// ============================================================
void getEthernetMac(byte mac[6]) {
    uint64_t chipId = ESP.getEfuseMac();
    mac[0] = 0xDE;   // prefix local (bit unicast + locally administered)
    mac[1] = (chipId >> 8)  & 0xFF;
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
    //    Non-blocant: <1ms (vs pana la 3s blocant anterior pe slaveClient.fetch()).
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
            g_ledIntensity    = snap.ledIntensity;
            g_ledSchedEnabled = snap.ledSchedEnabled;

            // Daca Slave a fost resetat si a pierdut schedule-ul, re-trimitem din NVS Master
            if (!ledPrefs.compare(snap.ledOnH, snap.ledOnM, snap.ledOffH, snap.ledOffM,
                                  snap.ledMaxI, snap.ledSchedEnabled)) {
                Serial.println("[LED] Slave schedule diverged, resyncing from NVS");
                SlaveCommand ledCmd{};
                ledCmd.type       = SLAVE_CMD_LED_SCHEDULE;
                ledCmd.ledOnH     = ledPrefs.onH;
                ledCmd.ledOnM     = ledPrefs.onM;
                ledCmd.ledOffH    = ledPrefs.offH;
                ledCmd.ledOffM    = ledPrefs.offM;
                ledCmd.ledMaxI    = ledPrefs.maxI;
                ledCmd.ledSchedEn = ledPrefs.enabled;
                slaveCommandSend(ledCmd);
            }
        }
    } else {
        const int slaveErrors = snapOk ? snap.consecutiveErrors : 0;
        if (slaveErrors >= SLAVE_FAILSAFE_AFTER_FAILS && !rightZone.isInFailsafe()) {
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
    leftZone.updateLogic(prefs.tempThresh, prefs.humThresh,
                          prefs.tempHyst,   prefs.humHyst);
    rightZone.updateLogic(prefs.tempThresh, prefs.humThresh,
                           prefs.tempHyst,   prefs.humHyst);

    // 6. MQTT publish state (incluzând LED status)
    mqtt.publishStateIfNeeded(leftZone, rightZone, slaveOnline,
                               slaveErrors, snap.lastSuccessMs,
                               g_ledIntensity, g_ledSchedEnabled);

    // 7. Procesare comenzi MQTT pending
    MqttPending& pending = mqtt.getPending();

    if (pending.refresh) {
        leftZone.readSensor(true);
        // Trezeste SlaveCommTask (Core 0) pentru fetch imediat.
        SlaveCommTask::forceRead();
        mqtt.requestPublishNow();
        pending.refresh = false;
    }

    if (pending.setOverrideL) {
        int v = pending.overrideLVal;
        if (v == 0) prefs.saveOverrideLeft(false);
        else if (v == 1) prefs.saveOverrideLeft(true);
        else if (v == 2) prefs.saveOverrideLeft(false);  // clear
        leftZone.setManualOverride(prefs.overrideLeft);
        mqtt.requestPublishNow();
        pending.setOverrideL = false;
    }

    if (pending.setOverrideR) {
        int v = pending.overrideRVal;
        if (v == 0) prefs.saveOverrideRight(false);
        else if (v == 1) prefs.saveOverrideRight(true);
        else if (v == 2) prefs.saveOverrideRight(false);  // clear
        rightZone.setManualOverride(prefs.overrideRight);
        mqtt.requestPublishNow();
        pending.setOverrideR = false;
    }

    if (pending.setConfig) {
        if (pending.threshT > 0) prefs.saveTempThresh(pending.threshT);
        if (pending.threshH > 0) prefs.saveHumThresh(pending.threshH);
        if (pending.interval > 0) prefs.saveIntervalSec(pending.interval);
        if (pending.hystT >= 0) prefs.saveTempHyst(pending.hystT);
        if (pending.hystH >= 0) prefs.saveHumHyst(pending.hystH);
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
        cmd.type       = SLAVE_CMD_LED_SET;
        cmd.ledPercent = pending.ledPercent;
        slaveCommandSend(cmd);
        Serial.printf("[LED] Set %u%% queued\n", pending.ledPercent);
        g_ledIntensity = pending.ledPercent;
        mqtt.requestPublishNow();
        pending.setLedNow = false;
    }

    if (pending.setLedSched) {
        SlaveCommand cmd{};
        cmd.type       = SLAVE_CMD_LED_SCHEDULE;
        cmd.ledOnH     = pending.ledOnH;
        cmd.ledOnM     = pending.ledOnM;
        cmd.ledOffH    = pending.ledOffH;
        cmd.ledOffM    = pending.ledOffM;
        cmd.ledMaxI    = pending.ledMaxI;
        cmd.ledSchedEn = pending.ledSchedEn;
        slaveCommandSend(cmd);
        Serial.printf("[LED] Schedule %02u:%02u→%02u:%02u @%u%% en=%d queued\n",
            pending.ledOnH, pending.ledOnM,
            pending.ledOffH, pending.ledOffM,
            pending.ledMaxI, pending.ledSchedEn);
        // Persist in NVS Master (mirror) — non-blocking, Core 1 OK
        ledPrefs.save(pending.ledOnH, pending.ledOnM,
                      pending.ledOffH, pending.ledOffM,
                      pending.ledMaxI, pending.ledSchedEn);
        g_ledSchedEnabled = pending.ledSchedEn;
        mqtt.requestPublishNow();
        pending.setLedSched = false;
    }

    if (pending.update) {
        Serial.println("[OTA] Master OTA triggered from MQTT");
        mqtt.publishOnline(false);
        delay(200);
        OtaResult result = OtaUpdater::start(
            pending.otaUrl,
            pending.otaSha,
            [](int pct) {
                Serial.printf("[OTA] %d%%\n", pct);
            });
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
        // Suspend SlaveCommTask (Core 0) — SlaveOtaProxy detine exclusiv Serial2/SlaveUartClient
        g_otaInProgress = true;
        SlaveCommTask::suspend();
        // Failsafe RIGHT pe durata OTA (Slave indisponibil ~30s)
        rightZone.enterFailsafe();
        SlaveOtaResult sres = SlaveOtaProxy::perform(
            pending.slaveOtaUrl,
            pending.slaveOtaSha,
            Serial2,
            [](uint32_t sent, uint32_t total) {
                static uint32_t lastPct = 0;
                uint32_t pct = (sent * 100UL) / (total ? total : 1);
                if (pct - lastPct >= 5 || pct == 100) {
                    char buf[96];
                    snprintf(buf, sizeof(buf),
                        "{\"type\":\"slave_ota_progress\",\"sent\":%u,\"total\":%u,\"percent\":%u}",
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
            snprintf(done, sizeof(done), "{\"type\":\"slave_ota_done\",\"result\":\"ok\"}");
        } else {
            snprintf(done, sizeof(done),
                "{\"type\":\"slave_ota_done\",\"result\":\"fail\",\"code\":%d}", (int)sres);
        }
        mqtt.publishEventJson(done);
        // Lasam failsafe in vigoare — exit-ul se face automat la primul fetch reusit.
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
    bool ethOk  = (Ethernet.linkStatus() == LinkON);
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

bool initNetwork(bool isHotPlug) {
    if (isHotPlug) {
        Serial.println("[Eth] W5500 detected (hot-plug)! Initializing network...");
    }

    // Reset hardware W5500 pentru stare curata
    pinMode(W5500_RST_PIN, OUTPUT);
    digitalWrite(W5500_RST_PIN, LOW);
    delay(50);
    digitalWrite(W5500_RST_PIN, HIGH);
    delay(200);

    // Remapam SPI pe pinii HSPI disponibili pe Carbon V3 (GPIO18/23 nu exista pe placa!)
    SPI.begin(W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
    Ethernet.init(W5500_CS_PIN);

    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        if (!isHotPlug) {
            Serial.println("[Eth] W5500 hardware not found! Running in NO-NETWORK mode.");
        }
        return false;
    }

    byte mac[6];
    getEthernetMac(mac);
    EthLinkMonitor::setMac(mac);   // cache pentru recovery la link-down
    Serial.printf("[Eth] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (Ethernet.begin(mac, ETH_DHCP_TIMEOUT_MS) == 0) {
        Serial.println("[Eth] DHCP FAILED. Continuing in OFFLINE mode (LinkMonitor will retry).");
        statusLed.setColor(200, 0, 0);
        if (!isHotPlug) delay(2000);
    } else {
        Serial.print("[Eth] IP: ");
        Serial.println(Ethernet.localIP());
        if (isHotPlug) {
            statusLed.setColor(0, 180, 0);
            Serial.println("[Eth] Hot-plug init complete — full network mode active.");
        }
    }

    // NTP + MQTT (necesită Ethernet funcțional, sau vor face retry intern)
    TimeSync::begin();
    mqtt.begin(&prefs);

    return true; // Hardware is present
}

void checkEthernetHotPlug() {
    unsigned long now = millis();
    if (now - _lastHotPlugCheckMs < ETH_HOTPLUG_CHECK_MS) return;
    _lastHotPlugCheckMs = now;

    // Interogare SPI — rapida, non-blocanta (<1ms)
    // SPI a fost deja initializat in setup() chiar daca a dat fail initial.
    if (Ethernet.hardwareStatus() == EthernetNoHardware) return;

    // W5500 tocmai a aparut pe magistrala!
    g_ethAvailable = initNetwork(true);
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
        
        Serial.println("[BootGuard] Halted in Safe Mode. Waiting 5min then restart.");
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
    leftZone.begin();    // init SHT30 + releu stânga
    rightZone.begin();   // doar releu dreapta (senzor vine de la Slave)

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
    Serial2.begin(SLAVE_UART_BAUD, SERIAL_8N1, SLAVE_UART_RX_PIN, SLAVE_UART_TX_PIN);
    slaveClient.begin(Serial2);
    Serial.printf("[UART] Serial2 ready: baud=%u TX=%d RX=%d\n",
        SLAVE_UART_BAUD, SLAVE_UART_TX_PIN, SLAVE_UART_RX_PIN);

    // 9. PSRAM + FreeRTOS primitives pentru shared Slave state (Core 0 ↔ Core 1)
    g_slaveData = (SlaveData*)ps_malloc(sizeof(SlaveData));
    if (!g_slaveData) g_slaveData = new SlaveData();
    memset(g_slaveData, 0, sizeof(SlaveData));

    g_slaveDataMutex    = xSemaphoreCreateMutex();
    g_slaveCommandQueue = xQueueCreate(SLAVE_CMD_QUEUE_DEPTH, sizeof(SlaveCommand));

    if (!g_slaveDataMutex || !g_slaveCommandQueue) {
        Serial.println("[FATAL] FreeRTOS primitive create failed");
        delay(500);
        ESP.restart();
    }
    Serial.printf("[PSRAM] g_slaveData @ %p  psramFree=%u KB\n",
                  g_slaveData, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    // 10. Pornire SlaveCommTask pe Core 0 (detine Serial2 / SlaveUartClient)
    SlaveCommTask::start(slaveClient);

    // 11. Ethernet W5500
    Serial.println("[Eth] Initializing W5500...");
    g_ethAvailable = initNetwork(false);

    // 14. PSRAM buffer pentru log dump (4KB — prea mare pentru stack)
    g_logBuf = (char*)ps_malloc(PSRAM_LOG_BUF_SIZE);
    if (!g_logBuf) g_logBuf = new char[PSRAM_LOG_BUF_SIZE];
    Serial.printf("[PSRAM] g_logBuf @ %p  psramFree=%u KB\n",
                  g_logBuf, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);

    // 15. Watchdog hardware 60s
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms     = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    // Folosim reconfigure in loc de init — evită eroarea "TWDT already initialized"
    esp_err_t wdtErr = esp_task_wdt_init(&wdtConfig);
    if (wdtErr == ESP_ERR_INVALID_STATE) {
        esp_task_wdt_reconfigure(&wdtConfig);
    }
    esp_task_wdt_add(NULL);

    // 14. Prima citire senzori
    leftZone.readSensor(true);   // force = true la boot

    statusLed.setColor(0, 180, 0);   // verde = boot OK
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

    // Network-dependent operations — skip complet dacă W5500 lipsește
    if (g_ethAvailable) {
        EthLinkMonitor::check(W5500_RST_PIN);
        PreventiveReboot::checkWeekly();
        mqtt.loop();
        TimeSync::loop();
        Ethernet.maintain();
    }

    // Buton reset
    handleResetButton();

    // Verificare hot-plug W5500 (doar daca nu e deja disponibil)
    if (!g_ethAvailable) {
        checkEthernetHotPlug();
    }

    // Process zones la intervalul configurat
    unsigned long now = millis();
    unsigned long intervalMs = (unsigned long)prefs.intervalSec * 1000UL;

    if (now - lastProcessMs >= intervalMs) {
        processZones();
    }

    // Procesare imediată la comenzi MQTT pending
    if (g_ethAvailable && mqtt.hasPendingCommands()) {
        processZones();
    }
}

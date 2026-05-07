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

#include "Config.h"
#include "Resilience.h"
#include "AppPreferences.h"
#include "LedConfigStorage.h"
#include "Sht30Sensor.h"
#include "SlaveUartClient.h"
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

    // 2. Fetch senzor Slave (dreapta) prin UART
    float slaveTemp = 0, slaveHum = 0;
    uint32_t slaveTs = 0;
    bool slaveFetchOk = slaveClient.fetch(slaveTemp, slaveHum, slaveTs);
    int slaveErrors = slaveClient.getConsecutiveErrors();

    // Slave online/offline logic
    bool slaveOnline;
    if (slaveFetchOk) {
        rightZone.setExternalSensorValues(slaveTemp, slaveHum, slaveTs);
        if (rightZone.isInFailsafe()) {
            rightZone.exitFailsafe();
            Serial.println("[Slave] Recovery: failsafe exited");
        }
        slaveOnline = true;
    } else {
        if (slaveErrors >= SLAVE_FAILSAFE_AFTER_FAILS && !rightZone.isInFailsafe()) {
            rightZone.enterFailsafe();
            eventLog.append(EVT_SENSOR_ERR, ZONE_RIGHT, "slave_offline");
            Serial.printf("[Slave] FAILSAFE after %d errors\n", slaveErrors);
        }
        slaveOnline = slaveErrors < SLAVE_FAILSAFE_AFTER_FAILS;
    }

    // 3. Citire senzor local (stânga)
    leftZone.readSensor(false);
    if (leftZone.getConsecErrors() >= 5 && leftZone.getConsecErrors() % 5 == 0) {
        // I2C bus recovery — toggle SCL 9× la SDA stuck low
        I2CRecovery::recoverBus(I2C_SDA_PIN, I2C_SCL_PIN);
    }

    // 3b. TIME_SYNC — trimitem epoch Slave-ului la fiecare oră
    unsigned long now2 = millis();
    if (slaveFetchOk && (lastTimeSyncMs == 0 || now2 - lastTimeSyncMs >= 3600000UL)) {
        uint32_t epoch = TimeSync::getEpochSec();
        if (epoch > 1700000000UL) {
            bool ok = slaveClient.sendTimeSync(epoch);
            Serial.printf("[TimeSync] Sent to Slave: %lu → %s\n", (unsigned long)epoch, ok ? "OK" : "FAIL");
            if (ok) lastTimeSyncMs = now2;
        }
    }

    // 3c. Fetch LED status de la Slave (pentru MQTT state)
    if (slaveFetchOk) {
        uint8_t s_onH, s_onM, s_offH, s_offM, s_maxI;
        if (slaveClient.fetchLedStatus(g_ledIntensity, g_ledSchedEnabled,
                                       s_onH, s_onM, s_offH, s_offM, s_maxI)) {
            // Verificare mirror NVS vs Slave
            if (!ledPrefs.compare(s_onH, s_onM, s_offH, s_offM, s_maxI, g_ledSchedEnabled)) {
                Serial.println("[LED] Slave schedule divergent. Re-syncing din NVS...");
                slaveClient.sendLedSchedule(ledPrefs.onH, ledPrefs.onM,
                                            ledPrefs.offH, ledPrefs.offM,
                                            ledPrefs.maxI, ledPrefs.enabled);
            }
        }
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
                               slaveErrors, slaveClient.getLastSuccessMs(),
                               g_ledIntensity, g_ledSchedEnabled);

    // 7. Procesare comenzi MQTT pending
    MqttPending& pending = mqtt.getPending();

    if (pending.refresh) {
        leftZone.readSensor(true);
        // Forțăm și re-fetch de la Slave
        slaveClient.fetch(slaveTemp, slaveHum, slaveTs);
        if (slaveFetchOk) {
            rightZone.setExternalSensorValues(slaveTemp, slaveHum, slaveTs);
        }
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
        bool ok = slaveClient.sendReboot();
        Serial.printf("[Slave] Reboot %s\n", ok ? "OK" : "FAIL");
        pending.rebootSlave = false;
    }

    if (pending.getLog) {
        char logBuf[4096];
        size_t n = eventLog.dumpJson(logBuf, sizeof(logBuf));
        if (n > 0) {
            mqtt.publishLog(logBuf, n);
        }
        pending.getLog = false;
    }

    // LED commands — forwarded to Slave via UART
    if (pending.setLedNow) {
        bool ok = slaveClient.sendLedSet(pending.ledPercent);
        Serial.printf("[LED] Set %u%% → %s\n", pending.ledPercent, ok ? "OK" : "FAIL");
        if (ok) {
            g_ledIntensity = pending.ledPercent;
            mqtt.requestPublishNow();
        }
        pending.setLedNow = false;
    }

    if (pending.setLedSched) {
        bool ok = slaveClient.sendLedSchedule(
            pending.ledOnH, pending.ledOnM,
            pending.ledOffH, pending.ledOffM,
            pending.ledMaxI, pending.ledSchedEn);
        Serial.printf("[LED] Schedule %02u:%02u→%02u:%02u @%u%% en=%d → %s\n",
            pending.ledOnH, pending.ledOnM,
            pending.ledOffH, pending.ledOffM,
            pending.ledMaxI, pending.ledSchedEn,
            ok ? "OK" : "FAIL");
        if (ok) {
            ledPrefs.save(pending.ledOnH, pending.ledOnM,
                          pending.ledOffH, pending.ledOffM,
                          pending.ledMaxI, pending.ledSchedEn);
            g_ledSchedEnabled = pending.ledSchedEn;
            mqtt.requestPublishNow();
        }
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

    // 9. Ethernet W5500
    Serial.println("[Eth] Initializing W5500...");
    // Reset W5500
    pinMode(W5500_RST_PIN, OUTPUT);
    digitalWrite(W5500_RST_PIN, LOW);
    delay(50);
    digitalWrite(W5500_RST_PIN, HIGH);
    delay(200);

    Ethernet.init(W5500_CS_PIN);
    byte mac[6];
    getEthernetMac(mac);
    EthLinkMonitor::setMac(mac);   // cache pentru recovery la link-down
    Serial.printf("[Eth] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (Ethernet.begin(mac, ETH_DHCP_TIMEOUT_MS) == 0) {
        Serial.println("[Eth] DHCP FAILED — restart in 5s");
        statusLed.setColor(200, 0, 0);
        delay(5000);
        ESP.restart();
    }
    Serial.print("[Eth] IP: ");
    Serial.println(Ethernet.localIP());

    // 10. NTP (necesită Ethernet funcțional)
    TimeSync::begin();

    // 11. MQTT
    mqtt.begin(&prefs);

    // 12. Watchdog hardware 60s
    const esp_task_wdt_config_t wdtConfig = {
        .timeout_ms     = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    esp_task_wdt_init(&wdtConfig);
    esp_task_wdt_add(NULL);

    // 13. Prima citire senzori
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
    EthLinkMonitor::check(W5500_RST_PIN);
    PreventiveReboot::checkWeekly();

    // MQTT pump
    mqtt.loop();

    // NTP resync la 24h
    TimeSync::loop();

    // Ethernet maintain (DHCP renew)
    Ethernet.maintain();

    // Buton reset
    handleResetButton();

    // Process zones la intervalul configurat
    unsigned long now = millis();
    unsigned long intervalMs = (unsigned long)prefs.intervalSec * 1000UL;

    if (now - lastProcessMs >= intervalMs) {
        processZones();
    }

    // Procesare imediată la comenzi MQTT pending
    if (mqtt.hasPendingCommands()) {
        processZones();
    }
}

// ============================================================
//  ProiectVentilatie.ino
//
//  Arhitectură production-grade pentru automatizare ventilaţie:
//  - Logica releelor este 100% locală şi autonomă.
//  - Blynk + HiveMQ MQTT în paralel (Faza 2: comenzi + lock).
//  - Parametrii sunt persistaţi în NVS (supravieţuiesc reboot).
//  - Sistemul funcţionează complet offline dacă Blynk/MQTT pică.
//  - Override manual cu timeout automat (nu rămâne blocat).
//  - Watchdog hardware 60s împotriva oricărui blocaj.
//  - Lock bidirecţional Blynk ↔ MAUI previne conflicte.
// ============================================================

#include "Config.h"
#include "AppPreferences.h"
#include "SystemLED.h"
#include "VentilationZone.h"
#include "MqttBridge.h"
#include "TimeSync.h"
#include "EventLog.h"
#include "OtaUpdater.h"

#include <WiFi.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp32.h>
#include <esp_task_wdt.h>

// ============================================================
//  OBIECTE GLOBALE
// ============================================================
AppPreferences  prefs;
SystemLED       statusLed(LED_COUNT, LED_PIN, LED_ENABLE_PIN);
VentilationZone leftZone (DHT_LEFT_PIN,  RELAY_LEFT_PIN,  "STANGA");
VentilationZone rightZone(DHT_RIGHT_PIN, RELAY_RIGHT_PIN, "DREAPTA");
BlynkTimer      timer;
MqttBridge      mqtt;
EventLog        eventLog;

// ============================================================
//  FORWARD DECLARATIONS
// ============================================================
void processZones();
void pushRelayState();
void rebuildMainTimer();
void safeStopRelays();
void checkMemory();

// ============================================================
//  STARE INTERNĂ
// ============================================================

// ID-ul timerului principal — reținut pentru a-l putea reface
// când se schimbă intervalul fără race condition.
int  mainTimerID        = -1;
bool timerRebuildNeeded = false;

// Pending flags pentru comenzile venite din Blynk.
// Handler-ii BLYNK_WRITE scriu aici; logica se aplică în processZones().
struct PendingCmd {
    bool    overrideLeftSet   = false;
    bool    overrideLeftVal   = false;
    bool    overrideLeftClear = false;

    bool    overrideRightSet   = false;
    bool    overrideRightVal   = false;
    bool    overrideRightClear = false;

    bool    resetDefaults = false;
    bool    restartDevice = false;
} pending;

// Valori trimise anterior către Blynk — pentru trimitere condiţionată.
float lastSentTempL = -999.f, lastSentHumL = -999.f;
float lastSentTempR = -999.f, lastSentHumR = -999.f;
int   cycleCount    = 0;

// WiFi reconnect throttle
unsigned long lastWiFiCheckMs = 0;

// WiFi down — restart preventiv după 10 min (Faza 3)
unsigned long wifiDownSinceMs = 0;
bool          wifiWasDown     = false;

// Stare anterioară releu — pentru detecție tranziție (event log)
bool prevRelayLeft  = false;
bool prevRelayRight = false;

// Buton fizic reset
unsigned long buttonPressMs  = 0;
bool          buttonHeld     = false;

// ============================================================
//  FUNCŢII HELPER
// ============================================================

void safeStopRelays() {
    leftZone.emergencyOff();
    rightZone.emergencyOff();
}

void rebuildMainTimer() {
    if (mainTimerID >= 0) timer.deleteTimer(mainTimerID);
    mainTimerID = timer.setInterval((long)prefs.intervalSec * 1000L, processZones);
    Serial.printf("[Timer] Interval setat la %d secunde.\n", prefs.intervalSec);
}

// Sincronizează starea curentă a releelor + lock spre Blynk.
// V5/V6 reflectă starea releului (auto sau override). Toggle-ul Blynk e bidirecțional.
void pushRelayState() {
    if (!Blynk.connected()) return;
    Blynk.virtualWrite(VP_RELAY_LEFT,  leftZone.getRelayState()  ? 1 : 0);
    Blynk.virtualWrite(VP_RELAY_RIGHT, rightZone.getRelayState() ? 1 : 0);
    Blynk.virtualWrite(VP_LOCK_OWNER,  (int)mqtt.getLockOwner());
}

// ============================================================
//  CICLU PRINCIPAL DE CITIRE + DECIZIE
//  Apelat periodic de BlynkTimer. Tot ce ţine de logica
//  releelor se întâmplă EXCLUSIV aici.
// ============================================================
void processZones() {
    Serial.println("\n--- Ciclu senzori ---");

    // 0. Procesare pending MQTT commands (Faza 2).
    //    Callback-ul MQTT setează doar flags; aici le aplicăm.
    bool mqttPendingProcessed = false;
    if (mqtt.hasPendingCommands()) {
        mqttPendingProcessed = true;
        MqttPending& mp = mqtt.getPending();

        if (mp.refresh) {
            mp.refresh = false;
            mqtt.requestPublishNow();
            Serial.println("[MQTT] cmd:refresh → push state.");
        }

        if (mp.setOverrideL) {
            mp.setOverrideL = false;
            if (mp.overrideLVal == 2) {
                prefs.saveOverrideLeft(false);
                leftZone.setManualOverride(false);
                Serial.println("[MQTT] Override stânga: cleared.");
            } else {
                bool val = (mp.overrideLVal == 1);
                prefs.saveOverrideLeft(val);
                leftZone.setManualOverride(val);
                Serial.printf("[MQTT] Override stânga: %s\n", val ? "ON" : "OFF");
            }
        }

        if (mp.setOverrideR) {
            mp.setOverrideR = false;
            if (mp.overrideRVal == 2) {
                prefs.saveOverrideRight(false);
                rightZone.setManualOverride(false);
                Serial.println("[MQTT] Override dreapta: cleared.");
            } else {
                bool val = (mp.overrideRVal == 1);
                prefs.saveOverrideRight(val);
                rightZone.setManualOverride(val);
                Serial.printf("[MQTT] Override dreapta: %s\n", val ? "ON" : "OFF");
            }
        }

        if (mp.setConfig) {
            mp.setConfig = false;
            if (mp.threshT > 0 && mp.threshT < 80.0f)
                prefs.saveTempThresh(mp.threshT);
            if (mp.threshH >= 0 && mp.threshH <= 100.0f)
                prefs.saveHumThresh(mp.threshH);
            if (mp.interval >= MIN_INTERVAL_SEC && mp.interval <= MAX_INTERVAL_SEC) {
                prefs.saveIntervalSec(mp.interval);
                timerRebuildNeeded = true;
            }
            if (mp.hystT >= 0.0f) prefs.saveTempHyst(mp.hystT);
            if (mp.hystH >= 0.0f) prefs.saveHumHyst(mp.hystH);
            // Sync Blynk UI cu valorile noi
            if (Blynk.connected()) {
                Blynk.virtualWrite(VP_THRESH_TEMP, prefs.tempThresh);
                Blynk.virtualWrite(VP_THRESH_HUM,  prefs.humThresh);
                Blynk.virtualWrite(VP_INTERVAL,    prefs.intervalSec);
                Blynk.virtualWrite(VP_HYST_TEMP,   prefs.tempHyst);
                Blynk.virtualWrite(VP_HYST_HUM,    prefs.humHyst);
            }
            Serial.printf("[MQTT] Config: T≥%.1f (hyst %.1f) H≥%.1f (hyst %.1f) Int:%d\n",
                prefs.tempThresh, prefs.tempHyst, prefs.humThresh, prefs.humHyst, prefs.intervalSec);
        }

        if (mp.resetDefaults) {
            mp.resetDefaults = false;
            prefs.resetToDefaults();
            leftZone.setManualOverride(false);
            rightZone.setManualOverride(false);
            timerRebuildNeeded = true;
            if (Blynk.connected()) {
                Blynk.virtualWrite(VP_THRESH_TEMP,     prefs.tempThresh);
                Blynk.virtualWrite(VP_THRESH_HUM,      prefs.humThresh);
                Blynk.virtualWrite(VP_INTERVAL,        prefs.intervalSec);
                Blynk.virtualWrite(VP_HYST_TEMP,       prefs.tempHyst);
                Blynk.virtualWrite(VP_HYST_HUM,        prefs.humHyst);
                Blynk.virtualWrite(VP_RESET_DEFAULTS,  0);
            }
            // Forțează retrimiterea senzorilor (FIX: Android nu mai vede valori după reset)
            lastSentTempL = -999.0f; lastSentHumL = -999.0f;
            lastSentTempR = -999.0f; lastSentHumR = -999.0f;
            Serial.println("[MQTT] cmd:reset → defaults restaurate.");
        }

        if (mp.reboot) {
            mp.reboot = false;
            Serial.println("[MQTT] cmd:reboot → restart curat.");
            if (Blynk.connected())
                Blynk.logEvent("system_restart", "Restart MQTT.");
            mqtt.publishOnline(false);
            delay(200);
            safeStopRelays();
            delay(300);
            ESP.restart();
        }

        // cmd:getLog — citește log din NVS și publică pe ventilatie/log
        if (mp.getLog) {
            mp.getLog = false;
            char logBuf[4096];
            size_t n = eventLog.dumpJson(logBuf, sizeof(logBuf));
            if (n > 0) {
                mqtt.publishLog(logBuf, n);
            } else {
                mqtt.publishLog("{\"entries\":[]}", 16);
            }
            Serial.println("[MQTT] cmd:getLog → log publicat.");
        }

        // cmd:update — OTA via GitHub releases
        if (mp.update) {
            mp.update = false;
            Serial.println("[MQTT] cmd:update → start OTA...");

            // Callback pentru progress reporting pe MQTT
            auto otaProgress = [](int pct) {
                char evBuf[64];
                snprintf(evBuf, sizeof(evBuf),
                    "{\"event\":\"ota_progress\",\"pct\":%d}", pct);
                mqtt.publishEventJson(evBuf);
            };

            OtaResult res = OtaUpdater::start(mp.otaUrl, mp.otaSha, otaProgress);

            if (res == OTA_OK) {
                // OTA reușit — restart
                mqtt.publishEventJson("{\"event\":\"ota_done\"}");
                mqtt.publishOnline(false);
                delay(200);
                ESP.restart();
            } else {
                // OTA eșuat
                const char* reasons[] = {
                    "ok", "url_invalid", "connect", "http",
                    "begin", "write", "end", "sha_mismatch"
                };
                char evBuf[128];
                snprintf(evBuf, sizeof(evBuf),
                    "{\"event\":\"ota_failed\",\"reason\":\"%s\"}",
                    reasons[(int)res]);
                mqtt.publishEventJson(evBuf);
                Serial.printf("[OTA] Failed: %s\n", reasons[(int)res]);
            }
        }

        // Sync Blynk relay/override state
        pushRelayState();

        // Release lock + push state cu lock=null
        mqtt.setLockOwner(LOCK_NONE);
        mqtt.requestPublishNow();
    }

    // 1. Aplică comenzile pending sosite din Blynk în ciclu anterior.
    //    Facem asta la începutul ciclului, nu în handler, pentru a evita
    //    orice interacţiune între ISR-ul Blynk şi digitalWrite.
    bool blynkPendingProcessed = false;
    if (pending.resetDefaults) {
        pending.resetDefaults = false;
        blynkPendingProcessed = true;
        prefs.resetToDefaults();
        leftZone.setManualOverride(false);
        rightZone.setManualOverride(false);
        timerRebuildNeeded = true;
        // Sincronizăm UI-ul Blynk cu noile valori default
        if (Blynk.connected()) {
            Blynk.virtualWrite(VP_THRESH_TEMP,     prefs.tempThresh);
            Blynk.virtualWrite(VP_THRESH_HUM,      prefs.humThresh);
            Blynk.virtualWrite(VP_INTERVAL,        prefs.intervalSec);
            Blynk.virtualWrite(VP_HYST_TEMP,       prefs.tempHyst);
            Blynk.virtualWrite(VP_HYST_HUM,        prefs.humHyst);
            Blynk.virtualWrite(VP_RESET_DEFAULTS,  0);
        }
        // Forțează retrimiterea senzorilor (FIX: Android nu mai vede valori după reset)
        lastSentTempL = -999.0f; lastSentHumL = -999.0f;
        lastSentTempR = -999.0f; lastSentHumR = -999.0f;
    }

    if (pending.overrideLeftClear) {
        pending.overrideLeftClear = false;
        blynkPendingProcessed = true;
        prefs.saveOverrideLeft(false);
        leftZone.setManualOverride(false);
    } else if (pending.overrideLeftSet) {
        pending.overrideLeftSet = false;
        blynkPendingProcessed = true;
        prefs.saveOverrideLeft(pending.overrideLeftVal);
        leftZone.setManualOverride(pending.overrideLeftVal);
    }

    if (pending.overrideRightClear) {
        pending.overrideRightClear = false;
        blynkPendingProcessed = true;
        prefs.saveOverrideRight(false);
        rightZone.setManualOverride(false);
    } else if (pending.overrideRightSet) {
        pending.overrideRightSet = false;
        blynkPendingProcessed = true;
        prefs.saveOverrideRight(pending.overrideRightVal);
        rightZone.setManualOverride(pending.overrideRightVal);
    }

    // Dacă am procesat un pending Blynk, publicăm state imediat + release lock
    if (blynkPendingProcessed) {
        mqtt.setLockOwner(LOCK_NONE);
        mqtt.requestPublishNow();
    }

    // 2. Verifică timeout overrides (expiră după N ore).
    bool overrideExpired = prefs.tickOverrideExpiry();
    if (overrideExpired) {
        leftZone.setManualOverride(prefs.overrideLeft);
        rightZone.setManualOverride(prefs.overrideRight);
        if (Blynk.connected()) {
            // V5/V6 vor fi sincronizate cu starea reală a releului în pushRelayState()
            Blynk.logEvent("override_expired", "Override manual a expirat automat.");
        }
        // Event log — override expired (Faza 3)
        if (!prefs.overrideLeft)
            eventLog.append(EVT_OVERRIDE_EXPIRED, ZONE_LEFT, "Override anulat");
        if (!prefs.overrideRight)
            eventLog.append(EVT_OVERRIDE_EXPIRED, ZONE_RIGHT, "Override anulat");
        mqtt.requestPublishNow();
    }

    // 3. Citeşte senzorii (cooldown intern în VentilationZone).
    leftZone.readSensor();
    rightZone.readSensor();

    // 4. Aplică logica de decizie locală — SINGURA sursă de adevăr.
    leftZone.updateLogic (prefs.tempThresh, prefs.humThresh, prefs.tempHyst, prefs.humHyst);
    rightZone.updateLogic(prefs.tempThresh, prefs.humThresh, prefs.tempHyst, prefs.humHyst);

    // FIX: dacă a fost procesat un pending Blynk/MQTT în acest ciclu, republicăm state
    // după readSensor + updateLogic, astfel ca MAUI/Blynk să vadă valorile actuale ale senzorilor
    // (nu cele cached pre-reset). Lock-ul a fost deja eliberat mai sus.
    if (mqttPendingProcessed || blynkPendingProcessed) {
        mqtt.requestPublishNow();
    }

    Serial.printf("  [STANGA]  T:%.1f°C  H:%.1f%%  Releu:%s  Override:%s\n",
        leftZone.getTemp(), leftZone.getHum(),
        leftZone.getRelayState() ? "ON" : "OFF",
        leftZone.getManualOverride() ? "DA" : "NU");
    Serial.printf("  [DREAPTA] T:%.1f°C  H:%.1f%%  Releu:%s  Override:%s\n",
        rightZone.getTemp(), rightZone.getHum(),
        rightZone.getRelayState() ? "ON" : "OFF",
        rightZone.getManualOverride() ? "DA" : "NU");

    // 5. Raportează datele către Blynk (condiţionat — nu trimiteri inutile).
    if (Blynk.connected()) {
        cycleCount++;
        bool forceUpdate = (cycleCount >= 10);   // heartbeat la 10 cicluri
        if (forceUpdate) cycleCount = 0;

        if (leftZone.isFirstReadDone()) {
            float tL = leftZone.getTemp(), hL = leftZone.getHum();
            if (forceUpdate || fabsf(tL - lastSentTempL) >= 0.5f
                            || fabsf(hL - lastSentHumL)  >= 1.0f) {
                Blynk.virtualWrite(VP_TEMP_LEFT, tL);
                Blynk.virtualWrite(VP_HUM_LEFT,  hL);
                lastSentTempL = tL; lastSentHumL = hL;
            }
        }

        if (rightZone.isFirstReadDone()) {
            float tR = rightZone.getTemp(), hR = rightZone.getHum();
            if (forceUpdate || fabsf(tR - lastSentTempR) >= 0.5f
                            || fabsf(hR - lastSentHumR)  >= 1.0f) {
                Blynk.virtualWrite(VP_TEMP_RIGHT, tR);
                Blynk.virtualWrite(VP_HUM_RIGHT,  hR);
                lastSentTempR = tR; lastSentHumR = hR;
            }
        }

        // Starea releelor şi overrides — mereu trimise (sunt valori de stare).
        pushRelayState();

        if (forceUpdate)
            Serial.println("  [Blynk] Heartbeat trimis.");
    }

    // Alertă erori consecutive senzor + event log (Faza 3)
    if (leftZone.getConsecErrors() >= 5) {
        if (Blynk.connected())
            Blynk.logEvent("sensor_error", "Senzor STANGA: 5+ erori consecutive!");
        // Log doar la exact 5 (nu la fiecare ciclu)
        if (leftZone.getConsecErrors() == 5)
            eventLog.append(EVT_SENSOR_ERR, ZONE_LEFT, "5 erori DHT");
    }
    if (rightZone.getConsecErrors() >= 5) {
        if (Blynk.connected())
            Blynk.logEvent("sensor_error", "Senzor DREAPTA: 5+ erori consecutive!");
        if (rightZone.getConsecErrors() == 5)
            eventLog.append(EVT_SENSOR_ERR, ZONE_RIGHT, "5 erori DHT");
    }

    // Detectare tranziție releu — event log (Faza 3)
    if (leftZone.getRelayState() != prevRelayLeft) {
        prevRelayLeft = leftZone.getRelayState();
        const char* msg = prevRelayLeft
            ? (leftZone.getManualOverride() ? "ON override" : "ON auto")
            : (leftZone.getManualOverride() ? "OFF override" : "OFF auto");
        eventLog.append(EVT_RELAY_CHANGE, ZONE_LEFT, msg);
    }
    if (rightZone.getRelayState() != prevRelayRight) {
        prevRelayRight = rightZone.getRelayState();
        const char* msg = prevRelayRight
            ? (rightZone.getManualOverride() ? "ON override" : "ON auto")
            : (rightZone.getManualOverride() ? "OFF override" : "OFF auto");
        eventLog.append(EVT_RELAY_CHANGE, ZONE_RIGHT, msg);
    }
}

void checkMemory() {
    uint32_t heap = ESP.getFreeHeap();
    Serial.printf("[Sistem] Heap liber: %u bytes\n", heap);
    if (Blynk.connected())
        Blynk.virtualWrite(VP_FREE_HEAP, heap / 1024);
    if (heap < 30000) {
        Serial.println("[CRITIC] Heap critic! Restart preventiv...");
        mqtt.publishOnline(false);
        delay(200);
        safeStopRelays();
        delay(300);
        ESP.restart();
    }
}

// ============================================================
//  BLYNK HANDLERS
//  REGULĂ: handler-ii NU scriu direct în releu şi NU apelează
//  digitalWrite. Setează doar flags/valori; processZones() decide.
// ============================================================

BLYNK_CONNECTED() {
    // Sincronizăm parametrii de configurare din cloud (sliderele care permit user input).
    // V5, V6 NU se sincronizează — ESP32 publică starea releelor curentă.
    Blynk.syncVirtual(VP_THRESH_TEMP, VP_THRESH_HUM, VP_INTERVAL,
                      VP_HYST_TEMP, VP_HYST_HUM, VP_RESET_DEFAULTS);
    // Trimite valorile curente hyst spre Blynk (în caz că NVS le-a salvat și sliderele Blynk
    // le ignoră la primul boot)
    Blynk.virtualWrite(VP_HYST_TEMP, prefs.tempHyst);
    Blynk.virtualWrite(VP_HYST_HUM,  prefs.humHyst);
    // Trimitem imediat starea curentă a releelor spre UI
    pushRelayState();
    // Lock owner reset + firmware build number (Faza 2)
    Blynk.virtualWrite(VP_LOCK_OWNER, (int)mqtt.getLockOwner());
    Blynk.virtualWrite(VP_FW_BUILD, (int)FW_BUILD_NUMBER);
    Serial.println("[Blynk] Conectat. Parametri sincronizaţi.");
}

BLYNK_WRITE(VP_THRESH_TEMP) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_THRESH_TEMP, prefs.tempThresh);
        return;
    }
    float v = param.asFloat();
    if (v > 0 && v < 80.0f) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        prefs.saveTempThresh(v);
        Serial.printf("[Blynk] Prag temperatură: %.1f°C\n", v);
    }
}

BLYNK_WRITE(VP_THRESH_HUM) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_THRESH_HUM, prefs.humThresh);
        return;
    }
    float v = param.asFloat();
    if (v >= 0 && v <= 100.0f) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        prefs.saveHumThresh(v);
        Serial.printf("[Blynk] Prag umiditate: %.1f%%\n", v);
    }
}

BLYNK_WRITE(VP_INTERVAL) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_INTERVAL, prefs.intervalSec);
        return;
    }
    int v = param.asInt();
    v = constrain(v, MIN_INTERVAL_SEC, MAX_INTERVAL_SEC);
    mqtt.setLockOwner(LOCK_BLYNK);
    mqtt.requestPublishNow();
    prefs.saveIntervalSec(v);
    // Timer-ul va fi refăcut în loop() — nu din handler Blynk.
    timerRebuildNeeded = true;
    Serial.printf("[Blynk] Interval solicitat: %d sec\n", v);
}

// V5 = toggle override stânga (0/1). 1 = override ON, 0 = revenire auto.
BLYNK_WRITE(VP_RELAY_LEFT) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_RELAY_LEFT, leftZone.getRelayState() ? 1 : 0);
        return;
    }
    mqtt.setLockOwner(LOCK_BLYNK);
    mqtt.requestPublishNow();
    int v = param.asInt();
    if (v == 1) {
        pending.overrideLeftSet = true;
        pending.overrideLeftVal = true;
    } else {
        pending.overrideLeftClear = true;
    }
    Serial.printf("[Blynk] V5 toggle override stânga: %s\n", v == 1 ? "ON" : "auto");
}

// V6 = toggle override dreapta
BLYNK_WRITE(VP_RELAY_RIGHT) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_RELAY_RIGHT, rightZone.getRelayState() ? 1 : 0);
        return;
    }
    mqtt.setLockOwner(LOCK_BLYNK);
    mqtt.requestPublishNow();
    int v = param.asInt();
    if (v == 1) {
        pending.overrideRightSet = true;
        pending.overrideRightVal = true;
    } else {
        pending.overrideRightClear = true;
    }
    Serial.printf("[Blynk] V6 toggle override dreapta: %s\n", v == 1 ? "ON" : "auto");
}

// V11 = hysteresis temperatură (Marja temp)
BLYNK_WRITE(VP_HYST_TEMP) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_HYST_TEMP, prefs.tempHyst);
        return;
    }
    float v = param.asFloat();
    if (v >= MIN_TEMP_HYST && v <= MAX_TEMP_HYST) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        prefs.saveTempHyst(v);
        Serial.printf("[Blynk] Hysteresis temp: %.1f°C\n", v);
    }
}

// V12 = hysteresis umiditate (Marja hum)
BLYNK_WRITE(VP_HYST_HUM) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_HYST_HUM, prefs.humHyst);
        return;
    }
    float v = param.asFloat();
    if (v >= MIN_HUM_HYST && v <= MAX_HUM_HYST) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        prefs.saveHumHyst(v);
        Serial.printf("[Blynk] Hysteresis hum: %.1f%%\n", v);
    }
}

BLYNK_WRITE(VP_RESET_DEFAULTS) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_RESET_DEFAULTS, 0);
        return;
    }
    if (param.asInt() == 1) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        pending.resetDefaults = true;
    }
}

BLYNK_WRITE(VP_RESTART) {
    if (param.asInt() == 1) {
        Serial.println("[Blynk] Comandă restart primită.");
        if (Blynk.connected())
            Blynk.logEvent("system_restart", "Restart la cererea utilizatorului.");
        mqtt.publishOnline(false);
        delay(200);
        statusLed.setBlue();
        safeStopRelays();
        delay(500);
        ESP.restart();
    }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== Pornire sistem ventilatie ===");

    // Iniţializăm hardware-ul înainte de orice altceva.
    statusLed.begin();
    leftZone.begin();
    rightZone.begin();
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

    // Încărcăm parametrii din NVS — aceştia vor ghida logica de la primul ciclu.
    prefs.begin();
    Serial.printf("[Prefs] Încărcaţi: T≥%.0f°C  H≥%.0f%%  Interval:%ds\n",
        prefs.tempThresh, prefs.humThresh, prefs.intervalSec);

    // Watchdog — iniţializat DUPĂ delay-urile de boot.
    // API-ul diferă între arduino-esp32 2.x şi 3.x — detectăm la compilare.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = WDT_TIMEOUT_SEC * 1000,
        .idle_core_mask = (1 << 0),
        .trigger_panic  = true
    };
    esp_task_wdt_init(&wdt_cfg);
#else
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
    esp_task_wdt_add(NULL);
    Serial.printf("[WDT] Watchdog iniţializat: %ds timeout.\n", WDT_TIMEOUT_SEC);

    // WiFiManager — portal captiv dacă nu există reţea salvată.
    WiFiManager wm;
    wm.setConnectTimeout(30);
    wm.setConfigPortalTimeout(180);
    if (!wm.autoConnect(WIFI_AP_NAME)) {
        Serial.println("[WiFi] Timeout portal. Restart...");
        safeStopRelays();
        delay(1000);
        ESP.restart();
    }
    Serial.printf("[WiFi] Conectat: %s  IP: %s\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    // NTP sync (Faza 3) — trebuie după WiFi connect
    TimeSync::begin();

    // Event log (Faza 3) — NVS namespace "log"
    eventLog.begin();

    Blynk.config(BLYNK_AUTH_TOKEN);
    // Nu blocăm în connect — sistemul porneşte şi fără Blynk.
    Blynk.connect(3000);

    // MQTT bridge — conectare non-blocking, încercări periodice în loop().
    mqtt.begin(&prefs);

    // Timer principal de citire senzori.
    mainTimerID = timer.setInterval((long)prefs.intervalSec * 1000L, processZones);
    // Verificare memorie la fiecare 10 minute.
    timer.setInterval(600000L, checkMemory);

    // Facem o primă citire imediată, fără să aşteptăm primul interval.
    processZones();

    Serial.printf("[Setup] Sistem pornit cu succes. Firmware build #%d\n",
                  (int)FW_BUILD_NUMBER);
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
    esp_task_wdt_reset();

    // Reconstrucţia timerului — se face în loop(), nu din handler Blynk.
    // Elimină race condition-ul cu deleteTimer în timp ce timerul rulează.
    if (timerRebuildNeeded) {
        timerRebuildNeeded = false;
        rebuildMainTimer();
    }

    // Gestionare WiFi + Blynk
    bool wifiOk = (WiFi.status() == WL_CONNECTED);

    if (!wifiOk) {
        // Tracking WiFi down pentru restart preventiv (Faza 3)
        if (!wifiWasDown) {
            wifiWasDown = true;
            wifiDownSinceMs = millis();
        }
        // Restart preventiv dacă WiFi e down > WIFI_DOWN_RESTART_MS (10 min)
        if (millis() - wifiDownSinceMs >= WIFI_DOWN_RESTART_MS) {
            Serial.println("[WiFi] Down > 10 min. Restart preventiv...");
            mqtt.publishOnline(false);
            delay(200);
            safeStopRelays();
            delay(300);
            ESP.restart();
        }
        if (millis() - lastWiFiCheckMs > 30000) {
            lastWiFiCheckMs = millis();
            Serial.println("[WiFi] Conexiune pierdută. Reconectare...");
            WiFi.reconnect();
        }
    } else {
        // WiFi OK — reset tracking
        wifiWasDown = false;

        Blynk.run();
        // MQTT pump — non-blocking, gestionează reconnect cu backoff.
        mqtt.loop();
        // NTP re-sync la 24h (Faza 3)
        TimeSync::loop();
        // Procesare imediată comenzi MQTT (nu așteptăm timer-ul periodic)
        if (mqtt.hasPendingCommands()) {
            processZones();
        }
        // Heartbeat 1h + push pe schimbare automată stare releu + pushNow.
        mqtt.publishStateIfNeeded(leftZone, rightZone);
    }

    // Timer rulează întotdeauna — logica autonomă nu depinde de WiFi.
    timer.run();

    // Actualizăm LED-ul cu starea reală.
    statusLed.updateStatus(wifiOk, Blynk.connected());

    // Buton fizic reset WiFiManager — ţinut apăsat 3 secunde.
    if (digitalRead(RESET_BUTTON_PIN) == LOW) {
        if (!buttonHeld) {
            buttonHeld    = true;
            buttonPressMs = millis();
            statusLed.setColor(180, 180, 0);
        } else if (millis() - buttonPressMs > 3000) {
            Serial.println("[Buton] Reset WiFiManager declanşat.");
            statusLed.setWhite();
            safeStopRelays();
            delay(500);
            WiFiManager wm;
            wm.resetSettings();
            ESP.restart();
        }
    } else {
        buttonHeld = false;
    }
}

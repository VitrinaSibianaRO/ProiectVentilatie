// ============================================================
//  ProiectVentilatie.ino
//
//  Arhitectură production-grade pentru automatizare ventilaţie:
//  - Logica releelor este 100% locală şi autonomă.
//  - Blynk este UI-only: trimite parametri, primeşte date.
//  - Parametrii sunt persistaţi în NVS (supravieţuiesc reboot).
//  - Sistemul funcţionează complet offline dacă Blynk pică.
//  - Override manual cu timeout automat (nu rămâne blocat).
//  - Watchdog hardware 60s împotriva oricărui blocaj.
// ============================================================

#include "Config.h"
#include "AppPreferences.h"
#include "SystemLED.h"
#include "VentilationZone.h"
#include "MqttBridge.h"

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

// Sincronizează starea curentă a releelor şi overrides spre Blynk.
// Apelată după orice schimbare locală, nu la comandă din cloud.
void pushRelayState() {
    if (!Blynk.connected()) return;
    Blynk.virtualWrite(VP_RELAY_LEFT,    leftZone.getRelayState()  ? 1 : 0);
    Blynk.virtualWrite(VP_RELAY_RIGHT,   rightZone.getRelayState() ? 1 : 0);
    Blynk.virtualWrite(VP_OVERRIDE_LEFT,  leftZone.getManualOverride()  ? 1 : 0);
    Blynk.virtualWrite(VP_OVERRIDE_RIGHT, rightZone.getManualOverride() ? 1 : 0);
}

// ============================================================
//  CICLU PRINCIPAL DE CITIRE + DECIZIE
//  Apelat periodic de BlynkTimer. Tot ce ţine de logica
//  releelor se întâmplă EXCLUSIV aici.
// ============================================================
void processZones() {
    Serial.println("\n--- Ciclu senzori ---");

    // 1. Aplică comenzile pending sosite din Blynk în ciclu anterior.
    //    Facem asta la începutul ciclului, nu în handler, pentru a evita
    //    orice interacţiune între ISR-ul Blynk şi digitalWrite.
    if (pending.resetDefaults) {
        pending.resetDefaults = false;
        prefs.resetToDefaults();
        leftZone.setManualOverride(false);
        rightZone.setManualOverride(false);
        timerRebuildNeeded = true;
        // Sincronizăm UI-ul Blynk cu noile valori default
        if (Blynk.connected()) {
            Blynk.virtualWrite(VP_THRESH_TEMP,     prefs.tempThresh);
            Blynk.virtualWrite(VP_THRESH_HUM,      prefs.humThresh);
            Blynk.virtualWrite(VP_INTERVAL,        prefs.intervalSec);
            Blynk.virtualWrite(VP_OVERRIDE_LEFT,   0);
            Blynk.virtualWrite(VP_OVERRIDE_RIGHT,  0);
            Blynk.virtualWrite(VP_RESET_DEFAULTS,  0);
        }
    }

    if (pending.overrideLeftClear) {
        pending.overrideLeftClear = false;
        prefs.saveOverrideLeft(false);
        leftZone.setManualOverride(false);
    } else if (pending.overrideLeftSet) {
        pending.overrideLeftSet = false;
        prefs.saveOverrideLeft(pending.overrideLeftVal);
        leftZone.setManualOverride(pending.overrideLeftVal);
    }

    if (pending.overrideRightClear) {
        pending.overrideRightClear = false;
        prefs.saveOverrideRight(false);
        rightZone.setManualOverride(false);
    } else if (pending.overrideRightSet) {
        pending.overrideRightSet = false;
        prefs.saveOverrideRight(pending.overrideRightVal);
        rightZone.setManualOverride(pending.overrideRightVal);
    }

    // 2. Verifică timeout overrides (expiră după N ore).
    bool overrideExpired = prefs.tickOverrideExpiry();
    if (overrideExpired) {
        leftZone.setManualOverride(prefs.overrideLeft);
        rightZone.setManualOverride(prefs.overrideRight);
        if (Blynk.connected()) {
            Blynk.virtualWrite(VP_OVERRIDE_LEFT,  0);
            Blynk.virtualWrite(VP_OVERRIDE_RIGHT, 0);
            Blynk.logEvent("override_expired", "Override manual a expirat automat.");
        }
    }

    // 3. Citeşte senzorii (cooldown intern în VentilationZone).
    leftZone.readSensor();
    rightZone.readSensor();

    // 4. Aplică logica de decizie locală — SINGURA sursă de adevăr.
    leftZone.updateLogic(prefs.tempThresh, prefs.humThresh);
    rightZone.updateLogic(prefs.tempThresh, prefs.humThresh);

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

    // Alertă erori consecutive senzor
    if (leftZone.getConsecErrors() >= 5 && Blynk.connected())
        Blynk.logEvent("sensor_error", "Senzor STANGA: 5+ erori consecutive!");
    if (rightZone.getConsecErrors() >= 5 && Blynk.connected())
        Blynk.logEvent("sensor_error", "Senzor DREAPTA: 5+ erori consecutive!");
}

void checkMemory() {
    uint32_t heap = ESP.getFreeHeap();
    Serial.printf("[Sistem] Heap liber: %u bytes\n", heap);
    if (Blynk.connected())
        Blynk.virtualWrite(VP_FREE_HEAP, heap / 1024);
    if (heap < 30000) {
        Serial.println("[CRITIC] Heap critic! Restart preventiv...");
        safeStopRelays();
        delay(500);
        ESP.restart();
    }
}

// ============================================================
//  BLYNK HANDLERS
//  REGULĂ: handler-ii NU scriu direct în releu şi NU apelează
//  digitalWrite. Setează doar flags/valori; processZones() decide.
// ============================================================

BLYNK_CONNECTED() {
    // Sincronizăm DOAR parametrii de configurare din cloud.
    // V5, V6 (starea releelor) NU sunt sincronizate — ESP32 le impune.
    Blynk.syncVirtual(VP_THRESH_TEMP, VP_THRESH_HUM, VP_INTERVAL, VP_RESET_DEFAULTS);
    // Trimitem imediat starea curentă a releelor spre UI
    pushRelayState();
    Serial.println("[Blynk] Conectat. Parametri sincronizaţi.");
}

BLYNK_WRITE(VP_THRESH_TEMP) {
    float v = param.asFloat();
    if (v > 0 && v < 80.0f) {
        prefs.saveTempThresh(v);
        Serial.printf("[Blynk] Prag temperatură: %.1f°C\n", v);
    }
}

BLYNK_WRITE(VP_THRESH_HUM) {
    float v = param.asFloat();
    if (v >= 0 && v <= 100.0f) {
        prefs.saveHumThresh(v);
        Serial.printf("[Blynk] Prag umiditate: %.1f%%\n", v);
    }
}

BLYNK_WRITE(VP_INTERVAL) {
    int v = param.asInt();
    v = constrain(v, MIN_INTERVAL_SEC, MAX_INTERVAL_SEC);
    prefs.saveIntervalSec(v);
    // Timer-ul va fi refăcut în loop() — nu din handler Blynk.
    timerRebuildNeeded = true;
    Serial.printf("[Blynk] Interval solicitat: %d sec\n", v);
}

// Override stânga: 1 = forţat ON, 0 = forţat OFF, 2 = clear (revenire la auto)
BLYNK_WRITE(VP_OVERRIDE_LEFT) {
    int v = param.asInt();
    if (v == 2) {
        pending.overrideLeftClear = true;
    } else {
        pending.overrideLeftSet = true;
        pending.overrideLeftVal = (v == 1);
    }
}

// Override dreapta: aceeaşi convenţie
BLYNK_WRITE(VP_OVERRIDE_RIGHT) {
    int v = param.asInt();
    if (v == 2) {
        pending.overrideRightClear = true;
    } else {
        pending.overrideRightSet = true;
        pending.overrideRightVal = (v == 1);
    }
}

BLYNK_WRITE(VP_RESET_DEFAULTS) {
    if (param.asInt() == 1)
        pending.resetDefaults = true;
}

BLYNK_WRITE(VP_RESTART) {
    if (param.asInt() == 1) {
        Serial.println("[Blynk] Comandă restart primită.");
        if (Blynk.connected())
            Blynk.logEvent("system_restart", "Restart la cererea utilizatorului.");
        statusLed.setBlue();
        safeStopRelays();
        delay(2000);
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
    if (!wm.autoConnect("ESP32_Ventilatie")) {
        Serial.println("[WiFi] Timeout portal. Restart...");
        safeStopRelays();
        delay(1000);
        ESP.restart();
    }
    Serial.printf("[WiFi] Conectat: %s  IP: %s\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

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
        if (millis() - lastWiFiCheckMs > 30000) {
            lastWiFiCheckMs = millis();
            Serial.println("[WiFi] Conexiune pierdută. Reconectare...");
            WiFi.reconnect();
        }
    } else {
        Blynk.run();
        // MQTT pump — non-blocking, gestionează reconnect cu backoff.
        mqtt.loop();
        // Heartbeat 1h + push pe schimbare automată stare releu.
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

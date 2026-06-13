// SharedState.h — Date Slave partajate intre Core 0 (SlaveCommTask) si Core 1 (loopTask).
// Acces protejat cu mutex FreeRTOS (timeout 10ms).
// Comenzi catre Slave transmise prin QueueHandle (Core 1 → Core 0).
//
// REGULA STRICTA thread-safety:
//   Core 0 (SlaveCommTask): detine SlaveUartClient, Serial2, fetchuri UART
//   Core 1 (loopTask):      detine SSLClient, PubSubClient, Wire, Preferences
//   Schimb de date: g_slaveData (cu mutex) + g_slaveCommandQueue (lockless queue)
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>

// ============================================================
//  Date Slave — citite de Core 1 (processZones, publishState)
// ============================================================
struct SlaveData {
    float    temp;
    float    hum;
    uint32_t ts;                   // epoch sec de la Slave (via TIME_SYNC)
    bool     fetchOk;              // ultima incercare fetch a reusit
    int      consecutiveErrors;
    unsigned long lastSuccessMs;   // millis() al ultimului fetch reusit

    // LED status (actualizat din LED_STATUS reply)
    uint8_t  ledIntensity;
    bool     ledSchedEnabled;
    uint8_t  ledOnH, ledOnM, ledOffH, ledOffM, ledMaxI;
    bool     ledScheduleFetched;   // true dupa primul LED_STATUS reusit
};

// ============================================================
//  Date TV — scrise de TvCommTask (Core 0), citite de loopTask (Core 1)
// ============================================================
struct TvData {
    bool     reachable;
    bool     power;
    uint8_t  backlight;      // 0-100 — sursa pentru LED cap
    uint32_t lastPollMs;     // millis() ultimul poll reuşit
    bool     newValueReady;  // flag setat de TvCommTask, consumat de loopTask
};

// ============================================================
//  CONTROL ↔ NETWORK (split pe core-uri)
//  taskControl (loopTask, Core 1) detine config + zone + GPIO relee.
//  taskNetwork (Core 0) detine WiFi + MQTT.
//
//  Comenzile config/override merg network→control prin g_controlCommandQueue:
//  config ramane owned EXCLUSIV de control → fara mutex pe config, fara race.
//  Telemetria merge control→network prin g_controlState (mutex).
// ============================================================

// Comenzi network → control. TOATE mutatiile de config/NVS trec pe aici → control
// e owner-ul EXCLUSIV pe AppPreferences/LedConfigStorage/zone (fara race cross-core).
// LED-urile se persista pe control si se forwardeaza la Slave din control.
enum ControlCommandType : uint8_t {
    CTRL_SET_CONFIG,
    CTRL_SET_OVERRIDE_L,
    CTRL_SET_OVERRIDE_R,
    CTRL_RESET,
    CTRL_REFRESH,        // citire fortata senzori + republicare
    CTRL_LED_SET,
    CTRL_LED_SCHEDULE,
    CTRL_LED_MODE,
    CTRL_LED_FOLLOW_TV,
    CTRL_LED_MORSE
};

struct ControlCommand {
    ControlCommandType type;
    // CTRL_SET_CONFIG (valori <=0 / <0 = "nu actualiza", ca in MqttPending)
    float threshT, threshH;
    float hystT,   hystH;
    int   interval;
    // CTRL_SET_OVERRIDE_L / _R
    int   overrideVal;   // 0=auto, 1=force ON, 2=force OFF
    // CTRL_LED_SET
    uint8_t ledPercent;
    // CTRL_LED_SCHEDULE
    uint8_t ledOnH, ledOnM, ledOffH, ledOffM, ledMaxI;
    bool    ledSchedEn;
    // CTRL_LED_MODE
    uint8_t  ledModeId;
    uint16_t ledModeP1, ledModeP2, ledModeP3, ledModeP4;
    // CTRL_LED_FOLLOW_TV
    bool    followTvValue;
    // CTRL_LED_MORSE
    char    morseText[52];
};

// Snapshot telemetrie control → network (network publica DOAR de aici).
struct ControlState {
    float    leftTemp,  leftHum;
    bool     leftRelay, leftOverride;
    int      leftErrs;
    float    rightTemp,  rightHum;
    bool     rightRelay, rightOverride, rightFailsafe;
    int      rightErrs;
    bool     slaveOnline;
    int      slaveErrors;
    uint32_t slaveLastSeenSec;
    uint8_t  ledIntensity;
    bool     followTvBrightness;   // din config — network il citeste DOAR de aici
    char     morseText[52];
    uint32_t seq;                  // bumpat de control la fiecare evaluare/comanda
};

// Comenzi network → TvCommTask (tvCtrl owned exclusiv de TvCommTask → fara race)
enum TvCommandType : uint8_t { TV_CMD_ACTION, TV_CMD_CONFIG };

struct TvCommand {
    TvCommandType type;
    char    action[16];   // TV_CMD_ACTION: "power_on","volume","mute",...
    int     value;
    char    ip[16];       // TV_CMD_CONFIG
    uint8_t mac[6];
};

// ============================================================
//  Comenzi catre Slave — trimise din Core 1 prin queue
// ============================================================
enum SlaveCommandType : uint8_t {
    SLAVE_CMD_LED_SET,
    SLAVE_CMD_LED_SCHEDULE,
    SLAVE_CMD_LED_MODE,
    SLAVE_CMD_LED_FOLLOW_TV,   // setează _followTvEnabled pe Slave
    SLAVE_CMD_LED_TV_CAP,      // trimite backlight TV ca plafon
    SLAVE_CMD_LED_MORSE_TEXT,  // text Morse dinamic
    SLAVE_CMD_REBOOT,
    SLAVE_CMD_TIME_SYNC
};

struct SlaveCommand {
    SlaveCommandType type;

    // SLAVE_CMD_LED_SET
    uint8_t  ledPercent;

    // SLAVE_CMD_LED_SCHEDULE
    uint8_t  ledOnH, ledOnM, ledOffH, ledOffM, ledMaxI;
    bool     ledSchedEn;

    // SLAVE_CMD_LED_MODE
    uint8_t  ledModeId;
    uint16_t ledModeP1, ledModeP2, ledModeP3, ledModeP4;

    // SLAVE_CMD_TIME_SYNC
    uint32_t epochSec;

    // SLAVE_CMD_LED_FOLLOW_TV
    bool     followTvEnabled;

    // SLAVE_CMD_LED_TV_CAP
    uint8_t  tvCapPercent;

    // SLAVE_CMD_LED_MORSE_TEXT
    char     morseText[52];
};

// ============================================================
//  FreeRTOS handles — definite in ProiectVentilatie.ino
// ============================================================
extern SemaphoreHandle_t g_slaveDataMutex;
extern QueueHandle_t     g_slaveCommandQueue;
extern SlaveData*        g_slaveData;           // alocat in PSRAM

extern SemaphoreHandle_t g_tvDataMutex;
extern TvData*           g_tvData;              // alocat in PSRAM

// Control ↔ Network
extern QueueHandle_t     g_controlCommandQueue; // network → control
extern SemaphoreHandle_t g_controlStateMutex;
extern ControlState*     g_controlState;        // alocat in PSRAM
extern QueueHandle_t     g_tvCommandQueue;       // network → TvCommTask

// ============================================================
//  Thread-safe accessors
// ============================================================

// Copiaza snapshot al datelor Slave. Returneaza false la timeout mutex.
inline bool slaveDataRead(SlaveData& out) {
    if (!g_slaveDataMutex || !g_slaveData) return false;
    if (xSemaphoreTake(g_slaveDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    memcpy(&out, g_slaveData, sizeof(SlaveData));
    xSemaphoreGive(g_slaveDataMutex);
    return true;
}

// Scrie date noi Slave. Returneaza false la timeout mutex.
inline bool slaveDataWrite(const SlaveData& in) {
    if (!g_slaveDataMutex || !g_slaveData) return false;
    if (xSemaphoreTake(g_slaveDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    memcpy(g_slaveData, &in, sizeof(SlaveData));
    xSemaphoreGive(g_slaveDataMutex);
    return true;
}

// Trimite comanda catre SlaveCommTask (Core 0) prin queue.
// Non-blocking (timeout=0) — comanda pierduta daca queue plina.
inline bool slaveCommandSend(const SlaveCommand& cmd) {
    if (!g_slaveCommandQueue) return false;
    return xQueueSend(g_slaveCommandQueue, &cmd, 0) == pdTRUE;
}

// ============================================================
//  Thread-safe accessors pentru TvData (TvCommTask ↔ loopTask)
// ============================================================

inline bool tvDataRead(TvData& out) {
    if (!g_tvDataMutex || !g_tvData) return false;
    if (xSemaphoreTake(g_tvDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    memcpy(&out, g_tvData, sizeof(TvData));
    xSemaphoreGive(g_tvDataMutex);
    return true;
}

inline bool tvDataWrite(const TvData& in) {
    if (!g_tvDataMutex || !g_tvData) return false;
    if (xSemaphoreTake(g_tvDataMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    memcpy(g_tvData, &in, sizeof(TvData));
    xSemaphoreGive(g_tvDataMutex);
    return true;
}

// ============================================================
//  Accesori Control ↔ Network
// ============================================================

// Snapshot telemetrie (control scrie, network citeste).
inline bool controlStateRead(ControlState& out) {
    if (!g_controlStateMutex || !g_controlState) return false;
    if (xSemaphoreTake(g_controlStateMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    memcpy(&out, g_controlState, sizeof(ControlState));
    xSemaphoreGive(g_controlStateMutex);
    return true;
}

inline bool controlStateWrite(const ControlState& in) {
    if (!g_controlStateMutex || !g_controlState) return false;
    if (xSemaphoreTake(g_controlStateMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    memcpy(g_controlState, &in, sizeof(ControlState));
    xSemaphoreGive(g_controlStateMutex);
    return true;
}

// Comanda network → control. Timeout scurt — producatorul nu blocheaza la nesfarsit.
inline bool controlCommandSend(const ControlCommand& cmd) {
    if (!g_controlCommandQueue) return false;
    return xQueueSend(g_controlCommandQueue, &cmd, pdMS_TO_TICKS(50)) == pdTRUE;
}

// Comanda network → TvCommTask. Non-blocking (timeout 0).
inline bool tvCommandSend(const TvCommand& cmd) {
    if (!g_tvCommandQueue) return false;
    return xQueueSend(g_tvCommandQueue, &cmd, 0) == pdTRUE;
}

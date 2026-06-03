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

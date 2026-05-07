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
//  Comenzi catre Slave — trimise din Core 1 prin queue
// ============================================================
enum SlaveCommandType : uint8_t {
    SLAVE_CMD_LED_SET,
    SLAVE_CMD_LED_SCHEDULE,
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

    // SLAVE_CMD_TIME_SYNC
    uint32_t epochSec;
};

// ============================================================
//  FreeRTOS handles — definite in ProiectVentilatie.ino
// ============================================================
extern SemaphoreHandle_t g_slaveDataMutex;
extern QueueHandle_t     g_slaveCommandQueue;
extern SlaveData*        g_slaveData;           // alocat in PSRAM
extern volatile bool     g_otaInProgress;       // guard SlaveOtaProxy

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

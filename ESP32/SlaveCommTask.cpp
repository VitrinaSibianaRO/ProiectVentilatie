// SlaveCommTask.cpp — Implementare task FreeRTOS Core 0 pentru comunicare Slave.
#include "SlaveCommTask.h"
#include "LedConfigStorage.h"
#include "TimeSync.h"

namespace SlaveCommTask {

TaskHandle_t     _taskHandle   = nullptr;
SemaphoreHandle_t _forceReadSem = nullptr;

// Intervale orar (ms)
static constexpr uint32_t TIME_SYNC_INTERVAL_MS  = 3600000UL;  // 1h
static constexpr uint32_t LED_STATUS_INTERVAL_MS = 3600000UL;  // 1h

struct TaskParams {
    SlaveUartClient* client;
};

// ============================================================
//  Task function — ruleaza pe Core 0
// ============================================================
static void taskFn(void* pvParams) {
    auto* p = static_cast<TaskParams*>(pvParams);
    SlaveUartClient& client = *p->client;
    delete p;

    // Inregistrare WDT pentru Core 0
    esp_task_wdt_add(NULL);

    Serial.printf("[SlaveCommTask] Core %d started\n", xPortGetCoreID());

    TickType_t  xLastWakeTime     = xTaskGetTickCount();
    uint32_t    lastTimeSyncMs    = 0;
    uint32_t    lastLedStatusMs   = 0;

    // Snapshot local — actualizat la fiecare fetch
    SlaveData snap{};
    // Incepe cu valorile curente (pot fi din PSRAM deja initializat)
    slaveDataRead(snap);

    for (;;) {
        // Guard OTA: cat timp Master trimite firmware catre Slave, nu interfera cu Serial2.
        if (g_otaInProgress) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_task_wdt_reset();
            continue;
        }

        // --- Fetch senzor Slave ---
        float t = 0.0f, h = 0.0f;
        uint32_t slaveTs = 0;
        const bool ok = client.fetch(t, h, slaveTs);

        snap.fetchOk  = ok;
        if (ok) {
            snap.temp             = t;
            snap.hum              = h;
            snap.ts               = slaveTs;
            snap.consecutiveErrors = 0;
            snap.lastSuccessMs    = (unsigned long)millis();
        } else {
            snap.consecutiveErrors = client.getConsecutiveErrors();
        }
        slaveDataWrite(snap);

        // --- Drain comanda queue (Core 1 → Core 0) ---
        SlaveCommand cmd;
        while (xQueueReceive(g_slaveCommandQueue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case SLAVE_CMD_LED_SET:
                    client.sendLedSet(cmd.ledPercent);
                    break;
                case SLAVE_CMD_LED_SCHEDULE:
                    client.sendLedSchedule(cmd.ledOnH, cmd.ledOnM,
                                           cmd.ledOffH, cmd.ledOffM,
                                           cmd.ledMaxI, cmd.ledSchedEn);
                    break;
                case SLAVE_CMD_REBOOT:
                    client.sendReboot();
                    break;
                case SLAVE_CMD_TIME_SYNC:
                    client.sendTimeSync(cmd.epochSec);
                    break;
            }
        }

        // --- Time sync orar ---
        const uint32_t nowMs = (uint32_t)millis();
        if (nowMs - lastTimeSyncMs >= TIME_SYNC_INTERVAL_MS || lastTimeSyncMs == 0) {
            uint32_t epoch = TimeSync::getEpochSec();
            if (epoch > 1700000000UL) {
                client.sendTimeSync(epoch);
                lastTimeSyncMs = nowMs;
                Serial.printf("[SlaveCommTask] TIME_SYNC %u\n", epoch);
            }
        }

        // --- LED status fetch orar ---
        // Scriem in SharedState; Core 1 (processZones) face comparatia cu NVS.
        if (nowMs - lastLedStatusMs >= LED_STATUS_INTERVAL_MS || lastLedStatusMs == 0) {
            uint8_t intensity = 0;
            bool en = false;
            uint8_t oh = 0, om = 0, fh = 0, fm = 0, mi = 0;
            if (client.fetchLedStatus(intensity, en, oh, om, fh, fm, mi)) {
                slaveDataRead(snap);
                snap.ledIntensity       = intensity;
                snap.ledSchedEnabled    = en;
                snap.ledOnH = oh; snap.ledOnM = om;
                snap.ledOffH = fh; snap.ledOffM = fm;
                snap.ledMaxI = mi;
                snap.ledScheduleFetched = true;
                slaveDataWrite(snap);
            }
            lastLedStatusMs = nowMs;
        }

        esp_task_wdt_reset();

        // Asteapta SLAVE_COMM_CADENCE_MS sau forceRead anticipat
        xSemaphoreTake(_forceReadSem, pdMS_TO_TICKS(SLAVE_COMM_CADENCE_MS));
    }
}

// ============================================================
//  API public
// ============================================================

void start(SlaveUartClient& client) {
    _forceReadSem = xSemaphoreCreateBinary();

    auto* params = new TaskParams{&client};
    xTaskCreatePinnedToCore(
        taskFn,
        "SlaveCommTask",
        SLAVE_COMM_TASK_STACK,
        params,
        SLAVE_COMM_TASK_PRIORITY,
        &_taskHandle,
        0    // Core 0
    );
}

void forceRead() {
    if (_forceReadSem) xSemaphoreGive(_forceReadSem);
}

void suspend() {
    if (_taskHandle) vTaskSuspend(_taskHandle);
}

void resume() {
    if (_taskHandle) vTaskResume(_taskHandle);
}

}  // namespace SlaveCommTask

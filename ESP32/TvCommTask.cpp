// TvCommTask.cpp — Task FreeRTOS Core 0, prioritate joasa (TV_COMM_TASK_PRIORITY=1).
// pollAll() poate dura pana la ~11s (11 conexiuni TCP × 1s timeout) — pe Core 0
// nu afecteaza loopTask (Core 1: MQTT, WiFi, logica zone).
#include "TvCommTask.h"
#include "Config.h"
#include "SharedState.h"
#include <esp_task_wdt.h>

// Definita in ESP32.ino la scop global
extern bool g_wifiAvailable;

namespace TvCommTask {

static TaskHandle_t      _taskHandle   = nullptr;
static SemaphoreHandle_t _forcePollSem = nullptr;

struct TaskParams {
    TvController* tvCtrl;
};

static void taskFn(void* pvParams) {
    auto* p = static_cast<TaskParams*>(pvParams);
    TvController& tvCtrl = *p->tvCtrl;
    delete p;

    // Subscrie task-ul curent la WDT (configurat cu 60s in setup).
    esp_task_wdt_add(NULL);

    Serial.printf("[TvCommTask] Core %d started (prio=%d)\n",
                  xPortGetCoreID(), TV_COMM_TASK_PRIORITY);

    for (;;) {
        esp_task_wdt_reset();

        if (g_wifiAvailable && tvCtrl.state().configured) {
            Serial.println("[TvCommTask] pollAll() start");
            tvCtrl.pollAll();   // blocant ~11s — OK pe Core 0

            TvData snap{};
            snap.reachable     = tvCtrl.state().reachable;
            snap.power         = tvCtrl.state().power;
            snap.backlight     = tvCtrl.state().backlight;
            snap.lastPollMs    = (uint32_t)millis();
            snap.newValueReady = true;
            tvDataWrite(snap);

            Serial.printf("[TvCommTask] pollAll() done: reachable=%d bk=%u\n",
                          snap.reachable, snap.backlight);
        }

        // Asteapta TV_POLL_MS in felii de 30s, alimentand WDT (timeout = 60s).
        // Un singur xSemaphoreTake de 6min ar depasi WDT si ar provoca panic.
        {
            uint32_t remaining = TV_POLL_MS;
            while (remaining > 0) {
                const uint32_t slice = (remaining > 30000U) ? 30000U : remaining;
                if (xSemaphoreTake(_forcePollSem, pdMS_TO_TICKS(slice)) == pdTRUE) break;
                esp_task_wdt_reset();
                remaining -= slice;
            }
        }
    }
}

void start(TvController& tvCtrl) {
    _forcePollSem = xSemaphoreCreateBinary();

    auto* params = new TaskParams{&tvCtrl};
    xTaskCreatePinnedToCore(
        taskFn,
        "TvCommTask",
        TV_COMM_TASK_STACK,
        params,
        TV_COMM_TASK_PRIORITY,
        &_taskHandle,
        0   // Core 0 (impreuna cu SlaveCommTask)
    );
}

void forcePoll() {
    if (_forcePollSem) xSemaphoreGive(_forcePollSem);
}

}  // namespace TvCommTask

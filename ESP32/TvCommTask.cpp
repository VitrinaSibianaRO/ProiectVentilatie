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

// Executa o comanda TV venita de la taskNetwork. tvCtrl e atins DOAR de
// TvCommTask (poll + actiuni) → fara race pe socketul RS-232/TCP.
static void execTvCommand(TvController& tv, const TvCommand& c) {
    if (c.type == TV_CMD_CONFIG) {
        tv.configure(c.ip, c.mac);
        tv.readDeviceInfo();
        Serial.printf("[TvCommTask] Config updated: IP=%s\n", c.ip);
        return;
    }
    // TV_CMD_ACTION
    const char* a = c.action;
    const int   v = c.value;
    bool ok = false;
    if      (strcmp(a, "power_on")     == 0) ok = tv.powerOn();
    else if (strcmp(a, "power_off")    == 0) ok = tv.powerOff();
    else if (strcmp(a, "volume")       == 0) ok = tv.setVolume((uint8_t)v);
    else if (strcmp(a, "mute")         == 0) ok = tv.setMute(v != 0);
    else if (strcmp(a, "input")        == 0) ok = tv.setInput((uint8_t)v);
    else if (strcmp(a, "backlight")    == 0) ok = tv.setBacklight((uint8_t)v);
    else if (strcmp(a, "pictureMode")  == 0) ok = tv.setPictureMode((uint8_t)v);
    else if (strcmp(a, "energySaving") == 0) ok = tv.setEnergySaving((uint8_t)v);
    else if (strcmp(a, "noSignalOff")  == 0) ok = tv.setNoSignalPowerOff(v != 0);
    Serial.printf("[TvCommTask] Action '%s' val=%d -> %s\n", a, v, ok ? "OK" : "FAIL");
}

// Scrie snapshot TvData cu newValueReady → taskNetwork publica starea TV.
static void writeTvSnapshot(TvController& tv) {
    TvData snap{};
    snap.reachable     = tv.state().reachable;
    snap.power         = tv.state().power;
    snap.backlight     = tv.state().backlight;
    snap.lastPollMs    = (uint32_t)millis();
    snap.newValueReady = true;
    tvDataWrite(snap);
}

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

        // 1. Drain comenzi TV (network → TvCommTask). Executie + snapshot pentru publish.
        {
            TvCommand tc;
            bool gotTvCmd = false;
            while (g_tvCommandQueue &&
                   xQueueReceive(g_tvCommandQueue, &tc, 0) == pdTRUE) {
                execTvCommand(tvCtrl, tc);
                gotTvCmd = true;
            }
            if (gotTvCmd) writeTvSnapshot(tvCtrl);
        }

        // 2. Poll periodic
        if (g_wifiAvailable && tvCtrl.state().configured) {
            Serial.println("[TvCommTask] pollAll() start");
            tvCtrl.pollAll();   // blocant ~11s — OK pe Core 0
            writeTvSnapshot(tvCtrl);
            Serial.printf("[TvCommTask] pollAll() done: reachable=%d bk=%u\n",
                          tvCtrl.state().reachable, tvCtrl.state().backlight);
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

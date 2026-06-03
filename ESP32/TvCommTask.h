// TvCommTask.h — Task FreeRTOS Core 0 pentru polling TV LG 75XS4P.
// Apeleaza TvController::pollAll() la fiecare TV_POLL_MS (6 min).
// Scrie rezultatul in TvData (SharedState) via g_tvDataMutex — NU publica MQTT.
// MQTT publish e delegat loopTask-ului (Core 1) prin flag newValueReady,
// deoarece PubSubClient nu este thread-safe.
#pragma once

#include "TvController.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace TvCommTask {

    void start(TvController& tvCtrl);
    void forcePoll();   // trezeste task-ul imediat (poll out-of-band)

}  // namespace TvCommTask

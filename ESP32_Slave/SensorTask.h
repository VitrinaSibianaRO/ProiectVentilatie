// SensorTask.h — Task FreeRTOS pe Core 0 pentru citire periodica SHT30.
// Core 1 (loopTask) citeste rezultatele din SharedSensorData fara blocare.
//
// Avantaj: GET_SENSOR raspunde in <1ms (fata de 0-80ms blocant anterior).
// I2C Wire e folosit EXCLUSIV de acest task pe Core 0 — nu e thread-safe.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "Sht30Sensor.h"
#include "SharedSensorData.h"
#include "Logger.h"
#include "I2CRecovery.h"

// Binary semaphore pentru trezire anticipata (forceRead din UART handler).
extern SemaphoreHandle_t g_forceReadSem;

namespace SensorTask {

// Porneste task-ul pe Core 0. Apelat din setup() dupa Wire.begin() si sensor.begin().
void start(Sht30Sensor& sensor);

// Trezire anticipata — urmatoarea iteratie a task-ului ruleaza imediat.
// Apelat din Core 1 (loopTask) — safe, semaphore e thread-safe.
inline void forceRead() {
    if (g_forceReadSem) xSemaphoreGive(g_forceReadSem);
}

namespace _internal {

struct TaskParams {
    Sht30Sensor* sensor;
};

inline void taskFn(void* pvParams) {
    auto* p = static_cast<TaskParams*>(pvParams);
    Sht30Sensor& sensor = *p->sensor;
    delete p;   // eliberam params alocati in start()

    // Inregistrare WDT pentru acest task pe Core 0.
    esp_task_wdt_add(NULL);
    LOG_INFO("[SensorTask] Core %d started", xPortGetCoreID());

    SharedSensorData snap{};

    for (;;) {
        // Asteapta SENSOR_READ_PERIOD_MS sau trezire anticipata.
        xSemaphoreTake(g_forceReadSem, pdMS_TO_TICKS(SENSOR_READ_PERIOD_MS));

        float t = 0.0f, h = 0.0f;
        bool ok = sensor.read(t, h, /*force=*/true);

        snap.valid = ok;
        if (ok) {
            snap.temp             = t;
            snap.hum              = h;
            snap.lastReadMs       = (uint32_t)millis();
            snap.consecutiveErrors = 0;
        } else {
            snap.consecutiveErrors++;
            // I2C bus recovery la fiecare 3 erori consecutive.
            if (snap.consecutiveErrors % 3 == 0) {
                LOG_WARN("[SensorTask] I2C recovery (errors=%d)", snap.consecutiveErrors);
                I2CRecovery::recoverBus(I2C_SDA_PIN, I2C_SCL_PIN);
                // Reinitializeaza senzorul dupa recovery.
                sensor.begin(SHT30_ADDR);
            }
        }

        sensorDataWrite(snap);

        if (ok) {
            LOG_DEBUG("[SensorTask] T=%.2f H=%.2f", t, h);
        } else {
            LOG_WARN("[SensorTask] Read fail (errors=%d)", snap.consecutiveErrors);
        }

        esp_task_wdt_reset();
    }
}

}  // namespace _internal

inline void start(Sht30Sensor& sensor) {
    // Semaphore binar pentru forceRead — initial luat (task incepe cu sleep).
    g_forceReadSem = xSemaphoreCreateBinary();

    auto* params = new _internal::TaskParams{&sensor};
    xTaskCreatePinnedToCore(
        _internal::taskFn,
        "SensorTask",
        SENSOR_TASK_STACK,
        params,
        SENSOR_TASK_PRIORITY,
        nullptr,
        0    // Core 0
    );
}

}  // namespace SensorTask

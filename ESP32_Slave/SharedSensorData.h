// SharedSensorData.h — Date senzor partajate intre Core 0 (SensorTask) si Core 1 (loopTask).
// Acces protejat cu mutex FreeRTOS (timeout 5ms).
// g_sensorData e alocat in PSRAM in ESP32_Slave.ino::setup().
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

struct SharedSensorData {
    float    temp;
    float    hum;
    bool     valid;               // true daca ultima citire a reusit
    int      consecutiveErrors;
    uint32_t lastReadMs;          // millis() la ultima citire reusita
};

// Definite in ESP32_Slave.ino
extern SemaphoreHandle_t g_sensorMutex;
extern SharedSensorData* g_sensorData;   // alocat in PSRAM

// Thread-safe read — copiaza snapshot al datelor senzorului.
// Returneaza false daca mutex nu a putut fi obtinut in 5ms.
inline bool sensorDataRead(SharedSensorData& out) {
    if (!g_sensorMutex || !g_sensorData) return false;
    if (xSemaphoreTake(g_sensorMutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    memcpy(&out, g_sensorData, sizeof(SharedSensorData));
    xSemaphoreGive(g_sensorMutex);
    return true;
}

// Thread-safe write — actualizeaza datele partajate.
// Returneaza false daca mutex nu a putut fi obtinut in 5ms.
inline bool sensorDataWrite(const SharedSensorData& in) {
    if (!g_sensorMutex || !g_sensorData) return false;
    if (xSemaphoreTake(g_sensorMutex, pdMS_TO_TICKS(5)) != pdTRUE) return false;
    memcpy(g_sensorData, &in, sizeof(SharedSensorData));
    xSemaphoreGive(g_sensorMutex);
    return true;
}

// SlaveCommTask.h — Task FreeRTOS pe Core 0 care detine exclusiv SlaveUartClient.
// Core 1 (loopTask) citeste rezultatele din SharedState si trimite comenzi prin queue.
//
// Responsabilitati exclusive Core 0:
//   - fetch() senzor Slave (GET_SENSOR) la fiecare 500ms
//   - drain comanda queue (LED_SET, LED_SCHEDULE, REBOOT, TIME_SYNC)
//   - fetchLedStatus() orar
//   - sendTimeSync() orar
//
// NU atinge niciodata: SSLClient, PubSubClient, Preferences, Wire, GPIO.
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "SlaveUartClient.h"
#include "SharedState.h"

namespace SlaveCommTask {

// Porneste task-ul pe Core 0. Apelat din setup() dupa Serial2.begin().
void start(SlaveUartClient& client);

// Trezire anticipata — urmatorul fetch ruleaza imediat (fara asteptare 500ms).
void forceRead();

void suspend();
void resume();

// Handle task (pentru suspend/resume extern).
extern TaskHandle_t _taskHandle;

// Binary semaphore pentru forceRead.
extern SemaphoreHandle_t _forceReadSem;

}  // namespace SlaveCommTask

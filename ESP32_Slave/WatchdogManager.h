// WatchdogManager.h — Wrapper esp_task_wdt pentru watchdog hardware.
// Resetează automat ESP32 dacă feed() nu este apelat in timeoutSec secunde.
#pragma once
#include <esp_task_wdt.h>
#include <esp_err.h>

class WatchdogManager {
public:
    // Apelat o singura data in setup(). panic=true → reset la timeout (nu doar log).
    // esp-idf v4.x (arduino core 2.x): esp_task_wdt_init ia timeout_sec + panic direct.
    static void begin(uint32_t timeoutSec, bool panic = true) {
        esp_task_wdt_init(timeoutSec, panic);
        esp_task_wdt_add(nullptr);   // subscrie task-ul curent (loopTask)
    }

    // Apelat la inceputul fiecarei iteratii loop(). Resetează timer-ul WDT.
    static void feed() {
        esp_task_wdt_reset();
    }

    // Apelat optional la sfarsitul unui task lung (ex. OTA chunk writing).
    static void feedNow() {
        esp_task_wdt_reset();
    }
};

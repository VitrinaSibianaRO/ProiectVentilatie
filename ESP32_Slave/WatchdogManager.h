// WatchdogManager.h — Wrapper esp_task_wdt pentru watchdog hardware.
// Resetează automat ESP32 dacă feed() nu este apelat in timeoutSec secunde.
#pragma once
#include <esp_task_wdt.h>
#include <esp_err.h>

class WatchdogManager {
public:
    // Apelat o singura data in setup(). panic=true → reset la timeout (nu doar log).
    // esp-idf v5.x (arduino core 3.x): esp_task_wdt_init ia o structura de config.
    static void begin(uint32_t timeoutSec, bool panic = true) {
        esp_task_wdt_config_t wdtConfig = {
            .timeout_ms = timeoutSec * 1000,
            .idle_core_mask = 0,
            .trigger_panic = panic
        };
        
        esp_err_t err = esp_task_wdt_reconfigure(&wdtConfig);
        if (err == ESP_ERR_INVALID_STATE) {
            esp_task_wdt_init(&wdtConfig);
        }
        
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

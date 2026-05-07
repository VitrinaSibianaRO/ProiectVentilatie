// SlaveResilience.h — Hardening mechanisms for Slave module.
#pragma once

#include <Arduino.h>
#include <esp_system.h>

class SlaveResilience {
public:
    static constexpr uint32_t IDLE_RESTART_MS   = 1800000UL;   // 30 min fara comenzi Master
    static constexpr uint32_t UPTIME_RESTART_MS = 604800000UL; // 7 zile (weekly restart)
    static constexpr uint32_t CRITICAL_HEAP     = 20000;       // 20KB

    static void check(unsigned long lastMasterCmdMs) {
        unsigned long now = millis();

        // 1. Idle watchdog (no comm from Master)
        if (now - lastMasterCmdMs > IDLE_RESTART_MS) {
            Serial.println("[Resilience] Master connection lost >30m. Restarting...");
            delay(100);
            ESP.restart();
        }

        // 2. Weekly preventive restart
        if (now > UPTIME_RESTART_MS) {
            Serial.println("[Resilience] Weekly uptime reached. Preventive restart...");
            delay(100);
            ESP.restart();
        }

        // 3. Heap monitor
        if (ESP.getFreeHeap() < CRITICAL_HEAP) {
            Serial.printf("[Resilience] CRITICAL HEAP (%u) - Restarting\n", ESP.getFreeHeap());
            delay(100);
            ESP.restart();
        }
    }
};

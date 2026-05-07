// SlaveResilience.h — Hardening mechanisms for Slave module.
// Source-of-truth pentru thresholds: Config.h (NU duplica aici).
#pragma once

#include <Arduino.h>
#include <esp_system.h>
#include "Config.h"

class SlaveResilience {
public:
    // 7 zile uptime → restart preventiv (Master are NTP, Slave doar uptime).
    static constexpr uint32_t UPTIME_RESTART_MS = 604800000UL;
    // Heap critic — restart inainte de exhaustion completa.
    static constexpr uint32_t CRITICAL_HEAP     = 20000;

    /**
     * @param lastMasterCmdMs millis() ultim comanda Master, 0 daca nicio comanda.
     */
    static void check(unsigned long lastMasterCmdMs) {
        unsigned long now = millis();

        // 1. Idle watchdog (no comm from Master)
        // GUARD: skip cat timp Master nu a contactat inca (cold boot Slave inainte de Master).
        if (lastMasterCmdMs != 0 &&
            (now - lastMasterCmdMs) > SELF_RESTART_IDLE_MS) {
            Serial.println("[Resilience] Master idle >30m, restart");
            delay(100);
            ESP.restart();
        }

        // 2. Weekly preventive restart
        if (now > UPTIME_RESTART_MS) {
            Serial.println("[Resilience] Weekly uptime reached, preventive restart");
            delay(100);
            ESP.restart();
        }

        // 3. Heap monitor
        const uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < CRITICAL_HEAP) {
            Serial.printf("[Resilience] CRITICAL HEAP (%u) - restart\n", freeHeap);
            delay(100);
            ESP.restart();
        }
    }
};

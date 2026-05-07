#pragma once
#include <Arduino.h>
#include <Ethernet.h>
#include "VentilationZone.h"
#include "SharedState.h"

extern bool g_ethAvailable;
extern VentilationZone leftZone;
extern SlaveData* g_slaveData;
extern SemaphoreHandle_t g_slaveDataMutex;

namespace DiagnosticLogger {

    inline void printPeriodicLog() {
        unsigned long now = millis();
        static unsigned long lastDiagLogMs = 0;
        
        if (now - lastDiagLogMs < 10000UL) return;
        lastDiagLogMs = now;
        
        Serial.println("\n--- [DIAGNOSTIC STATUS] ---");
        
        // Status Retea
        Serial.printf("Network (W5500): %s\n", g_ethAvailable ? "OK" : "NOT FOUND / OFFLINE");
        if (g_ethAvailable) {
            Serial.print("IP Address: ");
            Serial.println(Ethernet.localIP());
        }

        // Status Senzori Master (Stanga)
        Serial.printf("Senzor STANGA (Master): T=%.1f°C, H=%.1f%% [%s]\n", 
                      leftZone.getTemp(), leftZone.getHum(), 
                      (leftZone.getConsecErrors() > 0) ? "ERROR" : "OK");

        // Status Senzori Slave (Dreapta)
        float slaveT = 0.0f;
        float slaveH = 0.0f;
        bool slaveErr = true;
        bool slaveOnline = false;

        if (g_slaveDataMutex != NULL && g_slaveData != nullptr) {
            xSemaphoreTake(g_slaveDataMutex, portMAX_DELAY);
            slaveT = g_slaveData->temp;
            slaveH = g_slaveData->hum;
            slaveErr = (g_slaveData->consecutiveErrors > 0);
            slaveOnline = (now - g_slaveData->lastSuccessMs < 30000); 
            xSemaphoreGive(g_slaveDataMutex);
        }

        Serial.printf("Senzor DREAPTA (Slave): T=%.1f°C, H=%.1f%% [%s, UART=%s]\n", 
                      slaveT, slaveH, 
                      slaveErr ? "ERROR" : "OK",
                      slaveOnline ? "OK" : "OFFLINE/TIMEOUT");
                      
        Serial.println("---------------------------\n");
    }
}

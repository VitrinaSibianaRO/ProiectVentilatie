#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "SharedState.h"

extern bool g_wifiAvailable;

// Logging diagnostic periodic — ruleaza pe taskNetwork (Core 0).
// Citeste DOAR din snapshot-ul de control (g_controlState, mutex) — nu atinge
// niciodata zone vii (owned de taskControl pe Core 1), deci fara race cross-task.
namespace DiagnosticLogger {

    inline void printPeriodicLog() {
        unsigned long now = millis();
        static unsigned long lastDiagLogMs = 0;

        if (now - lastDiagLogMs < 10000UL) return;
        lastDiagLogMs = now;

        ControlState st{};
        const bool have = controlStateRead(st);

        Serial.println("\n--- [DIAGNOSTIC STATUS] ---");

        Serial.printf("Network (WiFi): %s\n", g_wifiAvailable ? "OK" : "OFFLINE");
        if (g_wifiAvailable) {
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
        }

        if (have) {
            Serial.printf("Senzor STANGA (Master): T=%.1f°C, H=%.1f%% [%s]\n",
                          st.leftTemp, st.leftHum,
                          (st.leftErrs > 0) ? "ERROR" : "OK");
            Serial.printf("Senzor DREAPTA (Slave): T=%.1f°C, H=%.1f%% [%s, UART=%s]\n",
                          st.rightTemp, st.rightHum,
                          (st.rightErrs > 0) ? "ERROR" : "OK",
                          st.slaveOnline ? "OK" : "OFFLINE/TIMEOUT");
        }

        Serial.println("---------------------------\n");
    }
}

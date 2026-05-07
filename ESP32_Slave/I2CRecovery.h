// I2CRecovery.h — Deblocheaza bus I2C prin 9 cicluri manuale SCL.
// Folosit cand un slave tine SDA jos (clock-stretch neconformat sau hang).
#pragma once
#include <Arduino.h>
#include <Wire.h>

class I2CRecovery {
public:
    static bool recoverBus(uint8_t sdaPin, uint8_t sclPin) {
        Wire.end();
        pinMode(sclPin, OUTPUT_OPEN_DRAIN);
        pinMode(sdaPin, INPUT_PULLUP);
        for (int i = 0; i < 9; i++) {
            digitalWrite(sclPin, LOW);  delayMicroseconds(5);
            digitalWrite(sclPin, HIGH); delayMicroseconds(5);
            if (digitalRead(sdaPin) == HIGH) break;
        }
        Wire.begin(sdaPin, sclPin);
        Wire.setClock(I2C_FREQ_HZ);
        return true;
    }
};

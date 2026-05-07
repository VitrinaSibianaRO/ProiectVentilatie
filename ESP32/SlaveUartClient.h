// SlaveUartClient.h — Client UART Master-side pentru comunicare cu Slave.
// Trimite comenzi cu CRC-16/Modbus, primeste raspunsuri cu CRC validate.
// Retry intern, timeout configurabil, error tracking.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

class SlaveUartClient {
public:
    SlaveUartClient();

    void begin(HardwareSerial& serial);

    // Cerere senzor de la Slave. Returneaza false la timeout/JSON invalid/CRC fail.
    bool fetch(float& temp, float& hum, uint32_t& slaveTs,
               uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);

    // Cerere reboot Slave. Returneaza true daca Slave a confirmat (OK).
    bool sendReboot(uint32_t timeoutMs = SLAVE_REBOOT_TIMEOUT_MS);

    // Diagnostic — masoara latency UART round-trip (PING/PONG).
    bool ping(uint32_t& latencyMs, uint32_t timeoutMs = 500);

    // LED control — forward to Slave
    bool sendLedSet(uint8_t percent, uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);
    bool sendLedSchedule(uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                         uint8_t maxI, bool enabled, uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);
    bool fetchLedStatus(uint8_t& intensity, bool& schedEnabled,
                        uint8_t& onH, uint8_t& onM, uint8_t& offH, uint8_t& offM, uint8_t& maxI,
                        uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);

    // Time sync — trimite epoch Slave-ului
    bool sendTimeSync(uint32_t epochSec, uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);

    // Numar de erori consecutive dupa ultima cerere reusita.
    int  getConsecutiveErrors() const { return _consecutiveErrors; }
    unsigned long getLastSuccessMs() const { return _lastSuccessMs; }

private:
    HardwareSerial* _serial;
    int             _consecutiveErrors;
    unsigned long   _lastSuccessMs;

    // Goleste buffer-ul UART (defensive — eventuale resturi de la transmisii anterioare).
    void _flushInput();

    // Citeste o linie completa cu timeout. Returneaza String gol la timeout.
    String _readLine(uint32_t timeoutMs);

    // Trimite o comanda text cu CRC atasat.
    void _sendCmd(const char* cmd);
};

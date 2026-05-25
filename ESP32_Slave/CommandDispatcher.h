// CommandDispatcher.h — Mapeaza comenzi UART → actiuni Slave.
// Dependency injection prin constructor. Non-copyable.
// Toate dependintele sunt referinte (non-owning) — caller detine resursele.
#pragma once
#include <ArduinoJson.h>
#include "Config.h"
#include "Sht30Sensor.h"
#include "UartProtocol.h"
#include "SystemLED.h"
#include "LedController.h"
#include "Logger.h"

class CommandDispatcher {
public:
    CommandDispatcher(Sht30Sensor& sensor,
                      UartProtocol& uart,
                      SystemLED& led,
                      LedController& ledCtrl)
        : _sensor(sensor), _uart(uart), _led(led),
          _ledCtrl(ledCtrl),
          _bootMs(millis()), _lastRequestMs(0) {}

    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

    // Apelat la fiecare iteratie loop(). Non-blocking.
    void tick();

    uint32_t getLastRequestMs() const { return _lastRequestMs; }

private:
    Sht30Sensor&   _sensor;
    UartProtocol&  _uart;
    SystemLED&     _led;
    LedController& _ledCtrl;
    uint32_t       _bootMs;
    uint32_t       _lastRequestMs;

    // Handlere comenzi standard
    void _handleGetSensor();
    void _handleReboot();
    void _handlePing();

    // Handlere LED
    void _handleLedSet(const char* args);
    void _handleLedSchedule(const char* args);
    void _handleLedStatus();
    void _handleLedMode(const char* args);

    // Time sync
    void _handleTimeSync(const char* args);

    void _handleUnknown(const char* cmd);
    void _updateIdleStatus();
};

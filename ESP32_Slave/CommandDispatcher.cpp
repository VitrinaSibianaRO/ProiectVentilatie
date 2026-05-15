// CommandDispatcher.cpp — Implementare handlere comenzi UART Slave.
#include "CommandDispatcher.h"
#include "SharedSensorData.h"
#include "SensorTask.h"

void CommandDispatcher::tick() {
    char cmd[UART_BUFFER_SIZE];

    if (!_uart.poll(cmd, sizeof(cmd))) {
        _updateIdleStatus();
        return;
    }

    _lastRequestMs = millis();
    LOG_DEBUG("RX: %s", cmd);

    // Routing comenzi (strcmp / strncmp pentru prefix-uri)
    if (strcmp(cmd, "GET_SENSOR") == 0)              _handleGetSensor();
    else if (strcmp(cmd, "REBOOT") == 0)              _handleReboot();
    else if (strcmp(cmd, "PING") == 0)                _handlePing();
    else if (strncmp(cmd, "LED_SET ", 8) == 0)       _handleLedSet(cmd + 8);
    else if (strncmp(cmd, "LED_SCHEDULE ", 13) == 0) _handleLedSchedule(cmd + 13);
    else if (strcmp(cmd, "LED_STATUS") == 0)          _handleLedStatus();
    else if (strncmp(cmd, "TIME_SYNC ", 10) == 0)    _handleTimeSync(cmd + 10);
    else                                               _handleUnknown(cmd);

    _updateIdleStatus();
}

// ============================================================
//  GET_SENSOR
// ============================================================
void CommandDispatcher::_handleGetSensor() {
    // Citire non-blocanta din SharedSensorData (actualizata de SensorTask pe Core 0).
    // Timp raspuns: <1ms (vs 0-80ms blocant anterior pe I2C).
    SharedSensorData snap{};
    const bool snapOk = sensorDataRead(snap);
    const bool ok = snapOk && snap.valid;

    const float t = ok ? snap.temp : 0.0f;
    const float h = ok ? snap.hum  : 0.0f;

    JsonDocument doc;
    doc["temp"]   = t;
    doc["hum"]    = h;
    doc["ts"]     = ok ? (snap.lastReadMs / 1000UL) : 0UL;
    doc["ok"]     = ok;
    doc["errors"] = snapOk ? snap.consecutiveErrors : -1;
    doc["uptime"] = (millis() - _bootMs) / 1000UL;

    _uart.sendJson(doc);
    _led.setStatus(ok ? SystemLED::Status::Active
                       : SystemLED::Status::SensorFail);
}

// ============================================================
//  REBOOT
// ============================================================
void CommandDispatcher::_handleReboot() {
    LOG_INFO("REBOOT requested");
    _uart.sendLine("OK");
    delay(100);
    ESP.restart();
}

// ============================================================
//  PING
// ============================================================
void CommandDispatcher::_handlePing() {
    _uart.sendLine("PONG");
}

// ============================================================
//  LED_SET <0-100>
// ============================================================
void CommandDispatcher::_handleLedSet(const char* args) {
    int percent = atoi(args);
    if (percent < 0 || percent > 100) {
        LOG_WARN("LED_SET out of range: %s", args);
        _uart.sendLine("ERR_RANGE");
        return;
    }
    _ledCtrl.setIntensity((uint8_t)percent);
    _uart.sendLine("OK");
}

// ============================================================
//  LED_SCHEDULE <onH> <onM> <offH> <offM> <maxInt> <enabled>
// ============================================================
void CommandDispatcher::_handleLedSchedule(const char* args) {
    int onH, onM, offH, offM, maxInt, en;
    if (sscanf(args, "%d %d %d %d %d %d", &onH, &onM, &offH, &offM, &maxInt, &en) != 6) {
        LOG_WARN("LED_SCHEDULE parse fail: %s", args);
        _uart.sendLine("ERR_PARSE");
        return;
    }
    _ledCtrl.setSchedule((uint8_t)onH, (uint8_t)onM,
                          (uint8_t)offH, (uint8_t)offM,
                          (uint8_t)maxInt, en != 0);
    _uart.sendLine("OK");
}

// ============================================================
//  LED_STATUS
// ============================================================
void CommandDispatcher::_handleLedStatus() {
    const auto sched = _ledCtrl.getSchedule();

    JsonDocument doc;
    doc["intensity"] = _ledCtrl.getCurrentIntensity();
    doc["enabled"]   = sched.enabled;
    JsonObject s = doc["sched"].to<JsonObject>();
    s["onH"]  = sched.onHour;
    s["onM"]  = sched.onMin;
    s["offH"] = sched.offHour;
    s["offM"] = sched.offMin;
    s["maxI"] = sched.maxIntensity;

    _uart.sendJson(doc);
}

// ============================================================
//  TIME_SYNC <epoch>
// ============================================================
void CommandDispatcher::_handleTimeSync(const char* args) {
    uint32_t epoch = (uint32_t)strtoul(args, nullptr, 10);
    _ledCtrl.syncTime(epoch);
    _uart.sendLine("OK");
}

// ============================================================
//  UNKNOWN COMMAND
// ============================================================
void CommandDispatcher::_handleUnknown(const char* cmd) {
    LOG_WARN("Unknown cmd: %s", cmd);
    _uart.sendLine("ERR_UNKNOWN_CMD");
}

// ============================================================
//  IDLE STATUS UPDATE
//  Self-restart la idle e gestionat de SlaveResilience::check() —
//  aici doar actualizam LED-ul vizual cand depasim IDLE_WARN_MS.
// ============================================================
void CommandDispatcher::_updateIdleStatus() {
    if (_lastRequestMs == 0) return;

    const uint32_t idleMs = millis() - _lastRequestMs;
    if (idleMs > IDLE_WARN_MS) {
        if (_led.getStatus() != SystemLED::Status::Idle &&
            _led.getStatus() != SystemLED::Status::SensorFail) {
            _led.setStatus(SystemLED::Status::Idle);
        }
    }
}

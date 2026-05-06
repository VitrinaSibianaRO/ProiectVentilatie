// CommandDispatcher.cpp — Implementare handlere comenzi UART Slave.
#include "CommandDispatcher.h"

void CommandDispatcher::tick() {
    char cmd[UART_BUFFER_SIZE];

    // OTA in desfasurare: trat OTA_CHUNK special (date binare dupa header)
    if (_ota.isActive()) {
        if (_uart.poll(cmd, sizeof(cmd))) {
            _lastRequestMs = millis();
            LOG_DEBUG("OTA RX: %s", cmd);

            if (strncmp(cmd, "OTA_CHUNK ", 10) == 0)  _handleOtaChunk(cmd + 10);
            else if (strcmp(cmd, "OTA_END") == 0)       _handleOtaEnd();
            else if (strcmp(cmd, "OTA_ABORT") == 0)     _handleOtaAbort();
            else {
                LOG_WARN("Unexpected cmd during OTA: %s", cmd);
                _uart.sendLine("ERR_OTA_ACTIVE");
            }
        }
        return;
    }

    if (!_uart.poll(cmd, sizeof(cmd))) {
        _updateIdleStatus();
        return;
    }

    _lastRequestMs = millis();
    LOG_DEBUG("RX: %s", cmd);

    // Routing comenzi (strcmp / strncmp pentru prefix-uri)
    if (strcmp(cmd, "GET_SENSOR") == 0)          _handleGetSensor();
    else if (strcmp(cmd, "REBOOT") == 0)          _handleReboot();
    else if (strcmp(cmd, "PING") == 0)            _handlePing();
    else if (strncmp(cmd, "LED_SET ", 8) == 0)   _handleLedSet(cmd + 8);
    else if (strncmp(cmd, "LED_SCHEDULE ", 13) == 0) _handleLedSchedule(cmd + 13);
    else if (strcmp(cmd, "LED_STATUS") == 0)      _handleLedStatus();
    else if (strncmp(cmd, "TIME_SYNC ", 10) == 0) _handleTimeSync(cmd + 10);
    else if (strncmp(cmd, "OTA_BEGIN ", 10) == 0) _handleOtaBegin(cmd + 10);
    else if (strcmp(cmd, "UART_BAUD_HIGH") == 0)  _handleUartBaudHigh();
    else if (strcmp(cmd, "UART_BAUD_LOW") == 0)   _handleUartBaudLow();
    else                                           _handleUnknown(cmd);

    _updateIdleStatus();
}

// ============================================================
//  GET_SENSOR
// ============================================================
void CommandDispatcher::_handleGetSensor() {
    float t = 0.0f, h = 0.0f;
    const bool ok = _sensor.read(t, h);

    JsonDocument doc;
    doc["temp"]   = ok ? t : 0.0f;
    doc["hum"]    = ok ? h : 0.0f;
    doc["ts"]     = (uint32_t)(millis() / 1000UL);
    doc["ok"]     = ok;
    doc["errors"] = _sensor.getConsecutiveErrors();
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
//  OTA_BEGIN <size> <sha256>
// ============================================================
void CommandDispatcher::_handleOtaBegin(const char* args) {
    uint32_t size;
    char sha[65];
    if (sscanf(args, "%u %64s", &size, sha) != 2) {
        LOG_ERROR("OTA_BEGIN parse fail: %s", args);
        _uart.sendLine("ERR_BEGIN_PARSE");
        return;
    }
    _led.setStatus(SystemLED::Status::OtaProgress);
    _uart.sendLine(_ota.begin(size, sha) ? "OK" : "ERR_BEGIN");
}

// ============================================================
//  OTA_CHUNK <length>  (urmat de N bytes binari)
// ============================================================
void CommandDispatcher::_handleOtaChunk(const char* args) {
    uint32_t length = (uint32_t)strtoul(args, nullptr, 10);
    if (_ota.writeChunk(length)) {
        char resp[32];
        snprintf(resp, sizeof(resp), "OK %u", length);
        _uart.sendLine(resp);
    } else {
        _uart.sendLine("ERR_CHUNK");
        _ota.abort();
        _led.setStatus(SystemLED::Status::SensorFail);
    }
}

// ============================================================
//  OTA_END
// ============================================================
void CommandDispatcher::_handleOtaEnd() {
    // _ota.end() reseteaza ESP32 la succes — nu mai ajungem la sendLine("OK")
    if (!_ota.end()) {
        _uart.sendLine("ERR_END");
        _led.setStatus(SystemLED::Status::SensorFail);
    }
    // la succes ESP.restart() e deja apelat in OtaReceiver::end()
}

// ============================================================
//  OTA_ABORT
// ============================================================
void CommandDispatcher::_handleOtaAbort() {
    _ota.abort();
    _led.setStatus(SystemLED::Status::Ready);
    _uart.sendLine("OK");
}

// ============================================================
//  UART_BAUD_HIGH / UART_BAUD_LOW
// ============================================================
void CommandDispatcher::_handleUartBaudHigh() {
    _uart.sendLine("OK");
    delay(50);
    _uart.updateBaudRate(OTA_UART_BAUD);
    LOG_INFO("Baud → %u", OTA_UART_BAUD);
}

void CommandDispatcher::_handleUartBaudLow() {
    _uart.sendLine("OK");
    delay(50);
    _uart.updateBaudRate(SLAVE_UART_BAUD);
    LOG_INFO("Baud → %u", SLAVE_UART_BAUD);
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
// ============================================================
void CommandDispatcher::_updateIdleStatus() {
    if (_lastRequestMs == 0) return;

    const uint32_t idleMs = millis() - _lastRequestMs;

    if (idleMs > SELF_RESTART_IDLE_MS) {
        LOG_WARN("Idle 30min — self-restart");
        delay(100);
        ESP.restart();
    }

    if (idleMs > IDLE_WARN_MS) {
        if (_led.getStatus() != SystemLED::Status::Idle &&
            _led.getStatus() != SystemLED::Status::SensorFail) {
            _led.setStatus(SystemLED::Status::Idle);
        }
    }
}

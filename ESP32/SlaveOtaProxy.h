// SlaveOtaProxy.h — Master proxy: descarca firmware Slave de pe github,
// streameaza chunked prin UART catre Slave (cu OtaReceiver).
// Switch baud rate la 460800 pentru OTA, revine la 115200 dupa.
#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoHttpClient.h>
#include "Config.h"

typedef void (*SlaveOtaProgressCb)(uint32_t sent, uint32_t total);

enum SlaveOtaResult {
    SOTA_OK,
    SOTA_ERR_URL_INVALID,
    SOTA_ERR_HTTP,
    SOTA_ERR_SIZE,
    SOTA_ERR_BEGIN_REJECT,
    SOTA_ERR_CHUNK_REJECT,
    SOTA_ERR_END_REJECT,
    SOTA_ERR_BAUD_SWITCH,
};

class SlaveOtaProxy {
public:
    // url + sha256 hex 64 chars. progress poate fi null.
    // Foloseste Serial2 (acelasi UART ca SlaveUartClient).
    static SlaveOtaResult perform(const char* url, const char* sha256,
                                   HardwareSerial& slaveSerial,
                                   SlaveOtaProgressCb progress = nullptr);

private:
    static bool _isUrlAllowed(const char* url);
    static bool _switchBaud(HardwareSerial& s, uint32_t baud, uint32_t newBaud);
    static bool _waitOk(HardwareSerial& s, uint32_t timeoutMs);
};

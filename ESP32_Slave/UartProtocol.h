// UartProtocol.h — Buffering UART linie-cu-linie cu CRC-16 Modbus.
// Poll() extrage linii complete; sendLine/sendJson ataseaza CRC automat.
// Zero alocari heap in hot path — buffer static char[UART_BUFFER_SIZE].
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "CrcUtil.h"
#include "Logger.h"

class UartProtocol {
public:
    explicit UartProtocol(HardwareSerial& serial)
        : _serial(serial), _bufLen(0) {}

    void begin(uint32_t baud, uint8_t rxPin, uint8_t txPin) {
        _serial.begin(baud, SERIAL_8N1, rxPin, txPin);
        _bufLen = 0;
    }

    // Polleaza UART non-blocking. La gasire '\n' valideaza CRC,
    // strip-uieste "*XXXX" si umple outCmd (zero-terminated).
    // Returneaza true daca o linie valida a fost extrasa.
    // La CRC fail: loghezi WARN si ignori linia (Master va face retry prin timeout).
    bool poll(char* outCmd, size_t outSize) {
        while (_serial.available()) {
            const char c = (char)_serial.read();
            if (c == '\n') {
                _buffer[_bufLen] = '\0';

                if (!Crc::validate(_buffer)) {
                    LOG_WARN("CRC fail: %s", _buffer);
                    _bufLen = 0;
                    return false;   // ignora, nu raspunde — Master timeout + retry
                }
                Crc::stripCrc(_buffer);

                const size_t copyLen = (_bufLen < outSize - 1) ? _bufLen : (outSize - 1);
                memcpy(outCmd, _buffer, copyLen);
                outCmd[copyLen] = '\0';
                _bufLen = 0;
                return true;
            }
            if (c == '\r') continue;
            if (_bufLen < UART_BUFFER_SIZE - 1) {
                _buffer[_bufLen++] = c;
            } else {
                LOG_WARN("UART overflow, line discarded");
                _bufLen = 0;
            }
        }
        return false;
    }

    // Trimite linie text cu CRC atasat + '\n'. flush() garanteaza trimiterea completa.
    void sendLine(const char* line) {
        char buf[UART_BUFFER_SIZE];
        strncpy(buf, line, sizeof(buf) - 6);
        buf[sizeof(buf) - 6] = '\0';
        Crc::appendCrc(buf, sizeof(buf));
        _serial.print(buf);
        _serial.print('\n');
        _serial.flush();
    }

    // Serializeaza JsonDocument + CRC + '\n'. Intern serializam in buf temporar.
    void sendJson(JsonDocument& doc) {
        char tmp[UART_BUFFER_SIZE];
        size_t n = serializeJson(doc, tmp, sizeof(tmp) - 6);
        if (n >= sizeof(tmp) - 6) {
            LOG_ERROR("JSON too big for UART buffer");
            sendLine("ERR_JSON_TOO_BIG");
            return;
        }
        Crc::appendCrc(tmp, sizeof(tmp));
        _serial.print(tmp);
        _serial.print('\n');
        _serial.flush();
    }

    // Switch baud rate (folosit la OTA pentru viteza mai mare).
    void updateBaudRate(uint32_t baud) {
        _serial.flush();
        _serial.updateBaudRate(baud);
    }

    HardwareSerial& serial() { return _serial; }

private:
    HardwareSerial& _serial;
    char            _buffer[UART_BUFFER_SIZE];
    size_t          _bufLen;
};

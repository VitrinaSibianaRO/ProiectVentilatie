// CrcUtil.h — CRC-16/Modbus polynomial 0xA001 (inverted 0x8005), table-less.
// Standard MIT, identic in ambele proiecte (ESP32/ si ESP32_Slave/).
// Format mesaj cu CRC: "<payload>*<crc4hex>\n"
#pragma once
#include <Arduino.h>

namespace Crc {

// Calculeaza CRC-16/Modbus (standard industrial ~40 ani).
// Test vector official: crc16("123456789", 9) == 0x4B37
inline uint16_t crc16(const char* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

inline void crcToHex(uint16_t crc, char out[5]) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(crc >> 12) & 0x0F];
    out[1] = hex[(crc >>  8) & 0x0F];
    out[2] = hex[(crc >>  4) & 0x0F];
    out[3] = hex[(crc      ) & 0x0F];
    out[4] = '\0';
}

inline bool hexToCrc(const char* hexStr, uint16_t& out) {
    out = 0;
    for (uint8_t i = 0; i < 4; i++) {
        char c = hexStr[i];
        uint8_t v;
        if      (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else return false;
        out = (out << 4) | v;
    }
    return true;
}

inline bool validate(const char* buf) {
    const char* asterisk = strrchr(buf, '*');
    if (!asterisk || asterisk == buf) return false;
    size_t payloadLen = (size_t)(asterisk - buf);
    if (strlen(asterisk + 1) != 4) return false;

    uint16_t expected;
    if (!hexToCrc(asterisk + 1, expected)) return false;

    uint16_t computed = crc16(buf, payloadLen);
    return computed == expected;
}

inline void stripCrc(char* buf) {
    char* asterisk = strrchr(buf, '*');
    if (asterisk) *asterisk = '\0';
}

inline bool appendCrc(char* buf, size_t out_size) {
    size_t payloadLen = strlen(buf);
    if (payloadLen + 5 > out_size) return false;
    uint16_t crc = crc16(buf, payloadLen);
    char hex[5];
    crcToHex(crc, hex);
    buf[payloadLen]     = '*';
    buf[payloadLen + 1] = hex[0];
    buf[payloadLen + 2] = hex[1];
    buf[payloadLen + 3] = hex[2];
    buf[payloadLen + 4] = hex[3];
    buf[payloadLen + 5] = '\0';
    return true;
}

}  // namespace Crc

#pragma once

// ============================================================
//  EventLog.h
//  Circular buffer de 50 evenimente stocat în NVS.
//
//  Tipuri: sensor_err, relay_change, override_expired.
//  Fiecare entry = 32 bytes, total = 1.6KB în NVS namespace "log".
//  Index circular în NVS key "logIdx".
//
//  Design:
//  - append() scrie direct în NVS (fără copie în RAM)
//  - dumpJson() citește din NVS și serializează JSON
//  - Timestamp via TimeSync (epoch sec)
// ============================================================

#include <Arduino.h>
#include "Config.h"

// Tipuri de evenimente
enum EventType : uint8_t {
    EVT_SENSOR_ERR       = 0,
    EVT_RELAY_CHANGE     = 1,
    EVT_OVERRIDE_EXPIRED = 2
};

// Zone
enum EventZone : uint8_t {
    ZONE_LEFT  = 0,
    ZONE_RIGHT = 1,
    ZONE_NA    = 0xFF
};

// Entry compactă — 32 bytes exact
struct LogEntry {
    uint32_t epochSec;       // timestamp (sau uptime dacă !NTP)
    uint8_t  type;           // EventType
    uint8_t  zone;           // EventZone
    uint8_t  data[26];       // payload text scurt (null-terminated)
};

static_assert(sizeof(LogEntry) == 32, "LogEntry must be 32 bytes");

class EventLog {
public:
    // Deschide namespace NVS "log" și citește indexul circular.
    void begin();

    // Adaugă o intrare. msg va fi trunchiat la 25 caractere + null.
    void append(EventType type, EventZone zone, const char* msg);

    // Serializează toate intrările (ordonate cronologic) într-un buffer JSON.
    // Returnează numărul de bytes scrise (0 la eroare).
    // Buffer trebuie să fie >= 4096 bytes.
    size_t dumpJson(char* buf, size_t bufLen);

    // Numărul de intrări stocate (maxim MAX_ENTRIES)
    int count() const;

private:
    static constexpr int MAX_ENTRIES = EVENT_LOG_MAX_ENTRIES;
    int _nextIdx = 0;    // indexul unde se va scrie următoarea intrare
    int _count   = 0;    // câte intrări sunt populate

    // Citește o intrare din NVS. Returnează true la succes.
    bool _readEntry(int idx, LogEntry& entry);

    // Scrie o intrare în NVS.
    void _writeEntry(int idx, const LogEntry& entry);

    // Salvează indexul circular în NVS.
    void _saveIndex();
};

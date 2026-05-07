// ============================================================
//  EventLog.cpp
//  Circular buffer 50 × 32B în NVS namespace "log".
// ============================================================

#include "EventLog.h"
#include "TimeSync.h"
#include "Config.h"

#include <Preferences.h>
#include <ArduinoJson.h>

// NVS namespace separat de NVS_PREFS_NAMESPACE (parametrii config)
static Preferences _logPrefs;

void EventLog::begin() {
    _logPrefs.begin(NVS_LOG_NAMESPACE, false);
    _nextIdx = _logPrefs.getInt("logIdx", 0);
    _count   = _logPrefs.getInt("logCnt", 0);

    if (_nextIdx < 0 || _nextIdx >= MAX_ENTRIES) _nextIdx = 0;
    if (_count < 0 || _count > MAX_ENTRIES) _count = 0;

    Serial.printf("[EventLog] Init: %d entries, next=%d\n", _count, _nextIdx);
}

void EventLog::append(EventType type, EventZone zone, const char* msg) {
    LogEntry entry;
    memset(&entry, 0, sizeof(entry));

    entry.epochSec = TimeSync::getEpochSec();
    entry.type = (uint8_t)type;
    entry.zone = (uint8_t)zone;

    // Copiem mesajul, trunchiat la 25 chars + null terminator
    if (msg) {
        strncpy((char*)entry.data, msg, sizeof(entry.data) - 1);
        entry.data[sizeof(entry.data) - 1] = '\0';
    }

    _writeEntry(_nextIdx, entry);

    _nextIdx = (_nextIdx + 1) % MAX_ENTRIES;
    if (_count < MAX_ENTRIES) _count++;

    _saveIndex();

    // Log serial
    const char* typeStr[] = {"sensor_err", "relay_change", "override_expired"};
    const char* zoneStr[] = {"left", "right", "N/A"};
    int zIdx = (zone <= 1) ? zone : 2;
    int tIdx = (type <= 2) ? type : 0;
    Serial.printf("[EventLog] +%s zone=%s msg=\"%s\"\n",
        typeStr[tIdx], zoneStr[zIdx], msg ? msg : "");
}

size_t EventLog::dumpJson(char* buf, size_t bufLen) {
    // Buffer static 2048B e suficient pentru 20 entries
    StaticJsonDocument<2048> doc;

    JsonArray entries = doc["entries"].to<JsonArray>();

    // Parcurgem circular: de la cel mai vechi la cel mai nou
    int start = (_count < MAX_ENTRIES) ? 0 : _nextIdx;

    for (int i = 0; i < _count; i++) {
        int idx = (start + i) % MAX_ENTRIES;
        LogEntry entry;
        if (!_readEntry(idx, entry)) continue;

        JsonObject obj = entries.add<JsonObject>();

        // Timestamp
        char tsBuf[32];
        TimeSync::formatISO(entry.epochSec, tsBuf, sizeof(tsBuf));
        obj["ts"] = (const char*)tsBuf;   // ArduinoJson va copia string-ul

        // Type
        switch (entry.type) {
            case EVT_SENSOR_ERR:       obj["type"] = "sensor_err"; break;
            case EVT_RELAY_CHANGE:     obj["type"] = "relay_change"; break;
            case EVT_OVERRIDE_EXPIRED: obj["type"] = "override_expired"; break;
            default:                   obj["type"] = "unknown"; break;
        }

        // Zone
        switch (entry.zone) {
            case ZONE_LEFT:  obj["zone"] = "left"; break;
            case ZONE_RIGHT: obj["zone"] = "right"; break;
            default:         obj["zone"] = (const char*)nullptr; break;
        }

        // Mesaj
        entry.data[sizeof(entry.data) - 1] = '\0';
        obj["msg"] = (const char*)entry.data;
    }

    size_t n = serializeJson(doc, buf, bufLen);
    return n;
}

int EventLog::count() const {
    return _count;
}

// ============================================================
//  NVS read/write helpers
//  Fiecare entry e stocată ca blob cu key "eNN" (e00..e49)
// ============================================================

bool EventLog::_readEntry(int idx, LogEntry& entry) {
    char key[8];
    snprintf(key, sizeof(key), "e%02d", idx);
    size_t len = _logPrefs.getBytes(key, &entry, sizeof(entry));
    return (len == sizeof(entry));
}

void EventLog::_writeEntry(int idx, const LogEntry& entry) {
    char key[8];
    snprintf(key, sizeof(key), "e%02d", idx);
    _logPrefs.putBytes(key, &entry, sizeof(entry));
}

void EventLog::_saveIndex() {
    _logPrefs.putInt("logIdx", _nextIdx);
    _logPrefs.putInt("logCnt", _count);
}

// ============================================================
//  MqttBridge.cpp — FAZA 2 (commands + lock)
// ============================================================

#include "MqttBridge.h"

#include <WiFi.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "HiveMqCert.h"

// Pointer static pentru rutare callback PubSubClient → instanță
MqttBridge* MqttBridge::_instance = nullptr;

MqttBridge::MqttBridge()
    : _net(),
      _client(_net),
      _prefs(nullptr),
      _initialized(false),
      _lastReconnectMs(0),
      _backoffMs(MQTT_RECONNECT_INITIAL_MS),
      _lastPublishMs(0),
      _lastHeartbeatMs(0),
      _lockOwner(LOCK_NONE),
      _publishNow(false),
      _lockSetAtMs(0)
{
    _lastRelayState[0] = false;
    _lastRelayState[1] = false;
}

void MqttBridge::begin(AppPreferences* prefs) {
    _prefs = prefs;
    _instance = this;

    _net.setCACert(HIVEMQ_ROOT_CA);
    _client.setServer(MQTT_HOST, MQTT_PORT);
    _client.setBufferSize(MQTT_BUF_SIZE);
    _client.setKeepAlive(60);
    _client.setCallback(_staticCallback);

    _initialized = true;
    Serial.println("[MQTT] Bridge initialized (Faza 2: commands + lock).");
}

bool MqttBridge::connected() {
    return _initialized && _client.connected();
}

void MqttBridge::loop() {
    if (!_initialized) return;
    if (WiFi.status() != WL_CONNECTED) return;

    if (!_client.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectMs < _backoffMs) return;
        _lastReconnectMs = now;

        if (_connect()) {
            _backoffMs = MQTT_RECONNECT_INITIAL_MS;   // reset
            // La reconectare reușită, forțăm un heartbeat la următorul publishStateIfNeeded
            _lastHeartbeatMs = 0;
        } else {
            // Backoff exponențial 5s → 60s
            unsigned long next = _backoffMs * 2;
            _backoffMs = (next > MQTT_RECONNECT_MAX_MS) ? MQTT_RECONNECT_MAX_MS : next;
            Serial.printf("[MQTT] Reconnect failed, rc=%d, retry in %lus\n",
                _client.state(), _backoffMs / 1000);
        }
    } else {
        _client.loop();   // keepalive + procesare mesaje callback
    }
}

bool MqttBridge::_connect() {
    Serial.print("[MQTT] Connecting to HiveMQ... ");

    // Client ID unic: prefix + ultimele 4 bytes din MAC (eFuse)
    char clientId[32];
    uint64_t chipId = ESP.getEfuseMac();
    snprintf(clientId, sizeof(clientId), "%s%08X",
        MQTT_CLIENT_PREFIX, (uint32_t)(chipId >> 16));

    // LWT: dacă ESP32 cade neașteptat, broker-ul publică automat "offline"
    // pe TOPIC_ONLINE (retained).
    bool ok = _client.connect(
        clientId,
        MQTT_USER, MQTT_PASS,
        TOPIC_ONLINE,    // willTopic
        1,               // willQos
        true,            // willRetain
        "offline"        // willMessage
    );

    if (!ok) {
        Serial.printf("FAILED rc=%d\n", _client.state());
        return false;
    }

    Serial.println("OK.");

    // Publicăm "online" (retained) imediat după conectare
    _client.publish(TOPIC_ONLINE, "online", true);

    // Subscribe la comenzi (QoS 1)
    _client.subscribe(TOPIC_CMD, 1);
    Serial.println("[MQTT] Subscribed to ventilatie/cmd (QoS 1).");

    return true;
}

// ============================================================
//  CALLBACK MQTT — ZERO alocări dinamice
//  Parsăm JSON pe stack, setăm doar flags.
// ============================================================

void MqttBridge::_staticCallback(char* topic, byte* payload, unsigned int length) {
    if (_instance) {
        _instance->_handleMessage(topic, payload, length);
    }
}

void MqttBridge::_handleMessage(char* topic, byte* payload, unsigned int length) {
    // Verificăm că e topic-ul corect
    if (strcmp(topic, TOPIC_CMD) != 0) return;

    // Parsare JSON pe stack — StaticJsonDocument, zero heap
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, payload, length);
    if (err) {
        Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
        return;
    }

    const char* cmd = doc["cmd"] | "";
    Serial.printf("[MQTT] Cmd received: %s\n", cmd);

    // cmd:refresh bypasses lock (conform §E din plan)
    if (strcmp(cmd, "refresh") == 0) {
        _mqttPending.refresh = true;
        return;
    }

    // Lock check: dacă Blynk deține lock-ul, respingem comanda
    if (_lockOwner == LOCK_BLYNK) {
        publishCmdRejected("locked", "blynk");
        Serial.println("[MQTT] Cmd rejected: lock activ Blynk.");
        return;
    }

    // Setăm lock MQTT + forțăm publish state cu lock activ
    _lockOwner = LOCK_MQTT;
    _lockSetAtMs = millis();
    _publishNow = true;

    if (strcmp(cmd, "setOverride") == 0) {
        const char* zone = doc["zone"] | "";
        int value = doc["value"] | -1;
        if (value < 0 || value > 2) {
            Serial.println("[MQTT] setOverride: value invalid.");
            return;
        }
        if (strcmp(zone, "left") == 0) {
            _mqttPending.setOverrideL = true;
            _mqttPending.overrideLVal = value;
        } else if (strcmp(zone, "right") == 0) {
            _mqttPending.setOverrideR = true;
            _mqttPending.overrideRVal = value;
        } else {
            Serial.printf("[MQTT] setOverride: zone '%s' necunoscută.\n", zone);
        }
    }
    else if (strcmp(cmd, "setConfig") == 0) {
        _mqttPending.setConfig = true;
        _mqttPending.threshT  = doc["threshT"]  | 0.0f;
        _mqttPending.threshH  = doc["threshH"]  | 0.0f;
        _mqttPending.interval = doc["interval"] | 0;
        _mqttPending.hystT    = doc["hystT"]    | -1.0f;
        _mqttPending.hystH    = doc["hystH"]    | -1.0f;
    }
    else if (strcmp(cmd, "reset") == 0) {
        _mqttPending.resetDefaults = true;
    }
    else if (strcmp(cmd, "reboot") == 0) {
        _mqttPending.reboot = true;
    }
    // getLog — bypass lock (read-only)
    else if (strcmp(cmd, "getLog") == 0) {
        _mqttPending.getLog = true;
        // Nu setăm lock (e read-only)
        _lockOwner = LOCK_NONE;
    }
    // update — OTA
    else if (strcmp(cmd, "update") == 0) {
        const char* otaUrl = doc["url"] | "";
        const char* otaSha = doc["sha256"] | "";
        if (strlen(otaUrl) == 0 || strlen(otaSha) != 64) {
            Serial.println("[MQTT] cmd:update — URL sau SHA-256 invalid.");
            _lockOwner = LOCK_NONE;
            return;
        }
        strncpy(_mqttPending.otaUrl, otaUrl, sizeof(_mqttPending.otaUrl) - 1);
        _mqttPending.otaUrl[sizeof(_mqttPending.otaUrl) - 1] = '\0';
        strncpy(_mqttPending.otaSha, otaSha, sizeof(_mqttPending.otaSha) - 1);
        _mqttPending.otaSha[sizeof(_mqttPending.otaSha) - 1] = '\0';
        _mqttPending.update = true;
    }
    else {
        Serial.printf("[MQTT] Cmd '%s' necunoscută.\n", cmd);
        _lockOwner = LOCK_NONE;
    }
}

// ============================================================
//  LOCK + PUBLISH
// ============================================================

void MqttBridge::setLockOwner(LockOwner owner) {
    _lockOwner = owner;
    if (owner != LOCK_NONE) {
        _lockSetAtMs = millis();
    }
}

LockOwner MqttBridge::getLockOwner() const {
    return _lockOwner;
}

void MqttBridge::requestPublishNow() {
    _publishNow = true;
}

void MqttBridge::publishCmdRejected(const char* reason, const char* by) {
    if (!connected()) return;

    StaticJsonDocument<128> doc;
    doc["event"]  = "cmd_rejected";
    doc["reason"] = reason;
    doc["by"]     = by;

    char buf[128];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        _client.publish(TOPIC_EVENT, (const uint8_t*)buf, n, false);
        Serial.printf("[MQTT] Event published: cmd_rejected (by %s)\n", by);
    }
}

bool MqttBridge::hasPendingCommands() const {
    return _mqttPending.refresh
        || _mqttPending.setOverrideL
        || _mqttPending.setOverrideR
        || _mqttPending.setConfig
        || _mqttPending.resetDefaults
        || _mqttPending.reboot
        || _mqttPending.getLog
        || _mqttPending.update;
}

MqttPending& MqttBridge::getPending() {
    return _mqttPending;
}

// ============================================================
//  STATE PUBLISH
// ============================================================

void MqttBridge::publishStateIfNeeded(const VentilationZone& l, const VentilationZone& r) {
    if (!connected()) return;

    unsigned long now = millis();

    // Throttle hard
    if (now - _lastPublishMs < MQTT_PUBLISH_MIN_INTERVAL_MS) return;

    // Heartbeat la fiecare MQTT_HEARTBEAT_MS
    bool heartbeat = (_lastHeartbeatMs == 0) ||
                     (now - _lastHeartbeatMs >= MQTT_HEARTBEAT_MS);

    // Schimbare automată stare releu (auto on/off declanșat de senzori)
    bool relayChanged = (l.getRelayState() != _lastRelayState[0]) ||
                        (r.getRelayState() != _lastRelayState[1]);

    // Push imediat (setat de comenzi MQTT/Blynk sau lock change)
    bool pushNow = _publishNow;

    if (heartbeat || relayChanged || pushNow) {
        _publishStateNow(l, r);
        _lastPublishMs = now;
        _publishNow = false;
        if (heartbeat) _lastHeartbeatMs = now;
        _lastRelayState[0] = l.getRelayState();
        _lastRelayState[1] = r.getRelayState();
    }
}

void MqttBridge::_publishStateNow(const VentilationZone& l, const VentilationZone& r) {
    if (!_prefs) return;

    StaticJsonDocument<640> doc;

    JsonObject left = doc["left"].to<JsonObject>();
    left["temp"]     = l.getTemp();
    left["hum"]      = l.getHum();
    left["relay"]    = l.getRelayState();
    left["override"] = l.getManualOverride();
    left["errs"]     = l.getConsecErrors();

    JsonObject right = doc["right"].to<JsonObject>();
    right["temp"]     = r.getTemp();
    right["hum"]      = r.getHum();
    right["relay"]    = r.getRelayState();
    right["override"] = r.getManualOverride();
    right["errs"]     = r.getConsecErrors();

    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["threshT"]    = _prefs->tempThresh;
    cfg["threshH"]    = _prefs->humThresh;
    cfg["interval"]   = _prefs->intervalSec;
    cfg["ovrTimeout"] = _prefs->overrideTimeoutMin;
    cfg["hystT"]      = _prefs->tempHyst;
    cfg["hystH"]      = _prefs->humHyst;

    // Lock object: {owner:"blynk"|"mqtt", ageMs:N} sau null
    if (_lockOwner != LOCK_NONE) {
        JsonObject lock = doc["lock"].to<JsonObject>();
        lock["owner"] = (_lockOwner == LOCK_BLYNK) ? "blynk" : "mqtt";
        lock["ageMs"] = (uint32_t)(millis() - _lockSetAtMs);
    } else {
        doc["lock"] = (const char*)nullptr;
    }

    doc["fw"]        = (int)FW_BUILD_NUMBER;
    doc["uptimeSec"] = (uint32_t)(millis() / 1000);
    doc["heap"]      = (uint32_t)ESP.getFreeHeap();

    char buf[700];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        Serial.println("[MQTT] State serialize error.");
        return;
    }

    bool ok = _client.publish(TOPIC_STATE, (const uint8_t*)buf, n, true);
    if (!ok) {
        Serial.println("[MQTT] State publish FAILED.");
    } else {
        Serial.printf("[MQTT] State published (%u bytes, lock=%s).\n",
            (unsigned)n,
            _lockOwner == LOCK_NONE ? "none" :
            _lockOwner == LOCK_BLYNK ? "blynk" : "mqtt");
    }
}

void MqttBridge::publishOnline(bool online) {
    if (!connected()) return;
    _client.publish(TOPIC_ONLINE, online ? "online" : "offline", true);
}

void MqttBridge::publishEventJson(const char* jsonStr) {
    if (!connected()) return;
    _client.publish(TOPIC_EVENT, (const uint8_t*)jsonStr, strlen(jsonStr), false);
}

void MqttBridge::publishLog(const char* jsonBuf, size_t len) {
    if (!connected()) return;
    // QoS 1, NOT retained
    _client.publish(TOPIC_LOG, (const uint8_t*)jsonBuf, len, false);
    Serial.printf("[MQTT] Log published (%u bytes).\n", (unsigned)len);
}

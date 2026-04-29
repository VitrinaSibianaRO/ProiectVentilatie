// ============================================================
//  MqttBridge.cpp — FAZA 1 (read-only)
// ============================================================

#include "MqttBridge.h"

#include <WiFi.h>
#include <ArduinoJson.h>

#include "Config.h"
#include "HiveMqCert.h"

MqttBridge::MqttBridge()
    : _net(),
      _client(_net),
      _prefs(nullptr),
      _initialized(false),
      _lastReconnectMs(0),
      _backoffMs(MQTT_RECONNECT_INITIAL_MS),
      _lastPublishMs(0),
      _lastHeartbeatMs(0)
{
    _lastRelayState[0] = false;
    _lastRelayState[1] = false;
}

void MqttBridge::begin(AppPreferences* prefs) {
    _prefs = prefs;

    _net.setCACert(HIVEMQ_ROOT_CA);
    _client.setServer(MQTT_HOST, MQTT_PORT);
    _client.setBufferSize(MQTT_BUF_SIZE);
    _client.setKeepAlive(60);
    // Callback nu e setat în Faza 1 (suntem read-only).
    // În Faza 2 vom adăuga: _client.setCallback(_staticCallback);

    _initialized = true;
    Serial.println("[MQTT] Bridge initialized.");
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
        _client.loop();   // keepalive + procesare mesaje (relevant Faza 2+)
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

    // În Faza 2 vom adăuga: _client.subscribe(TOPIC_CMD, 1);

    return true;
}

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

    if (heartbeat || relayChanged) {
        _publishStateNow(l, r);
        _lastPublishMs = now;
        if (heartbeat) _lastHeartbeatMs = now;
        _lastRelayState[0] = l.getRelayState();
        _lastRelayState[1] = r.getRelayState();
    }
}

void MqttBridge::_publishStateNow(const VentilationZone& l, const VentilationZone& r) {
    if (!_prefs) return;

    StaticJsonDocument<384> doc;

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

    // Lock e null în Faza 1 (nu acceptăm comenzi încă)
    doc["lock"] = (const char*)nullptr;

    doc["fw"]        = (int)FW_BUILD_NUMBER;
    doc["uptimeSec"] = (uint32_t)(millis() / 1000);
    doc["heap"]      = (uint32_t)ESP.getFreeHeap();

    char buf[512];
    size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        Serial.println("[MQTT] State serialize error.");
        return;
    }

    bool ok = _client.publish(TOPIC_STATE, (const uint8_t*)buf, n, true);
    if (!ok) {
        Serial.println("[MQTT] State publish FAILED.");
    } else {
        Serial.printf("[MQTT] State published (%u bytes).\n", (unsigned)n);
    }
}

void MqttBridge::publishOnline(bool online) {
    if (!connected()) return;
    _client.publish(TOPIC_ONLINE, online ? "online" : "offline", true);
}

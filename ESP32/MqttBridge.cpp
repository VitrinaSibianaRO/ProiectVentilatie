// ============================================================
//  MqttBridge.cpp — Ethernet + SSLClient + PubSubClient
//  Comunicatie cu HiveMQ Cloud (TLS 8883) prin W5500.
// ============================================================

#include "MqttBridge.h"

#include "Config.h"
#include "Resilience.h"
#include "TimeSync.h"
#include <ArduinoJson.h>

// Definite in ESP32.ino — accesibile din .cpp via extern.
extern bool g_ethAvailable;
extern bool g_wifiAvailable;

// Pointer static pentru rutare callback PubSubClient → instanță
MqttBridge *MqttBridge::_instance = nullptr;

MqttBridge::MqttBridge()
    : _baseClient(nullptr), _sslClient(nullptr), _wifiSecureClient(nullptr),
      _client(), _prefs(nullptr), _initialized(false), _lastReconnectMs(0),
      _backoffMs(MQTT_RECONNECT_INITIAL_MS), _lastPublishMs(0),
      _lastHeartbeatMs(0), _lockOwner(LOCK_NONE), _publishNow(false),
      _lockSetAtMs(0), _lastDiagMs(0), _lastSlaveErrors(0),
      _lastSlaveOnline(false), _stateBuf(nullptr), _diagBuf(nullptr) {
  _lastRelayState[0] = false;
  _lastRelayState[1] = false;

  // Alocare PSRAM cu fallback pe heap intern — o singura data la constructie.
  _stateBuf = (char *)ps_malloc(PSRAM_STAT_BUF_SIZE);
  if (!_stateBuf)
    _stateBuf = new char[PSRAM_STAT_BUF_SIZE];

  _diagBuf = (char *)ps_malloc(PSRAM_DIAG_BUF_SIZE);
  if (!_diagBuf)
    _diagBuf = new char[PSRAM_DIAG_BUF_SIZE];
}

void MqttBridge::begin(AppPreferences *prefs) {
  _prefs = prefs;
  _instance = this;

  // Cleanup transport anterior (poate fi re-apelat la comutare Eth↔WiFi).
  if (_client.connected())
    _client.disconnect();
  if (_sslClient) {
    delete _sslClient;
    _sslClient = nullptr;
  }
  if (_baseClient) {
    delete _baseClient;
    _baseClient = nullptr;
  }
  if (_wifiSecureClient) {
    delete _wifiSecureClient;
    _wifiSecureClient = nullptr;
  }

  // Ethernet are prioritate; WiFi e fallback.
  if (g_ethAvailable) {
    _baseClient = new EthernetClient();
    _sslClient =
        new SSLClient(*_baseClient, TrustAnchors, TrustAnchors_NUM, A0);
    _client.setClient(*_sslClient);
    Serial.println("[MQTT] Transport: Ethernet + SSLClient.");
  } else {
    // WiFiClientSecure nativ ESP32 — TLS hardware-accelerat
    _wifiSecureClient = new WiFiClientSecure();
    // Folosim setInsecure() pentru a evita erorile de tip rc=-2 (TLS handshake
    // failure) cauzate de verificarea certificatelor, păstrând în același timp
    // SNI activ.
    _wifiSecureClient->setInsecure();
    _wifiSecureClient->setHandshakeTimeout(30000); // 30s timeout

    _client.setClient(*_wifiSecureClient);
    Serial.println(
        "[MQTT] Transport: WiFi + WiFiClientSecure (Insecure mode).");
  }

  _client.setServer(MQTT_HOST, MQTT_PORT);
  _client.setBufferSize(MQTT_BUF_SIZE);
  _client.setKeepAlive(60);
  _client.setCallback(_staticCallback);

  _initialized = true;
}

bool MqttBridge::connected() { return _initialized && _client.connected(); }

void MqttBridge::loop() {
  if (!_initialized)
    return;
  // Daca folosim Ethernet, verificam link status
  if (!g_wifiAvailable && Ethernet.linkStatus() != LinkON)
    return;

  if (!_client.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnectMs < _backoffMs)
      return;
    _lastReconnectMs = now;

    if (_connect()) {
      _backoffMs = MQTT_RECONNECT_INITIAL_MS;
      _lastHeartbeatMs = 0;
      MqttReconnectGuard::onConnectSuccess();
    } else {
      unsigned long next = _backoffMs * 2;
      _backoffMs =
          (next > MQTT_RECONNECT_MAX_MS) ? MQTT_RECONNECT_MAX_MS : next;
      Serial.printf("[MQTT] Reconnect failed, rc=%d, retry in %lus\n",
                    _client.state(), _backoffMs / 1000);
      MqttReconnectGuard::onConnectFail();
    }
  } else {
    _client.loop();

    // Diag publish la fiecare 5 minute
    unsigned long now = millis();
    if (now - _lastDiagMs >= 300000UL || _lastDiagMs == 0) {
      _publishDiag();
      _lastDiagMs = now;
    }
  }
}

bool MqttBridge::_connect() {
  // TLS handshake consuma ~30KB heap. Skip daca avem prea putin
  // (evita aluneci in OOM crash mid-handshake).
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 50000UL) {
    Serial.printf("[MQTT] Heap %u < 50KB, skip TLS connect\n", freeHeap);
    return false;
  }

  Serial.printf("[MQTT] Connecting to HiveMQ via %s... ",
                g_ethAvailable ? "Ethernet" : "WiFi");

  char clientId[32];
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "%s%08X", MQTT_CLIENT_PREFIX,
           (uint32_t)(chipId >> 16));

  // LWT: dacă ESP32 cade neașteptat, broker-ul publică automat "offline"
  bool ok = _client.connect(clientId, MQTT_USER, MQTT_PASS,
                            TOPIC_ONLINE, // willTopic
                            1,            // willQos
                            true,         // willRetain
                            "offline"     // willMessage
  );

  if (!ok) {
    Serial.printf("FAILED rc=%d\n", _client.state());
    return false;
  }

  Serial.println("OK.");
  _client.publish(TOPIC_ONLINE, "online", true);
  _client.subscribe(TOPIC_CMD, 1);
  Serial.println("[MQTT] Subscribed to ventilatie/cmd (QoS 1).");

  return true;
}

// ============================================================
//  CALLBACK MQTT — ZERO alocări dinamice
// ============================================================

void MqttBridge::_staticCallback(char *topic, byte *payload,
                                 unsigned int length) {
  if (_instance) {
    _instance->_handleMessage(topic, payload, length);
  }
}

void MqttBridge::_handleMessage(char *topic, byte *payload,
                                unsigned int length) {
  if (strcmp(topic, TOPIC_CMD) != 0)
    return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
    return;
  }

  const char *cmd = doc["cmd"] | "";
  Serial.printf("[MQTT] Cmd received: %s\n", cmd);

  // cmd:refresh bypasses lock
  if (strcmp(cmd, "refresh") == 0) {
    _mqttPending.refresh = true;
    return;
  }

  // Lock check: doar LOCK_MQTT acum (LOCK_BLYNK eliminat)
  // Setăm lock MQTT + forțăm publish state cu lock activ
  _lockOwner = LOCK_MQTT;
  _lockSetAtMs = millis();
  _publishNow = true;

  if (strcmp(cmd, "setOverride") == 0) {
    const char *zone = doc["zone"] | "";
    int value = doc["value"] | -1;
    if (value < 0 || value > 2) {
      Serial.println("[MQTT] setOverride: value invalid.");
      _lockOwner = LOCK_NONE; // revert lock pe validation fail
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
      _lockOwner = LOCK_NONE;
    }
  } else if (strcmp(cmd, "setConfig") == 0) {
    _mqttPending.setConfig = true;
    _mqttPending.threshT = doc["threshT"] | 0.0f;
    _mqttPending.threshH = doc["threshH"] | 0.0f;
    _mqttPending.interval = doc["interval"] | 0;
    _mqttPending.hystT = doc["hystT"] | -1.0f;
    _mqttPending.hystH = doc["hystH"] | -1.0f;
  } else if (strcmp(cmd, "reset") == 0) {
    _mqttPending.resetDefaults = true;
  } else if (strcmp(cmd, "reboot") == 0) {
    _mqttPending.reboot = true;
  } else if (strcmp(cmd, "rebootSlave") == 0) {
    _mqttPending.rebootSlave = true;
  }
  // getLog — bypass lock (read-only)
  else if (strcmp(cmd, "getLog") == 0) {
    _mqttPending.getLog = true;
    _lockOwner = LOCK_NONE;
  }
  // LED control — forwarded to Slave via UART
  else if (strcmp(cmd, "setLed") == 0) {
    int pct = doc["percent"] | -1;
    if (pct < 0 || pct > 100) {
      Serial.println("[MQTT] setLed: percent out of range.");
      _lockOwner = LOCK_NONE;
      return;
    }
    _mqttPending.setLedNow = true;
    _mqttPending.ledPercent = (uint8_t)pct;
  } else if (strcmp(cmd, "setLedSchedule") == 0) {
    _mqttPending.setLedSched = true;
    _mqttPending.ledOnH = doc["onH"] | 0;
    _mqttPending.ledOnM = doc["onM"] | 0;
    _mqttPending.ledOffH = doc["offH"] | 0;
    _mqttPending.ledOffM = doc["offM"] | 0;
    _mqttPending.ledMaxI = doc["maxI"] | 80;
    _mqttPending.ledSchedEn = doc["enabled"] | false;
  }
  // updateSlave — OTA Slave (Master proxy)
  else if (strcmp(cmd, "updateSlave") == 0) {
    const char *otaUrl = doc["url"] | "";
    const char *otaSha = doc["sha"] | "";
    if (strlen(otaUrl) < 10 || strlen(otaSha) != 64) {
      Serial.println("[MQTT] updateSlave: url sau sha invalid.");
      _lockOwner = LOCK_NONE;
      return;
    }
    strncpy(_mqttPending.slaveOtaUrl, otaUrl,
            sizeof(_mqttPending.slaveOtaUrl) - 1);
    _mqttPending.slaveOtaUrl[sizeof(_mqttPending.slaveOtaUrl) - 1] = '\0';
    strncpy(_mqttPending.slaveOtaSha, otaSha,
            sizeof(_mqttPending.slaveOtaSha) - 1);
    _mqttPending.slaveOtaSha[sizeof(_mqttPending.slaveOtaSha) - 1] = '\0';
    _mqttPending.updateSlave = true;
  }
  // update — OTA Master
  else if (strcmp(cmd, "update") == 0) {
    const char *otaUrl = doc["url"] | "";
    const char *otaSha = doc["sha"] | "";
    if (strlen(otaUrl) < 10 || strlen(otaSha) != 64) {
      Serial.println("[MQTT] update: url sau sha invalid.");
      _lockOwner = LOCK_NONE;
      return;
    }
    strncpy(_mqttPending.otaUrl, otaUrl, sizeof(_mqttPending.otaUrl) - 1);
    _mqttPending.otaUrl[sizeof(_mqttPending.otaUrl) - 1] = '\0';
    strncpy(_mqttPending.otaSha, otaSha, sizeof(_mqttPending.otaSha) - 1);
    _mqttPending.otaSha[sizeof(_mqttPending.otaSha) - 1] = '\0';
    _mqttPending.update = true;
  } else {
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

LockOwner MqttBridge::getLockOwner() const { return _lockOwner; }

void MqttBridge::requestPublishNow() { _publishNow = true; }

void MqttBridge::publishCmdRejected(const char *reason, const char *by) {
  if (!connected())
    return;

  JsonDocument doc;
  doc["event"] = "cmd_rejected";
  doc["reason"] = reason;
  doc["by"] = by;

  char buf[128];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n > 0 && n < sizeof(buf)) {
    _client.publish(TOPIC_EVENT, (const uint8_t *)buf, n, false);
    Serial.printf("[MQTT] Event published: cmd_rejected (by %s)\n", by);
  }
}

bool MqttBridge::hasPendingCommands() const {
  return _mqttPending.refresh || _mqttPending.setOverrideL ||
         _mqttPending.setOverrideR || _mqttPending.setConfig ||
         _mqttPending.resetDefaults || _mqttPending.reboot ||
         _mqttPending.rebootSlave || _mqttPending.getLog ||
         _mqttPending.setLedNow || _mqttPending.setLedSched ||
         _mqttPending.update || _mqttPending.updateSlave;
}

MqttPending &MqttBridge::getPending() { return _mqttPending; }

// ============================================================
//  STATE PUBLISH
// ============================================================

void MqttBridge::publishStateIfNeeded(const VentilationZone &l,
                                      const VentilationZone &r,
                                      bool slaveOnline, int slaveErrors,
                                      unsigned long slaveLastSuccessMs,
                                      uint8_t ledIntensity,
                                      bool ledSchedEnabled) {
  // Cache pentru /diag (publish independent)
  _lastSlaveErrors = slaveErrors;
  _lastSlaveOnline = slaveOnline;

  if (!connected())
    return;

  bool pushNow = _publishNow;
  unsigned long now = millis();

  // Dacă este o cerere de publicare imediată (comandă manuală), sărim peste
  // throttling-ul de 500ms.
  if (!pushNow && (now - _lastPublishMs < MQTT_PUBLISH_MIN_INTERVAL_MS))
    return;

  bool heartbeat =
      (_lastHeartbeatMs == 0) || (now - _lastHeartbeatMs >= MQTT_HEARTBEAT_MS);
  bool relayChanged = (l.getRelayState() != _lastRelayState[0]) ||
                      (r.getRelayState() != _lastRelayState[1]);

  if (heartbeat || relayChanged || pushNow) {
    _publishStateNow(l, r, slaveOnline, slaveErrors, slaveLastSuccessMs,
                     ledIntensity, ledSchedEnabled);
    _lastPublishMs = now;
    _publishNow = false;
    if (heartbeat)
      _lastHeartbeatMs = now;
    _lastRelayState[0] = l.getRelayState();
    _lastRelayState[1] = r.getRelayState();
  }
}

void MqttBridge::_publishStateNow(const VentilationZone &l,
                                  const VentilationZone &r, bool slaveOnline,
                                  int slaveErrors,
                                  unsigned long slaveLastSuccessMs,
                                  uint8_t ledIntensity, bool ledSchedEnabled) {
  if (!_prefs)
    return;

  JsonDocument doc;

  JsonObject left = doc["left"].to<JsonObject>();
  left["temp"] = l.getTemp();
  left["hum"] = l.getHum();
  left["relay"] = l.getRelayState();
  left["override"] = l.getManualOverride();
  left["errs"] = l.getConsecErrors();

  JsonObject right = doc["right"].to<JsonObject>();
  right["temp"] = r.getTemp();
  right["hum"] = r.getHum();
  right["relay"] = r.getRelayState();
  right["override"] = r.getManualOverride();
  right["errs"] = r.getConsecErrors();
  right["failsafe"] = r.isInFailsafe();

  JsonObject cfg = doc["config"].to<JsonObject>();
  cfg["threshT"] = _prefs->tempThresh;
  cfg["threshH"] = _prefs->humThresh;
  cfg["interval"] = _prefs->intervalSec;
  cfg["ovrTimeout"] = _prefs->overrideTimeoutMin;
  cfg["hystT"] = _prefs->tempHyst;
  cfg["hystH"] = _prefs->humHyst;

  // Slave status
  JsonObject slave = doc["slave"].to<JsonObject>();
  slave["online"] = slaveOnline;
  slave["errors"] = slaveErrors;
  slave["lastSeen"] = (uint32_t)(slaveLastSuccessMs / 1000UL);

  // LED status (from Slave)
  JsonObject led = doc["led"].to<JsonObject>();
  led["intensity"] = ledIntensity;
  led["schedEnabled"] = ledSchedEnabled;

  // Lock object
  if (_lockOwner != LOCK_NONE) {
    JsonObject lock = doc["lock"].to<JsonObject>();
    lock["owner"] = "mqtt";
    lock["ageMs"] = (uint32_t)(millis() - _lockSetAtMs);
  }
  // else: doc["lock"] ramane absent (omitted din JSON)

  doc["fw"] = (int)FW_BUILD_NUMBER;
  doc["uptimeSec"] = (uint32_t)(millis() / 1000);
  doc["heap"] = (uint32_t)ESP.getFreeHeap();

  size_t n = serializeJson(doc, _stateBuf, PSRAM_STAT_BUF_SIZE);
  if (n == 0 || n >= PSRAM_STAT_BUF_SIZE) {
    Serial.println("[MQTT] State serialize error.");
    return;
  }

  bool ok = _client.publish(TOPIC_STATE, (const uint8_t *)_stateBuf, n, true);
  if (!ok) {
    Serial.println("[MQTT] State publish FAILED.");
  } else {
    Serial.printf("[MQTT] State published (%u bytes, lock=%s, slave=%s).\n",
                  (unsigned)n, _lockOwner == LOCK_NONE ? "none" : "mqtt",
                  slaveOnline ? "online" : "offline");
  }
}

void MqttBridge::publishOnline(bool online) {
  if (!connected())
    return;
  _client.publish(TOPIC_ONLINE, online ? "online" : "offline", true);
}

void MqttBridge::publishEventJson(const char *jsonStr) {
  if (!connected())
    return;
  _client.publish(TOPIC_EVENT, (const uint8_t *)jsonStr, strlen(jsonStr),
                  false);
}

void MqttBridge::publishLog(const char *jsonBuf, size_t len) {
  if (!connected())
    return;
  _client.publish(TOPIC_LOG, (const uint8_t *)jsonBuf, len, false);
  Serial.printf("[MQTT] Log published (%u bytes).\n", (unsigned)len);
}

void MqttBridge::_publishDiag() {
  JsonDocument doc;
  doc["uptimeSec"] = (uint32_t)(millis() / 1000);
  doc["heap"] = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();
  doc["ethLink"] = (Ethernet.linkStatus() == LinkON);
  doc["mqttOk"] = _client.connected();
  doc["fw"] = (int)FW_BUILD_NUMBER;
  doc["slaveErr"] = _lastSlaveErrors;
  doc["slaveOnline"] = _lastSlaveOnline;

  size_t n = serializeJson(doc, _diagBuf, PSRAM_DIAG_BUF_SIZE);
  _client.publish(TOPIC_DIAG, (const uint8_t *)_diagBuf, n, false);
}

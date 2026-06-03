// ============================================================
//  MqttBridge.cpp — WiFiClientSecure + PubSubClient
//  Comunicatie cu HiveMQ Cloud (TLS 8883) prin WiFi.
// ============================================================

#include "MqttBridge.h"

#include "Config.h"
#include "Resilience.h"
#include "TimeSync.h"
#include <ArduinoJson.h>

// Definite in ESP32.ino — accesibile din .cpp via extern.
extern bool g_wifiAvailable;

// Pointer static pentru rutare callback PubSubClient → instanță
MqttBridge *MqttBridge::_instance = nullptr;

MqttBridge::MqttBridge()
    : _wifiSecureClient(nullptr),
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

  // Cleanup transport anterior.
  if (_client.connected())
    _client.disconnect();
  if (_wifiSecureClient) {
    delete _wifiSecureClient;
    _wifiSecureClient = nullptr;
  }

  // WiFiClientSecure nativ ESP32 — TLS hardware-accelerat
  _wifiSecureClient = new WiFiClientSecure();
  _wifiSecureClient->setInsecure();
  _wifiSecureClient->setHandshakeTimeout(30000); // 30s timeout
  _client.setClient(*_wifiSecureClient);
  Serial.println("[MQTT] Transport: WiFi + WiFiClientSecure.");

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

  Serial.print("[MQTT] Connecting to HiveMQ via WiFi... ");

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
  // TV commands vin pe acelasi topic cmd, deci nu e nevoie de subscribe suplimentar.

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
  } else if (strcmp(cmd, "setLedMode") == 0) {
    int id = doc["mode"] | -1;
    int p1 = doc["p1"]  |  0;
    int p2 = doc["p2"]  |  0;
    int p3 = doc["p3"]  |  0;
    int p4 = doc["p4"]  |  0;
    if (id < 0 || id >= 12) {
      Serial.println("[MQTT] setLedMode: id invalid.");
      _lockOwner = LOCK_NONE;
      return;
    }
    _mqttPending.setLedMode = true;
    _mqttPending.ledModeId  = (uint8_t)id;
    _mqttPending.ledModeP1  = (uint16_t)p1;
    _mqttPending.ledModeP2  = (uint16_t)p2;
    _mqttPending.ledModeP3  = (uint16_t)p3;
    _mqttPending.ledModeP4  = (uint16_t)p4;
  } else if (strcmp(cmd, "setTv") == 0) {
    const char* action = doc["action"] | "";
    if (strlen(action) == 0 || strlen(action) >= sizeof(_mqttPending.tvAction)) {
      Serial.println("[MQTT] setTv: action invalida.");
      _lockOwner = LOCK_NONE;
      return;
    }
    strncpy(_mqttPending.tvAction, action, sizeof(_mqttPending.tvAction) - 1);
    _mqttPending.tvValue  = doc["value"] | 0;
    _mqttPending.setTv    = true;
  } else if (strcmp(cmd, "setTvConfig") == 0) {
    const char* ip  = doc["ip"]  | "";
    const char* mac = doc["mac"] | "";
    if (strlen(ip) == 0 || strlen(mac) == 0) {
      Serial.println("[MQTT] setTvConfig: ip sau mac lipsa.");
      _lockOwner = LOCK_NONE;
      return;
    }
    strncpy(_mqttPending.tvConfigIp, ip, sizeof(_mqttPending.tvConfigIp) - 1);
    if (!TvController::parseMacString(mac, _mqttPending.tvConfigMac)) {
      Serial.println("[MQTT] setTvConfig: MAC invalid.");
      _lockOwner = LOCK_NONE;
      return;
    }
    _mqttPending.setTvConfig = true;
  } else if (strcmp(cmd, "setFollowTvBrightness") == 0) {
    int v = doc["enabled"] | 0;
    _mqttPending.setFollowTvBrightness = true;
    _mqttPending.followTvValue         = (v != 0);
  } else if (strcmp(cmd, "setLedMorseText") == 0) {
    const char* t = doc["text"] | "";
    if (strlen(t) > 51) {
      _lockOwner = LOCK_NONE;
      return;
    }
    _mqttPending.setLedMorseText = true;
    strlcpy(_mqttPending.ledMorseText, t, sizeof(_mqttPending.ledMorseText));
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
         _mqttPending.setLedMode ||
         _mqttPending.setTv || _mqttPending.setTvConfig ||
         _mqttPending.setFollowTvBrightness || _mqttPending.setLedMorseText;
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

  // Config NU mai e inclus in state publish.
  // MAUI Settings UI = sursa locala (Preferences). ESP32 ramane sursa pentru NVS
  // si automatizare, dar nu echoeaza config-ul inapoi pentru a evita overwrite UI.

  // Slave status
  JsonObject slave = doc["slave"].to<JsonObject>();
  slave["online"] = slaveOnline;
  slave["errors"] = slaveErrors;
  slave["lastSeen"] = (uint32_t)(slaveLastSuccessMs / 1000UL);

  // LED status (from Slave)
  JsonObject led = doc["led"].to<JsonObject>();
  led["intensity"]  = ledIntensity;
  led["followTv"]   = _prefs->followTvBrightness;
  led["morseText"]  = _prefs->morseText;

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

void MqttBridge::publishTvState(const TvState &tv) {
  if (!connected()) return;

  JsonDocument doc;
  doc["power"]        = tv.power;
  doc["volume"]       = tv.volume;
  doc["muted"]        = tv.muted;
  doc["inputId"]      = tv.inputId;
  doc["tempC"]        = tv.temperatureC;
  doc["signal"]       = tv.hasSignal;
  doc["hours"]        = tv.usageHours;
  doc["backlight"]    = tv.backlight;
  doc["pictureMode"]  = tv.pictureMode;
  doc["energySaving"] = tv.energySaving;
  doc["noSignalOff"]  = tv.noSignalPowerOff;
  doc["serial"]       = tv.serial;
  doc["swVersion"]    = tv.swVersion;
  doc["reachable"]    = tv.reachable;

  char buf[512];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  if (n == 0 || n >= sizeof(buf)) return;

  _client.publish(TOPIC_TV_STATE, (const uint8_t *)buf, n, true);
  Serial.printf("[MQTT] TV state published (%u bytes).\n", (unsigned)n);
}

void MqttBridge::_publishDiag() {
  JsonDocument doc;
  doc["uptimeSec"] = (uint32_t)(millis() / 1000);
  doc["heap"] = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();
  doc["mqttOk"] = _client.connected();
  doc["fw"] = (int)FW_BUILD_NUMBER;
  doc["slaveErr"] = _lastSlaveErrors;
  doc["slaveOnline"] = _lastSlaveOnline;

  size_t n = serializeJson(doc, _diagBuf, PSRAM_DIAG_BUF_SIZE);
  _client.publish(TOPIC_DIAG, (const uint8_t *)_diagBuf, n, false);
}

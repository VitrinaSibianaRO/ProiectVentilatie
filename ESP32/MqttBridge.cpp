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
      _client(), _prefs(nullptr), _initialized(false), _wasConnected(false),
      _lastReconnectMs(0),
      _backoffMs(MQTT_RECONNECT_INITIAL_MS),
      _nextDelayMs(MQTT_RECONNECT_INITIAL_MS), _lastPublishMs(0),
      _lastHeartbeatMs(0), _lastSeq(0), _lockOwner(LOCK_NONE), _publishNow(false),
      _lockSetAtMs(0), _lastDiagMs(0), _lastSlaveErrors(0),
      _lastSlaveOnline(false), _stateBuf(nullptr), _diagBuf(nullptr) {

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
  // Timeouts reale in SECUNDE — bound blocarea loopTask sub WDT 60s.
  // setHandshakeTimeout(30000) era in secunde (=30000s ~8h), BUG-ul fix-uit.
  _wifiSecureClient->setHandshakeTimeout(MQTT_HANDSHAKE_TIMEOUT_S); // 8s
  _wifiSecureClient->setTimeout(MQTT_TCP_TIMEOUT_S);                 // 5s TCP
  _client.setClient(*_wifiSecureClient);
  _client.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);                   // 4s read
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

  // WiFi jos: elibereaza contextul TLS (mbedTLS ~30KB) la prima detectie,
  // apoi iesi imediat — fara incercari blocante de reconectare fara IP.
  if (WiFi.status() != WL_CONNECTED) {
    if (_wasConnected || _client.connected()) {
      _client.disconnect();
      if (_wifiSecureClient) _wifiSecureClient->stop();
      _wasConnected = false;
      _backoffMs = MQTT_RECONNECT_INITIAL_MS; // reset backoff la revenire WiFi
      Serial.println("[MQTT] WiFi down — conexiune+TLS eliberate.");
    }
    return;
  }

  // WiFi conectat dar fara IP valid — skip, evita DNS/TCP blocant.
  if ((uint32_t)WiFi.localIP() == 0) return;

  if (!_client.connected()) {
    unsigned long now = millis();
    if (now - _lastReconnectMs < _nextDelayMs)
      return;
    _lastReconnectMs = now;

    if (_connect()) {
      _backoffMs   = MQTT_RECONNECT_INITIAL_MS;
      _nextDelayMs = MQTT_RECONNECT_INITIAL_MS;
      _lastHeartbeatMs = 0;
      _wasConnected = true;
      MqttReconnectGuard::onConnectSuccess();
    } else {
      // Backoff exponential "curat" + jitter ±15% pe fereastra de asteptare
      // (jitter-ul NU se acumuleaza in _backoffMs → ramane previzibil).
      unsigned long next = _backoffMs * 2;
      _backoffMs =
          (next > MQTT_RECONNECT_MAX_MS) ? MQTT_RECONNECT_MAX_MS : next;
      _nextDelayMs = _jittered(_backoffMs);
      Serial.printf("[MQTT] Reconnect failed, rc=%d, retry in ~%lus\n",
                    _client.state(), _nextDelayMs / 1000);
      MqttReconnectGuard::onConnectFail();
    }
  } else {
    _wasConnected = true;
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
  // Pre-check: WiFi conectat + IP valid (evita DNS/TCP blocant fara retea).
  if (WiFi.status() != WL_CONNECTED || (uint32_t)WiFi.localIP() == 0) {
    Serial.println("[MQTT] _connect: fara WiFi/IP — skip.");
    return false;
  }

  // TLS handshake consuma ~30KB heap. Skip daca avem prea putin
  // (evita aluneci in OOM crash mid-handshake).
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < 50000UL) {
    Serial.printf("[MQTT] Heap %u < 50KB, skip TLS connect\n", freeHeap);
    return false;
  }

  // Slate curat: elibereaza context TLS rezidual din incercarea anterioara.
  if (_wifiSecureClient) _wifiSecureClient->stop();

  Serial.print("[MQTT] Connecting to HiveMQ via WiFi... ");

  char clientId[32];
  uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "%s%08X", MQTT_CLIENT_PREFIX,
           (uint32_t)(chipId >> 16));

  // LWT retain=false: evita rate-limiting HiveMQ free tier pe retained writes.
  // Broker-ul trimite "offline" catre clientii conectati la cadere, dar
  // nu retine mesajul — nu risca rc=3 (UNAVAILABLE) la reconectare rapida.
  bool ok = _client.connect(clientId, MQTT_USER, MQTT_PASS,
                            TOPIC_ONLINE, // willTopic
                            1,            // willQos
                            false,        // willRetain — non-retained (fix rc=3)
                            "offline"     // willMessage
  );

  if (!ok) {
    Serial.printf("FAILED rc=%d\n", _client.state());
    return false;
  }

  Serial.println("OK.");
  // "online" non-retained: consistent cu LWT; evita retained write throttling.
  _client.publish(TOPIC_ONLINE, "online", false);
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

void MqttBridge::publishFromSnapshot(const ControlState &st) {
  // Cache pentru /diag (publish independent)
  _lastSlaveErrors = st.slaveErrors;
  _lastSlaveOnline = st.slaveOnline;

  if (!connected())
    return;

  const unsigned long now = millis();
  const bool pushNow    = _publishNow;
  const bool seqChanged = (st.seq != _lastSeq);   // control a evaluat/aplicat ceva
  const bool heartbeat  =
      (_lastHeartbeatMs == 0) || (now - _lastHeartbeatMs >= MQTT_HEARTBEAT_MS);

  if (!(pushNow || seqChanged || heartbeat))
    return;

  // Debounce anti-flood: nu publicam mai des de MQTT_ONDEMAND_MIN_MS. Daca prea
  // devreme, pastram cererea (_publishNow / seq diferit) → reincercam la ciclul urmator.
  if (now - _lastPublishMs < MQTT_ONDEMAND_MIN_MS)
    return;

  const bool clearedLock = _publishStateNow(st);
  _lastPublishMs = now;
  // Daca tocmai am publicat o stare CU lock (si l-am curatat), cerem re-publicare
  // imediata a starii FARA lock → starea retained de pe broker devine curata
  // (bannerul "Control blocat" nu mai ramane agatat).
  _publishNow    = clearedLock;
  _lastSeq       = st.seq;
  if (heartbeat) {
    _lastHeartbeatMs = now;
    // Backstop liveness: republica "online" (non-retained) la fiecare heartbeat,
    // ca app-urile abonate sa-l prinda chiar daca au ratat publish-ul de la connect.
    _client.publish(TOPIC_ONLINE, "online", false);
  }
}

bool MqttBridge::_publishStateNow(const ControlState &st) {
  const bool hadLock = (_lockOwner != LOCK_NONE);
  JsonDocument doc;

  JsonObject left = doc["left"].to<JsonObject>();
  left["temp"] = st.leftTemp;
  left["hum"] = st.leftHum;
  left["relay"] = st.leftRelay;
  left["override"] = st.leftOverride;
  left["errs"] = st.leftErrs;

  JsonObject right = doc["right"].to<JsonObject>();
  right["temp"] = st.rightTemp;
  right["hum"] = st.rightHum;
  right["relay"] = st.rightRelay;
  right["override"] = st.rightOverride;
  right["errs"] = st.rightErrs;
  right["failsafe"] = st.rightFailsafe;

  // Config NU mai e inclus in state publish.
  // MAUI Settings UI = sursa locala (Preferences). ESP32 ramane sursa pentru NVS
  // si automatizare, dar nu echoeaza config-ul inapoi pentru a evita overwrite UI.

  // Slave status
  JsonObject slave = doc["slave"].to<JsonObject>();
  slave["online"] = st.slaveOnline;
  slave["errors"] = st.slaveErrors;
  slave["lastSeen"] = st.slaveLastSeenSec;

  // LED status (din snapshot-ul de control — network NU citeste config direct)
  JsonObject led = doc["led"].to<JsonObject>();
  led["intensity"]  = st.ledIntensity;
  led["followTv"]   = st.followTvBrightness;
  led["morseText"]  = st.morseText;

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
    return false;
  }

  bool ok = _client.publish(TOPIC_STATE, (const uint8_t *)_stateBuf, n, true);
  if (!ok) {
    Serial.println("[MQTT] State publish FAILED.");
    return false;
  }
  Serial.printf("[MQTT] State published (%u bytes, lock=%s, slave=%s).\n",
                (unsigned)n, hadLock ? "mqtt" : "none",
                st.slaveOnline ? "online" : "offline");
  // Sterge lock-ul dupa publicare — comenzile sunt procesate. Returnam hadLock
  // → publishFromSnapshot va re-publica imediat starea FARA lock (curata bannerul).
  if (hadLock) _lockOwner = LOCK_NONE;
  return hadLock;
}

void MqttBridge::publishOnline(bool online) {
  if (!connected())
    return;
  // Non-retained: consistent cu LWT. "offline" explicit la graceful disconnect.
  _client.publish(TOPIC_ONLINE, online ? "online" : "offline", false);
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

// Aplica jitter ±15% pe valoarea de backoff — desincronizeaza reconectarile
// (evita reconnect storm sincron). Foloseste RNG hardware.
unsigned long MqttBridge::_jittered(unsigned long base) {
  unsigned long j = base * 15UL / 100UL;   // 15%
  if (j == 0)
    return base;
  long delta = (long)(esp_random() % (2UL * j + 1UL)) - (long)j;
  long result = (long)base + delta;
  if (result < 1000)
    result = 1000;   // minim 1s
  return (unsigned long)result;
}

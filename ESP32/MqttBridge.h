#pragma once

// ============================================================
//  MqttBridge.h
//  Wrapper peste PubSubClient + WiFiClientSecure pentru
//  comunicația cu HiveMQ Cloud (TLS port 8883).
//
//  FAZA 2: connect, LWT, publishState, heartbeat,
//          callback comenzi, lock owner, pending flags,
//          push imediat la schimbare.
//
//  Reguli design:
//  - TLS persistent (un singur handshake), reconnect cu backoff
//  - Cert ISRG Root X1 stocat în PROGMEM (zero RAM)
//  - State JSON serializat în StaticJsonDocument (zero heap alloc)
//  - Throttle hard 500ms între publicări consecutive
//  - Callback MQTT: ZERO String/new — doar set flags
// ============================================================

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "AppPreferences.h"
#include "VentilationZone.h"

// Lock owner: cine controlează sistemul în acest moment
enum LockOwner { LOCK_NONE = 0, LOCK_BLYNK = 1, LOCK_MQTT = 2 };

// Pending MQTT commands — setat în callback, procesat în main loop.
// ZERO alocări dinamice. Valorile sunt copiate direct din JSON parse.
struct MqttPending {
    bool  refresh       = false;

    bool  setOverrideL  = false;
    int   overrideLVal  = 0;       // 0=OFF, 1=ON, 2=clear

    bool  setOverrideR  = false;
    int   overrideRVal  = 0;

    bool  setConfig     = false;
    float threshT       = 0;
    float threshH       = 0;
    int   interval      = 0;

    bool  resetDefaults = false;
    bool  reboot        = false;

    bool  getLog        = false;

    bool  update        = false;
    char  otaUrl[256]   = {0};
    char  otaSha[65]    = {0};
};

class MqttBridge {
public:
    MqttBridge();

    // Inițializare. Apelat o singură dată în setup() după WiFi.connect.
    // Configurează cert TLS, buffer size, callback.
    void begin(AppPreferences* prefs);

    // Pump loop. Apelat la fiecare iterație de loop().
    // Gestionează reconnect cu backoff + client.loop() pentru keepalive.
    void loop();

    // Stare conexiune (true = MQTT autentificat și subscribed)
    bool connected();

    // Publicare state. Decide intern dacă publică (throttle, heartbeat, push imediat).
    void publishStateIfNeeded(const VentilationZone& l, const VentilationZone& r);

    // Publicare explicită online/offline (folosit pre-restart).
    void publishOnline(bool online);

    // --- FAZA 2: Lock + Commands ---

    // Lock owner management
    void setLockOwner(LockOwner owner);
    LockOwner getLockOwner() const;

    // Forțează publicare state la următorul publishStateIfNeeded()
    void requestPublishNow();

    // Publicare cmd_rejected pe ventilatie/event
    void publishCmdRejected(const char* reason, const char* by);

    // Publicare event JSON pe ventilatie/event (OTA progress, done, failed)
    void publishEventJson(const char* jsonStr);

    // Publicare log JSON pe ventilatie/log (QoS 1, not retained)
    void publishLog(const char* jsonBuf, size_t len);

    // Verificare dacă există comenzi MQTT pending de procesat
    bool hasPendingCommands() const;

    // Acces la structura pending (pentru procesare din .ino)
    MqttPending& getPending();

private:
    WiFiClientSecure  _net;
    PubSubClient      _client;
    AppPreferences*   _prefs;

    bool          _initialized;
    unsigned long _lastReconnectMs;
    unsigned long _backoffMs;
    unsigned long _lastPublishMs;
    unsigned long _lastHeartbeatMs;
    bool          _lastRelayState[2];

    // Faza 2
    LockOwner     _lockOwner;
    bool          _publishNow;          // flag: publicare forțată
    unsigned long _lockSetAtMs;         // millis() când s-a setat lock-ul
    MqttPending   _mqttPending;

    // Conectare la broker (LWT setup, subscribe). Returnează true la succes.
    bool _connect();

    // Serializare + publish state. Reset throttle counters.
    void _publishStateNow(const VentilationZone& l, const VentilationZone& r);

    // Callback static → instanță (PubSubClient cere C-style function pointer)
    static MqttBridge* _instance;
    static void _staticCallback(char* topic, byte* payload, unsigned int length);
    void _handleMessage(char* topic, byte* payload, unsigned int length);
};

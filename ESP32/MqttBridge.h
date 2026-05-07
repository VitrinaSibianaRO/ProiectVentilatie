#pragma once

// ============================================================
//  MqttBridge.h
//  Wrapper peste PubSubClient + SSLClient + Ethernet pentru
//  comunicația cu HiveMQ Cloud (TLS port 8883).
//
//  Arhitectura: EthernetClient → SSLClient → PubSubClient
//  Cert ISRG Root X1 stocat în PROGMEM (zero RAM)
//  State JSON serializat în StaticJsonDocument (zero heap alloc)
//  Throttle hard 500ms între publicări consecutive
//  Callback MQTT: ZERO String/new — doar set flags
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <PubSubClient.h>

#include "AppPreferences.h"
#include "VentilationZone.h"
#include "HiveMqCert.h"

// Lock owner: cine controlează sistemul în acest moment
// LOCK_BLYNK eliminat — nu mai există interfață Blynk
enum LockOwner { LOCK_NONE = 0, LOCK_MQTT = 2 };

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
    float hystT         = -1.0f;   // -1 = absent în comandă (nu actualizăm)
    float hystH         = -1.0f;

    bool  resetDefaults = false;
    bool  reboot        = false;
    bool  rebootSlave   = false;   // NOU: restart Slave via UART

    bool  getLog        = false;

    // LED control (forwarded to Slave via UART)
    bool  setLedNow     = false;
    uint8_t ledPercent  = 0;        // 0-100%

    bool  setLedSched   = false;
    uint8_t ledOnH      = 0;
    uint8_t ledOnM      = 0;
    uint8_t ledOffH     = 0;
    uint8_t ledOffM     = 0;
    uint8_t ledMaxI     = 0;
    bool    ledSchedEn  = false;

    bool  update        = false;
    char  otaUrl[256]   = {0};
    char  otaSha[65]    = {0};

    // OTA Slave (proxy via Master)
    bool  updateSlave   = false;
    char  slaveOtaUrl[256] = {0};
    char  slaveOtaSha[65]  = {0};
};

class MqttBridge {
public:
    MqttBridge();

    // Inițializare. Apelat o singură dată în setup() după Ethernet init.
    void begin(AppPreferences* prefs);

    // Pump loop. Apelat la fiecare iterație de loop().
    void loop();

    // Stare conexiune
    bool connected();

    // Publicare state cu slave + LED status. Decide intern dacă publică.
    void publishStateIfNeeded(const VentilationZone& l, const VentilationZone& r,
                              bool slaveOnline, int slaveErrors,
                              unsigned long slaveLastSuccessMs,
                              uint8_t ledIntensity, bool ledSchedEnabled);

    // Publicare explicită online/offline (folosit pre-restart).
    void publishOnline(bool online);

    // Lock owner management
    void setLockOwner(LockOwner owner);
    LockOwner getLockOwner() const;

    // Forțează publicare state la următorul publishStateIfNeeded()
    void requestPublishNow();

    // Publicare cmd_rejected pe ventilatie/event
    void publishCmdRejected(const char* reason, const char* by);

    // Publicare event JSON pe ventilatie/event
    void publishEventJson(const char* jsonStr);

    // Publicare log JSON pe ventilatie/log (QoS 1, not retained)
    void publishLog(const char* jsonBuf, size_t len);

    // Verificare dacă există comenzi MQTT pending de procesat
    bool hasPendingCommands() const;

    // Acces la structura pending (pentru procesare din .ino)
    MqttPending& getPending();

private:
    EthernetClient    _baseClient;
    SSLClient         _sslClient;
    PubSubClient      _client;
    AppPreferences*   _prefs;

    bool          _initialized;
    unsigned long _lastReconnectMs;
    unsigned long _backoffMs;
    unsigned long _lastPublishMs;
    unsigned long _lastHeartbeatMs;
    bool          _lastRelayState[2];

    LockOwner     _lockOwner;
    bool          _publishNow;
    unsigned long _lockSetAtMs;
    unsigned long _lastDiagMs;
    MqttPending   _mqttPending;

    // Cached pentru /diag publish (actualizate la publishStateIfNeeded)
    int           _lastSlaveErrors;
    bool          _lastSlaveOnline;

    bool _connect();
    void _publishStateNow(const VentilationZone& l, const VentilationZone& r,
                          bool slaveOnline, int slaveErrors,
                          unsigned long slaveLastSuccessMs,
                          uint8_t ledIntensity, bool ledSchedEnabled);
    void _publishDiag();

    static MqttBridge* _instance;
    static void _staticCallback(char* topic, byte* payload, unsigned int length);
    void _handleMessage(char* topic, byte* payload, unsigned int length);
};

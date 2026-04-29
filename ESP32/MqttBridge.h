#pragma once

// ============================================================
//  MqttBridge.h
//  Wrapper peste PubSubClient + WiFiClientSecure pentru
//  comunicația cu HiveMQ Cloud (TLS port 8883).
//
//  FAZA 1 (read-only): connect, LWT, publishState la heartbeat.
//  Faze ulterioare vor adăuga: callback comenzi, lock owner,
//  pending flags, push imediat la schimbare.
//
//  Reguli design:
//  - TLS persistent (un singur handshake), reconnect cu backoff
//  - Cert ISRG Root X1 stocat în PROGMEM (zero RAM)
//  - State JSON serializat în StaticJsonDocument (zero heap alloc)
//  - Throttle hard 500ms între publicări consecutive
// ============================================================

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "AppPreferences.h"
#include "VentilationZone.h"

class MqttBridge {
public:
    MqttBridge();

    // Inițializare. Apelat o singură dată în setup() după WiFi.connect.
    // Configurează cert TLS, buffer size, callback (faza 2+).
    void begin(AppPreferences* prefs);

    // Pump loop. Apelat la fiecare iterație de loop().
    // Gestionează reconnect cu backoff + client.loop() pentru keepalive.
    void loop();

    // Stare conexiune (true = MQTT autentificat și subscribed)
    bool connected();

    // Publicare state. Decide intern dacă publică (throttle, heartbeat, push imediat).
    // Apelat din loop() main, dar și forțat din alte locuri (FAZA 2+).
    void publishStateIfNeeded(const VentilationZone& l, const VentilationZone& r);

    // Publicare explicită online/offline (folosit pre-restart).
    void publishOnline(bool online);

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

    // Conectare la broker (LWT setup, subscribe). Returnează true la succes.
    bool _connect();

    // Serializare + publish state. Reset throttle counters.
    void _publishStateNow(const VentilationZone& l, const VentilationZone& r);
};

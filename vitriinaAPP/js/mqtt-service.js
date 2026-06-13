/**
 * MqttService — singleton MQTT WebSocket client for vitriina web app.
 * Connects to HiveMQ Cloud via WSS (port 8884).
 */
class MqttService {
  constructor() {
    this.client = null;
    this.isConnected = false;
    this.lastState = null;
    this.lastTvState = null;
    this.lastStateReceivedAt = null;
    this._listeners = {};

    this.settings = {
      host: '264f95b78b1d4733a57c7d0c6e045828.s1.eu.hivemq.cloud',
      port: 8884,
      username: 'ventilatie_app',
      password: 'AppVentil2024!',
      stateTopic:   'ventilatie/state',
      commandTopic: 'ventilatie/cmd',
      onlineTopic:  'ventilatie/online',
      eventTopic:   'ventilatie/event',
      logTopic:     'ventilatie/log',
      tvTopic:      'ventilatie/tv/state',
    };
  }

  // ── Event system ─────────────────────────────────
  on(event, handler) {
    if (!this._listeners[event]) this._listeners[event] = [];
    this._listeners[event].push(handler);
    return () => this.off(event, handler);
  }

  off(event, handler) {
    if (this._listeners[event])
      this._listeners[event] = this._listeners[event].filter(h => h !== handler);
  }

  _emit(event, data) {
    (this._listeners[event] || []).forEach(h => { try { h(data); } catch(e) {} });
  }

  // ── Connection ───────────────────────────────────
  connect() {
    if (this.client) return;
    const { host, port, username, password } = this.settings;
    const clientId = `vitriina_web_${Date.now().toString(16)}`;

    try {
      this.client = mqtt.connect(`wss://${host}:${port}/mqtt`, {
        clientId,
        username,
        password,
        clean: true,
        reconnectPeriod: 4000,
        connectTimeout: 12000,
        keepalive: 30,
      });
    } catch (e) {
      console.error('[MQTT] connect() failed:', e);
      return;
    }

    this.client.on('connect', () => {
      console.log('[MQTT] Connected');
      this.isConnected = true;
      this._emit('onConnectionChanged', true);

      const { stateTopic, onlineTopic, eventTopic, logTopic, tvTopic } = this.settings;
      this.client.subscribe([stateTopic, onlineTopic, eventTopic, logTopic, tvTopic], { qos: 1 }, (err) => {
        if (err) console.error('[MQTT] Subscribe error:', err);
      });

      // Ask ESP32 for fresh state
      this.sendCommand({ cmd: 'refresh' });
    });

    this.client.on('close', () => {
      console.log('[MQTT] Disconnected');
      this.isConnected = false;
      this._emit('onConnectionChanged', false);
    });

    this.client.on('reconnect', () => {
      this._emit('onConnectionChanged', false);
    });

    this.client.on('error', (err) => {
      console.error('[MQTT] Error:', err);
      this.isConnected = false;
      this._emit('onConnectionChanged', false);
    });

    this.client.on('message', (topic, payload) => {
      const str = payload.toString();
      const { stateTopic, onlineTopic, eventTopic, logTopic, tvTopic } = this.settings;

      if (topic === tvTopic) {
        try {
          const tv = JSON.parse(str);
          this.lastTvState = tv;
          this._emit('onTvStateReceived', tv);
        } catch (e) { console.error('[MQTT] tv parse fail:', e); }

      } else if (topic === stateTopic) {
        try {
          const state = JSON.parse(str);
          this.lastState = state;
          this.lastStateReceivedAt = new Date();
          this._emit('onStateReceived', state);
        } catch (e) { console.error('[MQTT] state parse fail:', e); }

      } else if (topic === onlineTopic) {
        this._emit('onOnlineStatusChanged', str);

      } else if (topic === eventTopic) {
        this._emit('onEventReceived', str);

      } else if (topic === logTopic) {
        try {
          const log = JSON.parse(str);
          this._emit('onLogReceived', log);
        } catch (e) {}
      }
    });
  }

  // ── Commands ─────────────────────────────────────
  sendCommand(payload) {
    if (!this.client || !this.isConnected) {
      console.warn('[MQTT] Not connected — command dropped:', payload);
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      this.client.publish(
        this.settings.commandTopic,
        JSON.stringify(payload),
        { qos: 1 },
        (err) => (err ? reject(err) : resolve())
      );
    });
  }

  disconnect() {
    if (this.client) {
      this.client.end(true);
      this.client = null;
      this.isConnected = false;
    }
  }
}

// Singleton exposed to all modules
window.mqttService = new MqttService();

# 07 — Protocol MQTT (referință)

Documentație completă a protocolului MQTT folosit între ESP32 și aplicația MAUI.

## Cuprins

- [1. Topic-uri](#1-topic-uri)
- [2. State JSON (publicat de ESP32)](#2-state-json-publicat-de-esp32)
- [3. Comenzi (publicate de MAUI)](#3-comenzi-publicate-de-maui)
- [4. Online/LWT](#4-onlinelwt)
- [5. Event JSON](#5-event-json)
- [6. Log JSON](#6-log-json)
- [7. Politica de publicare ESP32](#7-politica-de-publicare-esp32)
- [8. Lock activ Blynk ↔ MAUI](#8-lock-activ-blynk--maui)
- [9. Test manual cu mosquitto](#9-test-manual-cu-mosquitto)

---

## 1. Topic-uri

| Topic | Direcție | Retain | QoS | Note |
|---|---|---|---|---|
| `ventilatie/state` | ESP32 → broker | **YES** | 0 | Stare completă; clienții noi primesc imediat |
| `ventilatie/online` | ESP32 (LWT) | **YES** | 1 | `"online"`/`"offline"` |
| `ventilatie/cmd` | MAUI → broker → ESP32 | NO | 1 | Comenzi |
| `ventilatie/event` | ESP32 → broker | NO | 0 | OTA progress, cmd_rejected, alerte |
| `ventilatie/log` | ESP32 → broker | NO | 1 | Răspuns la `cmd:getLog` |

**Conventie retain:**
- `state` și `online` sunt retained → când un client nou se abonează, primește imediat ultima valoare
- `cmd`, `event`, `log` NU sunt retained → mesajele sunt efemere, nu se replay-uiesc

## 2. State JSON (publicat de ESP32)

```json
{
  "left":  {
    "temp": 24.5,
    "hum": 55.2,
    "relay": false,
    "override": false,
    "errs": 0
  },
  "right": {
    "temp": 25.1,
    "hum": 58.7,
    "relay": false,
    "override": false,
    "errs": 0
  },
  "config": {
    "threshT": 45.0,
    "threshH": 60.0,
    "interval": 300,
    "ovrTimeout": 120
  },
  "lock": null,
  "fw": 42,
  "ts": "2026-04-29T14:32:15Z",
  "uptimeSec": 12345,
  "heap": 215000
}
```

### Câmpuri

| Câmp | Tip | Descriere |
|---|---|---|
| `left/right.temp` | float | Temperatură (°C) |
| `left/right.hum` | float | Umiditate (%) |
| `left/right.relay` | bool | Stare releu (true=ON) |
| `left/right.override` | bool | Override manual activ |
| `left/right.errs` | int | Erori consecutive senzor |
| `config.threshT` | float | Prag temperatură (°C) |
| `config.threshH` | float | Prag umiditate (%) |
| `config.interval` | int | Interval citire senzori (s) |
| `config.ovrTimeout` | int | Timeout override (min) |
| `lock` | obj?\|null | `{"owner":"blynk\|mqtt","ageMs":N}` sau `null` |
| `fw` | int | Build number firmware |
| `ts` | string | Timestamp ISO 8601 (NTP) |
| `uptimeSec` | int | Secunde de la ultimul boot |
| `heap` | int | Heap intern liber (bytes) |

## 3. Comenzi (publicate de MAUI)

Toate publicate pe `ventilatie/cmd` cu QoS 1, retain=false.

### Refresh (forțează publish state)

```json
{"cmd":"refresh"}
```

> **Note**: `refresh` bypass-uiește lock-ul (e read-only).

### Set override

```json
{"cmd":"setOverride","zone":"left","value":1}
```

| `value` | Efect |
|---|---|
| `0` | Forțat OFF (releu oprit) |
| `1` | Forțat ON (releu pornit) |
| `2` | Clear (revine la modul automat) |

`zone`: `"left"` sau `"right"`.

### Set config

```json
{"cmd":"setConfig","threshT":45.0,"threshH":60.0,"interval":300}
```

| Câmp | Range | Default |
|---|---|---|
| `threshT` | 0–80 | 45 |
| `threshH` | 0–100 | 60 |
| `interval` | 10–3600 | 300 |

Toate trei sunt obligatorii (batch update).

### Reset (la valori default)

```json
{"cmd":"reset"}
```

Echivalent cu V10 în Blynk.

### Reboot ESP32

```json
{"cmd":"reboot"}
```

ESP32 publică `offline` pe `ventilatie/online`, închide curat conexiunile, apoi `ESP.restart()`.

### Get log

```json
{"cmd":"getLog"}
```

ESP32 răspunde pe `ventilatie/log` (vezi §6).

### OTA update

```json
{
  "cmd": "update",
  "url": "https://github.com/USER/REPO/releases/download/v1.3/firmware.bin",
  "sha256": "a3f5e2c8d1b9..."
}
```

Vezi [05-ota-update.md](05-ota-update.md) pentru detalii.

## 4. Online/LWT

### Mesaje pe `ventilatie/online`

```
"online"     ← publicat retained de ESP32 imediat după conectare
"offline"    ← publicat automat de broker dacă ESP32 cade (LWT)
              ← sau publicat explicit înainte de reboot intenționat
```

### MAUI logic

```csharp
mqtt.OnEspOnlineChanged += (online) => {
    OnlineIndicator = online ? "🟢 ESP32 ONLINE" : "🔴 ESP32 OFFLINE";
};
```

## 5. Event JSON

Publicat pe `ventilatie/event` (NU retained, QoS 0). Mesaje fire-and-forget.

### OTA progress

```json
{"event":"ota_progress","pct":40}
{"event":"ota_progress","pct":80}
{"event":"ota_done"}
{"event":"ota_failed","reason":"sha_mismatch"}
{"event":"ota_failed","reason":"download_error"}
{"event":"ota_failed","reason":"url_not_allowed"}
{"event":"ota_failed","reason":"insufficient_space"}
```

### Comandă respinsă (lock activ)

```json
{"event":"cmd_rejected","reason":"locked","by":"blynk"}
```

### Alerte (extensibil)

```json
{"event":"sensor_critical","zone":"left","errs":15}
{"event":"heap_low","heap":25000}
```

## 6. Log JSON

Publicat pe `ventilatie/log` ca răspuns la `{"cmd":"getLog"}`.

```json
{
  "entries": [
    {
      "ts": "2026-04-29T14:32:15Z",
      "type": "sensor_err",
      "zone": "left",
      "msg": "5 erori DHT"
    },
    {
      "ts": "2026-04-29T15:01:02Z",
      "type": "relay_change",
      "zone": "right",
      "msg": "ON auto"
    },
    {
      "ts": "2026-04-29T17:01:02Z",
      "type": "override_expired",
      "zone": "left",
      "msg": "Override anulat"
    }
  ]
}
```

### Tipuri de evenimente

| `type` | Descriere |
|---|---|
| `sensor_err` | `consecutiveErrors >= 5` pe un DHT22 |
| `relay_change` | Tranziție ON/OFF a unui releu (auto sau override) |
| `override_expired` | Override manual a expirat după timeout (default 2h) |

Maxim 50 entries (circular în NVS).

## 7. Politica de publicare ESP32

ESP32 publică pe `ventilatie/state` în EXACT aceste cazuri:

1. **Heartbeat 1h** (macro `MQTT_HEARTBEAT_MS = 3 600 000`)
2. **`cmd:refresh`** — forțat imediat
3. **După aplicarea unei comenzi Blynk** (push imediat ~70ms)
4. **După aplicarea unei comenzi MQTT** (push imediat)
5. **Lock activate / deactivate** (push imediat)
6. **Schimbare automată stare releu** (când senzorii declanșează ON/OFF)

**Throttle hard**: minim 500ms între publicări consecutive (anti-spam).

**De ce nu publicare la fiecare ciclu de senzori?**
- Economie traficul MQTT (free tier HiveMQ are limite)
- UI-urile afișează "Actualizat acum X min" — utilizatorul apasă Refresh dacă vrea valori proaspete
- Pentru sensor data în timp real, folosește `cmd:refresh` sau scurtează `interval`

## 8. Lock activ Blynk ↔ MAUI

### Stare în JSON state

```json
"lock": {"owner": "blynk", "ageMs": 45}
```

| `owner` | Semnificație |
|---|---|
| `null` | Niciun control activ — ambele canale pot scrie |
| `"blynk"` | Blynk a făcut o comandă recent — MAUI primește lock |
| `"mqtt"` | MAUI/MQTT a făcut o comandă recent — Blynk primește lock |

### Flow timing

```
t=0    : Blynk modifică prag temp
t=10ms : ESP32 publică state cu lock={"owner":"blynk"}
t=20ms : MAUI primește, banner "🔒 Control blocat (Blynk)"
t=50ms : ESP32 procesează → aplică schimbarea → publish Blynk
t=70ms : ESP32 publică state cu lock=null
t=90ms : MAUI banner dispare
```

### Rejection (canalul blocat încearcă să scrie)

ESP32 publică pe `ventilatie/event`:

```json
{"event":"cmd_rejected","reason":"locked","by":"blynk"}
```

Blynk afișează notificare push: `cmd_rejected`.

## 9. Test manual cu mosquitto

### Subscribe la toate topic-urile

```bash
mosquitto_sub -h 1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud \
  -p 8883 --capath /etc/ssl/certs \
  -u ventilatie_app -P 'PAROLA' \
  -t 'ventilatie/#' -v
```

### Trimite comandă

```bash
mosquitto_pub -h 1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud \
  -p 8883 --capath /etc/ssl/certs \
  -u ventilatie_app -P 'PAROLA' \
  -t ventilatie/cmd -q 1 \
  -m '{"cmd":"refresh"}'
```

### Test setOverride

```bash
mosquitto_pub -h ... -t ventilatie/cmd -q 1 \
  -m '{"cmd":"setOverride","zone":"left","value":1}'

# Așteaptă state nou cu left.relay=true, left.override=true
```

### Test setConfig

```bash
mosquitto_pub -h ... -t ventilatie/cmd -q 1 \
  -m '{"cmd":"setConfig","threshT":40.0,"threshH":55.0,"interval":60}'
```

### Curăță retained (debug)

```bash
# Șterge state retained (publish gol cu retain=true)
mosquitto_pub -h ... -t ventilatie/state -r -m ''
```

> ⚠️ Nu face asta în producție — pierzi state-ul curent până la următorul publish.

## Diagrame timing

### Comandă MAUI happy path

```
MAUI                Broker                ESP32
 │                    │                     │
 ├──setOverride──────→│                     │
 │                    ├──delivery──────────→│
 │                    │                     ├─ set lockOwner=mqtt
 │                    │                     ├─ pending.ovrLeft=1
 │                    │                  ┌──┤
 │                    │                  │  ├─ publishStateNow (lock={mqtt})
 │←──state(lock=mqtt)─┤←──────────────────┤
 │                    │                     │
 │ banner "🔒"        │                     │
 │                    │                     ├─ processZones()
 │                    │                     ├─ apply pending (NVS, relay)
 │                    │                     ├─ Blynk.virtualWrite
 │                    │                     ├─ lockOwner=NONE
 │                    │                     ├─ publishStateNow (lock=null)
 │←──state(lock=null)─┤←────────────────────┤
 │                    │                     │
 │ banner dispare     │                     │
```

### OTA flow

```
MAUI               Broker             ESP32                GitHub
 │                   │                  │                    │
 ├──cmd:update──────→│                  │                    │
 │                   ├─delivery────────→│                    │
 │                   │                  ├─ valid url?        │
 │                   │                  ├─ setInsecure       │
 │                   │                  ├──HTTPS GET────────→│
 │                   │                  │←─bytes────────────│
 │                   │                  ├─ Update.write +    │
 │                   │                  │  SHA-256 update    │
 │                   │                  ├─ event(pct=10)     │
 │←──event(pct=10)───┤←─────────────────┤                    │
 │                   │                  │   ... loop ...     │
 │                   │                  ├─ verify SHA-256    │
 │                   │                  ├─ event(ota_done)   │
 │←──event(done)─────┤←─────────────────┤                    │
 │                   │                  ├─ publishOnline(false)
 │                   │                  ├─ ESP.restart()     │
 │←──online=offline─-┤←─────────────────┤                    │
 │ ... reboot ...    │                  ├─ boot              │
 │                   │                  ├─ publishOnline(true)
 │←──online=online───┤                  │                    │
```

Pentru probleme, vezi [08-troubleshooting.md](08-troubleshooting.md).

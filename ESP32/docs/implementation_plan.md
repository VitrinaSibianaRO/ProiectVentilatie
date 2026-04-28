# Plan v5 (FINAL): ProiectVentilatie — HiveMQ paralel cu Blynk, ESP32-PICO-V3-02

## Context

**Hardware**: GroundStudio Carbon V3 cu **ESP32-PICO-V3-02**. Dual-core 240MHz, **520KB SRAM intern + 2MB PSRAM**, 8MB Flash, USB-C cu CH340C, RGB LED onboard.

**Stare cod actuală**:
- ESP32: firmware Arduino/C++ care folosește **Blynk exclusiv**. Logică releelor 100% locală (`VentilationZone`), parametri în NVS (`AppPreferences`). Watchdog 60s, restart preventiv heap<30KB, override timeout 2h. Pinout existent **NU se modifică** (13/15/19/26/32/2/4).
- MAUI: schelet HiveMQtt cu credențiale placeholder. Modelul nealiniat cu Blynk-ul real. Settings/System pages stub-uri.
- Aplicația Blynk pe telefon — funcțională, NU se atinge.

**Cerință**: Adăugare **HiveMQ MQTT pe ESP32 în paralel cu Blynk**, completare MAUI app la nivel production-ready, cu:
- Toată automatizarea pe ESP32; UI-urile = view + remote control
- Memory management strict (chiar dacă avem PSRAM)
- Throttling MQTT (publish doar la refresh / heartbeat 1h / 4 evenimente specifice)
- Save MAUI diff-based (doar dacă s-a modificat ceva)
- Lock activ Blynk↔MAUI (până la prima publicare state)
- Sync MAUI la connect din state retained
- UI ago timestamp ("acum 5 min")
- OTA via GitHub releases cu SHA-256
- NTP sync pentru timestamp-uri reale
- NVS event log (50 evenimente: sensor error, relay change, override expired)
- Tab nou MAUI "Log evenimente"

---

## A. Configurare build ESP32

### Activare PSRAM
- În Arduino IDE: Tools → Board → "ESP32 PICO-D4" (sau "ESP32 Dev Module" cu PSRAM enabled)
- Tools → PSRAM: **Enabled**
- Partition Scheme: **Default 4MB with spiffs** (sau "Minimal SPIFFS (1.9MB APP/ 190KB SPIFFS)" pentru mai mult APP)
- Tools → Flash Size: **8MB**
- Tools → CPU Frequency: 240MHz

### Buget memorie cu PSRAM activ

| Componentă | RAM intern | PSRAM |
|---|---|---|
| Blynk + Wi-Fi stack | ~30KB | — |
| WiFiClientSecure (TLS, cert PROGMEM) | ~30–35KB | — |
| PubSubClient (buffer 1024B) | ~3KB | — |
| ArduinoJson static docs | ~1KB | — |
| **Heap intern target** | **>200KB** | **>1.5MB** |

**Reguli ferme** (chiar cu PSRAM):
- Cert TLS în `PROGMEM`
- Buffer-e critice TLS+MQTT în RAM intern (latență mică, fără cache miss PSRAM)
- ZERO `String`/`new` în callback MQTT — doar set flags
- Buffer OTA download: pe PSRAM (acceptăm latență mai mare la download)
- Event log: NVS direct (nu memory-resident)

---

## B. Pinout — neschimbat

```cpp
// Existent — NU se modifică
DHT_LEFT_PIN     = 19   DHT_RIGHT_PIN    = 32
RELAY_LEFT_PIN   = 15   RELAY_RIGHT_PIN  = 26
RESET_BUTTON_PIN = 13   LED_PIN          = 2   LED_ENABLE_PIN = 4
```

⚠️ **GPIO15 e strapping** — funcționează în firmware-ul existent (ordinea `digitalWrite HIGH apoi pinMode OUTPUT`). Nu schimbăm nimic.

**MQTT/OTA nu necesită pini noi** — totul prin Wi-Fi.

---

## C. Topic-uri MQTT și retain semantics

| Topic | Direcție | Retain | QoS | Note |
|---|---|---|---|---|
| `ventilatie/state` | ESP32 → broker | YES | 0 | Stare completă; clienții noi primesc imediat |
| `ventilatie/online` | ESP32 (LWT) | YES | 1 | `"online"`/`"offline"` |
| `ventilatie/cmd` | MAUI → broker → ESP32 | NO | 1 | Comenzi |
| `ventilatie/event` | ESP32 → broker | NO | 0 | OTA progress, cmd_rejected, alerte |
| `ventilatie/log` | ESP32 → broker | NO | 1 | Răspuns la `cmd:getLog` (JSON array) |

---

## D. Politica de publicare ESP32

State publicat în EXACT aceste cazuri:

1. **Heartbeat 1h** (macro `MQTT_HEARTBEAT_MS = 3 600 000`)
2. **`cmd:refresh`** — forțat
3. **După aplicarea unei comenzi Blynk** (push imediat)
4. **După aplicarea unei comenzi MQTT** (push imediat)
5. **Lock activate / deactivate** (push imediat — pentru sync UI lock)
6. **Schimbare automată stare releu** (când senzorii declanșează ON/OFF)

Throttle hard min 500ms între publicări consecutive.

---

## E. Lock activ — Blynk ↔ MAUI

### Stare în JSON state

```json
{
  "left": {...}, "right": {...}, "config": {...},
  "lock": {"owner": "blynk", "ageMs": 45},
  "ts": "2026-04-29T14:32:15Z",
  "uptimeSec": 12345
}
```

### Flow

1. Comandă primită → `lockOwner = caller` + `publishStateNow = true`
2. Main loop publică imediat state cu `lock.owner` setat (~10ms)
3. Comanda procesată în `processZones()` (declanșat imediat dacă pending) (~50ms)
4. La sfârșit `processZones()`: `lockOwner = NONE` + `publishStateNow = true`
5. Main loop publică state cu `lock = null` (~70ms)

**Rejection**: dacă vine cmd de pe canal blocat, ESP32 publică pe `ventilatie/event`:
```json
{"event":"cmd_rejected","reason":"locked","by":"blynk"}
```

**Blynk indicator lock**: nou virtual pin `VP_LOCK_OWNER = V22` (0=none, 1=blynk, 2=mqtt).

**MAUI indicator lock**: banner "🔒 Control blocat (Blynk activ)" + butoane Save/Toggle disabled cât timp `lock.owner == "blynk"`.

---

## F. Comenzi MQTT acceptate

```json
{"cmd":"refresh"}                                         // bypass lock
{"cmd":"setOverride","zone":"left|right","value":0|1|2}   // 0=OFF, 1=ON, 2=clear
{"cmd":"setConfig","threshT":45.0,"threshH":60.0,"interval":300}
{"cmd":"reset"}                                           // reset to defaults
{"cmd":"reboot"}
{"cmd":"getLog"}                                          // → publish on ventilatie/log
{"cmd":"update","url":"https://...","sha256":"abc..."}    // OTA
```

---

## G. OTA via GitHub releases

### Flow user
1. User build firmware local → `.bin`
2. Upload pe GitHub release: `https://github.com/<user>/<repo>/releases/download/vX.Y/firmware.bin`
3. Calculează SHA-256: `sha256sum firmware.bin`
4. Trimite din MAUI sau orice MQTT client:
```json
{"cmd":"update","url":"<github_url>","sha256":"<hash>"}
```

### Flow ESP32
1. Verifică url începe cu `https://github.com/` sau `https://objects.githubusercontent.com/` (whitelist)
2. `HTTPClient` + `WiFiClientSecure::setInsecure()` — acceptat pentru că **SHA-256 garantează integritatea**
3. Buffer download în PSRAM
4. `Update.begin(contentLength)` → write chunks → `Update.end()`
5. Verifică SHA-256 calculat pe parcurs vs hash așteptat
6. Dacă match: publish `{"event":"ota_done"}` + `mqtt.publishOnline(false)` + `ESP.restart()`
7. Dacă mismatch: `Update.abort()` + publish `{"event":"ota_failed","reason":"sha_mismatch"}`

### Progress reporting

În timpul download, publică pe `ventilatie/event` la fiecare 10%:
```json
{"event":"ota_progress","pct":40}
```

### MAUI UI

System page → buton "Update firmware" cu Entry pentru URL și SHA-256, sau pre-completat din clipboard. Progress bar pe baza mesajelor de pe `ventilatie/event`.

---

## H. NTP + NVS event log

### NTP

- `configTzTime("EET-2EEST,M3.5.0/3,M10.5.0/4", "pool.ntp.org", "time.google.com")` (Europa/București)
- Sync la boot (după Wi-Fi connect)
- Re-sync periodic la 24h
- Dacă sync fail: timestamp = `boot_count + uptime_ms` ca fallback

### Event log în NVS

Structură compactă (entry = 32 bytes):
```cpp
struct LogEntry {
    uint32_t epochSec;       // ts (sau boot count dacă !NTP)
    uint8_t  type;           // 0=sensor_err, 1=relay_change, 2=override_expired
    uint8_t  zone;           // 0=left, 1=right, 0xFF=N/A
    uint8_t  data[26];       // payload mic (text scurt)
};
```

Circular buffer 50 entries × 32B = 1.6KB total în NVS namespace `"log"`.
Index circular în NVS key `"logIdx"`.

### Tipuri de evenimente loghate

| Tip | Trigger | Payload |
|---|---|---|
| `sensor_err` | `consecutiveErrors >= 5` | "5 erori DHT" |
| `relay_change` | Tranziție ON/OFF | "ON auto" / "OFF override" / etc |
| `override_expired` | Timeout 2h | "Override stânga anulat" |

### Comanda `getLog`

ESP32 răspunde pe `ventilatie/log`:
```json
{
  "entries":[
    {"ts":"2026-04-29T14:32:15Z","type":"sensor_err","zone":"left","msg":"5 erori DHT"},
    {"ts":"2026-04-29T15:01:02Z","type":"relay_change","zone":"right","msg":"ON auto"},
    ...
  ]
}
```

QoS 1, NOT retained. Mesaj poate fi mare (~3KB pentru 50 entries) — split nu e necesar (PubSubClient buffer 1024 + chunk).

**Atenție**: 3KB > buffer 1024B. Soluții:
- Mărim buffer la 4096B (cost: ~4KB RAM intern) — preferat dat fiind PSRAM
- Sau trimitem entries pe rând (50 mesaje mici)

**Decizie**: setBufferSize(4096) pentru a permite log payload mare.

---

## I. MAUI — sincronizare last sent + diff Save

`MqttService` la connect:
- Subscribe `ventilatie/state` retained → primește imediat ultima stare
- Populare `LastState` → ViewModels primesc evenimentul `OnStateReceived`
- `_lastReceivedConfig` = `LastState.Config`

`SettingsViewModel`:
- `HasChanges` = (TempThreshold, HumThreshold, IntervalSec) != `_lastReceivedConfig`
- Save enabled doar dacă `HasChanges && !IsLocked && IsConnected`
- După Save → next state primit → `_lastReceivedConfig` updated → `HasChanges = false`

---

## J. Stabilitate 24/7

| Risc | Mitigare |
|---|---|
| Hang firmware | Watchdog 60s (existent) |
| Memorie low | Restart preventiv heap intern <30KB (existent) |
| WiFi pierdut | Restart preventiv după 10 min fără WiFi (NOU, `WIFI_DOWN_RESTART_MS=600000`) |
| MQTT broker down | Reconnect backoff 5s→60s; sistem autonom (releele decise local) |
| Blynk down | Idem (existent) |
| Override blocat ON | Timeout 2h existent + log eveniment |
| NVS wear | Scrieri NVS doar la schimbări reale config/override + log circular | 
| OTA întreruptă | `Update.abort()` + restart curat | 
| TLS cert expirat | ISRG Root X1 valid până 2035 |
| OTA URL malicious | Whitelist `github.com` + SHA-256 verification |
| Timestamp drift | Re-sync NTP la 24h |

Pre-restart intenționat (reboot, OTA): `mqtt.publishOnline(false); delay(200);` pentru clean LWT.

---

## K. UI MAUI — actualizări

### Dashboard
- Gauge-uri (existente)
- Label "Actualizat acum X min" cu cod culoare:
  - Verde <70min, Galben <180min, Roșu >180min
- Badge "ESP32 ONLINE/OFFLINE" (din `ventilatie/online`)
- Banner "🔒 Control blocat (Blynk)" când lock activ
- Toggle butoane: ACTIV (auto)/ ACTIV (override)/ INACTIV (auto)/ INACTIV (override)

### Settings
- 3 sliders: TempThreshold (20–60°C), HumThreshold (30–100%), IntervalSec (10–3600s, pas 10)
- Save button enabled doar dacă HasChanges && !IsLocked && IsConnected
- Reset to defaults cu confirmare

### System
- Status broker (host, port, conectat/deconectat)
- Status ESP32 (online/offline, uptime, heap)
- Erori senzori per zonă
- Last update ago
- Buton **Refresh** (cmd:refresh)
- Buton **Reboot ESP32** cu confirmare (cmd:reboot)
- Buton **Reset defaults** cu confirmare (cmd:reset)
- Sectiune **OTA Update**:
  - Entry URL (preset gol)
  - Entry SHA-256
  - Buton "Trimite update" (trimite cmd:update)
  - Progress bar bazat pe mesaje `ventilatie/event` cu type ota_progress

### Log evenimente (TAB NOU)
- Buton "Reîncarcă" (trimite cmd:getLog)
- ListView cu evenimente:
  - Ts (datetime local), tip cu icon (eroare = ⚠️, relay = 🔌, override = ⏱️), zonă, mesaj
- Filtru pe tip
- Auto-load la prima deschidere

---

## L. Fișiere modificate / create

### ESP32 (`/ESP32/`)

| Fișier | Acțiune |
|---|---|
| `Config.h` | EDIT: `MQTT_HOST/PORT/USER/PASS`, `TOPIC_*`, `MQTT_BUF_SIZE=4096`, `MQTT_HEARTBEAT_MS=3600000`, `MQTT_PUBLISH_MIN_INTERVAL_MS=500`, `WIFI_DOWN_RESTART_MS=600000`, `VP_LOCK_OWNER=V22`, declarație extern cert |
| `HiveMqCert.h` | CREATE: cert ISRG Root X1 în PROGMEM |
| `MqttBridge.h` | CREATE: class declaration |
| `MqttBridge.cpp` | CREATE: connect/loop/publish/callback (zero alocări dinamice în callback) |
| `EventLog.h` | CREATE: API circular log în NVS (init/append/dumpJson) |
| `EventLog.cpp` | CREATE: implementare circular buffer 50×32B |
| `OtaUpdater.h` | CREATE: API simplu (start, progress callback) |
| `OtaUpdater.cpp` | CREATE: HTTPClient + Update + SHA-256 streaming |
| `TimeSync.h` | CREATE: wrapper NTP cu re-sync 24h |
| `ProiectVentilatie.ino` | EDIT: init MqttBridge/EventLog/TimeSync, loop pump, lock management, push triggers, WiFi restart preventiv, BLYNK_WRITE handlers verifică lock, log apeluri la sensor_err/relay_change/override_expired |

**Library noi Arduino**: PubSubClient v2.8+, ArduinoJson v7+, (Update și HTTPClient deja incluse în Arduino-ESP32)

### MAUI (`/MobileApp/`)

| Fișier | Acțiune |
|---|---|
| `appsettings.json` | CREATE |
| `Models/MqttSettings.cs` | CREATE |
| `Models/VentilationState.cs` | EDIT (lock, ts, uptimeSec, heap) |
| `Models/LogEntry.cs` | CREATE |
| `Services/IMqttService.cs` | EDIT (LastState, LastStateReceivedAt, IsConnected, IsEspOnline, OnEspOnlineChanged, OnLogReceived, OnEventReceived, DisconnectAsync) |
| `Services/MqttService.cs` | REWRITE (config DI, backoff, lifecycle, retained) |
| `ViewModels/DashboardViewModel.cs` | EDIT (RelayText, override, ago timer, lock banner, online indicator) |
| `ViewModels/SettingsViewModel.cs` | CREATE (diff-based Save) |
| `ViewModels/SystemViewModel.cs` | CREATE (status, erori, OTA section) |
| `ViewModels/LogViewModel.cs` | CREATE (cmd getLog, filter, list) |
| `Views/SettingsPage.xaml` + `.cs` | REWRITE |
| `Views/SystemPage.xaml` + `.cs` | REWRITE |
| `Views/LogPage.xaml` + `.cs` | CREATE |
| `Views/DashboardPage.xaml` | EDIT |
| `AppShell.xaml` | EDIT (adaugă tab `Log`) |
| `MauiProgram.cs` | EDIT |
| `App.xaml.cs` | EDIT (OnSleep/OnResume) |
| `ProiectVentilatie.Mobile.csproj` | EDIT (EmbeddedResource + ConfigurationJson) |

---

## M. Setup manual înainte de testare

1. **HiveMQ Cloud Console** (https://console.hivemq.cloud):
   - Useri: `ventilatie_esp32` și `ventilatie_app` (parole separate)
   - ACL: ambii pe `ventilatie/#` publish+subscribe
2. Completează parolele:
   - `ESP32/Config.h` → `MQTT_USER`, `MQTT_PASS`
   - `MobileApp/appsettings.json` → `Username`, `Password`
3. Arduino IDE:
   - Install lib **PubSubClient** v2.8+
   - Install lib **ArduinoJson** v7+
   - Board: **ESP32 PICO-D4** (sau ESP32 Dev Module), PSRAM=Enabled, Flash=8MB, Partition=Default 4MB
4. GitHub repo pentru OTA (opțional la primul deploy): repo public sau private cu releases

---

## N. Verificare end-to-end

### Memorie ESP32
- Serial: `[Sistem] Heap intern liber: ... PSRAM liber: ...` → intern >200KB, PSRAM >1.5MB după boot

### MQTT economy
- 1h idle: maxim 1 mesaj heartbeat + tranziții releu
- Refresh manual: 1 mesaj
- Schimbare config MAUI: 2 mesaje (lock activ → lock null)

### Lock conflict
- Modificare în Blynk → MAUI banner "🔒" pentru ~100ms
- Save în MAUI cu lock Blynk → rejected, vizibil pe `ventilatie/event`

### Diff-based Save
- Settings deschis fără modificări → Save disabled
- Slider modificat → Save enabled
- Save reușit → Save redevine disabled la primul state primit

### Sync după restart MAUI
- Force kill app → reopen → state populat imediat din retained

### NTP + Log
- Boot: serial arată "NTP OK 2026-04-29 14:32"
- Generare evenimente test (deconectează DHT pentru sensor_err) → cmd:getLog → MAUI afișează listă

### OTA
- Build firmware nou local → upload GitHub release
- MAUI System → OTA → URL + SHA → Trimite
- Progress bar 0→100% → ESP32 reboot → state retained nou cu uptimeSec=0

### Stabilitate 24/7
- 48h: heap stabil, log fără crash, NVS scrieri rezonabile
- WiFi off 11 min → restart preventiv

### Build
- Arduino: compile clean cu ESP32 PICO-D4 + PSRAM
- MAUI: `dotnet build -f net10.0-android -c Release`
- `adb install -r APK/com.proiect.ventilatie-Signed.apk`

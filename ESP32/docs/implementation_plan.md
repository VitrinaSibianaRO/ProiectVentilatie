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

## FAZE DE IMPLEMENTARE

Fiecare fază e independentă și **testabilă end-to-end**. Trecerea la următoarea fază doar după validarea celei curente.

### Faza -1 — Documentație setup (1–2h)

**Scop**: înainte de orice compilare, scriem documentație clară pentru setup în `docs/`. Aceasta servește și ca referință pentru fazele următoare.

**Fișiere create în `/docs/` (toate Markdown):**

1. **`docs/README.md`** — index/overview
   - Ce este proiectul (1 paragraf)
   - Tabel cu link-uri către celelalte docs (HiveMQ, Blynk, ESP32 build, MAUI build, OTA, Versionare, Troubleshooting)
   - Diagramă text simplă: ESP32 ← (Wi-Fi) → [Blynk Cloud + HiveMQ Cloud] ← MAUI app

2. **`docs/01-hivemq-setup.md`** — configurare HiveMQ Cloud
   - Cum se accesează consola (https://console.hivemq.cloud)
   - Pas cu pas: creare 2 useri (`ventilatie_esp32`, `ventilatie_app`)
   - Configurare ACL (publish+subscribe pe `ventilatie/#`)
   - Unde se completează parolele: `ESP32/Config.h` și `MobileApp/appsettings.json`
   - Cum se testează cu HiveMQ Web Client (subscribe `ventilatie/#`)
   - Parametri cluster: host, port 8883 (TLS), 8884 (websocket)

3. **`docs/02-blynk-setup.md`** — configurare Blynk app
   - Conținutul §M.0 din plan (datastreams V1-V23, widgets, events)
   - Screenshots placeholder (TODO: utilizatorul adaugă)
   - Cum se obține `BLYNK_AUTH_TOKEN` (deja existent în Config.h, doar dacă vrea schimbare)

4. **`docs/03-esp32-build.md`** — compilare și flash firmware ESP32
   - Cerințe: Arduino IDE 2.x sau arduino-cli
   - Board manager: ESP32 Arduino Core (URL JSON pentru install)
   - Selectare board: `ESP32 PICO-D4`
   - Settings: PSRAM=Enabled, Flash=8MB, Partition=Default 4MB, CPU=240MHz
   - Libraries de instalat: `PubSubClient` v2.8+, `ArduinoJson` v7+ (DHT și NeoPixel deja folosite)
   - Comandă bump build number înainte de compilare: `bash ESP32/scripts/bump_build.sh`
   - Comandă compile + upload (arduino-cli)
   - Verificare Serial Monitor (115200 baud) — output expected la boot
   - Note despre ordinea pinilor (15 strapping pin) — NU se modifică

5. **`docs/04-maui-build.md`** — build & install app MAUI
   - Cerințe: .NET 10 SDK, Android workload (`dotnet workload install maui-android`)
   - Setup development: VS Code / Visual Studio 2026+ / `dotnet build`
   - Comandă build APK Release: `dotnet build -f net10.0-android -c Release`
   - Cum se semnează APK (folosind `ventilatie.keystore` existent)
   - Install pe device: `adb install -r APK/com.proiect.ventilatie-Signed.apk`
   - Notă: `ApplicationVersion` se incrementează automat la fiecare build (vezi `06-versioning.md`)
   - Verificare prima rulare: connection settings, ce trebuie să apară

6. **`docs/05-ota-update.md`** — workflow OTA
   - Build firmware nou local
   - Calcul SHA-256: `sha256sum firmware.bin`
   - Upload pe GitHub release (cum se creează release pe github)
   - Configurare în MAUI System page: URL repo, versiune, filename, SHA
   - Click "Trimite update" → progress bar
   - Rollback: cum se face dacă noul firmware are bugs (re-flash USB)
   - Format URL: `https://github.com/<user>/<repo>/releases/download/<tag>/<file>.bin`

7. **`docs/06-versioning.md`** — strategie versionare
   - ESP32: build number simplu, auto-incrementat de `bump_build.sh`
   - MAUI: SemVer manual (`ApplicationDisplayVersion`) + build auto (`ApplicationVersion`)
   - Cum se inițializează la primul build (creare `build_number.txt`)
   - Cum se modifică `ApplicationDisplayVersion` în csproj la release-uri majore
   - Sync versiune fw în MQTT state JSON și Blynk V23

8. **`docs/07-mqtt-protocol.md`** — protocol referință
   - Tabel topic-uri cu retain/QoS (din §C)
   - Format JSON state (din §E)
   - Comenzi disponibile cu exemple (din §F)
   - Format JSON log
   - Format JSON event (cmd_rejected, ota_progress, ota_done, ota_failed)
   - Cum se testează manual cu HiveMQ Web Client / mosquitto_pub

9. **`docs/08-troubleshooting.md`** — probleme comune
   - ESP32 nu se conectează la HiveMQ:
     - Verifică credențiale Config.h
     - Verifică certificat ISRG (data sistem corectă, NTP sync)
     - Output Serial relevant
   - MAUI nu vede state retained:
     - Verifică `appsettings.json` embedded
     - Verifică conexiune Wi-Fi telefon
     - Force kill app + reopen
   - Override blocat ON > 2h:
     - Override timeout reset prin Settings → Reset
   - Heap scădere progresivă (memory leak):
     - Verifică `[Sistem] Heap liber:` în Serial → dacă scade <30KB → restart preventiv
   - OTA failed `sha_mismatch`:
     - Recalculează SHA-256 cu `sha256sum`
     - Verifică că URL-ul e accesibil HTTPS
   - Lock activ permanent:
     - Force restart ESP32 (buton fizic 3s) → reset NVS → re-config

10. **`docs/09-quick-start.md`** — checklist primul deploy
    - Lista numerotată cu toți pașii primei configurări (de la 0):
      1. Cont HiveMQ + cluster (link)
      2. Creare useri și ACL
      3. Completare credențiale Config.h + appsettings.json
      4. Build ESP32 + flash
      5. Verificare Blynk app
      6. Build MAUI + install
      7. Test conexiune end-to-end
    - Indicii vizuale (LED-uri ESP32) pentru fiecare etapă

**Reguli pentru toate fișierele:**
- Limba: română (consistent cu codul)
- Format: GitHub-flavored Markdown
- Code blocks cu language hint (```bash, ```cpp, ```xml, ```json, ```csharp)
- Link-uri relative către alte docs din același folder
- TOC la început pentru fișiere >100 linii

**Validare**: după ce sunt scrise, un utilizator nou ar trebui să poată deploy-a sistemul în <2h fără ajutor extern doar urmând `docs/09-quick-start.md`.

---

### Faza 0 — Setup & infrastructure (1–2h)

**Scop**: pregătire totul în afara codului ESP32/MAUI.

**Pași:**
1. HiveMQ Cloud Console: creează 2 useri (`ventilatie_esp32`, `ventilatie_app`) cu ACL pe `ventilatie/#`
2. Blynk Console: adaugă datastreams V22 (LockOwner) + V23 (FwBuild) + event `cmd_rejected` (vezi §M.0)
3. Versionare:
   - Creează `ESP32/scripts/bump_build.sh` (vezi §L.1)
   - Creează `ESP32/build_number.txt` (init `0`)
   - Adaugă `.gitignore`: `ESP32/build_number.txt`, `ESP32/Version.h`, `MobileApp/build_number.txt`
   - Adaugă MSBuild target în `ProiectVentilatie.Mobile.csproj`
4. Arduino IDE: instalează **PubSubClient** v2.8+ și **ArduinoJson** v7+; setează board `ESP32 PICO-D4`, PSRAM=Enabled, Flash=8MB
5. Completează parolele HiveMQ în `Config.h` (ESP32) și `appsettings.json` (MAUI — fișier creat în Faza 4)

**Validare**: HiveMQ web client conectat cu `ventilatie_app`, abonat pe `ventilatie/#`, vede mesajele de test.

---

### Faza 1 — ESP32 MQTT bridge read-only (3–4h)

**Scop**: ESP32 publică state pe HiveMQ + LWT, fără să accepte comenzi încă.

**Fișiere:**
- `ESP32/HiveMqCert.h` (CREATE) — cert ISRG Root X1 PROGMEM
- `ESP32/Config.h` (EDIT) — adaugă macro-uri MQTT, topic-uri, includ Version.h
- `ESP32/MqttBridge.h` + `.cpp` (CREATE) — versiune minimală: connect, LWT, publishState, heartbeat 1h
- `ESP32/ProiectVentilatie.ino` (EDIT) — init mqtt în setup, mqtt.loop() + publishStateIfNeeded în loop()

**Validare:**
- Serial: `[MQTT] Conectat la HiveMQ`
- HiveMQ web client: vede `online` retained pe `ventilatie/online`
- Vede primul state JSON pe `ventilatie/state` cu valorile reale ale senzorilor
- După reboot ESP32: vede `offline` apoi `online`
- Heap intern >200KB după boot

**Atenție**: Blynk nu trebuie afectat — verifică că funcționează în paralel.

---

### Faza 2 — ESP32 commands + Blynk sync + lock (4–6h)

**Scop**: ESP32 acceptă comenzi MQTT, le sincronizează cu Blynk, gestionează lock-ul.

**Fișiere:**
- `ESP32/MqttBridge.h/.cpp` (EDIT) — adaugă callback, pending flags, lockOwner, publishCmdRejected
- `ESP32/ProiectVentilatie.ino` (EDIT):
  - Procesare pending MQTT în `processZones()` (vezi §I)
  - Sync `Blynk.virtualWrite` după aplicare
  - Trigger `processZones()` din loop dacă pending != empty
  - Lock release la sfârșit de processZones
  - BLYNK_WRITE handlers verifică lock (revert UI dacă MAUI activ)
  - VP_LOCK_OWNER setat la `BLYNK_CONNECTED`

**Validare:**
- Trimite `{"cmd":"refresh"}` din HiveMQ web client → primești state nou imediat
- `{"cmd":"setOverride","zone":"left","value":1}` → releu pornește, Blynk app afișează override ON
- `{"cmd":"setConfig","threshT":40,"threshH":55,"interval":60}` → Blynk afișează valorile noi în slidere
- Push imediat după fiecare cmd: vezi state cu `lock.owner=mqtt` apoi `lock=null`
- Modifică prag în Blynk → MAUI mock primește state nou cu valoarea actualizată în <1s
- Modifică prag în Blynk în timp ce un cmd MQTT e pending → primești `cmd_rejected` event

---

### Faza 3 — ESP32 NTP + EventLog + OTA + stability (4–6h)

**Scop**: features avansate firmware.

**Fișiere:**
- `ESP32/TimeSync.h` (CREATE) — wrapper NTP cu re-sync 24h
- `ESP32/EventLog.h/.cpp` (CREATE) — circular buffer 50 entries în NVS
- `ESP32/OtaUpdater.h/.cpp` (CREATE) — HTTPClient + Update + SHA-256 streaming
- `ESP32/ProiectVentilatie.ino` (EDIT):
  - Init TimeSync după Wi-Fi connect
  - log.append() la sensor_err / relay_change / override_expired (locuri existente: cycle senzori, updateLogic, tickOverrideExpiry)
  - Handler MQTT pentru `cmd:getLog` (publish pe `ventilatie/log`)
  - Handler MQTT pentru `cmd:update` (start OTA)
  - `wifiDownSinceMs` + restart preventiv >10min
  - Pre-restart: `mqtt.publishOnline(false); delay(200);`

**Validare:**
- Serial la boot: `[NTP] OK 2026-04-29 14:32`
- Deconectează DHT temporar → după 5 erori, log entry → `cmd:getLog` returnează lista
- Pornire/oprire releu manuală → log entry `relay_change`
- Așteaptă override expirat (sau scurtează timeout pentru test) → log entry
- Build firmware nou + upload GitHub release + `cmd:update` → progress 0→100% pe `ventilatie/event` → ESP32 reboot cu noul build #
- Oprește router 11min → ESP32 restart preventiv

---

### Faza 4 — MAUI foundation + Dashboard (4–6h)

**Scop**: MAUI conectat la HiveMQ, afișează date reale pe Dashboard.

**Fișiere:**
- `MobileApp/appsettings.json` (CREATE)
- `MobileApp/Models/MqttSettings.cs` (CREATE)
- `MobileApp/Models/VentilationState.cs` (EDIT) — aliniat cu JSON real (override, errs, lock, ts, fw, uptimeSec, heap)
- `MobileApp/Services/IMqttService.cs` (EDIT) — extins
- `MobileApp/Services/MqttService.cs` (REWRITE) — config DI, backoff, lifecycle, LWT, retained
- `MobileApp/MauiProgram.cs` (EDIT) — load embedded config, DI registration
- `MobileApp/App.xaml.cs` (EDIT) — OnSleep/OnResume
- `MobileApp/ProiectVentilatie.Mobile.csproj` (EDIT) — EmbeddedResource + ConfigurationJson
- `MobileApp/ViewModels/DashboardViewModel.cs` (EDIT) — RelayText, override, ago timer, lock banner, online
- `MobileApp/Views/DashboardPage.xaml` (EDIT) — labels, badges, banner

**Validare:**
- Pornire app → conectare la HiveMQ → state retained populat instant
- Gauge-urile arată valorile reale ale senzorilor
- Badge "ESP32 ONLINE" verde
- Label "Actualizat acum câteva secunde"
- Apasă Refresh → primești state nou
- Toggle relay → schimbarea se propagă pe ESP32 + Blynk
- Modifică ceva în Blynk → MAUI vede schimbarea în <2s
- Background app → revenire → reconnect automat

---

### Faza 5 — MAUI Settings cu diff-based Save (2–3h)

**Scop**: Pagina Settings funcțională cu Save inteligent.

**Fișiere:**
- `MobileApp/ViewModels/SettingsViewModel.cs` (CREATE) — diff-based Save (vezi §J)
- `MobileApp/Views/SettingsPage.xaml` (REWRITE) — sliders + Save + Reset
- `MobileApp/Views/SettingsPage.xaml.cs` (UPDATE)

**Validare:**
- Deschide Settings → sliderele populate cu valorile din ESP32
- Save disabled inițial
- Mișcă slider → Save enabled
- Apasă Save → ESP32 confirmă → Save redevine disabled
- Cu lock activ Blynk → Save disabled + banner

---

### Faza 6 — MAUI System + OTA UI (3–4h)

**Scop**: Pagina System + interfață OTA cu URL persistat.

**Fișiere:**
- `MobileApp/ViewModels/SystemViewModel.cs` (CREATE) — status, refresh, reboot, OTA fields persistate în Preferences
- `MobileApp/Views/SystemPage.xaml` (REWRITE) — status broker/ESP32, butoane, OTA section, version labels
- `MobileApp/Views/SystemPage.xaml.cs` (UPDATE)

**Validare:**
- System page afișează: host broker, status, ESP32 online/offline, uptime, heap, erori senzori
- Versiune app: "1.0 (build #N)" — citită din `AppInfo`
- Versiune firmware: "build #M" — din `LastState.Fw`
- Apasă Refresh → state nou imediat
- Apasă Reboot → confirmare → ESP32 restart → state retained nou cu uptime mic
- OTA: introduce URL repo + version + filename + SHA → trimite → progress bar 0→100% → reboot
- Ieși app → reintră → câmpurile OTA sunt pre-completate (Preferences)

---

### Faza 7 — MAUI Log tab (2–3h)

**Scop**: Tab nou cu log evenimente.

**Fișiere:**
- `MobileApp/Models/LogEntry.cs` (CREATE)
- `MobileApp/ViewModels/LogViewModel.cs` (CREATE) — cmd getLog, filter, list
- `MobileApp/Views/LogPage.xaml` + `.cs` (CREATE)
- `MobileApp/AppShell.xaml` (EDIT) — adaugă Tab "Log"
- `MobileApp/MauiProgram.cs` (EDIT) — DI nou
- `MobileApp/Services/MqttService.cs` (EDIT) — handler pentru topic `ventilatie/log` + event `OnLogReceived`

**Validare:**
- Click pe tab Log → cmd getLog → ESP32 răspunde → listă afișată
- Filtru pe tip funcționează
- Pull-to-refresh sau buton Reîncarcă re-trimite cmd
- Format timestamp local (datetime din NTP)

---

### Faza 8 — Integration testing 24/7 (variabil, 2–7 zile)

**Scop**: validare production-ready.

**Test scenarios:**

1. **Memorie ESP32 stabilă**: 48h continuu cu serial logging → heap intern stabil >150KB, fără leaks
2. **MQTT economy**: 24h idle → maxim 24 mesaje state heartbeat (1/oră) + tranziții releu
3. **Conflict resolution**: stress test cu 100 cmd-uri rapide din MAUI + Blynk simultan → toate procesate corect, lock funcționează
4. **OTA cycle**: 3 update-uri consecutive cu build numbers diferite, verificare rollback dacă SHA mismatch
5. **Network reziliență**:
   - Wi-Fi off 5min → Blynk + MQTT reconect, releele continuă
   - Wi-Fi off 11min → restart preventiv
   - HiveMQ blocked (firewall) → Blynk continuă, NVS scrieri normale
6. **NVS wear**: după 1 săptămână, verificare contor scrieri NVS (target <500/săptămână)
7. **MAUI lifecycle**: 50 cycle-uri foreground/background → fără leaks, reconnect rapid
8. **Battery (telefon MAUI)**: app în background 24h → drain rezonabil

**Criterii Go-Live:**
- Zero crash-uri ESP32 în 48h
- Heap intern stabil
- 100% mesaje MQTT delivered (cu QoS 1 unde necesar)
- Lock UX <200ms
- OTA rollback funcțional
- Build numbers consistente

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
4. **În MAUI, prima dată** introduce URL repo în câmpul "Repository URL" pe pagina System → salvat în `Preferences` (persistent local). La update-uri ulterioare câmpul e pre-completat.
5. Apasă "Trimite update" → MAUI publică:
```json
{"cmd":"update","url":"<github_url>","sha256":"<hash>"}
```

### Persistare URL în MAUI

`Preferences` (MAUI built-in) cu chei:
- `OtaRepoUrl` — URL bază release (ex: `https://github.com/user/repo/releases/download/`)
- `OtaLastVersion` — ultima versiune folosită (ex: `v1.3`)
- `OtaLastSha` — ultimul SHA-256 (auto-populat la fiecare apăsare)

Pe System page:
- **Câmp 1**: "URL repo" (Entry, persistent în Preferences) — ex: `https://github.com/user/repo/releases/download/`
- **Câmp 2**: "Versiune" (Entry, default ultima folosită) — ex: `v1.3`
- **Câmp 3**: "Numele fișierului" (Entry, default `firmware.bin`)
- **Câmp 4**: "SHA-256" (Entry multiline) — paste de la `sha256sum`
- URL final = `{repo}/{version}/{filename}` — afișat read-only sub câmpuri pentru verificare
- Buton "Trimite update" → trimite cmd, salvează valorile în Preferences

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

MAUI ascultă `ventilatie/event` și afișează progress bar pe System page.

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

## L.1 Versionare automată build

### ESP32 firmware — build number simplu auto-incrementat

**Fișiere:**
- `ESP32/build_number.txt` — conține un singur număr (gitignored, init `0`)
- `ESP32/Version.h` — auto-generat, gitignored: `#define FW_BUILD_NUMBER X`
- `ESP32/scripts/bump_build.sh` — script care incrementează (committed)

**Script `bump_build.sh`:**
```bash
#!/bin/bash
DIR="$(dirname "$0")/.."
FILE="$DIR/build_number.txt"
[ ! -f "$FILE" ] && echo "0" > "$FILE"
N=$(($(cat "$FILE") + 1))
echo "$N" > "$FILE"
cat > "$DIR/Version.h" << EOF
#pragma once
#define FW_BUILD_NUMBER $N
EOF
echo "[ESP32] Build #$N"
```

**Integrare în workflow:**
- `deploy.sh` (existent) modificat să apeleze `./scripts/bump_build.sh` **înainte** de `arduino-cli compile`
- Sau wrapper nou `ESP32/build.sh` care le rulează în ordine
- Manual: `bash ESP32/scripts/bump_build.sh && arduino-cli compile ...`

**Folosire în firmware:**
- `Config.h` adaugă `#include "Version.h"` (cu fallback `#ifndef FW_BUILD_NUMBER #define FW_BUILD_NUMBER 0 #endif`)
- În `state` JSON: `"fw":FW_BUILD_NUMBER`
- La boot, Serial: `Serial.printf("[Boot] Firmware build #%d\n", FW_BUILD_NUMBER);`
- Blynk virtualWrite pe nou pin **VP_FW_BUILD = V23** (Integer RO) la `BLYNK_CONNECTED()`

### MAUI — SemVer manual + build auto

**Fișiere:**
- `MobileApp/build_number.txt` — număr (gitignored, init `0`)
- `MobileApp/ProiectVentilatie.Mobile.csproj` — MSBuild target care incrementează

**Adăugat în `.csproj`:**
```xml
<Target Name="IncrementBuildNumber" BeforeTargets="BeforeBuild">
  <PropertyGroup>
    <BuildNumberFile>$(MSBuildProjectDirectory)/build_number.txt</BuildNumberFile>
  </PropertyGroup>
  <WriteLinesToFile Condition="!Exists('$(BuildNumberFile)')"
                    File="$(BuildNumberFile)" Lines="0" Overwrite="true" />
  <PropertyGroup>
    <_CurrentBuild>$([System.IO.File]::ReadAllText('$(BuildNumberFile)').Trim())</_CurrentBuild>
    <_NewBuild>$([MSBuild]::Add($(_CurrentBuild), 1))</_NewBuild>
  </PropertyGroup>
  <WriteLinesToFile File="$(BuildNumberFile)" Lines="$(_NewBuild)" Overwrite="true" />
  <PropertyGroup>
    <ApplicationVersion>$(_NewBuild)</ApplicationVersion>
  </PropertyGroup>
  <Message Importance="high" Text="[MAUI] Build #$(_NewBuild) (display $(ApplicationDisplayVersion))" />
</Target>
```

**`ApplicationDisplayVersion`** rămâne manual în csproj (`1.0`, `1.1`...) — schimbat când vrei să marchezi un release semnificativ.
**`ApplicationVersion`** auto-incrementat la fiecare `dotnet build`.

**Afișare în MAUI System page:**
- Label "Versiune app: 1.0 (build #42)" — citit din `AppInfo.Current.VersionString` și `AppInfo.Current.BuildString`
- Label "Firmware ESP32: build #38" — citit din `LastState.Fw`
- Avertisment vizual dacă fw build < ultima versiune cunoscută în Preferences (sugestie: "Update disponibil")

### `.gitignore` adăugări

```
ESP32/build_number.txt
ESP32/Version.h
MobileApp/build_number.txt
```

### Fluxul complet

1. Modifici cod ESP32 → rulezi `./deploy.sh` (sau `bump_build.sh && arduino-cli compile`)
2. `Version.h` regenerat cu numărul nou → compilare → flash
3. ESP32 publică pe MQTT `"fw":42` → MAUI System page afișează "Firmware ESP32: build #42"
4. Modifici cod MAUI → rulezi `dotnet build` → MSBuild incrementează automat → APK cu `ApplicationVersion=15`
5. Instalezi APK pe device → System page afișează "Versiune app: 1.0 (build #15)"

### Observații
- Build number e local, deci doi developeri ar avea contoare diferite — în acest proiect (single user) nu e o problemă
- Pentru OTA: SHA-256 împreună cu build number în nume fișier (ex: `firmware-build42.bin`) ajută la identificare
- Pentru rollback: păstrează ultimele N `.bin`-uri în repo / GitHub releases

---

## M.0 Configurare Blynk app — out-of-the-box ready

Aplicația Blynk e deja configurată în firmware (`BLYNK_TEMPLATE_ID`, `BLYNK_AUTH_TOKEN` în `Config.h`) și **funcționează imediat ce ESP32-ul se conectează la Wi-Fi**. Trebuie doar să verifici/setezi widget-urile în consola Blynk pentru a se mapa pe virtual pinii corecți.

### Acces consolă

1. https://blynk.cloud → login cu contul folosit la generarea token-ului `OSF1fWefKtBmV8c5QUDsgtpkMHafaB_I`
2. Templates → găsești template-ul cu ID `TMPL42ximIY6M` (numele "Add agency")

### Datastreams (virtual pini)

În tab-ul **Datastreams** verifică / creează:

| Pin | Nume | Type | Min | Max | Default | Notă |
|---|---|---|---|---|---|---|
| V1 | Temp Stânga | Double | -20 | 80 | 0 | RO |
| V2 | Hum Stânga | Double | 0 | 100 | 0 | RO |
| V3 | Temp Dreapta | Double | -20 | 80 | 0 | RO |
| V4 | Hum Dreapta | Double | 0 | 100 | 0 | RO |
| V5 | Releu Stânga | Integer | 0 | 1 | 0 | RO (afișare) |
| V6 | Releu Dreapta | Integer | 0 | 1 | 0 | RO (afișare) |
| V7 | Prag Temperatură | Double | 1 | 80 | 45 | RW |
| V8 | Prag Umiditate | Double | 0 | 100 | 60 | RW |
| V9 | Interval Citire | Integer | 10 | 3600 | 300 | RW (secunde) |
| V10 | Reset Defaults | Integer | 0 | 1 | 0 | RW (push) |
| V11 | Override Stânga | Integer | 0 | 2 | 0 | RW (0=OFF, 1=ON, 2=clear) |
| V12 | Override Dreapta | Integer | 0 | 2 | 0 | RW |
| V20 | Restart | Integer | 0 | 1 | 0 | RW (push) |
| V21 | Free Heap (KB) | Integer | 0 | 500 | 0 | RO |
| **V22** | **Lock Owner** | **Integer** | **0** | **2** | **0** | **RO (NOU: 0=none, 1=blynk, 2=mqtt)** |

### Web Dashboard / Mobile Dashboard — widgets

În tab-ul **Web Dashboard** (sau **Mobile Dashboard** din app):

**Sectiunea Senzori (RO):**
- Gauge V1 (Temp Stânga) — min -20, max 80, unit °C
- Gauge V2 (Hum Stânga) — min 0, max 100, unit %
- Gauge V3 (Temp Dreapta) — min -20, max 80, unit °C
- Gauge V4 (Hum Dreapta) — min 0, max 100, unit %

**Sectiunea Stare (RO):**
- LED V5 (Releu Stânga) — culoare verde când ON
- LED V6 (Releu Dreapta) — culoare verde când ON
- Label V21 (Heap KB) — pentru diagnostic
- Label V22 (Lock Owner) **NOU** — afișează "MAUI activ" când valoare=2; ascuns sau "—" la 0

**Sectiunea Comenzi (RW):**
- Slider V7 (Prag Temperatură) — pas 0.5
- Slider V8 (Prag Umiditate) — pas 1
- Numeric Input V9 (Interval Citire) — secunde
- Segmented Switch V11 (Override Stânga) — 3 valori: OFF(0), ON(1), AUTO(2)
- Segmented Switch V12 (Override Dreapta) — 3 valori: OFF(0), ON(1), AUTO(2)
- Switch V20 (Restart) — momentary push (revine la 0 după trimitere)
- Switch V10 (Reset Defaults) — momentary push

### Events (notificări push)

În tab-ul **Events** verifică/creează:

| Event Code | Description | Notify |
|---|---|---|
| `sensor_error` | Eroare senzor DHT | App notification |
| `override_expired` | Override anulat automat | App notification |
| `system_restart` | Restart sistem | Log only |
| `cmd_rejected` | Comandă respinsă (lock activ MAUI) **NOU** | App notification |

### Cum se "preia controlul" Blynk → MAUI

- Dacă MAUI a făcut o modificare recent (lock activ MQTT), V22=2 și Blynk afișează "MAUI activ"
- Orice modificare în Blynk în acest interval (~100ms) primește event `cmd_rejected` și UI-ul revine la valoarea anterioară (handler-ul Blynk face `Blynk.virtualWrite(VP_*, prefs.*)` pentru revert)
- După publicare state cu lock=null, V22 revine la 0 → Blynk poate modifica din nou

### Sync bidirecțional Blynk ↔ MAUI

Modificări în Blynk se reflectă în MAUI prin următorul push de state pe MQTT (după aplicarea pending-ului — vezi §D.3 push imediat). Tot procesul durează tipic <200ms.

Modificări în MAUI se sincronizează în Blynk prin `Blynk.virtualWrite(VP_*, prefs.*)` apelat în handler-ul MQTT după aplicarea schimbării (vezi §I).

### Test rapid (după prima conectare)

1. Buton ESP32 alimentat → după ~10s LED RGB devine verde (Blynk + MQTT conectați)
2. Web Dashboard: gauge-urile V1-V4 se umplu cu valorile DHT
3. V21 (heap KB) trebuie să arate ~200+ (cu PSRAM, ~500+)
4. Modifică V11 (override stânga) → 1 (ON) → ascultă releu click → LED V5 devine verde
5. Așteaptă 2h → eveniment `override_expired` în notificări → V11 revine la 0

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

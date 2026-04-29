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
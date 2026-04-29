# 09 — Quick Start (primul deploy)

Checklist complet pentru deploy de la zero. Estimat: **~2h** dacă urmezi pașii în ordine.

## Cerințe verificate înainte de start

- [ ] PC cu Linux/Mac/Windows
- [ ] .NET 10 SDK instalat (`dotnet --version` → `10.0.x`)
- [ ] Arduino IDE 2.x sau arduino-cli instalat
- [ ] Cablu USB-C
- [ ] Telefon Android cu USB Debugging activat
- [ ] Cont Blynk creat și template existent (vezi [02-blynk-setup.md](02-blynk-setup.md))
- [ ] Cont GitHub (pentru OTA — opțional la primul deploy)

---

## Pas 1: HiveMQ Cloud (15 min)

📖 Detalii complete: [01-hivemq-setup.md](01-hivemq-setup.md)

- [ ] **Creare cont** la https://www.hivemq.com/mqtt-cloud-broker/
- [ ] **Verificare cluster** — folosim cluster-ul existent: `1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud:8883`
- [ ] **Access Management** → creare 2 useri:
  - `ventilatie_esp32` (cu parolă A)
  - `ventilatie_app` (cu parolă B, diferită)
- [ ] **ACL** pentru ambii useri: Publish + Subscribe pe `ventilatie/#`
- [ ] **Test Web Client** — subscribe la `ventilatie/#`, publish test → vezi mesajul

---

## Pas 2: Configurare credențiale (5 min)

### ESP32

- [ ] Editează `ESP32/Config.h`:
  ```cpp
  #define MQTT_USER  "ventilatie_esp32"
  #define MQTT_PASS  "PAROLA_A"
  ```

### MAUI

- [ ] Editează `MobileApp/appsettings.json`:
  ```json
  {
    "Mqtt": {
      "Username": "ventilatie_app",
      "Password": "PAROLA_B"
    }
  }
  ```

---

## Pas 3: Blynk console (10 min)

📖 Detalii complete: [02-blynk-setup.md](02-blynk-setup.md)

- [ ] Login la https://blynk.cloud
- [ ] Templates → găsește `TMPL42ximIY6M` ("Add agency")
- [ ] **Datastreams** — adaugă noi V22 (Lock Owner) și V23 (FW Build) (restul există deja)
- [ ] **Web Dashboard** — adaugă widgets pentru V22 (Label format custom) și V23 (Label `Build #{}`)
- [ ] **Events** — adaugă event nou `cmd_rejected` (Severity: Warning)

---

## Pas 4: Build și flash ESP32 (20 min)

📖 Detalii complete: [03-esp32-build.md](03-esp32-build.md)

### 4.1 Setup Arduino

- [ ] Adaugă URL board manager pentru ESP32:
  ```
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
  ```
- [ ] Install board package `esp32 by Espressif` v3.x
- [ ] Setări board:
  - Board: **ESP32 PICO-D4**
  - PSRAM: **Enabled**
  - Flash Size: **8MB**
  - CPU Freq: 240MHz

### 4.2 Install librării

- [ ] PubSubClient v2.8+
- [ ] ArduinoJson v7+
- [ ] Blynk (deja prezent dacă ai compilat înainte)
- [ ] WiFiManager
- [ ] DHT sensor library
- [ ] Adafruit NeoPixel

### 4.3 Bump build + flash

```bash
bash ESP32/scripts/bump_build.sh
# [ESP32] Build #1

arduino-cli compile --upload --port /dev/ttyUSB0 \
  --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M ESP32/
```

- [ ] Compilare OK fără erori
- [ ] Upload OK pe ESP32
- [ ] Serial Monitor (115200) afișează:
  ```
  [Boot] Firmware build #1
  [WiFi] Conectat: <SSID>
  [Blynk] Conectat
  [NTP] OK 2026-04-29...
  [MQTT] Conectat la HiveMQ
  [Sistem] Heap intern: 220 KB  PSRAM: 1843 KB
  ```

### 4.4 Wi-Fi config (prima rulare)

Dacă e primul boot:
- [ ] Conectează telefonul/laptopul la SSID `ESP32_Ventilatie`
- [ ] Browser → http://192.168.4.1
- [ ] Configure WiFi → SSID + parolă → Save
- [ ] ESP32 restartează și se conectează

---

## Pas 5: Verificare ESP32 → Blynk (5 min)

📖 Detalii: [02-blynk-setup.md](02-blynk-setup.md#6-test-rapid)

- [ ] **LED RGB**: verde (toate conectate)
- [ ] **Web Dashboard Blynk**:
  - V1-V4: gauge cu valori reale ale senzorilor
  - V5/V6: LED OFF (default, fără override)
  - V21: heap în KB (>200)
  - V23: `Build #1`
- [ ] **Test override**: V11 → 1 (ON) → ascultă click releu → LED V5 verde
- [ ] **Reset override**: V11 → 2 (clear) → releu revine la auto

---

## Pas 6: Verificare ESP32 → HiveMQ (5 min)

📖 Detalii: [07-mqtt-protocol.md](07-mqtt-protocol.md)

În HiveMQ Web Client (subscribe `ventilatie/#`):

- [ ] `ventilatie/online` retained = `"online"`
- [ ] `ventilatie/state` retained = JSON cu temp/hum reale, lock=null, fw=1
- [ ] Publică test:
  ```json
  {"cmd":"refresh"}
  ```
  → primești state nou imediat
- [ ] Publică:
  ```json
  {"cmd":"setOverride","zone":"left","value":1}
  ```
  → primești 2 mesaje state (lock=mqtt → lock=null)
  → în Blynk, V11 devine 1 și V5 devine ON

---

## Pas 7: Build și install MAUI (15 min)

📖 Detalii complete: [04-maui-build.md](04-maui-build.md)

### 7.1 Install workload

```bash
dotnet workload install maui-android
```

### 7.2 Build APK

```bash
cd MobileApp
dotnet publish -f net10.0-android -c Release
```

- [ ] Build OK fără erori
- [ ] Output: `bin/Release/net10.0-android/publish/com.proiect.ventilatie-Signed.apk`

### 7.3 Install pe telefon

- [ ] USB Debugging activat
- [ ] `adb devices` → telefonul listat
- [ ] `adb install -r APK/com.proiect.ventilatie-Signed.apk`

---

## Pas 8: Verificare MAUI app (10 min)

📖 Detalii: [04-maui-build.md](04-maui-build.md#8-verificare-prima-rulare)

Deschide aplicația pe telefon.

### Tab Dashboard
- [ ] Status: "Conectat la HiveMQ • ESP32 ONLINE" (verde)
- [ ] Label "Actualizat acum câteva secunde"
- [ ] 4 gauge-uri cu valori reale
- [ ] Toggle Stânga → click → starea releu se schimbă pe ESP32 → vizibil și în Blynk

### Tab Setări
- [ ] Sliders populați cu valori curente (45°C, 60%, 300s)
- [ ] Save **disabled** inițial
- [ ] Mișcă slider TempThreshold la 50 → Save **enabled**
- [ ] Apasă Save → ESP32 confirmă → Save **disabled** din nou
- [ ] În Blynk verifică V7 = 50 → sync OK

### Tab Sistem
- [ ] Status broker: conectat
- [ ] ESP32: ONLINE, uptime, heap >200KB
- [ ] Versiune app: "1.0 (build #1)"
- [ ] Versiune firmware: "build #1"
- [ ] Apasă Refresh → state nou imediat

### Tab Log
- [ ] Apasă Reîncarcă
- [ ] Listă (poate fi goală inițial — fără evenimente)

---

## Pas 9: Test paralelism Blynk + MAUI (10 min)

- [ ] **Blynk → MAUI**: în Blynk modifică V7 (prag temp) la 42 → MAUI Settings afișează 42 după <2s
- [ ] **MAUI → Blynk**: în MAUI Settings setează prag la 48 + Save → în Blynk V7 = 48
- [ ] **Lock conflict**: în Blynk modifică ceva → în MAUI banner "🔒 Control blocat" apare ~100ms
- [ ] **Reboot test**: MAUI Sistem → Reboot ESP32 → confirmare → ESP32 LED albastru → revine verde → MAUI vede ONLINE din nou cu uptime mic

---

## Pas 10: 24/7 stability check (lasă peste noapte)

📖 Detalii: §Faza 8 din plan

- [ ] Lăsă sistemul activ 24h
- [ ] A doua zi verifică:
  - Heap stabil (Serial sau System page)
  - Niciun crash (uptimeSec >86400)
  - Notificări Blynk doar pentru evenimente reale (sensor_error, override_expired)
  - Log MAUI conține evenimente plauzibile

---

## ✅ Done — sistem production ready

Felicitări! Acum:
- ESP32 monitorizează automat senzorii și controlează releele
- Blynk app pe telefon — control direct (existent, neschimbat)
- MAUI app — control alternativ via HiveMQ
- Ambele canale lucrează în paralel cu lock prevention
- OTA disponibil pentru update-uri viitoare (vezi [05-ota-update.md](05-ota-update.md))

## Probleme la oricare pas?

Vezi [08-troubleshooting.md](08-troubleshooting.md).

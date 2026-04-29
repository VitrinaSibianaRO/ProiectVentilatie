# 03 — Build și flash firmware ESP32

Compilarea și încărcarea firmware-ului pe placa GroundStudio Carbon V3 (ESP32-PICO-V3-02).

## Cuprins

- [1. Cerințe](#1-cerințe)
- [2. Instalare Arduino IDE / arduino-cli](#2-instalare-arduino-ide--arduino-cli)
- [3. Instalare ESP32 board package](#3-instalare-esp32-board-package)
- [4. Setări board](#4-setări-board)
- [5. Instalare librării](#5-instalare-librării)
- [6. Build & flash](#6-build--flash)
- [7. Verificare prim boot](#7-verificare-prim-boot)
- [8. Reset Wi-Fi credentials](#8-reset-wi-fi-credentials)

---

## 1. Cerințe

- PC Linux/Mac/Windows
- Cablu USB-C
- ESP32 GroundStudio Carbon V3
- Senzori DHT22 conectați pe GPIO 19 (stânga) și 32 (dreapta)
- Module relee pe GPIO 15 (stânga) și 26 (dreapta)
- Buton reset pe GPIO 13

> ⚠️ **NU modifica pinout-ul** — firmware-ul existent depinde de aceste alocări (vezi `ESP32/Config.h`).

## 2. Instalare Arduino IDE / arduino-cli

### Opțiunea A: Arduino IDE 2.x (recomandat pentru începători)

Download de la https://www.arduino.cc/en/software → instalare.

### Opțiunea B: arduino-cli (recomandat pentru CI/scripting)

```bash
# Linux
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# macOS
brew install arduino-cli

# Windows
# https://arduino.github.io/arduino-cli/latest/installation/
```

## 3. Instalare ESP32 board package

### Arduino IDE

1. File → Preferences → Additional Board Manager URLs:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
2. Tools → Board → Boards Manager → caută `esp32` → instalează **esp32 by Espressif Systems** v3.x

### arduino-cli

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

## 4. Setări board

### Arduino IDE → Tools menu

| Setare | Valoare |
|---|---|
| Board | **ESP32 PICO-D4** (sau "ESP32 Dev Module") |
| Upload Speed | 921600 |
| CPU Frequency | 240MHz (WiFi/BT) |
| Flash Frequency | 80MHz |
| Flash Mode | QIO |
| Flash Size | **8MB (64Mb)** |
| Partition Scheme | **Default 4MB with spiffs** |
| **PSRAM** | **Enabled** |
| Core Debug Level | None |
| Erase All Flash Before Sketch Upload | Disabled (set Yes la primul flash) |
| Port | (selectează portul USB după conectare) |

### arduino-cli — FQBN echivalent

```bash
esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M,PartitionScheme=default,CPUFreq=240,FlashMode=qio,FlashFreq=80,UploadSpeed=921600,DebugLevel=none
```

## 5. Instalare librării

### Cerințe librării:

| Librărie | Versiune | Folosit pentru |
|---|---|---|
| Blynk | 1.3.0+ | Conexiune Blynk Cloud (existent) |
| WiFiManager | 2.0.16+ | Portal captiv config Wi-Fi (existent) |
| DHT sensor library | 1.4.4+ | Senzori DHT22 (existent) |
| Adafruit NeoPixel | 1.11+ | LED RGB (existent) |
| **PubSubClient** | **2.8+** | **MQTT client (NOU)** |
| **ArduinoJson** | **7+** | **JSON parse (NOU)** |

### Arduino IDE

Tools → Manage Libraries → caută și instalează fiecare librărie.

### arduino-cli

```bash
arduino-cli lib install "PubSubClient"
arduino-cli lib install "ArduinoJson"
arduino-cli lib install "Blynk"
arduino-cli lib install "WiFiManager"
arduino-cli lib install "DHT sensor library"
arduino-cli lib install "Adafruit NeoPixel"
```

## 6. Build & flash

### Pas 1: Bump build number (înainte de fiecare compilare)

```bash
cd ProiectVentilatie
bash ESP32/scripts/bump_build.sh
```

Acest script:
- Citește `ESP32/build_number.txt`, incrementează cu 1, scrie înapoi
- Generează `ESP32/Version.h` cu `#define FW_BUILD_NUMBER X`
- Output: `[ESP32] Build #N`

### Pas 2: Compile

#### Arduino IDE
- Deschide `ESP32/ProiectVentilatie.ino`
- Conectează ESP32 prin USB-C
- Selectează portul (Tools → Port)
- Click ✓ (Verify) pentru build
- Click → (Upload) pentru build + flash

#### arduino-cli

```bash
# Compile only (verificare)
arduino-cli compile --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M ESP32/

# Compile + upload (flash)
arduino-cli compile --upload --port /dev/ttyUSB0 \
  --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M ESP32/
```

### Pas 3: Wrapper unificat (opțional)

Modifică `deploy.sh` (existent) sau creează `ESP32/build.sh`:

```bash
#!/bin/bash
set -e
cd "$(dirname "$0")"
bash scripts/bump_build.sh
arduino-cli compile --upload --port "${1:-/dev/ttyUSB0}" \
  --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M .
```

Folosire: `bash ESP32/build.sh /dev/ttyUSB0`

## 7. Verificare prim boot

Deschide Serial Monitor (115200 baud) după upload:

```
=== Pornire sistem ventilatie ===
[Boot] Firmware build #42
[Prefs] Încărcaţi: T≥45°C  H≥60%  Interval:300s
[WDT] Watchdog iniţializat: 60s timeout.
[WiFi] Conectat: <SSID>  IP: 192.168.x.x
[Blynk] Conectat. Parametri sincronizaţi.
[NTP] OK 2026-04-29 14:32:15
[MQTT] Conectat la HiveMQ.
[Sistem] Heap intern liber: 220 KB  PSRAM liber: 1843 KB
[Setup] Sistem pornit cu succes.

--- Ciclu senzori ---
  [STANGA]  T:24.5°C  H:55.2%  Releu:OFF  Override:NU
  [DREAPTA] T:25.1°C  H:58.7%  Releu:OFF  Override:NU
  [Blynk] Heartbeat trimis.
```

LED RGB:
- **Albastru** = boot
- **Albastru pulsat** = WiFiManager portal (config Wi-Fi)
- **Verde** = totul conectat (Wi-Fi + Blynk + MQTT)
- **Roșu** = MQTT failed (dar Blynk poate fi OK)
- **Galben** = buton reset apăsat
- **Alb** = factory reset în curs

## 8. Reset Wi-Fi credentials

Dacă vrei să schimbi rețeaua Wi-Fi:

1. Apasă și ține **butonul fizic pe GPIO 13** timp de **3 secunde**
2. LED devine alb → ESP32 restartează
3. La reboot apare rețeaua `ESP32_Ventilatie` (open Wi-Fi) → conectează telefonul/laptopul
4. Browser → `http://192.168.4.1` → portal de configurare
5. Selectează SSID-ul nou + parolă → Save
6. ESP32 restartează automat și se conectează

Vezi [08-troubleshooting.md](08-troubleshooting.md) pentru probleme comune.

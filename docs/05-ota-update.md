# 05 — OTA Update (Over-The-Air firmware update)

ESP32 poate primi update-uri de firmware prin Wi-Fi, fără cablu USB. Mecanismul folosește un binar `.bin` găzduit pe GitHub releases, descărcat și verificat cu SHA-256.

## Cuprins

- [1. Cum funcționează](#1-cum-funcționează)
- [2. Build firmware nou](#2-build-firmware-nou)
- [3. Calcul SHA-256](#3-calcul-sha-256)
- [4. Upload pe GitHub release](#4-upload-pe-github-release)
- [5. Trigger OTA din MAUI](#5-trigger-ota-din-maui)
- [6. Trigger OTA manual (debug)](#6-trigger-ota-manual-debug)
- [7. Rollback](#7-rollback)

---

## 1. Cum funcționează

```
┌──────────┐       ┌────────────┐        ┌──────────┐
│  MAUI    │       │  HiveMQ    │        │  ESP32   │
│  System  │──cmd──→  Broker    │──cmd──→│  OTA     │
│  Page    │       └────────────┘        └────┬─────┘
└──────────┘                                  │
                                              ▼ HTTPS
                                       ┌─────────────┐
                                       │  GitHub     │
                                       │  Release    │
                                       │  firmware.bin│
                                       └─────────────┘
                              ESP32 download → SHA-256 verify
                              → Update.write() → restart
```

**Securitate**: ESP32 verifică SHA-256 al firmware-ului descărcat. Chiar dacă conexiunea HTTPS e compromisă (fapt improbabil), un fișier alterat va fi respins din cauza hash-ului diferit.

**Whitelist URL**: ESP32 acceptă doar URL-uri care încep cu:
- `https://github.com/`
- `https://objects.githubusercontent.com/`

Alte URL-uri sunt respinse cu `{"event":"ota_failed","reason":"url_not_allowed"}`.

## 2. Build firmware nou

```bash
# 1. Modifică codul în ESP32/
# 2. Bump build number și compilează
bash ESP32/scripts/bump_build.sh
arduino-cli compile --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M ESP32/

# 3. Locația .bin-ului
# Default: /tmp/arduino/ProiectVentilatie/ProiectVentilatie.ino.bin
# Sau în ESP32/build/ (dacă e configurat output dir)
ls ESP32/build/*.bin
```

Pentru output dir custom:

```bash
arduino-cli compile --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M \
  --output-dir ./ESP32/build ESP32/
```

## 3. Calcul SHA-256

```bash
sha256sum ESP32/build/ProiectVentilatie.ino.bin
# Output:
# a3f5e2c8d1b9... ProiectVentilatie.ino.bin
```

Copiază hash-ul (primul câmp, 64 caractere hex).

## 4. Upload pe GitHub release

### Opțiunea A: GitHub Web UI

1. Deschide repo-ul tău pe github.com
2. Releases → Draft a new release
3. Tag version: `v1.3` (incrementează față de release-ul anterior)
4. Title: `Firmware build #42`
5. Upload `firmware.bin` (drag & drop)
6. Publish release

URL-ul firmware-ului va fi:
```
https://github.com/<user>/<repo>/releases/download/v1.3/firmware.bin
```

### Opțiunea B: gh CLI

```bash
gh release create v1.3 \
  --title "Firmware build #42" \
  --notes "Bug fixes: ..." \
  ESP32/build/ProiectVentilatie.ino.bin#firmware.bin
```

> 💡 Recomandat: redenumește binarul la upload în `firmware-buildN.bin` pentru identificare ușoară a versiunii.

## 5. Trigger OTA din MAUI

1. Deschide aplicația MAUI → tab **Sistem**
2. Secțiunea **Update Firmware**:
   - **URL repo**: `https://github.com/<user>/<repo>/releases/download/` (persistent — completat o singură dată)
   - **Versiune**: `v1.3` (default = ultima folosită)
   - **Numele fișierului**: `firmware.bin` (default)
   - **SHA-256**: paste-uiește hash-ul de la `sha256sum`
3. URL final compus afișat read-only sub câmpuri pentru verificare
4. Click **Trimite update**
5. Progress bar 0% → 100% → ESP32 reboot automat

### Câmpurile sunt persistate

La a doua oară, doar SHA-ul nou trebuie introdus — restul sunt pre-completate din `Preferences`.

### Feedback timp real

ESP32 publică progres pe `ventilatie/event`:

```json
{"event":"ota_progress","pct":40}
{"event":"ota_progress","pct":80}
{"event":"ota_done"}
```

Sau în caz de eroare:

```json
{"event":"ota_failed","reason":"sha_mismatch"}
{"event":"ota_failed","reason":"download_error"}
{"event":"ota_failed","reason":"url_not_allowed"}
{"event":"ota_failed","reason":"insufficient_space"}
```

## 6. Trigger OTA manual (debug)

Folosind HiveMQ Web Client sau `mosquitto_pub`:

```bash
mosquitto_pub -h 1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud \
  -p 8883 --capath /etc/ssl/certs \
  -u ventilatie_app -P 'PAROLA' \
  -t ventilatie/cmd -q 1 \
  -m '{"cmd":"update","url":"https://github.com/USER/REPO/releases/download/v1.3/firmware.bin","sha256":"a3f5e2..."}'
```

Pentru a urmări progresul:

```bash
mosquitto_sub -h 1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud \
  -p 8883 --capath /etc/ssl/certs \
  -u ventilatie_app -P 'PAROLA' \
  -t ventilatie/event -v
```

## 7. Rollback

Dacă noul firmware are bugs:

### Opțiunea A: OTA cu versiunea anterioară

Tot prin OTA, trimite o comandă cu URL-ul release-ului anterior:

```json
{"cmd":"update","url":"https://github.com/USER/REPO/releases/download/v1.2/firmware.bin","sha256":"...vechi..."}
```

### Opțiunea B: Re-flash USB

Dacă noul firmware nu pornește deloc (boot loop):

1. Conectează ESP32 prin USB-C
2. Compile + upload versiunea anterioară din git history:
   ```bash
   git checkout <commit_anterior>
   bash ESP32/scripts/bump_build.sh   # va incrementa, dar OK
   arduino-cli compile --upload --port /dev/ttyUSB0 \
     --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M ESP32/
   ```

### Opțiunea C: Erase + flash factory

Dacă firmware-ul e complet corupt:

```bash
esptool.py --port /dev/ttyUSB0 erase_flash
# apoi flash normal
```

> 💡 **Bună practică**: păstrează ultimele 3-5 versiuni pe GitHub releases pentru rollback rapid.

## Note de securitate

- SHA-256 + HTTPS = integritate garantată
- Whitelist GitHub previne injectare de URL-uri arbitrare
- ESP32 nu acceptă comenzi OTA dacă nu e conectat la HiveMQ (lock implicit)
- În timpul OTA, ESP32 nu procesează alte comenzi (single-threaded download)

Pentru probleme, vezi [08-troubleshooting.md](08-troubleshooting.md#ota-failed).

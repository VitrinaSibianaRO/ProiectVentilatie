# 04 — Build și install aplicație MAUI

Aplicația mobilă .NET MAUI multi-platform (Android, iOS, macOS, Windows). Această documentație se concentrează pe build-ul Android (cel mai folosit la deploy).

## Cuprins

- [1. Cerințe](#1-cerințe)
- [2. Instalare .NET 10 SDK](#2-instalare-net-10-sdk)
- [3. Instalare workload-uri MAUI](#3-instalare-workload-uri-maui)
- [4. Configurare appsettings.json](#4-configurare-appsettingsjson)
- [5. Build APK](#5-build-apk)
- [6. Semnare APK](#6-semnare-apk)
- [7. Install pe device](#7-install-pe-device)
- [8. Verificare prima rulare](#8-verificare-prima-rulare)

---

## 1. Cerințe

- **Linux/Mac/Windows** cu **.NET 10 SDK**
- **Android SDK** (instalat automat de workload-ul `maui-android`)
- Pentru iOS: macOS + Xcode 15+
- **Java JDK 17** (pentru semnare APK)
- **adb** (Android Debug Bridge — în Android SDK platform-tools)

## 2. Instalare .NET 10 SDK

### Linux

```bash
# Ubuntu/Debian
wget https://dot.net/v1/dotnet-install.sh
chmod +x dotnet-install.sh
./dotnet-install.sh --channel 10.0
echo 'export PATH="$HOME/.dotnet:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### macOS

```bash
brew install --cask dotnet-sdk
```

### Windows

Download installer de la https://dotnet.microsoft.com/download/dotnet/10.0 → install.

### Verificare

```bash
dotnet --version
# 10.0.x
```

## 3. Instalare workload-uri MAUI

```bash
dotnet workload install maui-android
# Pentru iOS: dotnet workload install maui-ios
# Pentru Mac:  dotnet workload install maui-maccatalyst
# Pentru Win:  dotnet workload install maui-windows
```

Verificare:

```bash
dotnet workload list
```

## 4. Configurare appsettings.json

Editează `MobileApp/appsettings.json` cu credențialele tale HiveMQ:

```json
{
  "Mqtt": {
    "Host": "1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud",
    "Port": 8883,
    "Username": "ventilatie_app",
    "Password": "PAROLA_TA_AICI",
    "StateTopic":   "ventilatie/state",
    "CommandTopic": "ventilatie/cmd",
    "OnlineTopic":  "ventilatie/online",
    "EventTopic":   "ventilatie/event",
    "LogTopic":     "ventilatie/log",
    "ReconnectInitialMs": 1000,
    "ReconnectMaxMs": 30000
  }
}
```

> ⚠️ Acest fișier conține credențiale. Pentru proiect privat e acceptabil; pentru distribuire publică, mută parola în `SecureStorage` (vezi [08-troubleshooting.md](08-troubleshooting.md)).

## 5. Build APK

### Build Debug (rapid, pentru testare)

```bash
cd MobileApp
dotnet build -f net10.0-android
```

APK-ul va fi în `bin/Debug/net10.0-android/com.proiect.ventilatie-Signed.apk` (semnat automat cu debug keystore).

### Build Release (optimizat, pentru deployment)

```bash
cd MobileApp
dotnet publish -f net10.0-android -c Release
```

APK-ul va fi în `bin/Release/net10.0-android/publish/`.

> 💡 La fiecare build, **build number-ul se incrementează automat** (vezi [06-versioning.md](06-versioning.md)).

## 6. Semnare APK

Pentru distribuire pe Play Store sau install pe alt device, APK-ul trebuie semnat cu un keystore real (nu cel de debug).

### Cu keystore-ul existent

Proiectul are deja `ventilatie.keystore`. Folosire:

```bash
# Build unsigned
dotnet publish -f net10.0-android -c Release \
  -p:AndroidPackageFormat=apk \
  -p:AndroidKeyStore=true \
  -p:AndroidSigningKeyStore=../ventilatie.keystore \
  -p:AndroidSigningKeyAlias=ventilatie \
  -p:AndroidSigningKeyPass=PAROLA_KEYSTORE \
  -p:AndroidSigningStorePass=PAROLA_KEYSTORE
```

### Sau folosește scriptul `deploy.sh`

```bash
bash deploy.sh
```

Acesta build-uiește, semnează și copiază APK-ul în `APK/com.proiect.ventilatie-Signed.apk`.

## 7. Install pe device

### Activare Developer Mode + USB Debugging pe telefon

1. Settings → About Phone → tap pe Build Number de 7 ori → Developer mode activat
2. Settings → Developer options → USB Debugging: **Enabled**
3. Conectează telefonul prin USB la PC → acceptă RSA fingerprint

### Verificare conexiune

```bash
adb devices
# List of devices attached
# ABC123XYZ    device
```

### Install APK

```bash
# Install (prima dată)
adb install APK/com.proiect.ventilatie-Signed.apk

# Reinstall (peste versiunea existentă, păstrează datele)
adb install -r APK/com.proiect.ventilatie-Signed.apk

# Reinstall cu downgrade permis
adb install -r -d APK/com.proiect.ventilatie-Signed.apk
```

### Install pe Waydroid (emulator Linux)

Proiectul are deja scripts în `waydroid_script/`. Vezi `quick_install.sh` și `deploy.sh` pentru workflow integrat.

## 8. Verificare prima rulare

Deschide aplicația pe telefon. Ar trebui să vezi:

### Tab Dashboard
- Status bar sus: "Conectat la HiveMQ • ESP32 ONLINE" (verde)
- Label "Actualizat acum câteva secunde" (verde)
- 4 gauge-uri: Temp Stânga, Hum Stânga, Temp Dreapta, Hum Dreapta — cu valori reale
- 2 carduri cu Toggle pentru fiecare zonă

### Tab Setări
- Sliders pentru Prag Temperatură, Prag Umiditate, Interval Citire
- Valorile sunt sincronizate cu cele din ESP32
- Butonul SALVEAZĂ — disabled inițial (fără modificări)

### Tab Sistem
- Status broker: host, port, conectat
- Status ESP32: ONLINE, uptime, heap, erori senzori
- Versiune app: "1.0 (build #N)"
- Versiune firmware: "build #M"
- Butoane: Refresh, Reboot, Reset
- Secțiunea OTA Update (URL repo, versiune, filename, SHA-256)

### Tab Log
- Listă cu evenimente (poate fi goală la prima rulare)
- Buton Reîncarcă

Dacă oricare din pași eșuează, vezi [08-troubleshooting.md](08-troubleshooting.md).

## Build pentru iOS / Mac / Windows

```bash
# iOS (necesită macOS + Xcode)
dotnet build -f net10.0-ios

# macOS Catalyst
dotnet build -f net10.0-maccatalyst

# Windows
dotnet build -f net10.0-windows10.0.19041.0
```

Pentru iOS, semnarea necesită cont Apple Developer și provisioning profile (out of scope pentru acest doc).

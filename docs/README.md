# ProiectVentilatie — Documentație

Sistem smart de ventilație cu **ESP32-PICO-V3-02** (GroundStudio Carbon V3) care comunică în paralel cu două cloud-uri: **Blynk** (interfață mobilă existentă) și **HiveMQ MQTT** (interfață nouă pentru aplicația MAUI). Toată logica de automatizare rulează pe ESP32; aplicațiile mobile sunt strict pentru vizualizare, override manual și configurare parametri.

## Arhitectură

```
                    ┌─────────────────┐
                    │  ESP32 Carbon V3│
                    │  (firmware C++) │
                    │                 │
                    │  - Senzori DHT22│
                    │  - 2× relee     │
                    │  - Logică auto  │
                    │  - NVS prefs    │
                    └────────┬────────┘
                             │ Wi-Fi
                ┌────────────┴────────────┐
                │                         │
       ┌────────▼─────────┐    ┌──────────▼──────────┐
       │  Blynk Cloud     │    │  HiveMQ Cloud (MQTT)│
       │  (TCP)           │    │  (TLS port 8883)    │
       └────────┬─────────┘    └──────────┬──────────┘
                │                         │
       ┌────────▼─────────┐    ┌──────────▼──────────┐
       │  Blynk app       │    │  MAUI app (.NET)    │
       │  (telefon)       │    │  Android/iOS/Win/Mac│
       └──────────────────┘    └─────────────────────┘
```

## Documentație disponibilă

| # | Document | Conținut |
|---|---|---|
| 0 | [09-quick-start.md](09-quick-start.md) | **START AICI** — checklist primul deploy |
| 1 | [01-hivemq-setup.md](01-hivemq-setup.md) | Configurare cont și useri HiveMQ Cloud |
| 2 | [02-blynk-setup.md](02-blynk-setup.md) | Datastreams și widgets Blynk |
| 3 | [03-esp32-build.md](03-esp32-build.md) | Compilare firmware ESP32 |
| 4 | [04-maui-build.md](04-maui-build.md) | Build aplicație MAUI |
| 5 | [05-ota-update.md](05-ota-update.md) | Update firmware OTA |
| 6 | [06-versioning.md](06-versioning.md) | Strategie versionare |
| 7 | [07-mqtt-protocol.md](07-mqtt-protocol.md) | Referință protocol MQTT |
| 8 | [08-troubleshooting.md](08-troubleshooting.md) | Probleme comune și soluții |

## Structură proiect

```
ProiectVentilatie/
├── docs/                   ← documentație (acest folder)
├── ESP32/                  ← firmware Arduino C++
├── MobileApp/              ← aplicație .NET MAUI
├── APK/                    ← APK-uri compilate
├── deploy.sh               ← script deploy ESP32
└── ventilatie.keystore     ← cheie semnare APK
```

## Cerințe minime

- **ESP32**: GroundStudio Carbon V3 (ESP32-PICO-V3-02)
- **PC dev**: Linux/Mac/Windows cu .NET 10 SDK + Arduino IDE 2.x sau arduino-cli
- **Mobile**: Android 5.0+ sau iOS 14+
- **Cloud**: cont gratuit HiveMQ Cloud + cont gratuit Blynk

## Suport

Pentru probleme, consultă [08-troubleshooting.md](08-troubleshooting.md).

# ESP32 Slave — Senzor remote SHT30 + LED PWM 24V

Componenta minimalistă care expune un singur SHT30 (zona dreapta) către Master via UART și controlează o bandă LED 24V 36W prin NCEP01T18 MOSFET.

## Hardware (Carbon V3 #2)

| Functie | GPIO | Note |
|---|---|---|
| I2C SDA (SHT30) | 21 | bus scurt ~10cm |
| I2C SCL (SHT30) | 22 | |
| UART2 TX → Master RX | 17 | Cat6 1.5m |
| UART2 RX ← Master TX | 16 | |
| LED RGB WS2812B data | 2 | status vizual |
| LED enable | 4 | |
| LED PWM 24V (NCEP01T18) | 25 | 5kHz 12-bit |

**SHT30 addr**: 0x44 (ADDR pin la GND)  
**UART**: 115200 8N1, fara flow control  
**WiFi/BT**: OPRIT explicit (`WiFi.mode(WIFI_OFF)`)

## Build & Flash

```bash
# Prerequisite: arduino-cli + librarii (vezi README root)
bash scripts/build.sh          # Release (LOG_LEVEL=WARN)
bash scripts/build.sh --debug  # Debug  (LOG_LEVEL=INFO + serial verbose)

bash scripts/flash.sh                 # flash pe /dev/ttyUSB1 (default)
bash scripts/flash.sh /dev/ttyUSB0    # flash pe port specific
```

## Librarii necesare

```bash
arduino-cli lib install \
    "Adafruit SHT31 Library" \
    "Adafruit NeoPixel" \
    "ArduinoJson"
```

## Protocol UART (cu CRC-16 Modbus)

Toate mesajele: `<payload>*<crc4hex>\n` (ASCII, terminated `\n`)

| Comanda Master → Slave | Raspuns Slave → Master |
|---|---|
| `GET_SENSOR*XXXX\n` | JSON `{temp,hum,ts,ok,errors,uptime}*XXXX\n` |
| `PING*XXXX\n` | `PONG*XXXX\n` |
| `REBOOT*XXXX\n` | `OK*XXXX\n` → reboot dupa 100ms |
| `LED_SET <0-100>*XXXX\n` | `OK*XXXX\n` (override 1h) |
| `LED_SCHEDULE <onH> <onM> <offH> <offM> <maxI> <en>*XXXX\n` | `OK*XXXX\n` (NVS persist) |
| `LED_STATUS*XXXX\n` | JSON `{intensity,enabled,sched}*XXXX\n` |
| `TIME_SYNC <epoch>*XXXX\n` | `OK*XXXX\n` |
| `OTA_BEGIN <size> <sha>*XXXX\n` | `OK*XXXX\n` sau `ERR_BEGIN*XXXX\n` |
| `OTA_CHUNK <len>*XXXX\n` + N bytes binari | `OK <len>*XXXX\n` sau `ERR_CHUNK*XXXX\n` |
| `OTA_END*XXXX\n` | reboot pe firmware nou |
| `UART_BAUD_HIGH*XXXX\n` | `OK*XXXX\n` → switch la 460800 |
| `UART_BAUD_LOW*XXXX\n` | `OK*XXXX\n` → switch la 115200 |

## Module

| Fisier | Rol |
|---|---|
| `ESP32_Slave.ino` | Entry point: setup() + loop() (~50 linii) |
| `Config.h` | Toate constantele hardware/timing |
| `CrcUtil.h` | CRC-16 Modbus (identic cu ESP32/CrcUtil.h) |
| `Sht30Sensor.h` | Wrapper SHT30 cu cooldown + retry |
| `SystemLED.h` | Status LED cu enum Status |
| `LedController.h` | PWM LED 24V + schedule NVS |
| `UartProtocol.h` | Buffering UART cu CRC validation |
| `CommandDispatcher.h/.cpp` | Mapare comenzi → actiuni |
| `OtaReceiver.h` | Primire firmware OTA de la Master |
| `Logger.h` | LOG_* macros cu nivele compilate |
| `WatchdogManager.h` | Watchdog hardware ESP32 |

## Rezilienta

- **Watchdog hardware 60s** — reset la blocaj software
- **Self-restart la idle 30min** — daca Master dispare
- **SHT30 fail graceful** — raporteaza ok=false, nu blocheaza
- **OTA rollback** — bootloader revine la FW anterior daca noul nu se valideaza
- **WiFi/BT OFF** — zero interferenta radio, economie ~80mA

## Consum estimat

~80mA (vs ~150mA Master cu Ethernet activ)  
Sursa recomandata: Mean Well IRM-20-5ST (5V, 4A)

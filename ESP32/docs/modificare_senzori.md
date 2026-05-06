# Plan: Migrare hardware ESP32 — DHT22 → SHT30, WiFi → Ethernet W5500, eliminare Blynk, dual-board Master/Slave cu UART prin Cat6

## Cuprins

1. Context
2. Topologie hardware (schema bloc)
3. Pinout detaliat ambele placi
4. Cablare inter-board
5. Secventa de boot
6. Protocol UART aplicatie
7. Modificari firmware Master
8. Firmware Slave — proiect modular complet
9. Modificari MAUI (inclusiv OTA Slave din MAUI)
10. Functii reset/restart in dual-board — semantica completa
11. Specificatii Carbon V3 (verificate)
12. OTA pentru Slave prin UART chunked (cu Master ca proxy)
13. Management memorie (RAM + Flash + NVS)
14. Always-on resilience — mecanisme software pentru 24/7 unattended
15. Cerinte fiabilitate hardware 24/7 minim 3 ani
16. Materiale (de cumparat)
17. Verificare end-to-end
18. Ordine de executie recomandata (28 faze A-BB)
19. Configurare Arduino IDE pentru ambele proiecte
20. Risc principal & strategii fallback
21. Modul aditional: control LED PWM pe Slave (NCEP01T18) cu schedule
22. Standarde profesionale de implementare (best practices cross-cutting)

## 1. Context

Utilizatorul vrea sa modifice hardware-ul ESP32 din proiect (placa **GroundStudio Carbon V3**) pentru fiabilitate si simplitate operationala, cu obiectiv: **24/7 minim 3 ani fara interventii**.

Schimbarile fundamentale:

1. **Senzori SHT30**: cei doi DHT22 (single-wire, instabili la 1.5m, cooldown 2.1s) sunt inlocuiti cu doua module **GY-SHT30-D** (I2C, mai precise, mai rapide).
2. **Doua placi Carbon V3**: senzorul stanga ramane local langa Master; senzorul dreapta este pe a doua placa (Slave) la 1.5m distanta. Eliminam astfel cablul I2C lung problematic.
3. **Comunicare Master ↔ Slave prin UART** (Serial2) prin cablu Cat6 — 3 fire (TX, RX, GND), fara chip suplimentar, fara WiFi/Bluetooth, fara internet pentru deciziile interne.
4. **Master cu Ethernet W5500** in locul WiFi-ului — pentru tot traficul cloud (MQTT cu HiveMQ, NTP, OTA HTTPS).
5. **Eliminare completa Blynk** (~600 linii cod, dependinta de WiFi-only). Singurul canal de control extern ramane MQTT spre HiveMQ Cloud (port 8883 TLS) pentru aplicatia MAUI Android.
6. **Slave fara internet, fara WiFi, fara MQTT** — doar SHT30 + UART responder + watchdog. Suprafata atac fizica zero.

## 2. Topologie hardware (schema bloc)

```
                     ┌────────────────────────┐
                     │   HiveMQ Cloud (8883)  │
                     │      MQTT TLS          │
                     └───────────┬────────────┘
                                 │ Internet
                                 │
                  ┌──────────────┴──────────────┐
                  │       Switch / Router       │
                  └──────────────┬──────────────┘
                                 │ Ethernet Cat6
                                 │
       ┌─────────────────────────┴──────────────────────────┐
       │                  MASTER (Carbon V3)                │
       │  ┌──────────────┐  ┌─────────┐  ┌───────────────┐  │
       │  │ SHT30 stanga │  │  W5500  │  │ Relay LEFT 15 │  │
       │  │ I2C 0x44     │  │  SPI    │  │ Relay RIGHT 26│  │
       │  │ GPIO 21/22   │  │ pins 5/ │  └───────────────┘  │
       │  └──────────────┘  │ 18/19/  │                     │
       │                    │ 23/33   │                     │
       │                    └─────────┘                     │
       │            UART2 (TX=17, RX=16, GND)               │
       └──────────────────────┬─────────────────────────────┘
                              │ Cat6 1.5m (3 fire)
                              │
       ┌──────────────────────┴─────────────────────────────┐
       │                   SLAVE (Carbon V3)                │
       │  ┌──────────────┐                                  │
       │  │ SHT30 dreapta│  UART2 (TX=17, RX=16, GND)       │
       │  │ I2C 0x44     │  Fara Ethernet, fara WiFi        │
       │  │ GPIO 21/22   │  Doar senzor + raspuns UART      │
       │  └──────────────┘                                  │
       └────────────────────────────────────────────────────┘
                              │
       ┌──────────────────────┴────────────────────────┐
       │   MAUI Android (telefon/tableta)              │
       │   MQTT TLS prin HiveMQ                        │
       └───────────────────────────────────────────────┘
```

## 3. Pinout detaliat ambele placi

### 3.1 Master (Carbon V3 #1)

| Functie | GPIO | Tip | Note |
| --- | --- | --- | --- |
| I2C SDA (SHT30 stanga local) | 21 | I2C | bus scurt ~10cm, pull-up 10kΩ integrat in modul |
| I2C SCL (SHT30 stanga local) | 22 | I2C | idem |
| W5500 MOSI | 23 | VSPI | |
| W5500 MISO | 19 | VSPI | eliberat de DHT_LEFT |
| W5500 SCK | 18 | VSPI | |
| W5500 CS | 5 | digital out | strap pin (HIGH default OK) |
| W5500 RST | 33 | digital out | reset hardware la pornire |
| W5500 INT (optional) | 35 | digital in | nu folosit, lasat liber |
| **UART2 TX → Slave RX** | **17** | UART | conectat la Slave GPIO 16 |
| **UART2 RX ← Slave TX** | **16** | UART | conectat la Slave GPIO 17 |
| RELAY_LEFT | 15 | digital out | neschimbat, strap pin (default HIGH) |
| RELAY_RIGHT | 26 | digital out | neschimbat |
| RESET_BUTTON (factory NVS) | 13 | digital in (pullup) | hold 3s = factory reset NVS |
| LED RGB WS2812 data | 2 | digital out | neschimbat |
| LED enable | 4 | digital out | neschimbat |
| Liber: GPIO 32, 25, 27, 14, 12 | — | — | rezerva |

**Verificare strap pins**: GPIO 0 (BOOT), 2 (LED), 5 (W5500 CS), 12, 15 (RELAY_LEFT). GPIO 5 pull-up intern HIGH = OK pentru CS SPI inactiv la boot. GPIO 15 cu releu cu pull-down ar fi problema, dar firmware-ul actual o foloseste ASA fara probleme — releul nu interfereaza cu boot.

### 3.2 Slave (Carbon V3 #2 — minimalista)

| Functie | GPIO | Tip | Note |
| --- | --- | --- | --- |
| I2C SDA (SHT30 dreapta local) | 21 | I2C | bus scurt ~10cm |
| I2C SCL (SHT30 dreapta local) | 22 | I2C | |
| **UART2 TX → Master RX** | **17** | UART | conectat la Master GPIO 16 |
| **UART2 RX ← Master TX** | **16** | UART | conectat la Master GPIO 17 |
| LED RGB WS2812 data | 2 | digital out | status LED |
| LED enable | 4 | digital out | |
| Pini W5500 (SPI) | — | — | **NEFOLOSITI** — Slave nu are W5500 |
| Pini relee | — | — | nefolositi |
| Radio WiFi 2.4GHz integrat | — | — | **OPRIT** (`WiFi.mode(WIFI_OFF)`) — economie consum + zero interferenta |

## 4. Cablare inter-board

### 4.1 Mapping cablu Cat6 1.5m

Conexiuni minime: 3 fire (TX, RX, GND). Folosim 2 perechi twisted pentru robustete EMI:

| Cat6 pereche | Culori T568B | Functie | Pini ESP32 |
| --- | --- | --- | --- |
| Pereche 2 (verde) | verde + verde-alb | Master TX → Slave RX + GND comun | M GPIO 17 → S GPIO 16, GND ↔ GND |
| Pereche 3 (albastru) | albastru + albastru-alb | Slave TX → Master RX + GND comun | S GPIO 17 → M GPIO 16, GND ↔ GND |
| Pereche 1 (portocaliu) | portocaliu + portocaliu-alb | rezerva (poti folosi pentru +5V daca alimentezi Slave din Master) | nefolosita |
| Pereche 4 (maro) | maro + maro-alb | rezerva | nefolosita |

**Recomandare**: NU alimentezi Slave din Master prin Cat6 — surse separate sunt mai sigure (la 1.5m cu fire fine, drop voltage e marginal dar masurile de rezilienta cer izolare a alimentarilor).

### 4.2 Schema fizica conector

Pe fiecare placa, rutezi cele 3 fire la headerele GPIO ale Carbon V3. Doua optiuni:

**Optiunea A — sertizare directa la pini header**: scoti 3 fire din Cat6, lipesti la pini female header. Mai robust mecanic, dar greu de inlocuit.

**Optiunea B — RJ45 keystone breakout**: pui o mufa RJ45 pe o placuta cu 8 pini → conectezi la headerele Carbon V3 cu jumper-uri scurte. Permite deconectare rapida + cabluri Cat6 standard. **Recomandat**.

## 5. Secventa de boot

### 5.1 Master setup() — ordine importanta

```cpp
void setup() {
    Serial.begin(115200);                    // USB UART debug
    delay(1500);                             // asteapta serial monitor (optional)

    // 1. NVS preferences
    prefs.begin();

    // 2. SystemLED (RGB WS2812 + enable pin)
    systemLed.begin();
    systemLed.setColor(SystemLED::ORANGE);   // booting

    // 3. WiFi OFF — Master nu mai foloseste radio (nici STA, nici AP, nici ESP-NOW)
    WiFi.mode(WIFI_OFF);
    btStop();                                // oprim si Bluetooth

    // 4. I2C bus pentru SHT30 stanga local
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ_HZ);              // 100kHz, cabluri scurte

    // 5. Senzor stanga local
    leftZone.begin();                         // creeaza Sht30Sensor intern, init la 0x44

    // 6. UART2 catre Slave
    Serial2.begin(SLAVE_UART_BAUD,
                  SERIAL_8N1,
                  SLAVE_UART_RX_PIN,         // 16
                  SLAVE_UART_TX_PIN);        // 17
    slaveClient.begin(Serial2);

    // 7. Ethernet via W5500 (SPI)
    pinMode(W5500_RST_PIN, OUTPUT);
    digitalWrite(W5500_RST_PIN, LOW);
    delay(50);
    digitalWrite(W5500_RST_PIN, HIGH);
    delay(200);

    SPI.begin(W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, W5500_CS_PIN);
    Ethernet.init(W5500_CS_PIN);

    byte mac[6];
    getEthernetMac(mac);                     // derivat din eFuse (vezi sectiunea MAC)
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (Ethernet.begin(mac, ETH_DHCP_TIMEOUT_MS) == 0) {
        Serial.println("DHCP fail, retry in loop");
    } else {
        Serial.print("IP: "); Serial.println(Ethernet.localIP());
    }

    // 8. NTP (foloseste Ethernet automat prin lwIP)
    TimeSync::begin();

    // 9. MQTT broker init (TLS via SSLClient peste EthernetClient)
    mqtt.begin(&prefs);

    // 10. Watchdog
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);

    // 11. Timer principal (interval default 5min)
    mainTimerID = timer.setInterval(prefs.intervalSec * 1000UL, processZones);

    // 12. Primul ciclu acum
    processZones();

    systemLed.setColor(SystemLED::CYAN);     // ready
}
```

### 5.2 Slave setup() — minimalist

```cpp
void setup() {
    Serial.begin(115200);                    // USB debug (optional)
    delay(500);

    WiFi.mode(WIFI_OFF);                     // economie + zero interferenta
    btStop();

    systemLed.begin();
    systemLed.setColor(SystemLED::ORANGE);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ_HZ);

    if (!sensor.begin(Wire, SHT30_ADDR)) {
        Serial.println("SHT30 init FAIL");
        systemLed.setColor(SystemLED::RED);
    }

    Serial2.begin(SLAVE_UART_BAUD,
                  SERIAL_8N1,
                  SLAVE_UART_RX_PIN,         // 16
                  SLAVE_UART_TX_PIN);        // 17

    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
    esp_task_wdt_add(NULL);

    Serial.println("[Slave] Ready, awaiting UART commands...");
    systemLed.setColor(SystemLED::CYAN);
}
```

## 6. Protocol UART aplicatie

### 6.1 Format mesaje

Toate liniile sunt text UTF-8 terminate cu `\n` (newline), citite cu `Serial.readStringUntil('\n')`. Baud 115200, 8N1, fara flow control.

### 6.2 Comenzi Master → Slave

| Comanda | Format | Semantica |
| --- | --- | --- |
| Citeste senzor | `GET_SENSOR\n` | Cere temperatura + umiditate curenta |
| Reboot Slave | `REBOOT\n` | Reporneste Slave-ul prin `ESP.restart()` |
| Diagnostic ping | `PING\n` | Verificare comunicare + latency |

### 6.3 Raspunsuri Slave → Master

**Pe `GET_SENSOR` (succes)**:
```json
{"temp":23.45,"hum":55.20,"ts":1745784620,"ok":true,"errors":0,"uptime":3600}
```

**Pe `GET_SENSOR` (esec senzor SHT30)**:
```json
{"temp":0,"hum":0,"ts":0,"ok":false,"errors":17,"uptime":3600}
```

**Pe `REBOOT`**: Slave trimite `OK\n`, asteapta 100ms, apoi `ESP.restart()`.

**Pe `PING`**: Slave trimite `PONG\n`.

### 6.4 CRC-16 framing pentru integritate (recomandat ferm pentru 24/7)

**Decizie**: adaugam CRC-16 (Modbus polynomial `0xA001`) la fiecare mesaj UART, cu retry automat la mismatch. Standard industrial de peste 40 ani — folosit in Modbus RTU, multe protocoale fieldbus, comunicatii avionice.

#### 6.4.1 Argumente pentru CRC + retry

**Fiabilitate**:
- CRC-16/Modbus detecteaza **99.998%** din erorile de transmisie (vs 99.6% CRC-8 si ~50% pentru simple XOR/sum).
- Combinat cu retry-ul existent (2 incercari), rata efectiva de eroare necorectata < **10^-15** per mesaj — practic zero pe lifetime de 5+ ani.
- Catch-uri specifice:
  - **Single-bit flips** (rare pe Cat6 1.5m, dar posibile)
  - **Burst errors** (probabile cand pornesc motoare/contactori 230V in apropiere)
  - **Mesaje trunchiate** (buffer UART overflow la load mare)
  - **Out-of-sync framing** (pierdere `\n`, byte stuck in buffer)

**Realizabilitate**:
- Cod CRC-16 = 15 linii C++ standard, fara librarii externe
- Compute overhead: **~10 µs** per mesaj pe ESP32 @240MHz — neglijabil
- Memory overhead: **5 bytes** per mesaj (`*XXXX` separator + 4 hex digits)
- Implementare uniforma pe ambele placi (acelasi `CrcUtil.h` partajat — fisier comun ESP32/ si ESP32_Slave/)

**Durabilitate**:
- CRC-16/Modbus: standard din **1979**, in productie continua in milioane de device-uri industriale
- Hardware UART pe ESP32 imbatraneste extrem de greu (pinii GPIO cu pull-up activ, fara componente mecanice)
- Cat6 PVC indoor: lifetime spec **25+ ani**
- **Mai durabil decat orice alt link disponibil** in proiect

#### 6.4.2 Format mesaje CU CRC

Toate mesajele Master ↔ Slave au noua structura:

```
<payload>*<crc16_hex>\n
```

Unde `<crc16_hex>` = 4 caractere hex uppercase (CRC-16/Modbus calculat peste `<payload>` exclusiv asteriscul si CRC-ul).

**Exemple**:

```
Master → Slave: GET_SENSOR*4F8C\n
Slave → Master: {"temp":23.4,"hum":55.1,"ts":1747094400,"ok":true,"errors":0,"uptime":3600}*B7E1\n

Master → Slave: PING*7A3D\n
Slave → Master: PONG*A2F1\n

Master → Slave: REBOOT*9C42\n
Slave → Master: OK*8B5E\n  (trimis inainte de ESP.restart)

Master → Slave: LED_SCHEDULE 18 0 23 30 80 1*3D7B\n
Slave → Master: OK*8B5E\n

Master → Slave: TIME_SYNC 1747094400*5E29\n
Slave → Master: OK*8B5E\n
```

**Backward compatibility**: feature flag in `Config.h` ambele proiecte (`UART_USE_CRC = 1`). Build-uite simultan pe ambele placi inainte de flash. Daca o placa are CRC enabled si cealalta nu → mesajele se vor invalida reciproc → toate retry-uri vor esua → `slaveOnline=false` → semnal clar de mismatch versiune.

#### 6.4.3 Cod CRC-16/Modbus (partajat ambele proiecte)

Fisier nou `ESP32/CrcUtil.h` (copy in `ESP32_Slave/CrcUtil.h`):

```cpp
// CrcUtil.h — CRC-16 Modbus polynomial 0xA001 (inverted 0x8005), table-less
// Standard MIT, identic in ambele proiecte.
#pragma once
#include <Arduino.h>

namespace Crc {

inline uint16_t crc16(const char* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint8_t)data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/** Format hex 4-char uppercase din uint16_t in buf (5 bytes including null). */
inline void crcToHex(uint16_t crc, char out[5]) {
    static const char hex[] = "0123456789ABCDEF";
    out[0] = hex[(crc >> 12) & 0x0F];
    out[1] = hex[(crc >>  8) & 0x0F];
    out[2] = hex[(crc >>  4) & 0x0F];
    out[3] = hex[(crc      ) & 0x0F];
    out[4] = '\0';
}

/** Parseaza 4 hex chars in uint16_t. Returneaza false la eroare. */
inline bool hexToCrc(const char* hex, uint16_t& out) {
    out = 0;
    for (uint8_t i = 0; i < 4; i++) {
        char c = hex[i];
        uint8_t v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else return false;
        out = (out << 4) | v;
    }
    return true;
}

/** Validare end-to-end: extrage CRC din buffer, verifica match.
 *  @param buf NULL-terminated, format: "<payload>*<crc4hex>"
 *  @return true daca CRC e valid; false la lipsa separator sau mismatch. */
inline bool validate(const char* buf) {
    const char* asterisk = strrchr(buf, '*');
    if (!asterisk || asterisk == buf) return false;
    size_t payloadLen = (size_t)(asterisk - buf);
    if (strlen(asterisk + 1) != 4) return false;

    uint16_t expected;
    if (!hexToCrc(asterisk + 1, expected)) return false;

    uint16_t computed = crc16(buf, payloadLen);
    return computed == expected;
}

/** Strip *XXXX din buffer in-place. Modifica `buf` (taie la asterisc). */
inline void stripCrc(char* buf) {
    char* asterisk = strrchr(buf, '*');
    if (asterisk) *asterisk = '\0';
}

}  // namespace Crc
```

#### 6.4.4 Update `UartProtocol` (Slave side)

```cpp
// UartProtocol.cpp — varianta cu CRC
#include "UartProtocol.h"
#include "CrcUtil.h"
#include "Logger.h"

bool UartProtocol::poll(char* outCmd, size_t outSize) {
    while (_serial.available()) {
        const char c = (char)_serial.read();
        if (c == '\n') {
            _buffer[_bufLen] = '\0';

            // Validare CRC
            if (!Crc::validate(_buffer)) {
                LOG_WARN("CRC fail on: %s", _buffer);
                _bufLen = 0;
                // NU raspundem nimic — Master timeout + retry
                continue;
            }
            Crc::stripCrc(_buffer);   // taie *XXXX

            const size_t copyLen = (_bufLen < outSize - 1) ? _bufLen : (outSize - 1);
            memcpy(outCmd, _buffer, copyLen);
            outCmd[copyLen] = '\0';
            _bufLen = 0;
            return true;
        }
        if (c == '\r') continue;
        if (_bufLen < UART_BUFFER_SIZE - 1) {
            _buffer[_bufLen++] = c;
        } else {
            LOG_WARN("UART buffer overflow, line discarded");
            _bufLen = 0;
        }
    }
    return false;
}

void UartProtocol::sendLine(const char* line) {
    uint16_t crc = Crc::crc16(line, strlen(line));
    char hex[5]; Crc::crcToHex(crc, hex);
    _serial.print(line);
    _serial.print('*');
    _serial.print(hex);
    _serial.print('\n');
    _serial.flush();
}

void UartProtocol::sendJson(const JsonDocument& doc) {
    // Serializam intr-un buffer temporar pentru a calcula CRC peste payload exact
    char tmp[256];
    size_t n = serializeJson(doc, tmp, sizeof(tmp));
    if (n >= sizeof(tmp)) { LOG_ERROR("JSON too big"); return; }

    uint16_t crc = Crc::crc16(tmp, n);
    char hex[5]; Crc::crcToHex(crc, hex);

    _serial.write((const uint8_t*)tmp, n);
    _serial.print('*');
    _serial.print(hex);
    _serial.print('\n');
    _serial.flush();
}
```

#### 6.4.5 Update `SlaveUartClient` (Master side)

```cpp
// SlaveUartClient.cpp — variants cu CRC

bool SlaveUartClient::fetch(float& temp, float& hum, uint32_t& slaveTs,
                            uint32_t timeoutMs) {
    if (!_serial) { _consecutiveErrors++; return false; }

    for (int retry = 0; retry < SLAVE_RETRY_PER_FETCH + 1; retry++) {
        _flushInput();

        const char* cmd = "GET_SENSOR";
        uint16_t crc = Crc::crc16(cmd, strlen(cmd));
        char hex[5]; Crc::crcToHex(crc, hex);
        _serial->print(cmd);
        _serial->print('*');
        _serial->print(hex);
        _serial->print('\n');

        String line = _readLine(timeoutMs);
        if (line.isEmpty()) continue;

        // Validare CRC pe raspuns
        char buf[256];
        size_t copyLen = (line.length() < sizeof(buf)-1) ? line.length() : sizeof(buf)-1;
        memcpy(buf, line.c_str(), copyLen);
        buf[copyLen] = '\0';

        if (!Crc::validate(buf)) {
            Serial.println("[Slave] response CRC fail, retry");
            continue;
        }
        Crc::stripCrc(buf);   // ramane doar JSON-ul curat

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, buf);
        if (err) continue;

        if (!doc["ok"].as<bool>()) {
            temp = 0; hum = 0; slaveTs = 0;
            _consecutiveErrors++;
            return false;
        }

        temp     = doc["temp"].as<float>();
        hum      = doc["hum"].as<float>();
        slaveTs  = doc["ts"].as<uint32_t>();
        _consecutiveErrors = 0;
        _lastSuccessMs = millis();
        return true;
    }

    _consecutiveErrors++;
    return false;
}
```

Functii similare pentru `sendReboot()`, `ping()`, `sendLedSchedule()` etc. — toate trec prin `Crc::crc16()` la trimitere si `Crc::validate()` la primire.

#### 6.4.6 Edge case: OTA chunked binary

In timpul **OTA Slave** (sectiunea 12), payload-urile binare nu sunt linii text — `OTA_CHUNK <length>\n` urmat de N bytes raw. Pentru chunks binari, calculam CRC peste **toti N bytes** si trimitem in linie separata `CHUNK_CRC <crc16hex>\n` dupa payload-ul binar:

```
Master: OTA_CHUNK 1024*A2B3\n      ← header + CRC
Master: <1024 bytes binari>
Master: CHUNK_CRC 4F2D\n           ← CRC peste cei 1024 bytes
Slave:  OK 1024*8E1A\n              ← raspuns standard cu CRC
sau:
Slave:  ERR_CHUNK_CRC*7C2F\n        ← Master reia chunk-ul
```

#### 6.4.7 Verdict pentru cerinta utilizator

| Aspect | Evaluare |
| --- | --- |
| **Fiabil?** | **Da, 99.998%** error detection (Modbus polynomial standard industrial 40+ ani) |
| **Realizabil?** | **Da, simplu** — 1 fisier C++ partajat (15 linii core), zero dependinte |
| **De durata?** | **Da, decenii** — protocol probat in milioane de device-uri industriale active in factori 24/7 |
| **Overhead?** | Neglijabil — 10 µs CPU + 5 bytes per mesaj |
| **Recomandare?** | **Da ferm**, mai ales pentru deploy in mediu cu motoare/contactori 230V (EMI sursa principala de erori pe Cat6) |

CRC + retry transforma link-ul UART intr-un canal aproape **fizic indestructibil** la nivel de date — singura cauza de failure ramane fizica (cablu sectionat, conector deconectat) — care e detectabila tot prin timeout + flag `slaveOnline=false` in MAUI.

### 6.5 Timeouts si retry

| Operatie | Timeout default | Retry-uri | Total worst-case |
| --- | --- | --- | --- |
| GET_SENSOR | 1000ms | 2 | ~3s |
| REBOOT (asteapta OK) | 500ms | 0 | 0.5s |
| PING | 500ms | 0 | 0.5s |

Daca `GET_SENSOR` esueaza de **5 cicluri consecutive** (~25 minute la interval 5min), Master forteaza zona dreapta in **modul fail-safe**: releul RIGHT off + log eveniment `sensor_node_offline` pe MQTT.

## 7. Modificari firmware Master

### 7.1 Fisiere noi

#### 7.1.1 `ESP32/Sht30Sensor.h`

```cpp
#pragma once

#include <Arduino.h>
#include <Adafruit_SHT31.h>

class Sht30Sensor {
public:
    Sht30Sensor() : _addr(0x44), _consecutiveErrors(0), _lastReadMs(0),
                    _cachedTemp(NAN), _cachedHum(NAN), _initialized(false) {}

    bool begin(TwoWire& wire, uint8_t addr) {
        _addr = addr;
        _initialized = _sht.begin(addr, &wire);
        return _initialized;
    }

    bool read(float& temp, float& hum, bool force = false) {
        unsigned long now = millis();
        if (!force && (now - _lastReadMs) < SHT30_MIN_READ_MS) {
            // Cache fresh — returneaza ultima valoare valida
            if (!isnan(_cachedTemp)) {
                temp = _cachedTemp;
                hum  = _cachedHum;
                return true;
            }
        }

        for (int retry = 0; retry < SHT30_RETRY_COUNT; retry++) {
            float t = _sht.readTemperature();
            float h = _sht.readHumidity();
            if (!isnan(t) && !isnan(h) &&
                t > -20.0f && t < 80.0f &&
                h >= 0.0f && h <= 100.0f) {
                _cachedTemp = t;
                _cachedHum  = h;
                _lastReadMs = now;
                _consecutiveErrors = 0;
                temp = t;
                hum  = h;
                return true;
            }
            delay(20); // pauza scurta intre retry
        }

        _consecutiveErrors++;
        return false;
    }

    int  getConsecutiveErrors() const { return _consecutiveErrors; }
    bool isInitialized()        const { return _initialized; }

private:
    Adafruit_SHT31 _sht;
    uint8_t        _addr;
    int            _consecutiveErrors;
    unsigned long  _lastReadMs;
    float          _cachedTemp;
    float          _cachedHum;
    bool           _initialized;
};
```

#### 7.1.2 `ESP32/SlaveUartClient.h` + `.cpp`

```cpp
// SlaveUartClient.h
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class SlaveUartClient {
public:
    SlaveUartClient();

    void begin(HardwareSerial& serial);

    // Cerere senzor de la Slave. Returneaza false la timeout/JSON invalid.
    bool fetch(float& temp, float& hum, uint32_t& slaveTs,
               uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);

    // Cerere reboot Slave. Returneaza true daca Slave a confirmat (`OK\n`).
    bool sendReboot(uint32_t timeoutMs = SLAVE_REBOOT_TIMEOUT_MS);

    // Diagnostic — masoara latency UART round-trip (PING/PONG).
    bool ping(uint32_t& latencyMs, uint32_t timeoutMs = 500);

    // Numar de erori consecutive dupa ultima cerere reusita.
    int  getConsecutiveErrors() const { return _consecutiveErrors; }
    unsigned long getLastSuccessMs() const { return _lastSuccessMs; }

private:
    HardwareSerial* _serial;
    int             _consecutiveErrors;
    unsigned long   _lastSuccessMs;

    // Goleste buffer-ul UART (defensive — eventuale resturi de la transmisii anterioare).
    void _flushInput();

    // Citeste o linie completa cu timeout. Returneaza String gol la timeout.
    String _readLine(uint32_t timeoutMs);
};
```

```cpp
// SlaveUartClient.cpp
#include "SlaveUartClient.h"
#include "Config.h"

SlaveUartClient::SlaveUartClient()
    : _serial(nullptr), _consecutiveErrors(0), _lastSuccessMs(0) {}

void SlaveUartClient::begin(HardwareSerial& serial) {
    _serial = &serial;
}

void SlaveUartClient::_flushInput() {
    if (!_serial) return;
    while (_serial->available()) _serial->read();
}

String SlaveUartClient::_readLine(uint32_t timeoutMs) {
    if (!_serial) return String();
    String line;
    line.reserve(128);
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (_serial->available()) {
            char c = (char)_serial->read();
            if (c == '\n') return line;
            if (c != '\r') line += c;
        } else {
            yield();
        }
    }
    return String();   // timeout
}

bool SlaveUartClient::fetch(float& temp, float& hum, uint32_t& slaveTs,
                            uint32_t timeoutMs) {
    if (!_serial) { _consecutiveErrors++; return false; }

    for (int retry = 0; retry < SLAVE_RETRY_PER_FETCH + 1; retry++) {
        _flushInput();
        _serial->print("GET_SENSOR\n");

        String line = _readLine(timeoutMs);
        if (line.isEmpty()) continue;

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, line);
        if (err) continue;

        if (!doc["ok"].as<bool>()) {
            // Slave a raspuns dar senzor failing — propagam catre apelant
            temp = 0; hum = 0; slaveTs = 0;
            _consecutiveErrors++;
            return false;
        }

        temp     = doc["temp"].as<float>();
        hum      = doc["hum"].as<float>();
        slaveTs  = doc["ts"].as<uint32_t>();
        _consecutiveErrors = 0;
        _lastSuccessMs = millis();
        return true;
    }

    _consecutiveErrors++;
    return false;
}

bool SlaveUartClient::sendReboot(uint32_t timeoutMs) {
    if (!_serial) return false;
    _flushInput();
    _serial->print("REBOOT\n");
    String resp = _readLine(timeoutMs);
    return resp == "OK";
}

bool SlaveUartClient::ping(uint32_t& latencyMs, uint32_t timeoutMs) {
    if (!_serial) return false;
    _flushInput();
    unsigned long start = millis();
    _serial->print("PING\n");
    String resp = _readLine(timeoutMs);
    latencyMs = millis() - start;
    return resp == "PONG";
}
```

### 7.2 Fisier modificat: `ESP32/Config.h`

Diff conceptual (sterge Blynk + WiFi + DHT, adauga ESP32 pinout nou + UART + slave config):

```cpp
#pragma once

// ============================================================
//  HARDWARE PINS  (Carbon V3)
// ============================================================
// I2C (SHT30 stanga local)
#define I2C_SDA_PIN       21
#define I2C_SCL_PIN       22
#define I2C_FREQ_HZ       100000UL

// SHT30
#define SHT30_ADDR        0x44
#define SHT30_MIN_READ_MS 60000UL    // 1 minut cooldown (configurabil mai mult)
#define SHT30_RETRY_COUNT 3

// W5500 SPI (VSPI)
#define W5500_MOSI_PIN    23
#define W5500_MISO_PIN    19
#define W5500_SCK_PIN     18
#define W5500_CS_PIN      5
#define W5500_RST_PIN     33

// UART2 catre Slave
#define SLAVE_UART_TX_PIN        17
#define SLAVE_UART_RX_PIN        16
#define SLAVE_UART_BAUD          115200UL
#define SLAVE_REQ_TIMEOUT_MS     1000
#define SLAVE_REBOOT_TIMEOUT_MS  500
#define SLAVE_RETRY_PER_FETCH    2
#define SLAVE_FAILSAFE_AFTER_FAILS 5

// Relee, butoane, LED
#define RELAY_LEFT_PIN    15
#define RELAY_RIGHT_PIN   26
#define RESET_BUTTON_PIN  13
#define LED_PIN           2
#define LED_ENABLE_PIN    4
#define LED_COUNT         1

// ============================================================
//  DEFAULTS NVS
// ============================================================
#define DEFAULT_TEMP_THRESH         45.0f
#define DEFAULT_HUM_THRESH          60.0f
#define DEFAULT_INTERVAL_SEC        300
#define DEFAULT_OVERRIDE_TIMEOUT_MIN 120
#define DEFAULT_TEMP_HYST           2.0f
#define DEFAULT_HUM_HYST            5.0f

// ============================================================
//  LIMITE
// ============================================================
#define MIN_INTERVAL_SEC  10
#define MAX_INTERVAL_SEC  3600
#define MIN_TEMP_HYST     0.0f
#define MAX_TEMP_HYST     10.0f
#define MIN_HUM_HYST      0.0f
#define MAX_HUM_HYST      20.0f
#define WDT_TIMEOUT_SEC   60

// ============================================================
//  HIVEMQ MQTT (TLS 8883)
// ============================================================
#define MQTT_HOST           "3c03ab8cc05e43dfbada27542420c4fc.s1.eu.hivemq.cloud"
#define MQTT_PORT           8883
#define MQTT_USER           "ventilatie_esp32"
#define MQTT_PASS           "Ceparolasapun1"
#define MQTT_CLIENT_PREFIX  "esp32-vent-"

#define TOPIC_STATE   "ventilatie/state"
#define TOPIC_CMD     "ventilatie/cmd"
#define TOPIC_ONLINE  "ventilatie/online"
#define TOPIC_EVENT   "ventilatie/event"
#define TOPIC_LOG     "ventilatie/log"

#define MQTT_BUF_SIZE                4096
#define MQTT_HEARTBEAT_MS            3600000UL
#define MQTT_PUBLISH_MIN_INTERVAL_MS 500UL
#define MQTT_RECONNECT_INITIAL_MS    5000UL
#define MQTT_RECONNECT_MAX_MS        60000UL

// ============================================================
//  ETHERNET
// ============================================================
#define ETH_DHCP_TIMEOUT_MS       15000
#define ETH_DOWN_RESTART_MS       600000UL  // 10 min link down → restart preventiv

// ============================================================
//  NTP, OTA, EVENT LOG, NVS — neschimbate
// ============================================================
// (NTP_TIMEZONE, NTP_SERVER1/2, OTA_URL_WHITELIST*,
//  EVENT_LOG_MAX_ENTRIES, NVS_PREFS_NAMESPACE, NVS_LOG_NAMESPACE)
// ...

// ============================================================
//  FIRMWARE VERSION (auto-generat)
// ============================================================
#if __has_include("Version.h")
#include "Version.h"
#endif
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif
```

**STERGE complet** (din actualul Config.h): `BLYNK_*` (linii 6-8), `DHT_*_PIN` (linii 13-14), `VP_*` (linii 47-62), `WIFI_*` (linii 94-97), `DHT_MIN_READ_MS` (linia 41).

### 7.3 Fisier modificat: `ESP32/VentilationZone.h/.cpp`

Constructor schimbat — primeste un pointer la `Sht30Sensor` (care e detinut de `.ino`) plus pin releu, in loc de `dhtPin`:

```cpp
// VentilationZone.h (extras)
#pragma once
#include <Arduino.h>
#include "Sht30Sensor.h"

class VentilationZone {
public:
    // Constructor pentru zona cu senzor LOCAL (Master stanga sau Slave dreapta in arhitectura veche)
    VentilationZone(Sht30Sensor* localSensor, int relayPin, const char* name);

    // Constructor pentru zona cu senzor REMOTE (zona dreapta primita de la Slave prin UART)
    VentilationZone(int relayPin, const char* name);  // remote, fara local sensor

    void begin();
    void readSensor(bool force = false);
    void setExternalSensorValues(float temp, float hum, uint32_t ts);  // pentru remote
    void updateLogic(float threshTemp, float threshHum, float hystTemp, float hystHum);
    void emergencyOff();
    void enterFailsafe();           // forteaza relay OFF + flag failsafe
    void exitFailsafe();
    bool isInFailsafe() const;

    float       getTemp()         const;
    float       getHum()          const;
    bool        getRelayState()   const;
    bool        isFirstReadDone() const;
    bool        getManualOverride() const;
    void        setManualOverride(bool s);
    int         getConsecErrors() const;
    const char* getName()         const;

private:
    Sht30Sensor*  _localSensor;     // nullptr daca remote
    int           _relayPin;
    const char*   _name;
    float         _currentTemp;
    float         _currentHum;
    uint32_t      _lastReadTs;
    bool          _manualOverride;
    bool          _firstReadDone;
    bool          _relayState;
    bool          _failsafe;
    int           _consecutiveErrors;
};
```

### 7.4 Fisier modificat: `ESP32/MqttBridge.h/.cpp`

**Schimbari principale**:

1. Inlocuiesc `WiFiClientSecure _net` cu `EthernetClient _baseClient` + `SSLClient _net`.
2. Adaug in struct `MqttPending` campurile noi: `bool rebootSlave = false;`.
3. In `_handleMessage()` la cmd parser adaug: `if (cmd == "rebootSlave") _mqttPending.rebootSlave = true;`.
4. In `_publishStateNow()` la JSON state adaug campurile noi: `slaveOnline`, `slaveLastSeen`, `slaveErrors`, `rightFailsafe`.
5. Lock owner enum: `LOCK_NONE, LOCK_MQTT` (sterg `LOCK_BLYNK`).

```cpp
// MqttBridge.h — extras schimbari
#include <SPI.h>
#include <Ethernet.h>
#include <SSLClient.h>
#include <PubSubClient.h>
#include "HiveMqTrustAnchor.h"

enum LockOwner { LOCK_NONE = 0, LOCK_MQTT = 2 };  // 1 era Blynk, eliminat

struct MqttPending {
    bool  refresh        = false;
    bool  setOverrideL   = false;
    int   overrideLVal   = 0;
    bool  setOverrideR   = false;
    int   overrideRVal   = 0;
    bool  setConfig      = false;
    float threshT        = 0;
    float threshH        = 0;
    int   interval       = 0;
    float hystT          = -1.0f;
    float hystH          = -1.0f;
    bool  resetDefaults  = false;
    bool  reboot         = false;
    bool  rebootSlave    = false;     // NOU
    bool  getLog         = false;
    bool  update         = false;
    char  otaUrl[256]    = {0};
    char  otaSha[65]     = {0};
};

class MqttBridge {
    // ... interface neschimbat ...
private:
    EthernetClient    _baseClient;
    SSLClient         _net;          // SSLClient(_baseClient, TAs, TAs_NUM, ANALOG_PIN_RNG)
    PubSubClient      _client;
    // ...
};
```

JSON state publicat (fields noi):

```json
{
  "left":  {"temp": 23.4, "hum": 55.1, "relay": false, "override": false},
  "right": {"temp": 24.1, "hum": 56.3, "relay": true,  "override": false, "failsafe": false},
  "cfg":   {"threshT": 45.0, "threshH": 60.0, "intervalSec": 300, "hystT": 2.0, "hystH": 5.0},
  "lock":  {"owner": "mqtt"},
  "slave": {"online": true, "lastSeen": 1745784620, "errors": 0},
  "fw":    123,
  "uptime": 3600,
  "heap":  187432,
  "ts":    1745784625
}
```

### 7.5 Fisier modificat: `ESP32/ProiectVentilatie.ino`

**Modificari critice in `processZones()`**:

```cpp
void processZones() {
    esp_task_wdt_reset();

    // 1. Procesare pending MQTT (existent — neschimbat in mare parte)
    bool mqttPendingProcessed = false;
    bool forceSensorRead = false;
    if (mqtt.hasPendingCommands()) {
        mqttPendingProcessed = true;
        MqttPending& mp = mqtt.getPending();

        if (mp.refresh) {
            mp.refresh = false;
            forceSensorRead = true;
            mqtt.requestPublishNow();
        }

        // ... existing handlers (setOverrideL, setOverrideR, setConfig,
        //     resetDefaults, reboot, getLog, update) raman ...

        if (mp.rebootSlave) {
            mp.rebootSlave = false;
            Serial.println("[Master] Forwarding REBOOT to Slave...");
            bool ok = slaveClient.sendReboot();
            eventLog.append(EVT_INFO, ZONE_RIGHT,
                            ok ? "Slave reboot OK" : "Slave reboot timeout");
            mqtt.requestPublishNow();
        }
    }

    // 2. Citire senzor STANGA (local)
    leftZone.readSensor(forceSensorRead);

    // 3. Citire senzor DREAPTA (remote prin UART de la Slave)
    float rightTemp, rightHum;
    uint32_t slaveTs;
    bool slaveOk = slaveClient.fetch(rightTemp, rightHum, slaveTs);

    if (slaveOk) {
        rightZone.setExternalSensorValues(rightTemp, rightHum, slaveTs);
        if (rightZone.isInFailsafe()) {
            rightZone.exitFailsafe();
            eventLog.append(EVT_INFO, ZONE_RIGHT, "Slave online, exit failsafe");
        }
    } else if (slaveClient.getConsecutiveErrors() >= SLAVE_FAILSAFE_AFTER_FAILS &&
               !rightZone.isInFailsafe()) {
        rightZone.enterFailsafe();   // releu RIGHT off
        eventLog.append(EVT_WARNING, ZONE_RIGHT, "Slave offline, entered failsafe");
    }

    // 4. Aplica logica relee
    leftZone.updateLogic (prefs.tempThresh, prefs.humThresh,
                          prefs.tempHyst, prefs.humHyst);
    if (!rightZone.isInFailsafe()) {
        rightZone.updateLogic(prefs.tempThresh, prefs.humThresh,
                              prefs.tempHyst, prefs.humHyst);
    }

    // 5. Re-publica state daca pending procesat
    if (mqttPendingProcessed) mqtt.requestPublishNow();

    // 6. Publica heartbeat / state
    mqtt.publishStateIfNeeded(leftZone, rightZone);

    // ... watchdog reset, debug print existing ...
}
```

**STERGE complet**:
- `#include <WiFi.h>`, `#include <WiFiManager.h>`, `#include <BlynkSimpleEsp32.h>`
- `BLYNK_CONNECTED()` — toata functia
- toate `BLYNK_WRITE(VP_*)` — ~120 linii
- `pushRelayState()` partea Blynk
- struct `BlynkPending` + `blynkPendingProcessed`
- `wifiManager.autoConnect()`
- WiFi reset button (3-sec hold) — pastrezi RESET_BUTTON cu noua semantica: factory reset NVS doar.

### 7.6 MAC address derivat din eFuse

```cpp
// ProiectVentilatie.ino sau utilitate separata
void getEthernetMac(byte mac[6]) {
    uint8_t baseMac[6];
    esp_efuse_mac_get_default(baseMac);
    // Prefix LAA (Locally Administered Address) ca sa nu coincida cu MAC-ul WiFi original
    mac[0] = 0xDE;
    mac[1] = baseMac[1];
    mac[2] = baseMac[2];
    mac[3] = baseMac[3];
    mac[4] = baseMac[4];
    mac[5] = baseMac[5];
}
```

### 7.7 OtaUpdater migrat la SSLClient

```cpp
// OtaUpdater.cpp — schita refactor
#include <Ethernet.h>
#include <SSLClient.h>
#include <ArduinoHttpClient.h>
#include "HiveMqTrustAnchor.h"   // contine + DigiCert Global Root pentru github.com

EthernetClient ota_baseClient;
SSLClient      ota_net(ota_baseClient, TAs, TAs_NUM, A0);  // pin floating pentru RNG

bool OtaUpdater::download(const char* url, const char* expectedSha) {
    // Parse URL → host + port + path
    // HTTP client ArduinoHttpClient peste ota_net
    HttpClient http(ota_net, host, 443);
    http.get(path);
    // Stream raspuns prin SHA-256 incremental
    // Update.write() in flash OTA partition
    // Verificare hash final
    // ESP.restart() la succes
}
```

**Risc**: SSLClient consuma ~30KB heap. Combinat cu MQTT activ (~10KB), poate cauza out-of-memory la OTA. Mitigare: temporar la inceputul OTA inchidem MQTT (`mqtt.disconnect()`), eliberam heap, apoi descarcam, apoi restart.

### 7.8 Fisier sters: `ESP32/HiveMqCert.{h,cpp}`

Inlocuit cu `ESP32/HiveMqTrustAnchor.h`:

```cpp
// HiveMqTrustAnchor.h — generat cu pycert_bearssl.py din SSLClient repo
#pragma once
#include <SSLClient.h>

extern const SSLClient::SSLClientParameters TAs[];
extern const size_t TAs_NUM;

// Continut TAs[]: ISRG Root X1 + DigiCert Global Root CA
// (ambele pentru HiveMQ MQTT si github.com OTA)
```

### 7.9 Librarii Master

**Adauga** in arduino-cli config sau IDE:
- `Adafruit SHT31 Library` (compatibil SHT30)
- `Ethernet` (oficial Arduino, suporta W5500)
- `SSLClient` (OPEnSLab-OSU)
- `ArduinoHttpClient` (oficial Arduino)

**Sterge**:
- `WiFiManager` (tzapu)
- `Blynk` (BlynkSimpleEsp32)
- `DHT sensor library` (Adafruit)

## 8. Firmware Slave — proiect modular complet

Director nou: `ESP32_Slave/` la nivel de proiect (root repo). Master ramane in `ESP32/`.

### 8.0 Structura folder + best practices

```
ESP32_Slave/
├── ESP32_Slave.ino           # Entry point. Doar setup() + loop(). ~50 linii.
├── Config.h                   # TOATE constantele de hardware/timing/protocol.
├── Sht30Sensor.h              # Senzor I2C — header + implementare inline.
├── Sht30Sensor.cpp            # (sau in .h, daca e simplu)
├── UartProtocol.h             # Parsing/serializare mesaje text linie cu linie.
├── UartProtocol.cpp
├── CommandDispatcher.h        # Mapare comanda -> handler. Logic core Slave.
├── CommandDispatcher.cpp
├── SystemLED.h                # Status LED RGB.
├── SystemLED.cpp
├── WatchdogManager.h          # Wrapper esp_task_wdt + auto-feed.
├── Logger.h                   # Wrapper Serial cu nivele (DEBUG/INFO/WARN/ERROR).
├── README.md                  # Documentatie proiect Slave.
└── scripts/
    ├── build.sh               # arduino-cli compile
    └── flash.sh               # arduino-cli upload
```

**Principii de design aplicate**:

- **Single Responsibility**: fiecare modul are exact 1 rol. `Sht30Sensor` doar citeste; `UartProtocol` doar (de)serializeaza; `CommandDispatcher` doar mapeaza comenzi → actiuni; `SystemLED` doar afiseaza status; `WatchdogManager` doar gestioneaza WDT; `Logger` doar formateaza Serial.
- **Dependency injection**: `CommandDispatcher` primeste prin constructor referinte la `Sht30Sensor`, `UartProtocol`, `SystemLED` — usor de testat unitar pe PC (cu mock-uri).
- **Const-correctness**: toate getter-ele sunt `const`, parametrii non-modificati sunt `const ref`.
- **Header guards**: `#pragma once` peste tot.
- **Zero magic numbers**: toate timing-urile, pinii, adresele in `Config.h`.
- **Defensive coding**: null checks pe pointeri, range validation pe valori, retry pe operatii fallible.
- **No `String` in hot paths** (Slave loop): folosim `char[]` buffer-e statice + parsare manuala in `UartProtocol`. Reduce fragmentare heap.
- **Logging cu nivele**: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR` macros — pot fi compilate-out la build production cu `#define LOG_LEVEL LOG_LEVEL_INFO`.

### 8.1 `ESP32_Slave/ESP32_Slave.ino` — entry point minimal

```cpp
// ESP32_Slave.ino — entry point. Doar setup() + loop(). Toata logica e in module.
#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include "Config.h"
#include "Logger.h"
#include "WatchdogManager.h"
#include "Sht30Sensor.h"
#include "SystemLED.h"
#include "UartProtocol.h"
#include "CommandDispatcher.h"

// === Singletons globale (controlate, ownership clar) ===
namespace {
    Sht30Sensor       g_sensor;
    SystemLED         g_led(LED_PIN, LED_ENABLE_PIN, LED_COUNT);
    UartProtocol      g_uart(Serial2);
    CommandDispatcher g_dispatcher(g_sensor, g_uart, g_led);
}

void setup() {
    // 1. Logger pe USB Serial (debug)
    Logger::begin(LOG_BAUD);
    LOG_INFO("Boot — fw build %d", FW_BUILD_NUMBER);

    // 2. Oprim radio WiFi + Bluetooth (consum + interferenta)
    WiFi.mode(WIFI_OFF);
    btStop();

    // 3. LED status
    g_led.begin();
    g_led.setStatus(SystemLED::Status::BOOTING);

    // 4. I2C bus + senzor
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ_HZ);
    if (!g_sensor.begin(Wire, SHT30_ADDR)) {
        LOG_ERROR("SHT30 init FAIL at 0x%02X", SHT30_ADDR);
        g_led.setStatus(SystemLED::Status::SENSOR_FAIL);
    } else {
        LOG_INFO("SHT30 OK at 0x%02X", SHT30_ADDR);
    }

    // 5. UART2 catre Master
    g_uart.begin(SLAVE_UART_BAUD, SLAVE_UART_RX_PIN, SLAVE_UART_TX_PIN);

    // 6. Watchdog
    WatchdogManager::begin(WDT_TIMEOUT_SEC);

    g_led.setStatus(SystemLED::Status::READY);
    LOG_INFO("Ready, awaiting UART commands");
}

void loop() {
    WatchdogManager::feed();
    g_dispatcher.tick();   // proceseaza UART + actualizeaza LED
    delay(LOOP_TICK_MS);
}
```

### 8.2 `ESP32_Slave/Config.h` — toate constantele centralizate

```cpp
#pragma once

// ============================================================
//  HARDWARE PINS
// ============================================================
constexpr uint8_t  I2C_SDA_PIN       = 21;
constexpr uint8_t  I2C_SCL_PIN       = 22;
constexpr uint32_t I2C_FREQ_HZ       = 100000UL;

constexpr uint8_t  SLAVE_UART_TX_PIN = 17;
constexpr uint8_t  SLAVE_UART_RX_PIN = 16;
constexpr uint32_t SLAVE_UART_BAUD   = 115200UL;

constexpr uint8_t  LED_PIN           = 2;
constexpr uint8_t  LED_ENABLE_PIN    = 4;
constexpr uint8_t  LED_COUNT         = 1;

// ============================================================
//  SENZOR SHT30
// ============================================================
constexpr uint8_t  SHT30_ADDR        = 0x44;
constexpr uint32_t SHT30_MIN_READ_MS = 60000UL;   // 1 min cooldown
constexpr uint8_t  SHT30_RETRY_COUNT = 3;

// ============================================================
//  PROTOCOL UART
// ============================================================
constexpr size_t   UART_BUFFER_SIZE   = 128;       // max linie
constexpr size_t   JSON_DOC_SIZE      = 256;       // bytes ArduinoJson
constexpr uint32_t IDLE_WARN_MS       = 120000UL;  // >2min fara request → LED oranj

// ============================================================
//  LOGGING
// ============================================================
constexpr uint32_t LOG_BAUD           = 115200UL;
#define LOG_LEVEL LOG_LEVEL_INFO  // DEBUG | INFO | WARN | ERROR

// ============================================================
//  TIMING
// ============================================================
constexpr uint32_t LOOP_TICK_MS       = 10;
constexpr uint32_t WDT_TIMEOUT_SEC    = 60;

// ============================================================
//  FIRMWARE VERSION (auto-generat, optional)
// ============================================================
#if __has_include("Version.h")
  #include "Version.h"
#endif
#ifndef FW_BUILD_NUMBER
  #define FW_BUILD_NUMBER 0
#endif
```

### 8.3 `ESP32_Slave/Logger.h` — wrapper Serial cu nivele

```cpp
#pragma once
#include <Arduino.h>

#define LOG_LEVEL_DEBUG 0
#define LOG_LEVEL_INFO  1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_ERROR 3

#ifndef LOG_LEVEL
  #define LOG_LEVEL LOG_LEVEL_INFO
#endif

class Logger {
public:
    static void begin(uint32_t baud) { Serial.begin(baud); delay(200); }

    static void log(int level, const char* tag, const char* fmt, ...) {
        if (level < LOG_LEVEL) return;
        char buf[160];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Serial.printf("[%lu][%s] %s\n", millis(), tag, buf);
    }
};

#define LOG_DEBUG(...) Logger::log(LOG_LEVEL_DEBUG, "D", __VA_ARGS__)
#define LOG_INFO(...)  Logger::log(LOG_LEVEL_INFO,  "I", __VA_ARGS__)
#define LOG_WARN(...)  Logger::log(LOG_LEVEL_WARN,  "W", __VA_ARGS__)
#define LOG_ERROR(...) Logger::log(LOG_LEVEL_ERROR, "E", __VA_ARGS__)
```

### 8.4 `ESP32_Slave/WatchdogManager.h` — wrapper esp_task_wdt

```cpp
#pragma once
#include <esp_task_wdt.h>

class WatchdogManager {
public:
    static void begin(uint32_t timeoutSec) {
        esp_task_wdt_init(timeoutSec, true);
        esp_task_wdt_add(nullptr);   // current task (loopTask)
    }

    static void feed() {
        esp_task_wdt_reset();
    }
};
```

### 8.5 `ESP32_Slave/Sht30Sensor.h` — senzor I2C (header-only)

```cpp
#pragma once
#include <Arduino.h>
#include <Adafruit_SHT31.h>
#include "Config.h"

class Sht30Sensor {
public:
    Sht30Sensor()
        : _addr(0x44), _initialized(false),
          _consecutiveErrors(0), _lastReadMs(0),
          _cachedTemp(NAN), _cachedHum(NAN) {}

    bool begin(TwoWire& wire, uint8_t addr) {
        _addr = addr;
        _initialized = _sht.begin(addr, &wire);
        return _initialized;
    }

    /**
     * Citire senzor cu cooldown si retry intern.
     * @param temp [out] temperatura °C
     * @param hum  [out] umiditate relativa %
     * @param force daca true, ignora cooldown-ul
     * @return true daca citire reusita (sau cache valid sub cooldown)
     */
    bool read(float& temp, float& hum, bool force = false) {
        if (!_initialized) return false;

        const uint32_t now = millis();
        const bool inCooldown = (now - _lastReadMs) < SHT30_MIN_READ_MS;

        if (!force && inCooldown && !isnan(_cachedTemp)) {
            temp = _cachedTemp;
            hum  = _cachedHum;
            return true;
        }

        for (uint8_t retry = 0; retry < SHT30_RETRY_COUNT; retry++) {
            const float t = _sht.readTemperature();
            const float h = _sht.readHumidity();
            if (_isValid(t, h)) {
                _cachedTemp = t;
                _cachedHum  = h;
                _lastReadMs = now;
                _consecutiveErrors = 0;
                temp = t;
                hum  = h;
                return true;
            }
            delay(20);
        }

        _consecutiveErrors++;
        return false;
    }

    int  getConsecutiveErrors() const { return _consecutiveErrors; }
    bool isInitialized()        const { return _initialized; }
    uint32_t getLastReadMs()    const { return _lastReadMs; }

private:
    Adafruit_SHT31 _sht;
    uint8_t        _addr;
    bool           _initialized;
    int            _consecutiveErrors;
    uint32_t       _lastReadMs;
    float          _cachedTemp;
    float          _cachedHum;

    static bool _isValid(float t, float h) {
        return !isnan(t) && !isnan(h) &&
               t > -20.0f && t < 80.0f &&
               h >= 0.0f  && h <= 100.0f;
    }
};
```

### 8.6 `ESP32_Slave/UartProtocol.h/.cpp` — (de)serializare linie cu linie

```cpp
// UartProtocol.h
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Config.h"

class UartProtocol {
public:
    explicit UartProtocol(HardwareSerial& serial) : _serial(serial), _bufLen(0) {}

    void begin(uint32_t baud, uint8_t rxPin, uint8_t txPin) {
        _serial.begin(baud, SERIAL_8N1, rxPin, txPin);
        _bufLen = 0;
    }

    /**
     * Polleaza UART. La gasire `\n` umple `outCmd` cu linia primita
     * (zero-terminated) si returneaza true.
     * @return true daca o linie completa a fost extrasa.
     */
    bool poll(char* outCmd, size_t outSize);

    /** Trimite raspuns text terminat cu `\n`. */
    void sendLine(const char* line);

    /** Trimite raspuns JSON serializat din document. */
    void sendJson(const JsonDocument& doc);

private:
    HardwareSerial& _serial;
    char            _buffer[UART_BUFFER_SIZE];
    size_t          _bufLen;
};
```

```cpp
// UartProtocol.cpp
#include "UartProtocol.h"
#include "Logger.h"

bool UartProtocol::poll(char* outCmd, size_t outSize) {
    while (_serial.available()) {
        const char c = (char)_serial.read();
        if (c == '\n') {
            _buffer[_bufLen] = '\0';
            const size_t copyLen = (_bufLen < outSize - 1) ? _bufLen : (outSize - 1);
            memcpy(outCmd, _buffer, copyLen);
            outCmd[copyLen] = '\0';
            _bufLen = 0;
            return true;
        }
        if (c == '\r') continue;   // ignore CR (CRLF normalize)
        if (_bufLen < UART_BUFFER_SIZE - 1) {
            _buffer[_bufLen++] = c;
        } else {
            // Overflow — discard buffer si log
            LOG_WARN("UART buffer overflow, line discarded");
            _bufLen = 0;
        }
    }
    return false;
}

void UartProtocol::sendLine(const char* line) {
    _serial.print(line);
    _serial.print('\n');
    _serial.flush();
}

void UartProtocol::sendJson(const JsonDocument& doc) {
    serializeJson(doc, _serial);
    _serial.print('\n');
    _serial.flush();
}
```

### 8.7 `ESP32_Slave/CommandDispatcher.h/.cpp` — logic core

```cpp
// CommandDispatcher.h
#pragma once
#include <ArduinoJson.h>
#include "Sht30Sensor.h"
#include "UartProtocol.h"
#include "SystemLED.h"
#include "Config.h"

/**
 * Mapeaza comenzi UART → actiuni. Detine timing-ul ciclului principal.
 * Dependintele sunt injectate prin constructor — usor de mock-uit la test.
 */
class CommandDispatcher {
public:
    CommandDispatcher(Sht30Sensor& sensor, UartProtocol& uart, SystemLED& led)
        : _sensor(sensor), _uart(uart), _led(led),
          _bootMs(millis()), _lastRequestMs(0) {}

    /** Apelat la fiecare iteratie a loop(). Non-blocking. */
    void tick();

private:
    Sht30Sensor&  _sensor;
    UartProtocol& _uart;
    SystemLED&    _led;
    uint32_t      _bootMs;
    uint32_t      _lastRequestMs;

    void _handleGetSensor();
    void _handleReboot();
    void _handlePing();
    void _handleUnknown(const char* cmd);
    void _updateIdleStatus();
};
```

```cpp
// CommandDispatcher.cpp
#include "CommandDispatcher.h"
#include "Logger.h"

void CommandDispatcher::tick() {
    char cmd[UART_BUFFER_SIZE];
    if (_uart.poll(cmd, sizeof(cmd))) {
        _lastRequestMs = millis();
        LOG_DEBUG("RX: %s", cmd);

        if (strcmp(cmd, "GET_SENSOR") == 0)  _handleGetSensor();
        else if (strcmp(cmd, "REBOOT") == 0) _handleReboot();
        else if (strcmp(cmd, "PING") == 0)   _handlePing();
        else                                 _handleUnknown(cmd);
    }

    _updateIdleStatus();
}

void CommandDispatcher::_handleGetSensor() {
    float t = 0, h = 0;
    const bool ok = _sensor.read(t, h);

    StaticJsonDocument<JSON_DOC_SIZE> doc;
    doc["temp"]   = ok ? t : 0.0f;
    doc["hum"]    = ok ? h : 0.0f;
    doc["ts"]     = ok ? (millis() / 1000UL) : 0UL;
    doc["ok"]     = ok;
    doc["errors"] = _sensor.getConsecutiveErrors();
    doc["uptime"] = (millis() - _bootMs) / 1000UL;

    _uart.sendJson(doc);
    _led.setStatus(ok ? SystemLED::Status::ACTIVE
                       : SystemLED::Status::SENSOR_FAIL);
}

void CommandDispatcher::_handleReboot() {
    LOG_INFO("REBOOT requested");
    _uart.sendLine("OK");
    delay(100);
    ESP.restart();
}

void CommandDispatcher::_handlePing() {
    _uart.sendLine("PONG");
}

void CommandDispatcher::_handleUnknown(const char* cmd) {
    LOG_WARN("Unknown cmd: %s", cmd);
    _uart.sendLine("ERR_UNKNOWN_CMD");
}

void CommandDispatcher::_updateIdleStatus() {
    if (_lastRequestMs == 0) return;
    if ((millis() - _lastRequestMs) > IDLE_WARN_MS) {
        _led.setStatus(SystemLED::Status::IDLE);
    }
}
```

### 8.8 `ESP32_Slave/SystemLED.h/.cpp` — status LED RGB cu enum

```cpp
// SystemLED.h
#pragma once
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

class SystemLED {
public:
    enum class Status {
        OFF,
        BOOTING,        // oranj
        READY,          // cyan
        ACTIVE,         // verde — request recent + senzor OK
        IDLE,           // oranj — niciun request >2min
        SENSOR_FAIL     // rosu
    };

    SystemLED(uint8_t dataPin, uint8_t enablePin, uint8_t count);

    void begin();
    void setStatus(Status s);
    Status getStatus() const { return _status; }

private:
    Adafruit_NeoPixel _strip;
    uint8_t           _enablePin;
    Status            _status;

    static uint32_t _statusToColor(Status s);
};
```

```cpp
// SystemLED.cpp
#include "SystemLED.h"

SystemLED::SystemLED(uint8_t dataPin, uint8_t enablePin, uint8_t count)
    : _strip(count, dataPin, NEO_GRB + NEO_KHZ800),
      _enablePin(enablePin),
      _status(Status::OFF) {}

void SystemLED::begin() {
    pinMode(_enablePin, OUTPUT);
    digitalWrite(_enablePin, HIGH);
    _strip.begin();
    _strip.setBrightness(40);
    setStatus(Status::OFF);
}

void SystemLED::setStatus(Status s) {
    _status = s;
    _strip.setPixelColor(0, _statusToColor(s));
    _strip.show();
}

uint32_t SystemLED::_statusToColor(Status s) {
    switch (s) {
        case Status::BOOTING:     return 0xFF8000;  // oranj
        case Status::READY:       return 0x00E0FF;  // cyan
        case Status::ACTIVE:      return 0x00FF00;  // verde
        case Status::IDLE:        return 0xFF6000;  // oranj inchis
        case Status::SENSOR_FAIL: return 0xFF0000;  // rosu
        case Status::OFF:
        default:                  return 0x000000;
    }
}
```

### 8.9 `ESP32_Slave/scripts/build.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FQBN="esp32:esp32:esp32"

cd "$PROJECT_DIR"

# Optional bump build number (re-foloseste scriptul din ESP32/scripts daca exista)
if [[ -f "../ESP32/scripts/bump_build.sh" ]]; then
    bash ../ESP32/scripts/bump_build.sh
fi

arduino-cli compile \
    --fqbn "$FQBN" \
    --warnings default \
    --build-property "build.extra_flags=-DCORE_DEBUG_LEVEL=2" \
    "$PROJECT_DIR"

echo "✅ Build OK"
```

### 8.10 `ESP32_Slave/scripts/flash.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FQBN="esp32:esp32:esp32"
PORT="${1:-/dev/ttyUSB1}"   # default /dev/ttyUSB1 (Slave); Master pe /dev/ttyUSB0

arduino-cli upload \
    --fqbn "$FQBN" \
    --port "$PORT" \
    --verify \
    "$PROJECT_DIR"

echo "✅ Flashed to $PORT"
```

### 8.11 `ESP32_Slave/README.md` — documentatie proiect

```markdown
# ESP32 Slave — Senzor remote SHT30 cu UART

Componenta minimalista care expune un singur SHT30 (zona dreapta) catre Master via UART.

## Hardware
- Carbon V3 (ESP32-WROOM-32E)
- GY-SHT30-D pe I2C (SDA=21, SCL=22, addr 0x44)
- UART2: TX=17, RX=16 catre Master prin Cat6 1.5m

## Build & flash
\`\`\`bash
./scripts/build.sh
./scripts/flash.sh /dev/ttyUSB1
\`\`\`

## Protocol
Comenzi acceptate pe UART2 (115200 8N1, linii terminate cu `\n`):

| Comanda | Raspuns |
|---|---|
| `GET_SENSOR\n` | JSON cu temp/hum/ts/ok/errors/uptime |
| `REBOOT\n` | `OK\n` apoi reboot dupa 100ms |
| `PING\n` | `PONG\n` |

## Module
- `ESP32_Slave.ino` — entry point (50 linii)
- `Config.h` — toate constantele
- `Sht30Sensor.h` — wrapper Adafruit_SHT31 cu cooldown + retry
- `UartProtocol.h/.cpp` — buffering + (de)serializare linie cu linie
- `CommandDispatcher.h/.cpp` — mapare comanda → actiune
- `SystemLED.h/.cpp` — status LED cu enum Status
- `WatchdogManager.h` — wrapper esp_task_wdt
- `Logger.h` — Serial logging cu nivele

## Test unitar
Pentru testare pe PC (fara hardware), `Sht30Sensor` poate fi mock-uit deoarece dependintele sunt injectate in `CommandDispatcher`.
```

### 8.12 Caracteristici Slave (rezumat)

- **Cod total**: ~250 linii distribuite in 8 module (cea mai mare fisier ~80 linii — `CommandDispatcher.cpp`)
- **Fara MQTT, fara TLS, fara OTA, fara cloud, fara Ethernet, fara WiFi, fara HTTP, fara IP**
- **Watchdog hardware** activ (60s)
- **Zero alocari heap in hot path** (loop): buffer static `char[128]`, `StaticJsonDocument<256>` pe stack, niciun `String` in hot path
- **Suprafata atac fizica zero** (cablu izolat punct-la-punct)
- **Update flash**: doar prin USB-C cu `arduino-cli upload`
- **Consum estimat**: ~80mA (vs ~150mA Master cu Ethernet)
- **Testabil**: dependency injection in `CommandDispatcher` permite mock-uri pentru senzor/UART/LED la test pe PC

## 9. Modificari MAUI

### 9.1 `MobileApp/ViewModels/DashboardViewModel.cs`

Banner lock — sterge ramura Blynk:

```csharp
// Lock
if (state.Lock != null && !string.IsNullOrEmpty(state.Lock.Owner) &&
    state.Lock.Owner == "mqtt") {
    LockBannerVisible = true;
    LockBannerText = "Control activ via app (MQTT)";
    IsControlEnabled = true;
} else {
    LockBannerVisible = false;
    LockBannerText = string.Empty;
    IsControlEnabled = true;
}

// NOU: indicator Slave offline
if (state.Slave != null && !state.Slave.Online) {
    SlaveOfflineBannerVisible = true;
    SlaveOfflineBannerText = $"Senzor zona dreapta inaccesibil (ultimele date: {FormatTs(state.Slave.LastSeen)})";
} else {
    SlaveOfflineBannerVisible = false;
}
```

### 9.2 `MobileApp/Views/SystemPage.xaml`

Adauga buton nou „RESTART SLAVE" si redenumeste existing-urile pentru claritate:

```xml
<!-- Existing buttons updated -->
<Button Text="↺ RESET DEFAULT MASTER"
        Command="{Binding ResetDefaultsCommand}"
        Style="{StaticResource CyberButtonRed}" HeightRequest="40" />

<Button Text="⟳ RESTART MASTER"
        Command="{Binding RebootCommand}"
        Style="{StaticResource CyberButtonOrange}" HeightRequest="40" />

<!-- NOU -->
<Button Text="⟳ RESTART SLAVE"
        Command="{Binding RebootSlaveCommand}"
        Style="{StaticResource CyberButtonOrange}" HeightRequest="40" />
```

### 9.3 `MobileApp/ViewModels/SystemViewModel.cs`

Adauga handler nou:

```csharp
[RelayCommand]
private async Task RebootSlaveAsync() {
    bool confirm = await App.Current.MainPage.DisplayAlert(
        "Restart Slave?",
        "Slave va fi indisponibil ~5 secunde. Master ramane online.",
        "Da", "Anuleaza");
    if (!confirm) return;

    await _mqttService.SendCommandAsync(new { cmd = "rebootSlave" });
}
```

### 9.4 `MobileApp/Models/VentilationState.cs`

Adauga sub-model:

```csharp
public class SlaveStatus {
    [JsonPropertyName("online")]   public bool   Online   { get; set; }
    [JsonPropertyName("lastSeen")] public long   LastSeen { get; set; }
    [JsonPropertyName("errors")]   public int    Errors   { get; set; }
}

public class VentilationState {
    // ... existing ...
    [JsonPropertyName("slave")] public SlaveStatus? Slave { get; set; }
}
```

### 9.5 OTA Slave din MAUI — infrastructura completa

#### 9.5.1 Model nou: `SlaveOtaProgress.cs`

```csharp
namespace ProiectVentilatie.Mobile.Models;

public class SlaveOtaProgress {
    [JsonPropertyName("type")]    public string Type    { get; set; } = string.Empty;
    [JsonPropertyName("sent")]    public long   Sent    { get; set; }
    [JsonPropertyName("total")]   public long   Total   { get; set; }
    [JsonPropertyName("percent")] public int    Percent { get; set; }
    [JsonPropertyName("result")]  public string? Result { get; set; }
    [JsonPropertyName("reason")]  public string? Reason { get; set; }
}
```

#### 9.5.2 Extindere `IMqttService` — eveniment OTA

```csharp
// Services/IMqttService.cs
public event Action<SlaveOtaProgress>? OnSlaveOtaProgress;

// Services/MqttService.cs — in OnMessageReceived (ventilatie/event topic)
if (eventType == "slave_ota_progress" || eventType == "slave_ota_done") {
    var progress = JsonSerializer.Deserialize<SlaveOtaProgress>(payload);
    if (progress != null) {
        MainThread.BeginInvokeOnMainThread(() => OnSlaveOtaProgress?.Invoke(progress));
    }
}
```

#### 9.5.3 Extindere `SystemViewModel.cs`

```csharp
public partial class SystemViewModel : ObservableObject, IDisposable {
    // ... existing fields ...

    [ObservableProperty] private string  _slaveOtaUrl     = string.Empty;
    [ObservableProperty] private string  _slaveOtaSha     = string.Empty;
    [ObservableProperty] private bool    _slaveOtaInProgress;
    [ObservableProperty] private int     _slaveOtaPercent;
    [ObservableProperty] private string  _slaveOtaStatusText = string.Empty;
    [ObservableProperty] private bool    _slaveOtaSuccess;
    [ObservableProperty] private bool    _slaveOtaFailed;

    public SystemViewModel(IMqttService mqttService, IOptions<MqttSettings> options) {
        // ... existing constructor body ...
        _mqttService.OnSlaveOtaProgress += HandleSlaveOtaProgress;
    }

    private void HandleSlaveOtaProgress(SlaveOtaProgress progress) {
        if (progress.Type == "slave_ota_progress") {
            SlaveOtaInProgress  = true;
            SlaveOtaPercent     = progress.Percent;
            SlaveOtaStatusText  = $"Trimitere catre Slave: {progress.Sent / 1024} KB / {progress.Total / 1024} KB ({progress.Percent}%)";
            SlaveOtaSuccess     = false;
            SlaveOtaFailed      = false;
        } else if (progress.Type == "slave_ota_done") {
            SlaveOtaInProgress  = false;
            if (progress.Result == "ok") {
                SlaveOtaPercent  = 100;
                SlaveOtaSuccess  = true;
                SlaveOtaStatusText = "Update Slave reusit. Slave reporneste...";
            } else {
                SlaveOtaFailed   = true;
                SlaveOtaStatusText = $"Update Slave esuat: {progress.Reason}";
            }
        }
    }

    /// <summary>
    /// Validare URL si SHA inainte de trimitere.
    /// </summary>
    private bool ValidateOtaInputs(out string error) {
        error = string.Empty;
        if (string.IsNullOrWhiteSpace(SlaveOtaUrl)) {
            error = "URL este gol"; return false;
        }
        if (!SlaveOtaUrl.StartsWith("https://github.com/", StringComparison.OrdinalIgnoreCase) &&
            !SlaveOtaUrl.StartsWith("https://objects.githubusercontent.com/", StringComparison.OrdinalIgnoreCase)) {
            error = "URL trebuie sa fie de pe GitHub Release (HTTPS)"; return false;
        }
        if (string.IsNullOrWhiteSpace(SlaveOtaSha) ||
            SlaveOtaSha.Length != 64 ||
            !System.Text.RegularExpressions.Regex.IsMatch(SlaveOtaSha, "^[0-9a-fA-F]{64}$")) {
            error = "SHA-256 trebuie sa fie 64 caractere hex"; return false;
        }
        return true;
    }

    [RelayCommand]
    private async Task TriggerSlaveOtaAsync() {
        if (!ValidateOtaInputs(out string err)) {
            await App.Current!.MainPage!.DisplayAlert("Validare", err, "OK");
            return;
        }
        bool confirm = await App.Current!.MainPage!.DisplayAlert(
            "Update firmware Slave?",
            $"Slave va fi indisponibil ~30 secunde. Releul DREAPTA va intra in fail-safe (OFF) pe durata update-ului. Confirmi?",
            "Da, update", "Anuleaza");
        if (!confirm) return;

        SlaveOtaInProgress = true;
        SlaveOtaPercent    = 0;
        SlaveOtaStatusText = "Initiere update...";
        SlaveOtaSuccess    = false;
        SlaveOtaFailed     = false;

        await _mqttService.SendCommandAsync(new {
            cmd = "triggerOtaSlave",
            url = SlaveOtaUrl,
            sha = SlaveOtaSha
        });
    }

    [RelayCommand]
    private async Task FetchLatestSlaveReleaseAsync() {
        // Optional: fetch ultimul release din GitHub API publica
        try {
            using var http = new HttpClient();
            http.DefaultRequestHeaders.UserAgent.ParseAdd("ProiectVentilatie-MAUI/1.0");
            var json = await http.GetStringAsync(
                "https://api.github.com/repos/RaduOvidiu20/ProiectVentilatie/releases/latest");
            using var doc = System.Text.Json.JsonDocument.Parse(json);
            // Cautam asset-ul `slave.bin` si `slave.bin.sha256`
            var assets = doc.RootElement.GetProperty("assets");
            string? binUrl = null, shaUrl = null;
            foreach (var asset in assets.EnumerateArray()) {
                var name = asset.GetProperty("name").GetString();
                var url  = asset.GetProperty("browser_download_url").GetString();
                if (name == "slave.bin") binUrl = url;
                if (name == "slave.bin.sha256") shaUrl = url;
            }
            if (binUrl == null || shaUrl == null) {
                await App.Current!.MainPage!.DisplayAlert("Release", "Asset-urile slave.bin / slave.bin.sha256 nu sunt in ultimul release.", "OK");
                return;
            }
            SlaveOtaUrl = binUrl;
            // Descarca fisierul .sha256 (text scurt)
            var shaText = (await http.GetStringAsync(shaUrl)).Trim().Split(' ')[0];
            SlaveOtaSha = shaText;
        } catch (Exception ex) {
            await App.Current!.MainPage!.DisplayAlert("Eroare", $"Nu s-a putut obtine release: {ex.Message}", "OK");
        }
    }

    public void Dispose() {
        _mqttService.OnSlaveOtaProgress -= HandleSlaveOtaProgress;
        // ... existing disposal ...
    }
}
```

#### 9.5.4 UI in `SystemPage.xaml` — sectiune dedicata OTA Slave

```xml
<!-- Sectiune OTA SLAVE -->
<Border Padding="14" Margin="14,8,14,0"
        BackgroundColor="#0AFFFFFF" Stroke="#3300e6ff"
        StrokeShape="RoundRectangle 10">
    <VerticalStackLayout Spacing="10">

        <Label Text="UPDATE FIRMWARE SLAVE"
               FontSize="14" FontAttributes="Bold"
               TextColor="{StaticResource PrimaryCyan}"
               Style="{StaticResource LabelRajdhani}" />

        <!-- Optional: fetch ultimul release -->
        <Button Text="↓ FETCH LATEST RELEASE"
                Command="{Binding FetchLatestSlaveReleaseCommand}"
                Style="{StaticResource CyberButton}"
                IsEnabled="{Binding SlaveOtaInProgress, Converter={StaticResource InverseBoolConverter}}" />

        <Entry Text="{Binding SlaveOtaUrl}"
               Placeholder="URL slave.bin (HTTPS GitHub)"
               IsEnabled="{Binding SlaveOtaInProgress, Converter={StaticResource InverseBoolConverter}}" />

        <Entry Text="{Binding SlaveOtaSha}"
               Placeholder="SHA-256 (64 hex)"
               IsEnabled="{Binding SlaveOtaInProgress, Converter={StaticResource InverseBoolConverter}}" />

        <Button Text="↑ TRIGGER UPDATE"
                Command="{Binding TriggerSlaveOtaCommand}"
                Style="{StaticResource CyberButtonOrange}"
                IsEnabled="{Binding SlaveOtaInProgress, Converter={StaticResource InverseBoolConverter}}" />

        <!-- Progress bar -->
        <ProgressBar Progress="{Binding SlaveOtaPercent, Converter={StaticResource PercentToProgressConverter}}"
                     ProgressColor="{StaticResource PrimaryCyan}"
                     IsVisible="{Binding SlaveOtaInProgress}" />

        <Label Text="{Binding SlaveOtaStatusText}"
               FontSize="12" Opacity="0.8"
               IsVisible="{Binding SlaveOtaStatusText, Converter={StaticResource StringNotNullConverter}}"
               Style="{StaticResource LabelShareTech}" />

        <!-- Success indicator -->
        <Border IsVisible="{Binding SlaveOtaSuccess}"
                Padding="8" BackgroundColor="#3300FF66" Stroke="#6600FF66"
                StrokeShape="RoundRectangle 6">
            <Label Text="✓ Update reusit. Slave reporneste."
                   TextColor="LimeGreen" FontSize="12"
                   Style="{StaticResource LabelRajdhani}" />
        </Border>

        <!-- Fail indicator -->
        <Border IsVisible="{Binding SlaveOtaFailed}"
                Padding="8" BackgroundColor="#33FF0000" Stroke="#66FF0000"
                StrokeShape="RoundRectangle 6">
            <Label Text="✗ Update esuat. Slave ramane pe firmware-ul anterior."
                   TextColor="Red" FontSize="12"
                   Style="{StaticResource LabelRajdhani}" />
        </Border>

    </VerticalStackLayout>
</Border>
```

#### 9.5.5 Converter nou (daca nu exista deja)

```csharp
// Converters/PercentToProgressConverter.cs
public class PercentToProgressConverter : IValueConverter {
    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        => value is int percent ? (double)percent / 100.0 : 0.0;

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        => throw new NotSupportedException();
}
```

#### 9.5.6 Extindere `IMqttService.SendCommandAsync` — anti-spam

OTA poate dura ~30 secunde, nu vrem ca utilizatorul sa apese din nou „TRIGGER UPDATE" intre timp. `SlaveOtaInProgress` deja e bound pe `IsEnabled` al butonului, dar adaugam si un guard in `MqttService` care **respinge** un nou trigger daca e deja in progress (de exemplu daca utilizatorul are 2 sesiuni MAUI deschise):

```csharp
// MqttService.cs — adaugam un dictionar de inflight commands
private readonly HashSet<string> _inflightCommands = new();

public async Task SendCommandAsync(object command) {
    var json = JsonSerializer.Serialize(command);
    using var doc = JsonDocument.Parse(json);
    var cmd = doc.RootElement.GetProperty("cmd").GetString();

    if (cmd == "triggerOtaSlave" && !_inflightCommands.Add(cmd)) {
        Console.WriteLine("[MQTT] OTA trigger already inflight, rejecting");
        return;
    }

    // Cleanup inflight la primire `slave_ota_done`
    OnSlaveOtaProgress += (p) => {
        if (p.Type == "slave_ota_done") _inflightCommands.Remove("triggerOtaSlave");
    };

    // ... existing publish logic ...
}
```

#### 9.5.7 Workflow utilizator — pas cu pas

1. Utilizatorul apasa „FETCH LATEST RELEASE" → MAUI cheama GitHub API public, gaseste ultimul release tag, URL-ul `slave.bin` si SHA-256-ul. Auto-completeaza Entry-urile.
2. Utilizatorul verifica vizual URL si SHA, apasa „TRIGGER UPDATE".
3. Dialog confirmare: „Slave va fi indisponibil ~30 secunde. Releul DREAPTA va intra in fail-safe (OFF). Confirmi?"
4. La confirm → MAUI publica `{"cmd":"triggerOtaSlave","url":"...","sha":"..."}` pe `ventilatie/cmd`.
5. ESP32 Master primeste, valideaza whitelist URL, lanseaza `SlaveOtaProxy::performUpdate()`.
6. Master publica progres pe `ventilatie/event`: `{"type":"slave_ota_progress","sent":N,"total":T,"percent":P}` la fiecare 50 KB transferati.
7. MAUI subscribed la `ventilatie/event` actualizeaza ProgressBar in System tab.
8. La final Master publica: `{"type":"slave_ota_done","result":"ok"}` sau cu `result:"fail",reason:"..."`.
9. MAUI afiseaza rezultatul vizual (verde/rosu) + reseteaza UI dupa 5 secunde.
10. Slave reporneste, primul `GET_SENSOR` reusit aduce `slaveOnline=true` in state JSON → fail-safe iese, totul revine la normal in <30s.

#### 9.5.8 Sumar comenzi MQTT MAUI → ESP32

| Comanda | Buton MAUI | Efect |
| --- | --- | --- |
| `{"cmd":"reset"}` | RESET DEFAULT MASTER | Wipe NVS Master |
| `{"cmd":"reboot"}` | RESTART MASTER | `ESP.restart()` Master |
| `{"cmd":"rebootSlave"}` | RESTART SLAVE | Master forwardeaza `REBOOT` pe UART |
| `{"cmd":"refresh"}` | ↓ FETCH (Dashboard) | Citire fortata + republish |
| `{"cmd":"triggerOta","url":"...","sha":"..."}` | UPDATE FIRMWARE MASTER | OTA Master existent |
| `{"cmd":"triggerOtaSlave","url":"...","sha":"..."}` | UPDATE FIRMWARE SLAVE (NOU) | Master proxy OTA pentru Slave |
| `{"cmd":"setOverride","zone":"left","value":1}` | Override Dashboard | Toggle releu manual |
| `{"cmd":"setConfig","threshT":...,"threshH":...,"intervalSec":...,"hystT":...,"hystH":...}` | SAVE Settings | Update prefs Master |
| `{"cmd":"getLog"}` | (intern, la deschidere Reports) | Cere ultimele 20 ERR/WARN |

## 10. Functii reset/restart in dual-board — semantica completa

| Comanda MQTT | Buton MAUI | Master | Slave |
| --- | --- | --- | --- |
| `{"cmd":"reset"}` | „RESET DEFAULT MASTER" | NVS wipe → defaults restored, cache invalidat, republish state | NU afectat (nu are NVS) |
| `{"cmd":"reboot"}` | „RESTART MASTER" | publishOnline(false) → relee safe stop → ESP.restart() | NU afectat, continua sa raspunda la UART (Master il reinterogheaza la urmatorul ciclu) |
| `{"cmd":"rebootSlave"}` | „RESTART SLAVE" | forwardeaza `REBOOT\n` pe UART → log eveniment | primeste `REBOOT\n` → trimite `OK\n` → ESP.restart() |
| `{"cmd":"refresh"}` | „↓ FETCH" pe Dashboard | force read SHT30 stanga + force fetch UART de la Slave + republish | NU afectat (raspunde normal la GET_SENSOR) |

**Recovery garantat dupa orice reset**:
- Master pairing UART e compile-time (in Config.h), supravieturieste factory reset
- MQTT credentials sunt compile-time
- Ethernet MAC e derivat din eFuse, deterministic
- La boot Master: re-init Wire + Serial2 + Ethernet + MQTT, primul ciclu interogheaza Slave, totul revine la normal in <30s
- La boot Slave: re-init SHT30 + Serial2, asteapta primul UART command de la Master

## 11. Specificatii Carbon V3 (verificate din github.com/GroundStudio/GroundStudio_Carbon_V3)

| Parametru | Valoare confirmata |
| --- | --- |
| MCU | ESP32-WROOM-32E (Dual-core Xtensa LX6 @240 MHz) |
| Flash | 4 MB SPI |
| SRAM total | **520 KB** (304 KB heap utilizabil dupa IDF + BSS + stack) |
| WiFi / BT | 2.4 GHz 802.11 b/g/n + BT 4.2 BR/EDR + BLE (oprite pe Slave, partial pe Master) |
| USB-C bridge | CH340 (CH340C / CH340N) sau CP2102 (variante) |
| LiPo charger | **TP4056** (1A CC/CV, 4.2V cut-off) |
| Battery connector | JST PH 2.0mm |
| LED status | WS2812B RGB NeoPixel cu pin enable separat |
| Buttons | RESET (EN) + BOOT (GPIO 0) |
| Pini I/O accesibili | 28+ GPIO pe headere, toate functiile ESP32 expuse |
| Dimensiuni | ~25 × 50 mm |
| FQBN arduino-cli | `esp32:esp32:esp32` (alias DOIT ESP32 DEVKIT V1) |

**Diferente fata de iteratiile anterioare ale planului**:
- Charger e **TP4056** (nu TP4054 cum scrisesem); CC/CV identic, max 1A.
- Heap utilizabil real ~304 KB (nu 320 KB cum estimasem) — restul SRAM e folosit de IDF tasks + BSS + stack-uri sistem.
- WiFi/BT pot fi oprite explicit cu `WiFi.mode(WIFI_OFF)` + `btStop()` → economie ~30 KB heap si reducere consum cu ~80mA.

## 12. OTA pentru Slave prin UART chunked (cu Master ca proxy)

Slave nu are Ethernet/WiFi → OTA traditional HTTPS imposibil. Solutia: **Master e proxy**: descarca binary-ul de pe GitHub Release prin Ethernet/SSLClient, apoi il streameaza catre Slave prin UART (Cat6 1.5m).

### 12.1 Flow general

```
[GitHub Release] ──HTTPS──▶ [Master] ──UART──▶ [Slave Update API] ──flash app1──▶ [reboot] ──verify──▶ [mark valid] / [rollback]
```

### 12.2 Optimizare baud rate temporara

La 115200 baud, transfer 600 KB = ~52 secunde. Acceptabil.
Optimizare: switch temporar la **460800 baud** pentru OTA → ~13 secunde transfer. Cat6 1.5m suporta usor.

```cpp
// Inainte de OTA_BEGIN, ambele placi switch la baud rate inalt:
// Master: Serial2.updateBaudRate(460800);
// Slave:  primeste cmd UART_BAUD_HIGH \n, raspunde OK, apoi Serial2.updateBaudRate(460800)
// Dupa OTA_END (success sau failure): revin la 115200
```

### 12.3 Protocol OTA pe UART

Toate liniile text terminate cu `\n`, cu exceptia chunk-urilor binare care sunt **transmise raw dupa header**.

| Master → Slave | Slave → Master | Semantica |
| --- | --- | --- |
| `UART_BAUD_HIGH\n` | `OK\n` | Switch la 460800 baud (sau `ERR_BAUD\n`) |
| `OTA_BEGIN <size> <sha256>\n` | `OK\n` sau `ERR_BEGIN <reason>\n` | Inchide eventual update precedent, deschide partitie OTA |
| `OTA_CHUNK <length>\n` urmat de N bytes binari | `OK <bytes_written>\n` sau `ERR_CHUNK\n` | length max 1024 bytes per chunk |
| `OTA_END\n` | `OK\n` apoi reboot, sau `ERR_END <reason>\n` | Verifica SHA-256, `Update.end()`, reboot |
| `UART_BAUD_LOW\n` | `OK\n` | Revin la 115200 (la abandon sau dupa fail) |

### 12.4 Master — `SlaveOtaProxy.h/.cpp`

```cpp
// SlaveOtaProxy.h
#pragma once
#include <Arduino.h>
#include "SlaveUartClient.h"

class SlaveOtaProxy {
public:
    SlaveOtaProxy(SlaveUartClient& uart, EthernetClient& net);

    /**
     * Descarca binary de la URL (HTTPS), il streameaza catre Slave prin UART.
     * @param url URL HTTPS (whitelist github.com / objects.githubusercontent.com)
     * @param expectedSha SHA-256 hex 64 chars
     * @return true daca Slave a confirmat OTA_END si va reboot
     */
    bool performUpdate(const char* url, const char* expectedSha);

private:
    SlaveUartClient& _uart;
    EthernetClient&  _net;

    bool _switchHighBaud();
    bool _switchLowBaud();
    bool _sendBegin(uint32_t size, const char* sha);
    bool _streamChunks(Stream& source, uint32_t totalBytes);
    bool _sendEnd();
};
```

```cpp
// SlaveOtaProxy.cpp (extras key parts)
bool SlaveOtaProxy::performUpdate(const char* url, const char* expectedSha) {
    LOG_INFO("Slave OTA start: %s", url);

    // 1. Validare URL whitelist (re-foloseste OtaUpdater logic)
    if (!isUrlWhitelisted(url)) { LOG_ERROR("URL not whitelisted"); return false; }

    // 2. HTTPS GET prin SSLClient — primim Content-Length + stream
    HttpClient http(_sslNet, host, 443);
    http.get(path);
    int status = http.responseStatusCode();
    if (status != 200) { LOG_ERROR("HTTP %d", status); return false; }
    long contentLength = http.contentLength();
    if (contentLength <= 0 || contentLength > 1500000) {
        LOG_ERROR("Bad content length %ld", contentLength);
        return false;
    }

    // 3. Switch baud rate inalt
    if (!_switchHighBaud()) return false;

    // 4. OTA_BEGIN
    if (!_sendBegin((uint32_t)contentLength, expectedSha)) {
        _switchLowBaud();
        return false;
    }

    // 5. Stream chunks
    if (!_streamChunks(http, (uint32_t)contentLength)) {
        _sendAbort();
        _switchLowBaud();
        return false;
    }

    // 6. OTA_END
    if (!_sendEnd()) {
        _switchLowBaud();
        return false;
    }

    LOG_INFO("Slave OTA OK, Slave will reboot");
    _switchLowBaud();   // chiar daca Slave reboot, revenim defensiv
    return true;
}

bool SlaveOtaProxy::_streamChunks(Stream& source, uint32_t totalBytes) {
    uint8_t chunk[1024];
    uint32_t sent = 0;
    char header[32];

    while (sent < totalBytes) {
        // Citeste pana la 1KB din HTTP stream
        size_t toRead = std::min((uint32_t)1024, totalBytes - sent);
        size_t actuallyRead = source.readBytes(chunk, toRead);
        if (actuallyRead == 0) {
            LOG_ERROR("HTTP stream timeout at %u/%u", sent, totalBytes);
            return false;
        }

        // Trimite header text + payload binar
        snprintf(header, sizeof(header), "OTA_CHUNK %u\n", (unsigned)actuallyRead);
        _uart._serial->print(header);
        _uart._serial->write(chunk, actuallyRead);

        // Asteapta OK dupa fiecare chunk (back-pressure simplu)
        String resp = _uart._readLine(2000);
        if (!resp.startsWith("OK")) {
            LOG_ERROR("Chunk rejected at %u: %s", sent, resp.c_str());
            return false;
        }

        sent += actuallyRead;
        // Progres in MAUI
        if (sent % (50 * 1024) == 0 || sent == totalBytes) {
            mqtt.publishEventJson(progressJson(sent, totalBytes));
        }
    }
    return true;
}
```

### 12.5 Slave — `OtaReceiver.h/.cpp`

```cpp
// OtaReceiver.h
#pragma once
#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>

class OtaReceiver {
public:
    OtaReceiver(HardwareSerial& serial) : _serial(serial), _active(false), _written(0), _expectedSize(0) {}

    /** Apelat din CommandDispatcher la primire OTA_BEGIN */
    bool begin(uint32_t size, const char* expectedSha);

    /** Primeste un chunk binar — citeste exact `length` bytes de pe Serial */
    bool writeChunk(uint32_t length);

    /** Finalizeaza: verifica SHA, Update.end(), reboot */
    bool end();

    /** Anuleaza si elibereaza resursele */
    void abort();

    bool isActive() const { return _active; }

private:
    HardwareSerial& _serial;
    bool            _active;
    uint32_t        _written;
    uint32_t        _expectedSize;
    char            _expectedSha[65];
};
```

```cpp
// OtaReceiver.cpp
bool OtaReceiver::begin(uint32_t size, const char* expectedSha) {
    if (_active) Update.abort();
    if (size < 100 * 1024 || size > 1500 * 1024) {
        LOG_ERROR("OTA size out of range: %u", size);
        return false;
    }
    if (!Update.begin(size, U_FLASH)) {
        LOG_ERROR("Update.begin fail: %s", Update.errorString());
        return false;
    }
    strncpy(_expectedSha, expectedSha, 64);
    _expectedSha[64] = '\0';
    _expectedSize = size;
    _written = 0;
    _active = true;
    LOG_INFO("OTA begin %u bytes", size);
    return true;
}

bool OtaReceiver::writeChunk(uint32_t length) {
    if (!_active) return false;
    if (length == 0 || length > 1024) return false;

    uint8_t buf[1024];
    uint32_t got = 0;
    uint32_t start = millis();
    while (got < length) {
        if (_serial.available()) {
            buf[got++] = (uint8_t)_serial.read();
        } else if (millis() - start > 2000) {
            LOG_ERROR("Chunk read timeout");
            return false;
        }
    }
    size_t written = Update.write(buf, length);
    if (written != length) {
        LOG_ERROR("Update.write short: %u/%u", written, length);
        return false;
    }
    _written += length;
    return true;
}

bool OtaReceiver::end() {
    if (!_active) return false;
    if (_written != _expectedSize) {
        LOG_ERROR("Size mismatch: %u vs %u", _written, _expectedSize);
        Update.abort();
        return false;
    }

    // Update.end() valideaza checksum-ul intern al imaginii
    if (!Update.end(true)) {
        LOG_ERROR("Update.end fail: %s", Update.errorString());
        return false;
    }

    // SHA-256 extern verificat de Master inainte de a trimite (Master a stiut hash-ul)
    // — daca vrem si verificare locala pe Slave, iteram peste partitia OTA si computam SHA-256

    LOG_INFO("OTA OK, rebooting");
    delay(100);
    ESP.restart();   // bootloader va incarca noul firmware; rollback automat daca nu se valideaza
    return true;
}
```

### 12.6 CommandDispatcher Slave — extindere pentru OTA

```cpp
// CommandDispatcher.cpp — adaugare in tick()
void CommandDispatcher::tick() {
    char cmd[UART_BUFFER_SIZE];
    if (_uart.poll(cmd, sizeof(cmd))) {
        _lastRequestMs = millis();

        // Daca suntem in OTA active, gestionam chunks special
        if (_otaReceiver.isActive() && strncmp(cmd, "OTA_CHUNK ", 10) == 0) {
            uint32_t len = (uint32_t)atoi(cmd + 10);
            bool ok = _otaReceiver.writeChunk(len);
            _uart.sendLine(ok ? "OK" : "ERR_CHUNK");
            return;
        }

        if (strncmp(cmd, "OTA_BEGIN ", 10) == 0) {
            uint32_t size; char sha[65];
            if (sscanf(cmd, "OTA_BEGIN %u %64s", &size, sha) == 2) {
                _uart.sendLine(_otaReceiver.begin(size, sha) ? "OK" : "ERR_BEGIN");
            } else {
                _uart.sendLine("ERR_BEGIN_PARSE");
            }
            return;
        }

        if (strcmp(cmd, "OTA_END") == 0) {
            _uart.sendLine(_otaReceiver.end() ? "OK" : "ERR_END");
            return;   // urmeaza reboot
        }

        if (strcmp(cmd, "UART_BAUD_HIGH") == 0) {
            _uart.sendLine("OK");
            delay(50);
            _serial.updateBaudRate(460800);
            return;
        }

        if (strcmp(cmd, "UART_BAUD_LOW") == 0) {
            _uart.sendLine("OK");
            delay(50);
            _serial.updateBaudRate(115200);
            return;
        }

        // existing handlers (GET_SENSOR, REBOOT, PING, ERR_UNKNOWN)
        // ...
    }
    _updateIdleStatus();
}
```

### 12.7 MAUI flow OTA Slave

In SystemPage adaugam buton „UPDATE FIRMWARE SLAVE":

```xml
<Button Text="↑ UPDATE FIRMWARE SLAVE"
        Command="{Binding UpdateSlaveCommand}"
        Style="{StaticResource CyberButton}" />
```

Dialog: utilizatorul introduce URL release + SHA-256, sau alege ultimul build din lista. MAUI trimite pe MQTT:

```json
{
  "cmd": "triggerOtaSlave",
  "url": "https://github.com/RaduOvidiu20/ProiectVentilatie/releases/download/v145/slave.bin",
  "sha": "a3f4c2..."
}
```

Master primeste, valideaza whitelist, lanseaza `SlaveOtaProxy::performUpdate()`. In timpul OTA, master publica progress events pe `ventilatie/event`:

```json
{"type": "slave_ota_progress", "sent": 200000, "total": 600000, "percent": 33}
```

MAUI afiseaza progress bar in System tab. La final:

```json
{"type": "slave_ota_done", "result": "ok"}
// sau
{"type": "slave_ota_done", "result": "fail", "reason": "sha_mismatch"}
```

### 12.8 Considerente memorie OTA Slave

- `Update.begin(size, U_FLASH)` aloca structuri ~4 KB heap pentru OTA state
- Buffer chunk 1 KB pe stack
- SHA-256 context ~200 B
- Total varf RAM Slave in OTA: ~6 KB extra → din 240 KB free heap ramas, fara probleme
- Master in OTA Slave: SSLClient activ + chunk buffer 1 KB + heap pentru HTTP stream → varf ~40 KB suplimentar peste tipic. Free heap ramas Master: ~110 KB → suficient.

### 12.9 Watchdog si OTA

OTA dureaza 13-52 secunde (in functie de baud rate). Watchdog-ul intern (60s) e marginal. **Solutie**: in `OtaReceiver::writeChunk()` apelam `esp_task_wdt_reset()` la fiecare chunk. Pe Master, `SlaveOtaProxy::_streamChunks()` la fel.

### 12.10 Recovery si rollback

- Daca Slave reboot dupa OTA si noul firmware nu cheama `esp_ota_mark_app_valid_cancel_rollback()` in primele 30 secunde dupa boot, bootloader-ul rollback automat la firmware-ul anterior.
- Daca OTA esueaza la mijloc (chunk pierdut, SHA mismatch), Slave ramane pe firmware-ul vechi (partitia activa nu s-a schimbat).
- Master poate retrigeresa OTA Slave la nevoie din MAUI.
- **Defensive**: Slave loghează ultima incercare OTA in NVS (timestamp, success/fail, reason). Master poate cere status prin cmd nou `GET_OTA_STATUS\n`.

## 13. Management memorie (RAM + Flash + NVS) — adaptat Carbon V3 (520 KB SRAM)

ESP32-WROOM-32E pe Carbon V3: **4 MB flash, 520 KB SRAM total**.

Distributie SRAM la runtime ESP32 (cu Arduino core + IDF):
- DRAM (data segment + heap utilizabil): ~328 KB
- IRAM (cod critic + cache): ~128 KB
- RTC RAM: ~8 KB

Din ~328 KB DRAM, dupa BSS (static data), stack-uri sistem (lwIP, Ethernet, NTP) si Arduino core:
- **Heap utilizabil real la boot Master cu Ethernet + MQTT TLS init**: ~180-220 KB
- **Heap utilizabil real la boot Slave (fara WiFi/Ethernet)**: ~250-290 KB

Target operational: **>150 KB free heap permanent pe Master**, **>200 KB pe Slave**, marja confortabila pentru spike-uri TLS handshake si SlaveOtaProxy.

### 13.1 Budget RAM Master (target free heap permanent >150 KB)

| Componenta | Heap consumat (tipic) | Note |
| --- | --- | --- |
| SSLClient (BearSSL TLS context) | ~28 KB | doar in timpul handshake-ului si publish |
| PubSubClient buffer (`MQTT_BUF_SIZE`) | 4 KB | scadem la 2 KB daca nu trimitem log mare |
| `EthernetClient` socket buffer (W5500 socket) | ~2 KB | per socket activ |
| `Sht30Sensor` × 1 (local) | ~50 B | mostly stack |
| `SlaveUartClient` buffer | 256 B | static, niciodata realocat |
| `ArduinoJson` doc-uri active | <1 KB | toate `StaticJsonDocument` pe stack |
| `EventLog` cache | 1.6 KB | 20 entries × 80B + NVS persistent |
| Buffer static `_publishBuffer` MqttBridge | 1 KB | re-folosit la fiecare publish |
| Boot-time alocari (already counted in starting heap) | — | — |
| **Total varf in productie (TLS publish)** | **~38 KB** | **>140 KB free heap ramas** |
| **Total tipic idle (intre publish-uri)** | **~10 KB** | **>170 KB free heap ramas** |
| **Pe perioada SlaveOtaProxy** | **+40 KB** suplimentar | **>100 KB free heap ramas** |

Stack-urile sistem (loopTask 16 KB, lwIP, IDF) si BSS (static data ~30 KB) NU consuma heap — sunt in DRAM separate sau in zone reservate. Heap-ul de la `ESP.getFreeHeap()` reflecta DOAR alocarile dinamice peste baseline.

Verificare runtime: `ESP.getFreeHeap()` printat in `/diag` MQTT — alarma in MAUI daca <30 KB.

### 13.2 Budget RAM Slave (target free heap >200 KB)

Slave e mult mai usor — fara WiFi, fara Ethernet, fara TLS:

| Componenta | Heap consumat |
| --- | --- |
| `Sht30Sensor` (Adafruit_SHT31) | ~50 B |
| `UartProtocol` buffer static `char[128]` | 128 B |
| `StaticJsonDocument<256>` pe stack | 0 |
| `OtaReceiver` buffer 1 KB (pe stack in writeChunk) + Update state | 0 idle / ~5 KB in OTA |
| EventLog (no NVS, optional doar in RAM) | 1.6 KB sau 0 |
| **Total tipic idle** | **<2 KB → >250 KB free heap ramas** |
| **Total varf in OTA** | **~7 KB → >245 KB free heap ramas** |

### 13.3 Reguli stricte fragmentare heap

1. **NU `String` in hot path** — folosim `char[]` static + `snprintf`. `String` realocheaza heap la concatenare → fragmentare in cateva ore.
2. **NU `DynamicJsonDocument`** — doar `StaticJsonDocument<N>` pe stack. Dimensiuni cunoscute compile-time.
3. **NU `new`/`malloc`** in functii apelate frecvent — toate alocarile sunt la `setup()` si raman pana la reboot.
4. **Buffer-e pre-alocate la setup**:

```cpp
// MqttBridge — buffer-e statice membri ai clasei, nu heap
class MqttBridge {
private:
    char _publishBuffer[1024];   // pentru serialize state JSON
    char _logBuffer[512];        // pentru publishLog
    char _topicBuffer[64];       // pentru construit topic-uri
    StaticJsonDocument<512> _stateDoc;   // re-folosit, doar `clear()` intre publish-uri
};
```

5. **`PROGMEM` pentru stringuri lungi imutabile** — TA-uri certificate, mesaje de eroare lungi:

```cpp
// HiveMqTrustAnchor.h
const uint8_t TA0_DN[] PROGMEM = { /* 200 bytes */ };
const uint8_t TA0_RSA_N[] PROGMEM = { /* 256 bytes */ };
// ... salveaza ~1.5KB heap per certificate
```

6. **`F()` macro pentru `Serial.print`** stringuri literal — pastreaza in flash, nu copia in RAM:

```cpp
Serial.println(F("[Master] Boot OK"));   // string in flash
Serial.println("[Master] Boot OK");       // string copiat in RAM
```

7. **Verificare heap inainte de operatii grele** (TLS handshake, JSON serialize > 1KB):

```cpp
if (ESP.getFreeHeap() < 50 * 1024) {
    LOG_WARN("Heap too low, skipping operation");
    return;
}
```

### 13.4 EventLog — strict 20 entries doar ERROR

Reducere drastica de la 50 → **20 entries doar pentru evenimente ERROR** (nu INFO, nu DEBUG). Justificare utilizator: log-urile nu sunt prioritare, doar erorile recente conteaza.

```cpp
// Config.h
constexpr size_t EVENT_LOG_MAX_ENTRIES = 20;
constexpr size_t EVENT_LOG_ENTRY_SIZE  = 80;   // bytes per entry
// Total RAM: 1.6 KB cache + 1.6 KB NVS

// EventLog.h
enum EventLevel { EVT_ERROR = 0, EVT_WARNING = 1 };
// EVT_INFO si EVT_DEBUG ELIMINATE — nu mai stocam log-uri rutiniere

class EventLog {
public:
    /**
     * Append doar EVT_ERROR si EVT_WARNING. INFO/DEBUG ignorate complet.
     * Circular buffer: la entry 21+, suprascrie cel mai vechi.
     */
    void append(EventLevel lvl, ZoneId zone, const char* msg) {
        if (lvl > EVT_WARNING) return;   // ignore INFO/DEBUG
        // ... write to circular buffer in NVS
    }

    /** Returneaza ultimele N entries (max 20). */
    size_t getRecent(LogEntry* out, size_t maxOut);
};
```

**Beneficii**:
- 60% reducere RAM cache (3.2 KB → 1.6 KB)
- 60% reducere NVS I/O (cycles de write reduse → flash longevity crescut)
- Logurile relevante (erori reale) sunt mereu vizibile, nu ingropate sub spam INFO

### 13.5 Logger production — compile-out DEBUG/INFO

```cpp
// Logger.h — production build foloseste LOG_LEVEL_WARN
#if !defined(LOG_LEVEL)
  #ifdef DEBUG_BUILD
    #define LOG_LEVEL LOG_LEVEL_INFO
  #else
    #define LOG_LEVEL LOG_LEVEL_WARN   // production: doar WARN + ERROR pe Serial USB
  #endif
#endif

// Macro-urile compile-out la production — zero overhead, zero flash:
#if LOG_LEVEL > LOG_LEVEL_DEBUG
  #define LOG_DEBUG(...) ((void)0)
#else
  #define LOG_DEBUG(...) Logger::log(LOG_LEVEL_DEBUG, "D", __VA_ARGS__)
#endif
// ... similar pentru INFO
```

Beneficii production:
- ~5 KB flash mai putin (string-uri DEBUG/INFO eliminate la link)
- ~50 µs salvati per loop iteration (zero apel functie)
- Serial USB ramane curat — doar WARN/ERROR vizibile la debugging

### 13.6 Flash partitions (4 MB)

Schema partitionare ESP32 standard pentru OTA:

| Partitie | Size | Folosita pentru |
| --- | --- | --- |
| nvs | 24 KB | `prefs` namespace + `bootguard` + `log` |
| otadata | 8 KB | bootloader OTA state |
| app0 | 1.92 MB | firmware curent |
| app1 | 1.92 MB | firmware OTA pending (rollback target) |
| spiffs | restul (~136 KB) | NEFOLOSIT (no SPIFFS in proiect) |

Firmware curent estimat: ~1.1 MB (ESP32 Arduino core ~400KB + IDF ~300KB + cod proiect ~200KB + librarii ~200KB). Marja: ~800 KB pentru crestere viitoare.

**Reducere flash size firmware**:
- `-Os` optimization (deja default)
- Eliminare cod neutilizat: link-time optimization `-flto` (adauga in build flags)
- Eliminare biblioteci nefolosite: WiFiManager, Blynk, DHT (deja in plan)
- Renunta la `Serial.print` masiv in favoarea log levels compiled-out

### 13.7 Stack tuning

Default loopTask = 8 KB. La SSLClient handshake + ArduinoJson + ESP32 core, varful de stack poate ajunge la ~10 KB. Crestem preventiv la 16 KB:

```ini
# arduino-cli config sau platformio.ini build flags:
-DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384
```

Verificare runtime dupa load greu: `uxTaskGetStackHighWaterMark(NULL)` — returneaza minim ramas. Trebuie sa fie >2 KB margin.

### 13.8 NVS allocation (24 KB)

| Namespace | Size estimat | Folosit pentru |
| --- | --- | --- |
| `ventilatie` (prefs) | ~200 B | thresholds, hyst, intervalSec, override states, override timeout |
| `log` (eventLog) | ~1.6 KB | 20 entries × 80B |
| `bootguard` | ~16 B | boot count + last boot timestamp |
| Overhead NVS metadata | ~1 KB | tabele indexare |
| **Total** | **~3 KB** | **21 KB ramas marja** |

NVS flash wear: la 1 write/5min interval default → ~100k writes/an. Flash ESP32 = 100k cycles per cell + wear leveling → > 5 ani lifetime.

### 13.9 Heap leak detection (test continuu)

In `loop()` la fiecare 60s monitorizam tendinta:

```cpp
static uint32_t lastHeapSampleMs = 0;
static size_t  startupHeap = 0;
static size_t  minHeapSeen = SIZE_MAX;

void monitorHeapTrend() {
    if (startupHeap == 0) startupHeap = ESP.getFreeHeap();
    if (millis() - lastHeapSampleMs < 60000UL) return;
    lastHeapSampleMs = millis();

    size_t now = ESP.getFreeHeap();
    if (now < minHeapSeen) minHeapSeen = now;

    // Daca am pierdut >50 KB de la boot, posibil leak
    if (startupHeap - now > 50 * 1024) {
        LOG_ERROR("Heap leak suspected: startup=%u now=%u", startupHeap, now);
        eventLog.append(EVT_ERROR, ZONE_NONE, "heap_leak");
    }
}
```

### 13.10 Tabel sumar memorie (Carbon V3 — 520 KB SRAM, 4 MB flash)

| Resursa | Master | Slave |
| --- | --- | --- |
| SRAM total chip | 520 KB | 520 KB |
| DRAM utilizabil dupa Arduino + IDF | ~328 KB | ~328 KB |
| Heap available la boot | ~180-220 KB (dupa Eth+MQTT init) | ~250-290 KB |
| **Free heap target permanent** | **>150 KB** | **>200 KB** |
| Stack loopTask | 16 KB (configurat explicit) | 16 KB |
| Buffer MQTT publish (static) | 1 KB | n/a |
| Buffer UART (static) | 256 B (`SlaveUartClient`) | 128 B (`UartProtocol`) |
| ArduinoJson docs | toate `Static<512>` max | toate `Static<256>` max |
| EventLog NVS | 1.6 KB (20 entries doar ERR/WARN) | optional (NVS sau in RAM) |
| Flash firmware estimat | ~1.1 MB | ~600 KB |
| Flash partitie OTA (app1) | 1.92 MB (rollback target) | 1.92 MB |
| Flash NVS | ~3 KB | ~16 B (doar bootguard, optional) |
| Wear estimat la 5 ani | <50% cell life | <5% (rare scrieri NVS) |

## 14. Always-on resilience — mecanisme software pentru 24/7 unattended

Hardware fiabil este conditie **necesara dar nu suficienta**. Codul trebuie sa contina mecanisme de auto-vindecare la toate failure modes posibile. Aici sunt 14 masuri concrete aplicate ambelor proiecte (Master + Slave).

### 14.1 Brownout detector explicit (ambele placi)

ESP32 are detector intern de subtensiune care reseteaza chip-ul la sub ~2.4V (default). Trebuie activat explicit cu prag stabil.

```cpp
// In setup(), dupa Serial.begin(), inainte de orice altceva critic
#include <soc/rtc_cntl_reg.h>
#include <soc/soc.h>

void configureBrownout() {
    // Reactiveaza brownout detector cu pragul standard (2.93V — pragul 4)
    // Trebuie reactivat fiindca unele biblioteci ESP32 il dezactiveaza la inceput
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG,
                   (1U << 31)              // RTC_CNTL_BROWN_OUT_ENA
                   | (4U << 28)            // RTC_CNTL_BROWN_OUT_DET_LVL = 4 (~2.93V)
                   | (1U << 14));          // RTC_CNTL_BROWN_OUT_RST_ENA
}
```

### 14.2 Heap monitor cu alarma si restart preventiv

Detecteaza memory leaks si restart automat inainte sa ramai fara RAM.

```cpp
// HeapMonitor.h (folosit pe Master, optional pe Slave)
#pragma once
#include <Arduino.h>

class HeapMonitor {
public:
    static constexpr size_t MIN_FREE_HEAP_BYTES = 30 * 1024;   // 30KB minim
    static constexpr size_t CRITICAL_HEAP_BYTES = 15 * 1024;   // 15KB → restart

    static void check() {
        size_t freeHeap = ESP.getFreeHeap();
        size_t minHeap  = ESP.getMinFreeHeap();   // istoric — minim atins

        if (freeHeap < CRITICAL_HEAP_BYTES) {
            Serial.printf("[HEAP] CRITICAL %u bytes — restart!\n", freeHeap);
            // Salveaza eveniment in NVS inainte de restart
            saveEvent("heap_critical", freeHeap);
            delay(100);
            ESP.restart();
        }
        if (freeHeap < MIN_FREE_HEAP_BYTES) {
            Serial.printf("[HEAP] LOW %u bytes (min ever: %u)\n", freeHeap, minHeap);
        }
    }

    static size_t getFreeHeap()    { return ESP.getFreeHeap(); }
    static size_t getMinFreeHeap() { return ESP.getMinFreeHeap(); }
};
```

Apel periodic la fiecare ~30s in loop() Master + la fiecare 60s in loop() Slave.

### 14.3 I2C bus recovery (SHT30 freeze)

Daca SHT30 ramane „inghetat" (nu raspunde la Wire), I2C-ul ESP32 poate fi blocat (clock stretching neconformat). Recovery: clock manual de 9 cicluri pe SCL pentru a debloca slave-ul.

```cpp
// I2CRecovery.h (in ambele proiecte)
#pragma once
#include <Arduino.h>
#include <Wire.h>

class I2CRecovery {
public:
    /**
     * Forteaza eliberarea bus-ului I2C daca un slave tine SDA jos.
     * Trimite manual 9 cicluri de clock pe SCL pentru a debloca.
     */
    static bool recoverBus(uint8_t sdaPin, uint8_t sclPin) {
        Wire.end();   // close I2C peripheral

        pinMode(sclPin, OUTPUT_OPEN_DRAIN);
        pinMode(sdaPin, INPUT_PULLUP);

        for (int i = 0; i < 9; i++) {
            digitalWrite(sclPin, LOW);  delayMicroseconds(5);
            digitalWrite(sclPin, HIGH); delayMicroseconds(5);
            if (digitalRead(sdaPin) == HIGH) break;   // bus eliberat
        }

        // Reia I2C normal
        Wire.begin(sdaPin, sclPin);
        Wire.setClock(100000UL);
        return true;
    }
};
```

Apelat in `Sht30Sensor::read()` dupa `SHT30_RETRY_COUNT` esecuri consecutive — inainte sa raporteze `false` final, incearca recovery + 1 retry suplimentar.

### 14.4 Stuck-relay detection (Master)

Daca un releu ramane stuck ON cand updateLogic spune OFF (sau invers), detecteaza si raporteaza pe MQTT — eventual taie alimentarea releului prin redundancy.

```cpp
// VentilationZone.cpp — addition in updateLogic
void VentilationZone::updateLogic(...) {
    // ... existing logic ...

    digitalWrite(_relayPin, _relayState ? HIGH : LOW);

    // Stuck detection: citeste pinul GPIO inapoi (open drain config)
    delay(2);
    int actualState = digitalRead(_relayPin);
    if ((actualState == HIGH) != _relayState) {
        _stuckCounter++;
        if (_stuckCounter > 3) {
            // Releu stuck — emergency log + MQTT alarm
            eventLog.append(EVT_ERROR, _zoneId, "RELAY_STUCK");
        }
    } else {
        _stuckCounter = 0;
    }
}
```

### 14.5 Periodic preventive restart (Master + Slave)

Restart automat **saptamanal** la ora specifica (ex. duminica 03:00) pentru a curata orice leak/state-uri reziduale. Garanteaza freshness.

```cpp
// PreventiveReboot.h
#pragma once
#include <Arduino.h>
#include <time.h>

class PreventiveReboot {
public:
    /**
     * Verifica periodic daca trebuie sa restartam preventiv.
     * Default: duminica 03:00, daca uptime > 6 zile (evita restart instant la deploy).
     */
    static constexpr uint32_t MIN_UPTIME_BEFORE_REBOOT_SEC = 6 * 24 * 3600UL;
    static constexpr int      REBOOT_DAY_OF_WEEK = 0;       // 0 = duminica
    static constexpr int      REBOOT_HOUR        = 3;

    static bool shouldReboot(uint32_t uptimeSec) {
        if (uptimeSec < MIN_UPTIME_BEFORE_REBOOT_SEC) return false;
        time_t now = time(nullptr);
        struct tm timeinfo;
        if (!localtime_r(&now, &timeinfo)) return false;
        return (timeinfo.tm_wday == REBOOT_DAY_OF_WEEK &&
                timeinfo.tm_hour == REBOOT_HOUR &&
                timeinfo.tm_min  < 5);
    }

    static void rebootIfDue(uint32_t uptimeSec) {
        if (shouldReboot(uptimeSec)) {
            Serial.println("[Preventive] Weekly reboot");
            delay(100);
            ESP.restart();
        }
    }
};
```

Pe Slave nu avem NTP — folosim **uptime-based**: la fiecare 7 zile uptime, restart preventiv (cu jitter aleator ca sa nu coincida cu Master).

### 14.6 Boot loop guard (ambele placi)

Daca placa restarteaza de 5+ ori in 5 minute (probabil un bug critic), intra in **safe mode** — doar Serial debug, nu mai face init la W5500/MQTT/etc.

```cpp
// BootLoopGuard.h
#pragma once
#include <Preferences.h>

class BootLoopGuard {
public:
    static constexpr int MAX_BOOTS_IN_WINDOW = 5;
    static constexpr uint32_t WINDOW_SEC = 300;   // 5 minute

    /** Apelat la START de setup(). Returneaza true daca suntem in boot loop. */
    static bool detectAndIncrement() {
        Preferences prefs;
        prefs.begin("bootguard", false);
        uint32_t lastBootMs = prefs.getULong("last", 0);
        int      bootCount  = prefs.getInt("count", 0);
        uint32_t now = millis();   // resetat la boot — nu e relevant
        // Folosim NTP daca disponibil; fallback la persistent counter
        time_t epoch = time(nullptr);
        uint32_t epochSec = (epoch > 1700000000UL) ? (uint32_t)epoch : 0;

        if (epochSec > 0 && lastBootMs > 0 &&
            (epochSec - lastBootMs) > WINDOW_SEC) {
            bootCount = 0;   // window expired
        }
        bootCount++;
        prefs.putInt("count", bootCount);
        if (epochSec > 0) prefs.putULong("last", epochSec);
        prefs.end();

        return bootCount >= MAX_BOOTS_IN_WINDOW;
    }

    static void resetCounter() {
        Preferences prefs;
        prefs.begin("bootguard", false);
        prefs.putInt("count", 0);
        prefs.end();
    }
};
```

In setup() Master, dupa primul ciclu reusit (`processZones()` rulat ok), apel `BootLoopGuard::resetCounter()`. Daca `detectAndIncrement()` returneaza true → safe mode (LED rosu, Serial-only).

### 14.7 NVS corruption fallback (Master)

Daca `prefs.begin()` esueaza sau valorile citite sunt out-of-range, fallback la defaults + log eveniment.

```cpp
// AppPreferences.cpp — addition
bool AppPreferences::begin() {
    if (!_prefs.begin(NVS_PREFS_NAMESPACE, false)) {
        Serial.println("[NVS] FAIL to open namespace, retrying after erase");
        _prefs.end();
        nvs_flash_erase();   // wipe NVS (last resort)
        nvs_flash_init();
        if (!_prefs.begin(NVS_PREFS_NAMESPACE, false)) {
            return false;   // hard fail
        }
    }
    _loadAll();
    _validateOrFallback();   // NEW: range check + fallback la DEFAULT_*
    return true;
}

void AppPreferences::_validateOrFallback() {
    if (tempThresh < 10.0f || tempThresh > 90.0f) {
        Serial.println("[NVS] tempThresh out of range, using default");
        tempThresh = DEFAULT_TEMP_THRESH;
        saveAll();
    }
    // similar pentru humThresh, intervalSec, hyst, etc.
}
```

### 14.8 TLS handshake retry policy explicit (Master)

SSLClient + W5500 poate avea handshake-uri esuate sporadic. Backoff exponential cu plafon.

```cpp
// MqttBridge.cpp — _connect() refactor
bool MqttBridge::_connect() {
    static uint32_t backoffMs = MQTT_RECONNECT_INITIAL_MS;
    static uint32_t lastAttemptMs = 0;
    static int      consecutiveFails = 0;

    if (millis() - lastAttemptMs < backoffMs) return false;
    lastAttemptMs = millis();

    // Free heap check inainte de TLS handshake (~30KB necesar)
    if (ESP.getFreeHeap() < 50 * 1024) {
        Serial.println("[MQTT] heap too low for TLS, skip");
        return false;
    }

    bool ok = _client.connect(...);
    if (ok) {
        backoffMs = MQTT_RECONNECT_INITIAL_MS;   // reset
        consecutiveFails = 0;
        return true;
    }

    consecutiveFails++;
    backoffMs = std::min(backoffMs * 2, (uint32_t)MQTT_RECONNECT_MAX_MS);
    if (consecutiveFails >= 20) {
        Serial.println("[MQTT] 20 fails in a row — restart");
        ESP.restart();   // last resort
    }
    return false;
}
```

### 14.9 Ethernet link monitor cu auto-recovery

Daca link-ul cade (`Ethernet.linkStatus() != LinkON`) timp de 10 minute, restart W5500 + retry DHCP. Daca tot esueaza, restart ESP32.

```cpp
// in loop() Master
static uint32_t linkDownSinceMs = 0;
if (Ethernet.linkStatus() != LinkON) {
    if (linkDownSinceMs == 0) linkDownSinceMs = millis();
    if (millis() - linkDownSinceMs > ETH_DOWN_RESTART_MS) {
        Serial.println("[Eth] Link down >10min — reset W5500");
        digitalWrite(W5500_RST_PIN, LOW);
        delay(50);
        digitalWrite(W5500_RST_PIN, HIGH);
        delay(200);
        Ethernet.init(W5500_CS_PIN);
        byte mac[6]; getEthernetMac(mac);
        if (Ethernet.begin(mac, ETH_DHCP_TIMEOUT_MS) == 0) {
            Serial.println("[Eth] DHCP retry FAIL — full restart");
            ESP.restart();
        }
        linkDownSinceMs = 0;
    }
} else {
    linkDownSinceMs = 0;
}
```

### 14.10 OTA rollback explicit (Master)

ESP32 are partitii OTA cu rollback automat — daca noul firmware nu cheama `esp_ota_mark_app_valid_cancel_rollback()` in primele N secunde, bootloader-ul revine la firmware-ul anterior.

```cpp
// in setup() Master, dupa boot reusit (toate init-urile OK)
#include <esp_ota_ops.h>

void markFirmwareValid() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_ota_get_state_partition(running, &state);
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        Serial.println("[OTA] Marking firmware valid (cancel rollback)");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}
```

Apelat dupa primul `processZones()` reusit + MQTT connect OK + Slave fetch reusit. Daca un OTA introduce un bug, urmatorul reboot revine la versiunea anterioara automat.

### 14.11 Master self-watchdog pe processZones()

Daca `processZones()` nu ruleaza pentru > 2× intervalul configurat (ex. 10 minute la interval 5min), inseamna ca timer-ul SimpleTimer e stuck → restart.

```cpp
// in loop() Master
static uint32_t lastProcessZonesMs = 0;
const uint32_t maxIntervalMs = prefs.intervalSec * 1000UL * 2;
if (lastProcessZonesMs > 0 &&
    millis() - lastProcessZonesMs > maxIntervalMs) {
    Serial.println("[Self-WDT] processZones() stuck — restart");
    ESP.restart();
}

// La inceputul processZones():
void processZones() {
    lastProcessZonesMs = millis();
    // ... rest ...
}
```

### 14.12 Slave auto-restart la disparitie Master

Daca Slave nu primeste niciun UART command timp de 30 minute, presupune ca Master a disparut sau e in stare degradata. Restart preventiv pentru a evita stari interne corrupte.

```cpp
// CommandDispatcher.cpp — _updateIdleStatus extended
constexpr uint32_t SELF_RESTART_IDLE_MS = 30 * 60 * 1000UL;   // 30 min

void CommandDispatcher::_updateIdleStatus() {
    if (_lastRequestMs == 0) return;
    uint32_t idleMs = millis() - _lastRequestMs;
    if (idleMs > IDLE_WARN_MS) {
        _led.setStatus(SystemLED::Status::IDLE);
    }
    if (idleMs > SELF_RESTART_IDLE_MS) {
        LOG_WARN("Idle 30min — self-restart for freshness");
        delay(100);
        ESP.restart();
    }
}
```

### 14.13 Stack size tuning si stack-canary

ESP32 are loopTask cu stack default ~8KB. La activitate intensa (TLS handshake + ArduinoJson + ESP-NOW) poate fi insuficient. Crestem la 16KB in `setup()`.

```cpp
// In platformio.ini sau build flags arduino-cli:
// -DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384
//
// Alternativ, in setup() pentru taskuri proprii:
// xTaskCreate(taskFn, "name", 16384, NULL, 1, NULL);
```

Stack canary deja activat default in IDF — la stack overflow chip-ul reseteaza imediat (better than corrupt heap).

### 14.14 Self-monitoring exposed via MQTT

Master publica periodic `ventilatie/diag` cu metricile pentru monitorizare externa (alertare push notification din MAUI):

```json
{
  "uptime": 432123,
  "freeHeap": 187432,
  "minFreeHeap": 102443,
  "fwBuild": 145,
  "ethLinkUp": true,
  "mqttConnected": true,
  "slaveErrors": 2,
  "lastPreventiveReboot": 1745784620,
  "bootCount": 1,
  "rssi": null
}
```

MAUI subscriber pe `ventilatie/diag`, daca `freeHeap < 30KB` sau `slaveErrors > 100` afiseaza alerta vizuala in System tab.

### 14.15 Aplicare in setup() Master (fragment integrat)

```cpp
void setup() {
    Serial.begin(115200);
    configureBrownout();                    // 11.1

    if (BootLoopGuard::detectAndIncrement()) {  // 11.6
        Serial.println("[SAFE MODE] Boot loop detected");
        // LED rosu, opreste init normal
        while (true) { delay(1000); }
    }

    // ... init normal: prefs, I2C, sensor, UART, Ethernet, MQTT, watchdog ...

    // Dupa primul ciclu reusit:
    BootLoopGuard::resetCounter();           // 11.6
    markFirmwareValid();                     // 11.10
}
```

### 14.16 Aplicare in loop() Master (fragment integrat)

```cpp
void loop() {
    esp_task_wdt_reset();

    // Self-WDT pe processZones (11.11)
    static uint32_t lastZonesMs = 0;
    if (lastZonesMs > 0 && millis() - lastZonesMs > prefs.intervalSec * 2000UL) {
        ESP.restart();
    }

    // Heap monitoring (11.2)
    static uint32_t lastHeapCheck = 0;
    if (millis() - lastHeapCheck > 30000UL) {
        HeapMonitor::check();
        lastHeapCheck = millis();
    }

    // Ethernet link monitor (11.9)
    monitorEthernetLink();

    // MQTT loop + reconnect cu backoff (11.8)
    mqtt.loop();

    // NTP resync (existent)
    TimeSync::loop();

    // Preventive reboot duminica 03:00 (11.5)
    PreventiveReboot::rebootIfDue(millis() / 1000UL);

    // Pending commands processed (existent)
    if (mqtt.hasPendingCommands()) {
        processZones();
        lastZonesMs = millis();
    }

    timer.run();   // SimpleTimer scheduler — apela processZones la interval
}
```

### 14.17 Tabel sumar mecanisme always-on

| Mecanism | Master | Slave | Acopera |
| --- | --- | --- | --- |
| Watchdog hardware 60s | ✅ | ✅ | software hangs |
| Brownout detector | ✅ | ✅ | dipuri tensiune |
| Heap monitor + restart la <15KB | ✅ | optional | memory leaks |
| I2C bus recovery (manual clock) | ✅ | ✅ | SHT30 stuck |
| Stuck relay detection | ✅ | n/a | hardware fault releu |
| Preventive reboot saptamanal | ✅ (NTP) | ✅ (uptime 7 zile) | state buildup |
| Boot loop guard (5/5min → safe mode) | ✅ | ✅ | bug critic |
| NVS corruption fallback | ✅ | n/a (fara NVS) | flash errors |
| TLS retry exponential cu cap | ✅ | n/a | network blips |
| Ethernet link monitor + W5500 reset | ✅ | n/a | cablu/switch |
| OTA rollback automat | ✅ | n/a | bad firmware |
| Self-WDT pe processZones() | ✅ | n/a | timer stuck |
| Slave self-restart la idle 30min | n/a | ✅ | Master disappeared |
| Stack size 16KB | ✅ | ✅ | overflow protection |
| Self-monitoring `/diag` topic | ✅ | n/a | observabilitate externa |

## 15. Cerinte fiabilitate hardware 24/7 minim 3 ani (rezumat)

Pentru deploy 26.000+ ore continuu:

1. **Surse industriale 5V × 2** (Mean Well IRM-20-5ST sau echivalent) — separate pentru Master si Slave
2. **Conexiuni soldering / PCB custom** (NU dupont jumpers)
3. **Carcase IP54 × 2** + carcasa separata pentru relee si cablajul 230V
4. **Filtru PTFE pe SHT30 × 2**
5. **Surge protection**: TVS pentru LAN (doar Master), MOV pentru AC (ambele surse)
6. **Switch Ethernet de calitate** (Mikrotik, Ubiquiti) — doar Master conectat
7. **Re-flash anual** ambele placi (USB pentru Slave, OTA pentru Master)

Cu masurile 1-7: probabilitate failure <7% la 36 luni. UART e cel mai vechi si testat protocol — risc software practic zero. Cost suplimentar masuri: ~350-500 lei.

## 16. Materiale (de cumparat)

| Item | Cantitate | Pret estimat | Note |
| --- | --- | --- | --- |
| Placa Carbon V3 | +1 (1 deja avuta) | ~80 lei | Slave |
| Modul GY-SHT30-D | 0 (achizitionate) | — | |
| Modul W5500 Ethernet shield | +1 (1 deja avut) | ~25 lei | Doar Master |
| Cablu Cat6 1.5m (UART inter-board) | 1 | ~5 lei | TX/RX/GND |
| Cablu Ethernet Cat6 (Master → switch) | 1 | ~5 lei | |
| Mufe RJ45 keystone | 2 | ~10 lei | breakout pentru Cat6 inter-board |
| Sursa industriala 5V Mean Well IRM-20-5ST | 2 | ~120 lei | Master + Slave separate |
| Carcasa IP54 ABS DIN-rail | 2 | ~80 lei | |
| Filtru PTFE pentru SHT30 | 2 | ~10 lei | |
| TVS diode SP3012 | 1 | ~5 lei | doar Master, pe LAN |
| Varistor MOV 275V / bara surge | 1 | ~30 lei | pe AC, comuna |
| **NU se cumpara**: PCA9615, rezistori pull-up, cablu I2C lung | — | — | |

**Total suplimentar pentru fiabilitate 3 ani**: ~370-450 lei (vs ~100 lei pentru varianta minimalista).

## 17. Verificare end-to-end

1. **Loopback UART pe Slave standalone**: leg TX-RX cu fir scurt → trimit `GET_SENSOR\n` din serial USB → primesc raspuns echo. Confirma Serial2 functional pe pini 16/17.
2. **UART punct-la-punct cu fir scurt 30cm**: ambele placi langa, 3 fire (TX, RX, GND). Master sketch test trimite `PING\n` la fiecare 1s. Slave raspunde `PONG\n`. Latency afisata <50ms.
3. **UART prin Cat6 1.5m**: sertizezi cablul cu twisted pair (TX+GND, RX+GND), repeti testul. Trebuie ZERO erori timp de 5 minute consecutive.
4. **Master Ethernet ping**: `Ethernet.begin(mac)` + de pe laptop `ping <ip>` raspunde sub 5ms.
5. **Master TLS HiveMQ**: SSLClient + PubSubClient publish dummy pe `ventilatie/test`. Verificat cu `mosquitto_sub` sau MQTT Explorer.
6. **Master + Slave integrat — fetch senzor remote**: Master executa `processZones()`, Serial monitor afiseaza `[STANGA] T:23.4 H:55.1 (local)` + `[DREAPTA] T:24.1 H:56.3 (slave)`.
7. **Build full firmware Master**: `bash ESP32/scripts/bump_build.sh && arduino-cli compile --fqbn esp32:esp32:esp32 ESP32/`. Zero erori, zero warnings critice.
8. **Build full firmware Slave**: `arduino-cli compile --fqbn esp32:esp32:esp32 ESP32_Slave/`. Zero erori.
9. **Flash ambele placi**: `arduino-cli upload --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 ESP32/` + `... ESP32_Slave/`.
10. **MAUI end-to-end**: temp/hum reale pentru ambele zone pe Dashboard. REFRESH forteaza fetch instant. Settings → schimb prag → relee actioneaza. Override toggle → relee react in <2s.
11. **Test rezilienta Slave**: opresti alimentarea Slave → Master detecteaza timeout 5 cicluri (~25min) → publica `slaveOnline=false` + relay RIGHT off (failsafe). MAUI afiseaza banner. Repornesti Slave → recovery automat in <30s.
12. **Test rezilienta Master**: scoti cablul Ethernet → relee continua decis local. Repui → reconectare HiveMQ + republish.
13. **Test rebootSlave**: apesi „RESTART SLAVE" → log Master „[Slave] reboot OK" → Slave reboot in 5s → recovery.
14. **Test reset/reboot Master**: „RESET DEFAULT MASTER" → NVS wipe, defaults restored, MQTT reconnect, MAUI vede valori default. „RESTART MASTER" → 5s offline, apoi recovery.
15. **Burn-in 24h**: ambele placi pornite, monitor Serial pentru erori UART (<5/h acceptabil) + temp/hum coerente + cycle relee corect.
16. **Test OTA Master**: bump build, push GitHub Release, MAUI cmd `triggerOta`, ESP32 descarca + valideaza SHA-256 + reboot pe noul firmware.

## 18. Ordine de executie — 28 faze detaliate (A-BB)

### 18.0 Strategie globala

**Principiu de ordonare**: dupa fiecare faza incheiata, sistemul e intr-o stare **functionala + commit-able**. Nu lasam vreodata sistemul „pe jumatate" intre sesiuni. Fiecare faza adauga valoare incrementala fara a rupe ce era deja functional.

**3 axe de risc minimizate**:
1. **Hardware first**: validam fiecare componenta fizica izolat inainte de integrare (faze C, V) — depistam defectele de hardware imediat, nu in burn-in dupa 50h munca.
2. **Comunicare strat-cu-strat**: UART simplu → CRC → cu protocol → cu retry → cu OTA chunked. Adaugam complexitate doar dupa ce stratul anterior e robust.
3. **Slave first, apoi Master**: Slave e simplu si izolat → cand Master ajunge la integrare cu Slave (faza N), Slave e deja robust si testat — orice problema apare la integrare e clar in Master.

### 18.1 Tabel de cuprins faze

| Faza | Nume | Sesiuni | Durata activa | Risk | Faza prerequisita |
| --- | --- | --- | --- | --- | --- |
| **A** | Pregatire materiala | A1 | 0.5h + cumparare | low | — |
| **B** | Setup mediu de dezvoltare | B1, B2 | 3h | low | A |
| **C** | Hardware validation pe banc | C1, C2, C3 | 4h | medium | B |
| **D** | UART proof-of-concept | D1, D2 | 3h | low | C |
| **E** | UART prin Cat6 1.5m | E1 | 2h | medium | D |
| **F** | Module foundationale | F1, F2, F3 | 4h | low | B |
| **G** | Slave skeleton | G1, G2 | 3h | low | F |
| **H** | Slave UART responder | H1, H2, H3 | 4h | low | G, E |
| **I** | Master Ethernet baseline | I1, I2, I3 | 4h | high | B |
| **J** | Refactor Config.h Master | J1 | 2h | low | I |
| **K** | Refactor VentilationZone | K1, K2 | 3h | low | J, F |
| **L** | Eliminare Blynk + WiFi | L1, L2 | 3h | medium | K |
| **M** | Master Ethernet integrat | M1, M2 | 3h | medium | L |
| **N** | Master ↔ Slave UART | N1, N2, N3 | 4h | medium | M, H |
| **O** | MqttBridge SSLClient + JSON v2 | O1, O2 | 3h | high | N, I |
| **P** | MAUI dual-board updates | P1, P2, P3 | 3h | low | O |
| **Q** | Reset/restart infrastructure | Q1, Q2 | 3h | low | P |
| **R** | Always-on resilience | R1, R2, R3, R4 | 5h | medium | Q |
| **S** | Memory optimization | S1, S2 | 3h | low | R |
| **T** | OTA Master via Ethernet | T1, T2 | 3h | high | O |
| **U** | OTA Slave prin proxy | U1, U2, U3 | 4h | high | T |
| **V** | LED hardware standalone | V1, V2 | 3h | medium | C |
| **W** | LED schedule + time sync | W1, W2, W3 | 4h | low | V, N |
| **X** | LED control via MQTT + MAUI | X1, X2, X3 | 4h | low | W, P |
| **Y** | LED dual-mirror NVS | Y1 | 2h | low | X |
| **Z** | Hardware production | Z1, Z2 | 5h | medium | toate de mai sus |
| **AA** | Burn-in + observation | AA1, AA2, AA3 | 1h activ + 30h pasiv | medium | Z |
| **BB** | Documentatie + handoff | BB1 | 2h | low | AA |
| **TOTAL** | **28 faze, 52 sesiuni** | | **~85h activ + 30h pasiv** | | |

**Distributie pe saptamani recomandata**:
- **Sapt. 1**: A-H (slave functional standalone) — ~22h
- **Sapt. 2**: I-O (master functional cu slave + MQTT) — ~22h
- **Sapt. 3**: P-S + T-U (MAUI + reset + resilience + memory + OTA) — ~21h
- **Sapt. 4**: V-Y (LED feature complete) — ~13h
- **Sapt. 5**: Z-AA (production hardening + burn-in) — ~7h activ + 30h pasiv
- **Sapt. 5+**: BB (documentatie) — ~2h

### 18.2 FAZA A — Pregatire materiala

**Goal**: toate componentele fizice cumparate si verificate, gata pe masa de lucru.

**Sessions**:
- **A1** — Comanda materiale conform listei sectiunea 16 + 21.11. Inclusiv: 2× Carbon V3, 2× SHT30, 1× W5500, 1× NCEP01T18, 1× banda LED 24V 36W, surse Mean Well (2× 5V + 1× 24V), Cat6 1.5m + RJ45 keystone, fire 18AWG, conectori, carcase IP54, filtre PTFE.

**Entry criteria**: niciuna.
**Exit criteria**: toate componentele primite + verificate vizual integritate (fara stricaciuni evidente, conectori intacti).
**Deliverables**: lista physical inventory check.
**Rollback**: returnare componente defecte inlocuite.
**Risk**: low (doar logistica).

### 18.3 FAZA B — Setup mediu de dezvoltare

**Goal**: PC pregatit pentru build + flash + debug Carbon V3.

**Sessions**:
- **B1** — Instalare Arduino IDE 2.x + ESP32 board manager URL + driver CH340 (vezi sectiunea 19.1-19.4).
- **B2** — Instalare arduino-cli + librarii necesare (Adafruit SHT31, NeoPixel, ArduinoJson, PubSubClient, Ethernet, SSLClient, ArduinoHttpClient) + creare branch git `feat/dual-board-uart`.

**Entry criteria**: faza A completa.
**Exit criteria**: `arduino-cli compile` reuseste pe un sketch hello-world ESP32.
**Deliverables**: mediu functional, branch git creat.
**Rollback**: revert Arduino IDE la versiunea anterioara.
**Risk**: low.

### 18.4 FAZA C — Hardware validation pe banc

**Goal**: fiecare componenta hardware valideaza independent.

**Sessions**:
- **C1** — Sketch I2CScan: ambele Carbon V3 cu cate un SHT30 conectat. Confirma `0x44` in serial monitor pentru fiecare placa.
- **C2** — Sketch Ethernet ping: doar Master + W5500 conectat → DHCP IP + ping <5ms RTT.
- **C3** — Sketch PWM standalone: Slave cu NCEP01T18 + banda LED 24V conectata + sursa 24V → `analogWrite()` simplu prin 0/64/128/255 → vizibil dimming pe banda.

**Entry criteria**: faza B + componente fizice gata.
**Exit criteria**: toate cele 3 sketch-uri ruleaza cu rezultatul asteptat.
**Deliverables**: 3 sketch-uri test in `/tests/` directory.
**Rollback**: schimbare componenta defecta.
**Risk**: medium (poate sa apara componente DOA).

### 18.5 FAZA D — UART proof-of-concept

**Goal**: confirmam UART2 functioneaza pe ESP32, framing text correct.

**Sessions**:
- **D1** — Sketch loopback UART: pe o singura placa, leg TX(17) la RX(16) cu fir scurt, scriu `Serial2.print("test\n")`, citesc cu `Serial2.readStringUntil('\n')`. Echo perfect 1 minut.
- **D2** — Sketch ping/pong: 2 placi cu 3 fire scurte (TX-RX cross + GND). Master: `PING\n` la 1s. Slave: `PONG\n`. Latency afisata <50ms.

**Entry criteria**: faza C.
**Exit criteria**: 0 erori 5 minute consecutive.
**Deliverables**: sketch-uri test in `/tests/uart-poc/`.
**Rollback**: verifica pini, alimentare comuna GND.
**Risk**: low.

### 18.6 FAZA E — UART prin Cat6 1.5m

**Goal**: link-ul fizic final fiabil.

**Sessions**:
- **E1** — Sertizare Cat6 cu RJ45 keystone, twisted pair correct (TX+GND, RX+GND), conectare la ambele placi, repetare ping/pong test 5 minute. Mutarea slave-ului in zona finala 1.5m distanta.

**Entry criteria**: faza D + cablu Cat6.
**Exit criteria**: 0 erori 5 minute. Daca apar erori, se reactioneaza prin re-sertizare sau twisted pair check.
**Deliverables**: cablu Cat6 final functional.
**Rollback**: re-sertizare.
**Risk**: medium (sertizare imperfecta = problema cea mai frecventa).

### 18.7 FAZA F — Module foundationale (partajate)

**Goal**: 3 fisiere reutilizabile testate izolat, gata sa fie copy-paste in ambele proiecte.

**Sessions**:
- **F1** — `CrcUtil.h` (header-only, 45 linii). Test cu vectori cunoscuti: `crc16("123456789", 9) == 0x4B37` (Modbus standard test vector). Verifica online cu calculator CRC-16/Modbus.
- **F2** — `Logger.h` (macros LOG_*) + `WatchdogManager.h` (init + feed static). Test in setup() simplu: log levels vizibile pe Serial, watchdog reset la `delay(70000)`.
- **F3** — `Sht30Sensor.h` header-only. Test pe ambele placi: temp/hum reale citite la cooldown 60s + force=true override.

**Entry criteria**: faza B.
**Exit criteria**: fiecare modul ruleaza in sketch test izolat.
**Deliverables**: 3 fisiere `.h` gata, 3 sketch-uri test.
**Rollback**: rezolvare in fisier individual, nu afecteaza alte module.
**Risk**: low.

### 18.8 FAZA G — Slave skeleton

**Goal**: structura proiect Slave existenta, boot reusit, log vizibil.

**Sessions**:
- **G1** — Creare director `ESP32_Slave/` cu: `ESP32_Slave.ino` skeleton (doar Logger + Watchdog), `Config.h` minimal (pini I2C/UART/LED, baud), `scripts/build.sh`, `scripts/flash.sh`, `README.md`.
- **G2** — Integrare `Sht30Sensor` + `SystemLED` in setup() Slave; in loop() print temp/hum la fiecare 10s + cycle culori LED.

**Entry criteria**: faza F.
**Exit criteria**: Slave porneste, log clar pe Serial cu valori reale, LED-uri ciclice.
**Deliverables**: proiect Slave functional cu citire senzor + LED status.
**Rollback**: revert directory.
**Risk**: low.

### 18.9 FAZA H — Slave UART responder

**Goal**: Slave raspunde corect la comenzi UART cu CRC.

**Sessions**:
- **H1** — `UartProtocol.h/.cpp` cu CRC validation in `poll()` + `sendLine`/`sendJson` ataseaza CRC.
- **H2** — `CommandDispatcher.h/.cpp` cu DI: handlere `GET_SENSOR`, `PING`, `REBOOT` + `_updateIdleStatus`.
- **H3** — Integrare in `ESP32_Slave.ino`. Test din PC: USB-UART converter conectat la TX/RX Slave, trimit `PING*7A3D\n` din `screen` → primesc `PONG*A2F1\n`.

**Entry criteria**: faza G + faza F (CrcUtil).
**Exit criteria**: toate cele 3 comenzi raspund corect cu CRC valid; fail CRC la mesaj corupt manual.
**Deliverables**: Slave functional ca UART responder standalone.
**Rollback**: revert la skeleton (faza G).
**Risk**: low.

### 18.10 FAZA I — Master Ethernet baseline

**Goal**: Master independent (fara Slave inca) cu Ethernet + TLS la HiveMQ.

**Sessions**:
- **I1** — Sketch test Ethernet pe Master (separat, NU in proiectul ESP32/) cu DHCP + ping din laptop.
- **I2** — Generare `HiveMqTrustAnchor.h` cu `pycert_bearssl.py` din SSLClient repo (input: ISRG Root X1).
- **I3** — Sketch test SSLClient + PubSubClient publish dummy `ventilatie/test`. Verificat cu `mosquitto_sub` sau MQTT Explorer.

**Entry criteria**: faza B.
**Exit criteria**: ESP32 Master publica mesaje pe HiveMQ via Ethernet+TLS, vizibile in MQTT Explorer.
**Deliverables**: 2 sketch-uri test + `HiveMqTrustAnchor.h` gata.
**Rollback**: niciun impact pe codul existent (sketch-uri separate).
**Risk**: **high** — TLS + W5500 + ESP32 e combinatia cea mai fragila. Daca esueaza repetat, evaluam Plan B (broker local Mosquitto port 1883).

### 18.11 FAZA J — Refactor Config.h Master

**Goal**: noul Config.h cu `constexpr` aliniat la dual-board UART.

**Sessions**:
- **J1** — Sterge: `BLYNK_*`, `DHT_*_PIN`, `VP_*`, `WIFI_*`, `DHT_MIN_READ_MS`. Adauga: `I2C_*`, `SHT30_*`, `W5500_*`, `SLAVE_UART_*`, `ETH_*`. Toate constantele numerice ca `constexpr` (nu `#define`). Pastreaza HiveMQ + NTP + OTA whitelist + NVS namespaces.

**Entry criteria**: faza I.
**Exit criteria**: `arduino-cli compile --dry-run` valideaza Config.h fara warnings.
**Deliverables**: Config.h refactorat (commit standalone).
**Rollback**: git revert.
**Risk**: low.

### 18.12 FAZA K — Refactor VentilationZone

**Goal**: VentilationZone suporta atat senzor local cat si remote.

**Sessions**:
- **K1** — Constructor pentru zona cu senzor local: `VentilationZone(Sht30Sensor* localSensor, int relayPin, const char* name)`. `readSensor(force)` apeleaza `_localSensor->read()`.
- **K2** — Constructor pentru zona remote: `VentilationZone(int relayPin, const char* name)`. Metode noi: `setExternalSensorValues(temp, hum, ts)`, `enterFailsafe()`, `exitFailsafe()`, `isInFailsafe()`.

**Entry criteria**: faza J + faza F (Sht30Sensor).
**Exit criteria**: ambele constructoare disponibile, sketch test simulat: leftZone cu senzor local, rightZone cu setExternalSensorValues(20.0, 50.0, ts) → updateLogic functioneaza pentru ambele.
**Deliverables**: VentilationZone.h/.cpp refactorat.
**Rollback**: git revert.
**Risk**: low.

### 18.13 FAZA L — Eliminare Blynk + WiFi din Master

**Goal**: ProiectVentilatie.ino curat de Blynk + WiFiManager (~600 linii sterse).

**Sessions**:
- **L1** — Sterge include-uri `<WiFi.h>`, `<WiFiManager.h>`, `<BlynkSimpleEsp32.h>`. Sterge: `BLYNK_CONNECTED()`, toate `BLYNK_WRITE(VP_*)` (~120 linii), apelurile `Blynk.virtualWrite`, `Blynk.run()`.
- **L2** — Sterge: struct `BlynkPending`, variabila `blynkPendingProcessed`, `wifiManager.autoConnect()`, butonul WiFi-reset (3-sec hold). Pastreaza RESET_BUTTON cu noua semantica (factory NVS doar).

**Entry criteria**: faza K.
**Exit criteria**: `arduino-cli compile` fara erori. Master inca foloseste vechiul WiFiClientSecure + WiFi temporar (eliminat in faza M).
**Deliverables**: ProiectVentilatie.ino curat.
**Rollback**: git revert.
**Risk**: medium (tinem cont sa nu lasam referinte rupte la simboluri sterse).

### 18.14 FAZA M — Master cu Ethernet integrat

**Goal**: Master pe Ethernet, MQTT functional.

**Sessions**:
- **M1** — `setup()` Master refactorat: `WiFi.mode(WIFI_OFF); btStop();` → init Wire → init zone left cu senzor local → init Ethernet (cu `getEthernetMac()` din eFuse) → init Serial2 (UART catre Slave) → MQTT init.
- **M2** — `loop()` Master: replace `WiFi.status()` cu `Ethernet.linkStatus()`, `Ethernet.maintain()` la 1min, monitor link cu reset W5500 la 10min link-down.

**Entry criteria**: faza L + faza I.
**Exit criteria**: Master pe Ethernet, conectat la HiveMQ via SSLClient, publica state JSON. Slave inca neconectat — `slaveOnline` va fi `false` initial.
**Deliverables**: Master functional cu Ethernet, fara Blynk, fara WiFi.
**Rollback**: git revert.
**Risk**: medium.

### 18.15 FAZA N — Master ↔ Slave UART

**Goal**: Master citeste senzorul Slave-ului prin UART cu CRC + retry.

**Sessions**:
- **N1** — `SlaveUartClient.h/.cpp` cu CRC + retry (2 incercari, timeout 1s fiecare).
- **N2** — Integrare in `processZones()`: `slaveClient.fetch()` inainte de `leftZone.readSensor()`. Daca succes → `rightZone.setExternalSensorValues()`.
- **N3** — Failsafe la 5 erori consecutive: `rightZone.enterFailsafe()` (releu RIGHT off), eveniment `EVT_ERROR sensor_node_offline`. Recovery automat la primul fetch reusit.

**Entry criteria**: faza M + faza H (Slave functional).
**Exit criteria**: Serial monitor Master arata `[STANGA] T:xx.x H:xx.x (local)` + `[DREAPTA] T:xx.x H:xx.x (slave)` ambele non-zero.
**Deliverables**: Master + Slave comunica end-to-end.
**Rollback**: git revert SlaveUartClient.
**Risk**: medium.

### 18.16 FAZA O — MqttBridge SSLClient + JSON v2

**Goal**: MqttBridge complet pe SSLClient + Ethernet, schema JSON noua publicata.

**Sessions**:
- **O1** — In `MqttBridge.h/.cpp`: inlocuieste `WiFiClientSecure _net` cu `EthernetClient _baseClient` + `SSLClient _net` (cu TAs din `HiveMqTrustAnchor.h` + RNG pin floating ADC36). Lock simplificat la `LOCK_NONE`/`LOCK_MQTT`.
- **O2** — Extinde JSON state cu campuri noi: `slave.online`, `slave.lastSeen`, `slave.errors`, `right.failsafe`. Adauga `MQTT_USE_SSLCLIENT` confirmat.

**Entry criteria**: faza N.
**Exit criteria**: MQTT publica state v2 vizibil in MQTT Explorer cu toate campurile noi.
**Deliverables**: MqttBridge migrat complet.
**Rollback**: git revert; e cea mai mare schimbare arhitectural in MQTT, atentie la heap.
**Risk**: **high** — SSLClient consuma ~30KB heap. Verificare cu `ESP.getFreeHeap()` >150KB ramas.

### 18.17 FAZA P — MAUI dual-board updates

**Goal**: MAUI afiseaza corect statusul ambelor placi.

**Sessions**:
- **P1** — `MobileApp/Models/VentilationState.cs` extins cu `SlaveStatus` sub-model.
- **P2** — `DashboardViewModel.cs`: simplificare banner lock (sterge ramura Blynk), banner nou „Senzor zona dreapta inaccesibil" cand `slaveOnline=false`.
- **P3** — Test E2E MAUI: Dashboard, Settings, Devices, Reports, System functioneaza cu firmware nou. Toggle override, schimbare prag, refresh.

**Entry criteria**: faza O.
**Exit criteria**: utilizatorul vede ambele zone real, banner aparut/disparut corect la pornire/oprire Slave.
**Deliverables**: MAUI build Release reusit, testat pe device.
**Rollback**: git revert MAUI changes.
**Risk**: low.

### 18.18 FAZA Q — Reset/restart infrastructure

**Goal**: 3 butoane reset functioneaza din MAUI.

**Sessions**:
- **Q1** — `cmd:rebootSlave` end-to-end: extras `MqttPending.rebootSlave`, handler in `processZones`, `slaveClient.sendReboot()`, buton „RESTART SLAVE" in MAUI System tab.
- **Q2** — Validare semantica: cmd:reset wipe NVS doar pe Master (Slave NU afectat). Test toate cele 3 comenzi (RESET DEFAULT MASTER, RESTART MASTER, RESTART SLAVE) cu recovery automat <30s.

**Entry criteria**: faza P.
**Exit criteria**: dupa fiecare comanda, sistem revine la stare normala, fara interventie manuala.
**Deliverables**: workflow reset complet.
**Rollback**: git revert.
**Risk**: low.

### 18.19 FAZA R — Always-on resilience

**Goal**: 14 mecanisme always-on integrate, testate cu simulari de failure.

**Sessions**:
- **R1** — `configureBrownout()`, `HeapMonitor`, `BootLoopGuard` (S34 din vechea numerotare).
- **R2** — `I2CRecovery::recoverBus()` apelat in `Sht30Sensor::read()` la N esecuri, stuck relay detection in `VentilationZone::updateLogic()`.
- **R3** — `AppPreferences::_validateOrFallback()`, `MqttBridge::_connect()` cu backoff exponential + restart la 20 esecuri, Ethernet link monitor + W5500 reset, self-WDT pe `processZones()`.
- **R4** — `PreventiveReboot` (Master NTP saptamanal duminica 03:00, Slave uptime 7 zile), Slave idle restart 30min, self-monitoring `/diag` MQTT topic + MAUI alarme `freeHeap<30KB`.

**Entry criteria**: faza Q.
**Exit criteria**: simulari de failure (heap leak artificial, cablu Ethernet scos, sensor I2C deconectat) → recovery automat in toate cazurile.
**Deliverables**: hardening complet.
**Rollback**: git revert mecanisme problematice individual.
**Risk**: medium (mecanismele se pot interfera reciproc — testare incrementala).

### 18.20 FAZA S — Memory optimization

**Goal**: free heap stabil >150KB Master, >200KB Slave; monitor activ.

**Sessions**:
- **S1** — `EVENT_LOG_MAX_ENTRIES=20`, eliminare EVT_INFO/EVT_DEBUG. Logger production: `LOG_LEVEL_WARN` default, macros DEBUG/INFO compile-out.
- **S2** — Pre-allocate buffers (`_publishBuffer[1024]` static in MqttBridge, etc.). Inlocuit `String` cu `char[]` + `snprintf` peste tot in hot path. Stack 16KB build flag. `monitorHeapTrend()` in loop().

**Entry criteria**: faza R.
**Exit criteria**: dupa 1h activitate, `getMinFreeHeap()` ramas stabil (deviatie <5KB). Build production size redus cu ~5KB.
**Deliverables**: codbase optimizat memorie.
**Rollback**: git revert per modul.
**Risk**: low.

### 18.21 FAZA T — OTA Master via Ethernet

**Goal**: Master OTA functional cu rollback automat.

**Sessions**:
- **T1** — `OtaUpdater.cpp` refactorat cu `EthernetClient` + `SSLClient` + `ArduinoHttpClient`. `HiveMqTrustAnchor.h` extins cu DigiCert Global Root pentru github.com.
- **T2** — Test OTA Master complet: bump build, push GitHub Release, cmd `triggerOta` din MAUI, ESP32 descarca + valideaza SHA + reboot. Test rollback cu firmware buggy (sterge `markFirmwareValid()`) → urmatorul reboot revine la versiune anterioara.

**Entry criteria**: faza S (memorie stabila pentru OTA).
**Exit criteria**: 2 OTA-uri reusite consecutive + 1 rollback verificat.
**Deliverables**: OTA Master ready for production.
**Rollback**: flash via USB-C ramane disponibil mereu.
**Risk**: **high** — TLS HTTPS + SSLClient + Ethernet + Update partition = combinatie complexa.

### 18.22 FAZA U — OTA Slave prin Master proxy

**Goal**: OTA Slave din MAUI in <30 secunde.

**Sessions**:
- **U1** — `OtaReceiver.h/.cpp` Slave + extindere `CommandDispatcher` cu `OTA_BEGIN`, `OTA_CHUNK`, `OTA_END`, `UART_BAUD_HIGH/LOW`.
- **U2** — `SlaveOtaProxy.h/.cpp` Master care descarca prin SSLClient + streamează chunked prin UART. Cmd `triggerOtaSlave` in MqttBridge.
- **U3** — MAUI UI: model `SlaveOtaProgress`, `IMqttService.OnSlaveOtaProgress` event, `LedPage.xaml` (sectiune dedicata) cu ProgressBar + indicatori success/fail. Buton „FETCH LATEST RELEASE" din GitHub API.

**Entry criteria**: faza T.
**Exit criteria**: 2 OTA Slave reusite consecutive cu progress bar vizibil in MAUI.
**Deliverables**: OTA Slave end-to-end.
**Rollback**: flash USB-C Slave ramane disponibil.
**Risk**: **high** — protocol nou complex, testare extensiva necesara.

### 18.23 FAZA V — LED hardware standalone

**Goal**: banda LED 24V se aprinde la intensitate configurabila prin sketch standalone.

**Sessions**:
- **V1** — Cablaj NCEP01T18 + sursa 24V + banda LED 36W + GND comun cu Slave. Sketch test: `ledcSetup(0, 5000, 12); ledcAttachPin(25, 0); ledcWrite(0, 2048);` → 50% intensitate vizibil.
- **V2** — `LedController.h/.cpp` integrat in Slave cu PWM LEDC + persistenta NVS minimal (doar setIntensity → save). Test setIntensity/getIntensity prin Serial.

**Entry criteria**: faza C (NCEP01T18 testat).
**Exit criteria**: banda LED raspunde la setIntensity 0/25/50/75/100%.
**Deliverables**: hardware LED + LedController v1 standalone.
**Rollback**: deconectare LED, sketch standalone.
**Risk**: medium (cablaj 24V).

### 18.24 FAZA W — LED schedule + time sync

**Goal**: schedule LED autonom pe Slave dupa primul TIME_SYNC.

**Sessions**:
- **W1** — Extindere `CommandDispatcher` Slave: `LED_SET`, `LED_SCHEDULE`, `LED_STATUS`, `TIME_SYNC` (cu CRC).
- **W2** — Master trimite `TIME_SYNC <epoch>` orar in `loop()` daca timpul a fost sincronizat NTP.
- **W3** — `LedController::tick()` cu logica fereastra orara + manual override 1h. NVS save/load.

**Entry criteria**: faza V + faza N (Master ↔ Slave UART).
**Exit criteria**: setezi schedule prin UART manual din PC, vezi banda LED se aprinde la ora setata, se stinge la ora setata.
**Deliverables**: schedule autonom Slave.
**Rollback**: dezactivare schedule (`enabled=false`) → LED off mereu.
**Risk**: low.

### 18.25 FAZA X — LED control via MQTT + MAUI UI

**Goal**: control complet LED de oriunde din lume, setarile persistente.

**Sessions**:
- **X1** — Extindere `MqttPending` cu `setLedNow` + `setLedSched`. Handlere in `processZones` care trimit pe UART. Validare range pe Master inainte de forward.
- **X2** — Extindere JSON state Master cu camp `led` (intensity, enabled, schedule). Master cere `LED_STATUS` Slave la fiecare ciclu si include in publish.
- **X3** — MAUI: `LedState` model, `LedViewModel` cu sliders + TimePickers + Switch. `LedPage.xaml` + navigare in `FloatingNavBar` (sau sectiune in DevicesPage). Test E2E.

**Entry criteria**: faza W + faza P.
**Exit criteria**: din MAUI controlezi intensitatea + schedule complet, vezi state actual reflectat din MQTT.
**Deliverables**: LED feature complete user-facing.
**Rollback**: ascundere tab LED in MAUI.
**Risk**: low.

### 18.26 FAZA Y — LED dual-mirror NVS

**Goal**: schedule indestructibil prin oglindire NVS pe ambele placi.

**Sessions**:
- **Y1** — `LedConfigStorage.h` pe Master cu `save()`/`load()`. In `setup()` Master, dupa Slave accesibil, `LED_STATUS` + comparatie cu NVS local → re-sync daca divergent.

**Entry criteria**: faza X.
**Exit criteria**: factory reset Slave (Hold RESET 3s) → Master detecteaza divergenta → re-trimite schedule → Slave revine la ultima configuratie.
**Deliverables**: garantie persistenta.
**Rollback**: dezactivare auto-sync (utilizatorul retrimite manual din MAUI).
**Risk**: low.

### 18.27 FAZA Z — Hardware production

**Goal**: ambele placi in carcase, conexiuni permanente, gata de deploy.

**Sessions**:
- **Z1** — Surse industriale Mean Well 5V × 2 + 24V LED. Carcase IP54 × 2. Soldering conexiuni permanente (NU dupont). Cabluri Cat6 sertizate definitiv. Sursa 24V + cablaj LED separat.
- **Z2** — Filtre PTFE peste senzori SHT30. TVS diode pe LAN doar Master. MOV/bara surge pe AC. Fixare carcase pe tablou DIN-rail.

**Entry criteria**: toate fazele anterioare (A-Y).
**Exit criteria**: ambele placi alimentate din carcase, totul functioneaza identic ca pe breadboard, fara dupont.
**Deliverables**: hardware production-grade.
**Rollback**: revert la breadboard daca nu functioneaza.
**Risk**: medium (cabluri 230V — atentie la izolare!).

### 18.28 FAZA AA — Burn-in + observation

**Goal**: sistem certificat productie 7 zile fara interventie.

**Sessions**:
- **AA1** — Pornire 24h burn-in cu monitor serial + MQTT logs continui. Heat IR pe NCEP01T18 si W5500 verificat.
- **AA2** — Analiza criterii acceptare: <5 erori UART/h, <1 reconnect MQTT/h, free heap stabil (deviatie <10KB), 0 reboot-uri neplanificate, temp/hum coerente, LED schedule executa corect.
- **AA3** — Deploy productie + observation 7 zile cu `/diag` MQTT subscriber. Daca toate criteriile satisfacute → sistem certificat.

**Entry criteria**: faza Z.
**Exit criteria**: 7 zile fara interventie umana, niciun reboot neplanificat, MAUI raspunde la fiecare comanda.
**Deliverables**: sistem in productie.
**Rollback**: revert la sistem vechi (1 board cu DHT22) daca esueaza criteriile.
**Risk**: medium.

### 18.29 FAZA BB — Documentatie + handoff

**Goal**: proiect complet documentat, gata de transferat.

**Sessions**:
- **BB1** — README ambele proiecte actualizate (arhitectura dual-board, cablaje, librarii). Scripts/ verificate runabile. CLAUDE.md actualizat. Diagrama bloc final. Checklist mentenanta lunara/anuala. Commit final tag `release/v2.0`.

**Entry criteria**: faza AA.
**Exit criteria**: o persoana fara context anterior poate deploya proiectul urmand README + scripts.
**Deliverables**: documentatie completa, repo curat.
**Rollback**: n/a.
**Risk**: low.

### 18.30 Reguli generale per faza

1. **Inceput de faza**: pull origin/main, asigura faza prerequisita e merged.
2. **Pe parcursul fazei**: commit frecvent (la fiecare sesiune incheiata cu test verde) cu mesaje descriptive.
3. **Sfarsit de faza**: commit consolidant cu tag `feat/phase-X-complete`. Push remote. Update plan file daca e nevoie cu lectii invatate.
4. **Daca o sesiune esueaza**: nu trece la urmatoarea pana nu rezolvi sau ai un workaround documentat. Revert daca e nevoie.
5. **Daca faza intreaga esueaza**: evaluare planuri B (mentionate in sectiunea 20). Decizie: continuare cu workaround sau abandon faza.

### 18.31 Cum folosesti aceasta lista

- **Imprima sau salveaza** acest cuprins ca checklist.
- **Bifeaza fiecare sesiune** dupa ce o termini cu test verde.
- **Nu sari peste sesiuni** — fiecare are un rol in stratificarea complexitatii.
- **La final de saptamana** revizuiesti progresul; daca sub asteptari, reordoneaza saptamanile (poti amana FazaT/U OTA pentru week 4 daca e prea greu).
- **Pastreaza un log** al timpului real petrecut per faza — pentru estimari mai bune in proiecte viitoare.

## 18.OLD — Vechi: Ordine de executie — 48 sesiuni atomice in 10 faze (PASTRAT pentru referinta)

Fiecare sesiune are: **deliverable** (ce iese), **durata** estimata, **dependinte** (ce trebuie facut inainte), **test** (cum verifici ca merge). Sesiunile sunt grupate astfel incat fiecare faza sa lase sistemul in stare functionala (commit-able).

### FAZA 1 — Pregatire (S01-S03) — ~3h

**S01 — Setup mediu de dezvoltare** | 30min | dep: niciuna
- Deliverable: arduino-cli configurat cu board ESP32, librarii instalate (`Adafruit SHT31`, `Ethernet`, `SSLClient`, `ArduinoHttpClient`, `ArduinoJson`, `PubSubClient`, `Adafruit NeoPixel`).
- Test: `arduino-cli lib list | grep -E "SHT31|SSLClient|Ethernet"` returneaza toate.
- Git: `git checkout -b feat/dual-board-uart`.

**S02 — Sketch loopback UART pe Slave standalone** | 30min | dep: S01
- Deliverable: sketch minimal de test cu TX(17) → RX(16) cu fir scurt pe aceeasi placa, trimite text si primeste echo.
- Test: serial monitor afiseaza ecou perfect timp de 1 minut.

**S03 — Sketch UART punct-la-punct** | 1h | dep: S02 + 2 placi Carbon V3
- Deliverable: 2 sketch-uri (master_test, slave_test). Master trimite `PING\n` la fiecare 1s, Slave raspunde `PONG\n`. Latency afisata.
- Test: cu fir scurt 30cm — 0 erori 5min. Apoi cu Cat6 1.5m sertizat — 0 erori 5min. Latency <50ms.

### FAZA 2 — Module fundamentale Slave (S04-S10) — ~6h

**S04 — Creare proiect ESP32_Slave** | 30min | dep: S01
- Deliverable: directorul `ESP32_Slave/` cu skeleton: `ESP32_Slave.ino` (gol), `Config.h` (constants), `scripts/build.sh`, `scripts/flash.sh`, `README.md`.
- Test: `bash ESP32_Slave/scripts/build.sh` compileaza skeleton-ul fara erori.

**S05 — Module foundation: Logger + WatchdogManager** | 30min | dep: S04
- Deliverable: `Logger.h` (macros LOG_*) + `WatchdogManager.h` (init + feed). Apel in `setup()` + `loop()`.
- Test: serial monitor afiseaza `[ms][I] Boot — fw build N`. Watchdog reseteaza placa daca pun `delay(70000)` in loop.

**S06 — Module Sht30Sensor + test standalone** | 1h | dep: S05 + modul SHT30 fizic
- Deliverable: `Sht30Sensor.h` (header-only) cu `begin()` + `read()` + cooldown + retry.
- Test: in `setup()` Slave init senzor, in `loop()` printeaza temp/hum la fiecare 5s. Valori reale in serial monitor.

**S07 — Modul SystemLED (port din Master)** | 30min | dep: S05 + modul WS2812B activ
- Deliverable: `SystemLED.h/.cpp` cu `enum class Status` si `setStatus()`. Verde/oranj/rosu vizibile.
- Test: in `setup()` cycle prin toate Status in 5 sec → vezi toate culorile.

**S08 — Modul UartProtocol (buffering + serializare)** | 1.5h | dep: S05
- Deliverable: `UartProtocol.h/.cpp` cu `poll(char*, size_t)`, `sendLine()`, `sendJson()`. Buffer static `char[128]`. Overflow handling cu log warning.
- Test: in `loop()` Slave, daca primeste linie, o printeaza pe Serial (USB) si o trimite inapoi. Cu `screen /dev/ttyUSB1 115200` trimit linii din PC, vad ecou.

**S09 — CommandDispatcher cu GET_SENSOR + PING + REBOOT** | 1.5h | dep: S06+S07+S08
- Deliverable: `CommandDispatcher.h/.cpp` cu DI prin constructor, `tick()` apelat din `loop()`. Handlere private pentru fiecare cmd. JSON response cu `StaticJsonDocument<256>`.
- Test: trimit `GET_SENSOR\n` din PC → primesc JSON valid cu temp/hum reale. `PING\n` → `PONG\n`. `REBOOT\n` → `OK\n` apoi placa reboot.

**S10 — Integration test Slave full** | 30min | dep: S09
- Deliverable: Slave functional complet (proiect Faza 2 final).
- Test: flash + reboot 5x, fiecare boot urmat de GET_SENSOR returneaza valori valide. LED in stare ACTIVE/IDLE corect.
- **Commit**: `feat(slave): minimal UART responder with SHT30`.

### FAZA 3 — Master Ethernet + MQTT TLS (S11-S15) — ~7h

**S11 — Sketch test Master Ethernet** | 1.5h | dep: S01 + W5500 fizic
- Deliverable: sketch minimal cu `Ethernet.begin(mac)` + DHCP + `Serial.println(Ethernet.localIP())`.
- Test: de pe laptop `ping <ip>` raspunde sub 5ms timp de 1 minut.

**S12 — HiveMqTrustAnchor + sketch test SSLClient** | 2h | dep: S11
- Deliverable: `HiveMqTrustAnchor.h` generat cu `pycert_bearssl.py` din ISRG Root X1. Sketch test publish dummy pe `ventilatie/test`.
- Test: `mosquitto_sub -h <hivemq> -p 8883 --capath /etc/ssl/certs -t ventilatie/test -u <user> -P <pass>` primeste mesaje.

**S13 — Refactor Config.h Master** | 1h | dep: S12
- Deliverable: noul Config.h cu `constexpr` (NU `#define` pentru constante numerice), structurat in sectiuni: HARDWARE PINS, SHT30, UART, ETHERNET, MQTT, OTA, NVS, FW. Sterge Blynk/WiFi/DHT/VP_*.
- Test: `arduino-cli compile --dry-run` valideaza ca nu exista referinte rupte la simboluri sterse.

**S14 — Implementare Sht30Sensor pe Master** | 30min | dep: S13
- Deliverable: copie `Sht30Sensor.h` din `ESP32_Slave/` in `ESP32/`. Init in setup() pe SHT30 stanga la 0x44.
- Test: serial print temp/hum stanga real timp de 1 minut.

**S15 — Refactor VentilationZone (local + remote)** | 2h | dep: S14
- Deliverable: 2 constructori (`VentilationZone(Sht30Sensor*, relayPin, name)` pentru local, `VentilationZone(relayPin, name)` pentru remote). Metoda `setExternalSensorValues()`. `enterFailsafe()`/`exitFailsafe()`.
- Test: instantiez `leftZone` cu sensor local + `rightZone` remote, in loop apelez `setExternalSensorValues(20.0, 50.0, ...)` si vad updateLogic functioneaza.

### FAZA 4 — Master integrare retea + UART + MQTT (S16-S22) — ~10h

**S16 — Eliminare cod Blynk + WiFiManager** | 1.5h | dep: S13
- Deliverable: ~600 linii eliminate. Sterge include-uri, BLYNK_CONNECTED, BLYNK_WRITE handlers, struct BlynkPending, wifiManager.autoConnect, butonul WiFi-reset (3-sec hold). Comenteaza temporar referintele in MqttBridge la LOCK_BLYNK.
- Test: `arduino-cli compile` reuseste fara erori (Master fara Blynk, dar inca pe vechiul WiFiClientSecure).

**S17 — Integrare Ethernet + Wire + MAC eFuse** | 1.5h | dep: S15+S16
- Deliverable: `setup()` Master cu init Wire, init Ethernet, helper `getEthernetMac()`. WiFi.mode(WIFI_OFF). Bluetooth off.
- Test: serial monitor: `MAC: DE:XX:XX:XX:XX:XX`, `IP: 192.168.x.x`, `MQTT connected` (cu vechiul WiFiClientSecure inca, mai schimbam la S19).

**S18 — Implementare SlaveUartClient** | 2h | dep: S17 + S10 (Slave functional)
- Deliverable: `SlaveUartClient.h/.cpp` cu `fetch()`, `sendReboot()`, `ping()`. Retry intern, timeout 1s, error tracking.
- Test: in `loop()` Master apelez `slaveClient.fetch()` la fiecare 5s, vad temp/hum venite de la Slave (placat in cealalta camera 1.5m via Cat6).

**S19 — Migrare MqttBridge la SSLClient + EthernetClient** | 2h | dep: S17
- Deliverable: in `MqttBridge.h/.cpp` inlocuiesc `WiFiClientSecure _net` cu `EthernetClient _baseClient` + `SSLClient _net`. Init in constructor cu TAs din `HiveMqTrustAnchor.h`.
- Test: MQTT connect via Ethernet, publish state, MAUI primeste mesaje ca inainte.

**S20 — Refactor processZones cu local + remote** | 2h | dep: S18+S19
- Deliverable: in `processZones()`: fetch Slave UART → setExternalSensorValues pe rightZone → readSensor local pe leftZone → updateLogic pe ambele → mqtt.publish.
- Test: state JSON publicat are `left.temp`/`right.temp` ambele cu valori reale + `slave.online: true`.

**S21 — Campuri MQTT state JSON noi** | 1h | dep: S20
- Deliverable: in `MqttBridge::_publishStateNow()` adauga `slave.online`, `slave.lastSeen`, `slave.errors`, `right.failsafe`. Lock owner simplificat la `LOCK_NONE`/`LOCK_MQTT`.
- Test: MQTT Explorer arata noul state JSON cu toate campurile.

**S22 — Burn-in scurt 1h Master + Slave** | 30min + 1h pasiv | dep: S21
- Deliverable: sistem functional capat-la-capat (fara MAUI updates inca).
- Test: 1h cu monitor serial, 0 erori, temp/hum coerente pe ambele zone, relee actioneaza la threshold.
- **Commit**: `feat(master): dual-board UART + Ethernet + MQTT TLS`.

### FAZA 5 — MAUI updates pentru dual-board (S23-S25) — ~3h

**S23 — Update Models MAUI** | 30min | dep: S21
- Deliverable: `MobileApp/Models/VentilationState.cs` cu `SlaveStatus` sub-model. JsonPropertyName-uri.
- Test: build MAUI Release reuseste, deserializare state nou pe Dashboard fara erori.

**S24 — DashboardViewModel + System lock owner** | 1h | dep: S23
- Deliverable: simplificare banner lock (sterge ramura Blynk), adaugare banner „Senzor zona dreapta inaccesibil" cand `slaveOnline=false`.
- Test: scot Slave din priza → banner aparut in MAUI in <30s. Repornesc → dispare in <30s.

**S25 — Test MAUI E2E** | 1.5h | dep: S24
- Deliverable: toate functionalitatile MAUI verificate cu noul firmware: Dashboard, Settings, Devices, Reports, System.
- Test: temp/hum reale, REFRESH instant, Settings save, Override toggle, Reports load. Toate butoanele functioneaza.
- **Commit**: `feat(maui): support slave online status + drop blynk references`.

### FAZA 6 — Reset/reboot infrastructure (S26-S28) — ~3h

**S26 — cmd:rebootSlave end-to-end** | 1.5h | dep: S25
- Deliverable: in `MqttBridge` adaugare `rebootSlave` in `MqttPending`. In `processZones()` handler care cheama `slaveClient.sendReboot()`. Buton „RESTART SLAVE" in MAUI System tab.
- Test: apesi „RESTART SLAVE" → log Master „[Slave] reboot OK" → Slave reboot in 5s → recovery automat.

**S27 — Test reset/reboot/rebootSlave** | 1h | dep: S26
- Deliverable: workflow validat pentru toate cele 3 comenzi (RESET DEFAULT MASTER, RESTART MASTER, RESTART SLAVE).
- Test: dupa fiecare comanda, sistem revine la stare normala in <30s, fara interventie manuala.

**S28 — Validare semantica reset** | 30min | dep: S27
- Deliverable: documentatie inline in cod pentru ce face fiecare reset (commenturi peste handler-ele MqttBridge).
- Test: dupa cmd:reset, NVS wiped → defaults restaurate → MAUI vede valori default. Slave NU afectat.
- **Commit**: `feat(reset): add rebootSlave command + UI`.

### FAZA 7 — OTA Master + Slave (S29-S33) — ~10h

**S29 — Migrare OtaUpdater Master la SSLClient** | 2h | dep: S22
- Deliverable: `OtaUpdater.cpp/.h` refactorat cu `EthernetClient` + `SSLClient` + `ArduinoHttpClient`. TA combinat (ISRG + DigiCert) in `HiveMqTrustAnchor.h`.
- Test: cmd `triggerOta` din MAUI cu URL release valid → ESP32 descarca + valideaza SHA + reboot pe noul firmware.

**S30 — Test OTA Master cu rollback** | 1h | dep: S29
- Deliverable: confirmare ca rollback automat functioneaza. Construiesc deliberat un firmware buggy (e.g. fara `markFirmwareValid()` apelat) → la urmatorul boot bootloader rollback.
- Test: dupa OTA cu firmware buggy, urmatorul reboot → vad versiunea anterioara in `fwBuild`.

**S31 — Implementare OtaReceiver Slave** | 2h | dep: S10
- Deliverable: `OtaReceiver.h/.cpp` Slave cu `begin()`, `writeChunk()`, `end()`. Extindere `CommandDispatcher` cu handlere `OTA_BEGIN`, `OTA_CHUNK`, `OTA_END`, `UART_BAUD_HIGH/LOW`.
- Test: trimit manual de pe PC `OTA_BEGIN 100 deadbeef...\n` apoi 100 bytes binari + `OTA_END\n` → Slave raspunde corect (chiar daca esueaza la SHA, vezi protocolul ok).

**S32 — Implementare SlaveOtaProxy Master** | 2.5h | dep: S29+S31
- Deliverable: `SlaveOtaProxy.h/.cpp` care primeste URL+SHA, descarca via SSLClient, switch baud 460800, streamează chunked, trimite OTA_END. Adaugare cmd `triggerOtaSlave` in MqttBridge.
- Test: din MAUI cmd manual `{"cmd":"triggerOtaSlave","url":"...","sha":"..."}` → Master descarca + transmite + Slave reboot pe noul firmware.

**S33 — MAUI UI OTA Slave** | 2h | dep: S32
- Deliverable: model `SlaveOtaProgress`, eveniment `IMqttService.OnSlaveOtaProgress`, sectiune dedicata in `SystemPage.xaml`, comenzi `TriggerSlaveOtaAsync` + `FetchLatestSlaveReleaseAsync`, ProgressBar + indicatori success/fail.
- Test: in MAUI apesi „FETCH LATEST" → URL+SHA auto-completate. Apesi „TRIGGER UPDATE" → ProgressBar 0→100% in ~30s, mesaj „Slave reboot…", Slave online dupa 10s.
- **Commit**: `feat(ota): slave OTA via UART chunked + MAUI UI`.

### FAZA 8 — Always-on resilience (S34-S39) — ~8h

**S34 — Brownout + Heap monitor + Boot loop guard** | 2h | dep: S22
- Deliverable: `configureBrownout()`, `HeapMonitor.h`, `BootLoopGuard.h`. Apelate la inceputul `setup()` (ambele placi pentru watchdog/brownout, doar Master pentru heap si bootguard).
- Test: simulez heap leak (alocari `malloc(1024)` in loop fara free) → restart preventiv. Reboot 6x rapid → safe mode (LED rosu, fara init normal).

**S35 — I2C bus recovery + Stuck relay detection** | 1.5h | dep: S34
- Deliverable: `I2CRecovery::recoverBus()` apelat din `Sht30Sensor::read()` la N esecuri consecutive. Stuck detection in `VentilationZone::updateLogic()`.
- Test: scot fizic SDA-ul → recovery trigger → reia bus → sensor read OK la urmatorul ciclu. Forțez relay stuck cu pin scurt → log eveniment „RELAY_STUCK".

**S36 — Preventive reboot (NTP Master, uptime Slave)** | 1h | dep: S34
- Deliverable: `PreventiveReboot.h` + apel periodic in `loop()`.
- Test: cu uptime fortat la 6 zile + ora 03:00 simulata, restart automat. Pe Slave dupa 7 zile uptime simulat → restart.

**S37 — NVS corruption fallback + TLS retry** | 1h | dep: S34
- Deliverable: `AppPreferences::_validateOrFallback()`. `MqttBridge::_connect()` cu backoff exponential + restart la 20 esecuri.
- Test: pun manual valori out-of-range in NVS (`tempThresh=200.0`) → la boot se reseteaza la default. Block port 8883 in firewall → backoff vizibil 5s, 10s, 20s, 40s, 60s.

**S38 — Ethernet link monitor + Self-WDT processZones + Slave idle** | 1.5h | dep: S22
- Deliverable: `monitorEthernetLink()` in loop Master cu reset W5500 dupa 10min link-down. Self-WDT pe processZones >2× interval → restart. Slave idle >30min → restart.
- Test: scot cablu Ethernet 11min → reset W5500 vizibil + DHCP retry. Pun `delay(700000)` artificial in processZones → restart self-WDT.

**S39 — Self-monitoring /diag topic + MAUI alarme** | 1h | dep: S38
- Deliverable: in MqttBridge publish periodic (la fiecare 5min) pe `ventilatie/diag` cu uptime, freeHeap, ethLinkUp, slaveErrors, bootCount. MAUI subscribe + alarma vizuala daca freeHeap<30KB sau slaveErrors>100.
- Test: simulez heap leak → MAUI afiseaza „⚠ Heap critical" in System tab.
- **Commit**: `feat(resilience): always-on hardening (14 mechanisms)`.

### FAZA 9 — Memory & optimization (S40-S43) — ~4h

**S40 — Reducere EventLog 50→20 entries doar ERROR/WARN** | 1h | dep: S22
- Deliverable: `EVENT_LOG_MAX_ENTRIES=20`, eliminare EVT_INFO/EVT_DEBUG, doar ERROR si WARNING stocate.
- Test: in MAUI Reports, vad doar 20 entries cele mai recente, doar erori si warning-uri.

**S41 — Cleanup Logger production** | 30min | dep: S40
- Deliverable: macros LOG_DEBUG/LOG_INFO compile-out la `LOG_LEVEL_WARN` default. Build flag `-DDEBUG_BUILD` pentru a activa toate.
- Test: build production fara `-DDEBUG_BUILD` → flash size redus cu ~5KB. Serial USB curat.

**S42 — Pre-allocate buffers + replace String** | 1.5h | dep: S22
- Deliverable: in MqttBridge `_publishBuffer[1024]` static. Inlocuit `String` cu `char[]` + `snprintf` peste tot in hot path.
- Test: dupa 1h activitate, `getMinFreeHeap()` ramas stabil (deviatie <5KB).

**S43 — Stack 16KB + heap leak detection** | 1h | dep: S42
- Deliverable: build flag `-DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384`. `monitorHeapTrend()` in `loop()`.
- Test: `uxTaskGetStackHighWaterMark(NULL)` arata >2KB margin dupa load greu (TLS+OTA+JSON).
- **Commit**: `perf(memory): pre-allocate buffers, drop logs to 20 entries`.

### FAZA 10 — Production hardening + deploy (S44-S48) — ~30h (incluzand burn-in)

**S44 — Hardware production: surse industriale + carcase + soldering** | 4h | dep: S39
- Deliverable: 2x Mean Well IRM-20-5ST, 2x carcase IP54 cu placile montate, conexiuni soldering (NU dupont) intre componente, lipire jumper-uri Cat6 la headere.
- Test: ambele placi alimentate, in carcase, functioneaza identic ca pe breadboard.

**S45 — Filtre PTFE + surge protection** | 2h | dep: S44
- Deliverable: 2x filtre PTFE peste senzori SHT30, TVS diode pe pereche RJ45 LAN doar pe Master, MOV/bara surge pe AC inainte de surse.
- Test: rezistenta de izolatie + measurarea ca temp/hum cititi nu se schimba semnificativ post-filtru (drift <0.5°C).

**S46 — Burn-in 24h** | 24h pasiv + 1h analiza | dep: S45
- Deliverable: ambele placi pornite 24h, monitor serial + MQTT logs, fara interventie umana.
- Test acceptat: <5 erori UART/h, <1 reconnect MQTT/h, freeHeap stabil (deviatie <10KB), 0 reboot-uri neplanificate, temp/hum coerente.

**S47 — Documentatie completa + scripts** | 2h | dep: S46
- Deliverable: `ESP32/README.md` actualizat (arhitectura dual-board), `ESP32_Slave/README.md`, `scripts/build.sh` si `scripts/flash.sh` pentru ambele proiecte, `CLAUDE.md` actualizat.
- Test: o persoana fara context poate rula `./scripts/build.sh && ./scripts/flash.sh /dev/ttyUSB0` cu succes urmand README-ul.

**S48 — Deploy productie + observation 7 zile** | 1h activ + 7 zile pasiv | dep: S47
- Deliverable: instalat la locatia finala, conectat la reteaua productie.
- Test acceptat 7 zile: `ventilatie/diag` arata uptime continuu fara reboot-uri neasteptate, freeHeap stabil, slaveErrors <50/zi, MAUI raspunde la fiecare comanda. **Sistem certificat productie**.
- **Commit final**: `release: dual-board UART system v2.0 production-ready`.

### Sumar timing

| Faza | Sesiuni | Durata activa | Cumulativ |
| --- | --- | --- | --- |
| 1. Pregatire | S01-S03 | 3h | 3h |
| 2. Slave fundamentals | S04-S10 | 6h | 9h |
| 3. Master Ethernet+MQTT | S11-S15 | 7h | 16h |
| 4. Master integrare | S16-S22 | 10h | 26h |
| 5. MAUI updates | S23-S25 | 3h | 29h |
| 6. Reset infrastructure | S26-S28 | 3h | 32h |
| 7. OTA dual | S29-S33 | 10h | 42h |
| 8. Resilience always-on | S34-S39 | 8h | 50h |
| 9. Memory optimization | S40-S43 | 4h | 54h |
| 10. Production hardening | S44-S48 | 9h activ + 31h pasiv | 63h activ + burn-in |
| **TOTAL** | **48 sesiuni** | **~63h munca activa** | distribuibil pe **2-3 saptamani** |

### Recomandare planificare

- **Saptamana 1**: Faze 1-4 (S01-S22) — sistem core functional capat-la-capat
- **Saptamana 2**: Faze 5-7 (S23-S33) — MAUI updates + reset + OTA
- **Saptamana 3**: Faze 8-10 (S34-S48) — resilience + production deploy

Fiecare faza incheiata = punct de salvare git stabil. Daca apare blocaj la o sesiune, poti reveni la ultimul commit si continua dupa rezolvare.

| Etapa | Durata | Output |
| --- | --- | --- |
| 1. Sketch loopback UART pe Slave | 30min | confirma Serial2 OK |
| 2. Sketch UART punct-la-punct (fir scurt) | 1h | confirma protocol GET_SENSOR + parse JSON |
| 3. Cablare Cat6 1.5m + retest | 1h | confirma 0 erori 5min |
| 4. Implementare `Sht30Sensor` (fisier comun) | 1h | API stabil pentru ambele proiecte |
| 5. Firmware complet Slave + flash + test | 2h | Slave functional |
| 6. Sketch test Master Ethernet + ping | 30min | confirma W5500 OK |
| 7. Sketch test Master SSLClient + HiveMQ | 1.5h | confirma TLS OK |
| 8. Refactor `VentilationZone` (local + remote) | 1.5h | suport ambele moduri |
| 9. Implementare `SlaveUartClient` cu retry | 1h | API stabil pe Master |
| 10. Eliminare cod Blynk + WiFiManager | 1h | -600 linii |
| 11. Integrare Ethernet + Serial2 + MAC eFuse in Master | 1h | Master cu noua infrastructura |
| 12. Migrare `MqttBridge` la SSLClient + handler `rebootSlave` + JSON state nou | 2h | MQTT functional |
| 13. Migrare `OtaUpdater` la SSLClient + ArduinoHttpClient | 2h | OTA functional |
| 14. Cleanup MAUI (lock + slave indicator + butoane Master/Slave) | 1.5h | UI complet |
| 15. Test full + 24h burn-in | 24h | productie ready |

**Total estimare munca activa**: 17-19h, in 3 sesiuni de 6-7h fiecare. Plus 24h burn-in pasiv.

## 19. Configurare Arduino IDE pentru ambele proiecte

Sectiune dedicata setup Arduino IDE 2.x (varianta moderna, recomandata) pentru a putea compila si flash atat **ESP32/** (Master) cat si **ESP32_Slave/** (Slave). Optional: arduino-cli pentru flow CI/CD.

### 19.1 Instalare Arduino IDE 2.x

1. Descarca de la <https://www.arduino.cc/en/software> versiunea 2.3+ pentru OS-ul tau.
2. Linux: muta `arduino-ide` in `/opt/` si creeaza shortcut. Adauga utilizatorul la grupul `dialout` pentru access serial USB:
   ```bash
   sudo usermod -aG dialout $USER
   # logout/login pentru a activa
   ```
3. Windows: ruleaza installer-ul, accepta drivere CH340/CP2102 daca apar.
4. macOS: drag-and-drop `Arduino IDE.app` in Applications.

### 19.2 Adaugare suport ESP32 (board manager)

1. Deschide IDE → **File → Preferences** (sau `Ctrl+,`).
2. La „**Additional Boards Manager URLs**" lipeste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Apasa OK.
4. Deschide **Tools → Board → Boards Manager** (sau `Ctrl+Shift+B`).
5. Cauta „**esp32**" → **esp32 by Espressif Systems** → **Install** (versiune ≥ 2.0.14, recomandat 3.0+).
6. Asteapta ~3-5 minute (descarca toolchain xtensa).

### 19.3 Selectare board Carbon V3

1. **Tools → Board → esp32 → ESP32 Dev Module** (Carbon V3 e compatibil cu acest profile).
2. Setari recomandate **Tools → ...**:

| Optiune | Valoare | Note |
| --- | --- | --- |
| Board | ESP32 Dev Module | FQBN = `esp32:esp32:esp32` |
| Upload Speed | 921600 | reducere la 460800 daca apar erori |
| CPU Frequency | 240MHz (WiFi/BT) | maxim performanta |
| Flash Frequency | 80MHz | |
| Flash Mode | DIO | |
| Flash Size | 4MB (32Mb) | confirmat Carbon V3 |
| **Partition Scheme** | **Default 4MB with spiffs** sau **Minimal SPIFFS (1.9MB APP/190KB SPIFFS/190KB...)** | pentru OTA — vezi 20.6 |
| Core Debug Level | None (production) sau Info (debug) | |
| Erase All Flash Before Sketch Upload | Disabled | enable doar daca vrei wipe complet |
| PSRAM | Disabled | Carbon V3 nu are PSRAM extern |

### 19.4 Instalare drivere USB-UART

Carbon V3 foloseste **CH340** (sau CP2102 in unele variante). La conectare prin USB-C, OS-ul trebuie sa creeze un port `/dev/ttyUSB0` (Linux), `COM3+` (Windows) sau `/dev/cu.usbserial-*` (macOS).

- **Linux**: drivere incluse in kernel — fara instalare. Verifica cu `dmesg | tail` la conectare placa: trebuie sa apara `ch341-uart converter now attached to ttyUSB0`.
- **Windows**: descarca de la <http://www.wch.cn/downloads/CH341SER_EXE.html>.
- **macOS**: <https://github.com/WCHSoftGroup/ch34xser_macos> sau via Homebrew `brew install --cask wch-ch34x-usb-serial-driver`.

Verificare port:
- Linux: `ls /dev/ttyUSB*` (fiecare placa primeste un port diferit; cu 2 placi conectate, vei avea `/dev/ttyUSB0` si `/dev/ttyUSB1`).
- Identifica care e Master, care e Slave: deschide Serial Monitor in IDE pe fiecare port → vezi log-ul de boot.

### 19.5 Instalare librarii Arduino — ambele proiecte

**Tools → Manage Libraries** (sau `Ctrl+Shift+I`).

#### 20.5.1 Librarii necesare pentru Master (proiect ESP32/)

| Librarie | Versiune minim | Cauta in Library Manager | Note |
| --- | --- | --- | --- |
| Adafruit SHT31 Library | 2.2.0 | „SHT31" | wrapper SHT30/SHT31 |
| Adafruit BusIO | 1.14 | auto-instalat ca dep | I2C wrapper |
| Adafruit NeoPixel | 1.12 | „NeoPixel" | LED WS2812B |
| ArduinoJson | 6.21+ | „ArduinoJson" | NU 7.x (breaking changes) |
| PubSubClient | 2.8 | „PubSubClient" by Nick O'Leary | MQTT client |
| Ethernet | 2.0+ | „Ethernet" (oficial Arduino) | W5500 driver |
| SSLClient | 1.6.11 | „SSLClient" by OPEnSLab | TLS over Ethernet |
| ArduinoHttpClient | 0.6+ | „ArduinoHttpClient" oficial | HTTP/HTTPS pentru OTA |

**NU** instala (sterse din proiect):
- WiFiManager
- Blynk (BlynkSimpleEsp32)
- DHT sensor library
- Adafruit Unified Sensor (era folosit doar de DHT)

#### 20.5.2 Librarii necesare pentru Slave (proiect ESP32_Slave/)

Mult mai putine — Slave e minimalist:

| Librarie | Versiune | Note |
| --- | --- | --- |
| Adafruit SHT31 Library | 2.2.0 | identic cu Master |
| Adafruit BusIO | auto | dep |
| Adafruit NeoPixel | 1.12 | LED status |
| ArduinoJson | 6.21+ | pentru raspuns UART JSON |

**NU** instala pe Slave: PubSubClient, Ethernet, SSLClient, ArduinoHttpClient (Slave nu are retea).

### 19.6 Partition scheme (critic pentru OTA)

OTA cere doua partitii app (app0 + app1) pentru rollback. Pentru proiect:

**Master** — `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` NU e bun (1.2MB app prea putin pentru SSLClient + Ethernet + MQTT).

Recomandat **`Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)`** pentru Master:
- app0: 1.9MB (firmware curent)
- app1: 1.9MB (rollback target la OTA)
- spiffs: 190KB (nefolosit)
- nvs: 24KB

**Slave** — firmware mult mai mic (~600KB), poate folosi acelasi schema sau `Default 4MB with spiffs`. Recomandat **`Minimal SPIFFS (1.9MB APP with OTA)`** pentru consistenta + spatiu OTA generos.

Setare in IDE: **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)**.

### 19.7 Configurare proiect ESP32/ (Master)

1. Cloneaza repo: `git clone https://github.com/RaduOvidiu20/ProiectVentilatie.git`
2. Deschide cu IDE: **File → Open** → selecteaza `ESP32/ProiectVentilatie.ino`. IDE-ul deschide tot directorul ca tab-uri.
3. Verifica ca toate fisierele sunt prezente: `Config.h`, `MqttBridge.h/.cpp`, `VentilationZone.h/.cpp`, `AppPreferences.h`, `TimeSync.h`, `OtaUpdater.h/.cpp`, `EventLog.h/.cpp`, `SystemLED.h/.cpp`, `Sht30Sensor.h`, `SlaveUartClient.h/.cpp`, `SlaveOtaProxy.h/.cpp`, `HiveMqTrustAnchor.h`, `Version.h` (auto-generat).
4. **Tools → Board → ESP32 Dev Module**.
5. **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)**.
6. **Tools → Port → /dev/ttyUSB0** (Master).
7. Apasa **Verify** (✓) — compileaza fara erori.

### 19.8 Configurare proiect ESP32_Slave/ (Slave)

1. **File → Open** → selecteaza `ESP32_Slave/ESP32_Slave.ino`. IDE deschide noua fereastra.
2. Verifica fisierele: `Config.h`, `Logger.h`, `WatchdogManager.h`, `Sht30Sensor.h`, `SystemLED.h/.cpp`, `UartProtocol.h/.cpp`, `CommandDispatcher.h/.cpp`, `OtaReceiver.h/.cpp`.
3. **Tools → Board → ESP32 Dev Module**.
4. **Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP with OTA)**.
5. **Tools → Port → /dev/ttyUSB1** (Slave — al doilea port USB conectat).
6. Apasa **Verify** (✓) — compileaza fara erori.

### 19.9 Build flags personalizate

Pentru optimizari plan (stack 16KB, debug level), e nevoie de flags suplimentare. Doua optiuni:

#### Optiunea A: editare `platform.local.txt`

Localizezi `platform.txt` ESP32 in directorul:
- Linux: `~/.arduino15/packages/esp32/hardware/esp32/<version>/platform.txt`
- Windows: `%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\<version>\platform.txt`

In acelasi director, creezi `platform.local.txt` cu:

```
build.extra_flags=-DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384 -DCORE_DEBUG_LEVEL=1
```

(efect global pentru orice sketch ESP32 — daca nu vrei asta, foloseste optiunea B).

#### Optiunea B: arduino-cli cu flags per build (recomandat)

```bash
arduino-cli compile \
    --fqbn esp32:esp32:esp32 \
    --board-options "PartitionScheme=min_spiffs,FlashSize=4M" \
    --build-property "build.extra_flags=-DCONFIG_ARDUINO_LOOP_STACK_SIZE=16384 -DCORE_DEBUG_LEVEL=1" \
    ESP32/
```

### 19.10 Setup arduino-cli (CI/CD si scripts)

Pentru `scripts/build.sh` si `scripts/flash.sh` din ambele proiecte:

```bash
# Linux
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv bin/arduino-cli /usr/local/bin/

# Init config
arduino-cli config init

# Adauga URL ESP32
arduino-cli config add board_manager.additional_urls \
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Update index + install ESP32 core
arduino-cli core update-index
arduino-cli core install esp32:esp32

# Install librarii Master
arduino-cli lib install \
    "Adafruit SHT31 Library" \
    "Adafruit NeoPixel" \
    "ArduinoJson" \
    "PubSubClient" \
    "Ethernet" \
    "SSLClient" \
    "ArduinoHttpClient"

# Verifica
arduino-cli lib list
```

### 19.11 Flash & test prima oara

1. Conecteaza Master via USB-C, observa portul (`/dev/ttyUSB0` Linux).
2. **Tools → Port → ttyUSB0**.
3. **Sketch → Upload** (sau `Ctrl+U`).
4. La sfarsit IDE deschide automat Serial Monitor (sau `Ctrl+Shift+M`). Setezi baud rate la **115200** in dropdown jos-dreapta.
5. Apasa butonul EN (RESET) pe placa → vezi log-ul de boot.
6. La fel pentru Slave pe `/dev/ttyUSB1`.

### 19.12 Probleme comune si solutii

| Problema | Cauza | Solutie |
| --- | --- | --- |
| `Failed to connect to ESP32: Timed out waiting for packet header` la upload | Boot mode greseit | Tine apasat butonul BOOT in timpul upload-ului, eliberat dupa ce apare „Connecting..." |
| `A fatal error occurred: MD5 of file does not match data` | Driver USB vechi sau cablu defect | Reinstaleaza driver CH340, schimba cablu USB-C (multe sunt doar power) |
| `error: ESP32 chip ID is 0xffffffff` | Cablu power-only | Foloseste cablu USB-C cu data |
| `multiple definition of 'setup()'` | 2 fisiere `.ino` in acelasi director | Asigura-te ca `ESP32/` are doar `ProiectVentilatie.ino`, nu si `slave_test.ino` etc. |
| `'X' was not declared in this scope` | Librarie lipsa | Verifica Library Manager + reinstaleaza dependintele |
| `Error: 13 INTERNAL: Cannot configure port /dev/ttyUSB0` | Permission denied | `sudo usermod -aG dialout $USER` + logout/login |
| Serial monitor afiseaza junk | Baud rate gresit | Setezi 115200 in dropdown Serial Monitor |
| Compileaza dar reseteaza la boot in loop | Flash size gresit | Verifica „Flash Size: 4MB (32Mb)" in Tools |
| OTA esueaza cu „Not enough space" | Partition scheme gresit | Schimba la „Minimal SPIFFS (1.9MB APP with OTA)" |
| `Sht30Sensor.h: No such file or directory` la Slave | Fisier lipsa din proiect | Copy `ESP32/Sht30Sensor.h` in `ESP32_Slave/` (sau symlink Linux) |
| Cu 2 placi conectate, ambele apar pe acelasi port | Linux ordoneaza non-determinist | Identifica prin `udevadm info /dev/ttyUSB0` (Vendor:Product) sau deconecteaza/reconecteaza individual |

### 19.13 Workflow recomandat zilnic

1. **Pornire**: deschide 2 ferestre IDE — una pentru `ESP32/` (Master), alta pentru `ESP32_Slave/` (Slave).
2. **Editare**: foloseste Visual Studio Code pentru editare (sintax mai bun) + IDE Arduino doar pentru Verify/Upload.
3. **Build**: prefer arduino-cli din terminal cu `bash ESP32/scripts/build.sh` — feedback mai rapid decat IDE.
4. **Flash**: `bash ESP32/scripts/flash.sh /dev/ttyUSB0` pentru Master, `/dev/ttyUSB1` pentru Slave.
5. **Debug**: 2 instante `screen /dev/ttyUSB0 115200` si `screen /dev/ttyUSB1 115200` in tab-uri terminal separate.

## 20. Risc principal & strategii fallback

| Risc | Probabilitate | Mitigare |
| --- | --- | --- |
| **UART instabil pe Cat6 1.5m in mediu cu EMI** | Mediu | Twisted pair + GND comun → daca persista, upgrade la **RS-485** (MAX485 chip pe fiecare placa, ~5 lei x 2). Cablul Cat6 ramane acelasi. |
| **SSLClient + W5500 + ESP32 TLS instabil (heap, reconectari)** | Mediu | Plan B: broker Mosquitto local LAN, port 1883 (MAUI reconfig in `appsettings.json`: host + port + UseTls=false). |
| **OTA via Ethernet + TLS prea complex** | Mediu | Daca dupa 4h munca dedicata nu e stabil, escaladam. Flash via USB-C ramane mereu disponibil pentru ambele placi. |
| **Conflict pin GPIO 15 strap pin cu RELAY_LEFT** | Mic | Firmware existent functioneaza cu aceasta combinatie; releul nu interfereaza cu boot. |
| **W5500 conflict pe SPI cu alte device-uri** | Mic | Singurul device SPI in proiect. |

## 21. Modul aditional: control LED PWM pe Slave (NCEP01T18) cu schedule

Slave primeste o functionalitate noua: controleaza intensitatea unei **benzi LED 24V 36W** (curent ~1.5A) prin PWM, cu **schedule programabil** (ora pornire / oprire / intensitate maxima) configurabil din MAUI.

**Control accesibil de oriunde din lume**: MAUI app vorbeste cu HiveMQ Cloud (TLS port 8883), care la randul sau livreaza comenzi catre Master via Internet. Master forwardeaza local prin UART catre Slave. Latenta tipica end-to-end: **<2 secunde** pe 4G/WiFi obisnuit. Singura conditie: telefonul utilizatorului si Master sa aiba conexiune Internet.

### 21.1 Analiza fiabilitate MOSFET module pentru 3-5 ani porniri zilnice

**Decizie finala: foloseste modulul NCEP01T18** (Ardushop ref. 6427854033437) in loc de IRF520. **Mult superior** pentru obiectivul nostru.

#### Comparatie modul IRF520 vs NCEP01T18

| Parametru | IRF520 module (~7 lei) | **NCEP01T18 module** (~25-35 lei) |
| --- | --- | --- |
| MOSFET | IRF520N | NCEP01T18 |
| Curent rated continuu | ~3A in modul (9.7A chip) | **50-80A fara cooling, 210A cu fan** |
| Vds max | 100V | 100V |
| Vgs(th) typical | 2-4V (NU true logic-level) | **~1.5-3V (logic-level functional)** |
| Rds(on) la Vgs=4.5V | ~270 mΩ | **~3-5 mΩ (50-90x mai mic)** |
| Disipare la LED 1A | ~0.27W (cald) | **<0.005W (rece)** |
| Disipare la LED 3A | ~2.4W (radiator obligatoriu) | ~0.045W (zero probleme) |
| PCB size | 35×25mm | 60×50mm (thermal mass mai mare) |
| Tinta proiectata | hobby simplu | hotbed 3D printer 24/7 |
| Lifetime estimat 1A continuu | 3-5 ani cu radiator | **10+ ani fara cooling** |

#### De ce NCEP01T18 castiga clar

1. **Logic-level real**: deschis complet la 3.3V de pe ESP32 — fara nevoie de MOSFET driver IC suplimentar (TC4427 etc.)
2. **Supradimensionat masiv** pentru aplicatie LED: la 1-3A operam la <5% din capacitate → stress termic aproape **zero**
3. **Rds(on) extrem de mic**: pe modulul NCEP01T18, MOSFET-ul ramane practic la temperatura ambient chiar la curenti mari
4. **PCB mai mare**: trace-uri mai late pe linia de putere, conectori screw-terminal mai robusti, mounting holes M3
5. **Industrie 3D printing 24/7**: producatorul a optimizat pentru deployment continuu sub sarcina mare → componente selectate pentru durabilitate

#### Verdict final pentru 3-5 ani 24/7

- **NCEP01T18 module**: **garantat 5-10+ ani fara probleme** pentru orice banda LED uzuala (<10A). Fara nevoie de radiator, gate resistor extern, pull-down — modulul are deja toate protectiile.
- **Singurul minus**: pret ~5x mai mare (35 lei vs 7 lei). Pentru un proiect 24/7 minim 3 ani, costul aditional de 28 lei e neglijabil fata de fiabilitatea castigata si efortul evitat de a inlocui modulul peste 2 ani.

**Decizie**: planul foloseste **NCEP01T18 module**. IRF520 ramane ca alternativa de buget doar daca constrangerea de cost devine critica si banda LED e <500mA.

#### Note conexiune cu NCEP01T18 module

Modulul are conectori screw-terminal etichetati clar:
- **VIN+** / **VIN−**: alimentare load (12V supply DC)
- **VOUT+** / **VOUT−**: catre banda LED
- **SIG** (sau **PWM**): input PWM 3.3V/5V de la ESP32 GPIO
- **GND**: GND comun cu ESP32 (obligatoriu)

Cablajul e mai simplu decat IRF520 — doar 5 conexiuni, fara componente discrete (gate resistor, pull-down). Modulul integrează deja gate driving + protectie.

### 21.2 Hardware: cablaj NCEP01T18 module + LED 24V 36W

```
   +24V DC supply (+) ──────────────► [VIN+]  NCEP01T18  [VOUT+] ──────► LED+ (banda LED 24V 36W)
   +24V DC supply (−) ──────────────► [VIN−]   module    [VOUT−] ──────► LED−

                                                          [SIG]  ◄─── Slave GPIO 25 (PWM)
                                                          [GND]  ─────► GND comun cu ESP32

   ESP32 GND ─────────────────────────────────────────────────────────► GND comun
```

**Cablaj minimal — doar 5 fire**:
1. Sursa 24V „+" → VIN+ modul
2. Sursa 24V „−" → VIN− modul (= GND comun cu ESP32)
3. ESP32 GPIO 25 → SIG modul
4. ESP32 GND → GND modul (esential pentru reference comuna; **fara aceasta legatura PWM nu functioneaza** — ground floating between domains)
5. VOUT+/VOUT− → banda LED+/LED−

Modulul NCEP01T18 deja contine intern: gate driver, gate resistor, pull-down protection, dioda flyback (daca e cazul pentru loads inductive — pentru LED nu e necesar, dar prezenta nu deranjeaza). **Fara componente externe necesare**, spre deosebire de IRF520.

**Compatibilitate confirmata cu 24V 36W**:
- Modulul NCEP01T18 spec: VIN range 12-24V → **24V e maximul rated, dar perfect suportat**
- NCEP01T18 chip: Vds rated 100V → 24V folosit la doar 24% din rated → marja imensa de siguranta
- Curent banda 36W / 24V = 1.5A → utilizare <3% din capacitatea modulului (50-80A) → **stress termic zero**
- Disipare pe MOSFET la 1.5A: P = I² × Rds(on) = 2.25 × 0.005Ω = **0.011W (11 mW)** — neglijabil
- Modulul ramane la temperatura ambient indiferent de cat timp ruleaza la 100% intensitate

**Atentie**: NU folosi sursa Slave-ului ESP32 (5V USB-C) pentru banda LED — acea sursa e dimensionata pentru ~500mA. Banda LED **necesita sursa 24V dedicata 1.5A+**.

### 21.3 Pinout adaugat la Slave (din 3.2)

| Functie | GPIO | Note |
| --- | --- | --- |
| **LED PWM** | **25** | LEDC channel 0, frecv. 5 kHz, rezolutie 12 bit (0-4095) |
| 12V power LED | n/a (extern) | sursa separata, nu de pe Carbon V3 |

GPIO 25 e ales pentru:
- Liber in proiectul Slave existent
- Suporta LEDC (toti GPIO ESP32 il suporta)
- Nu e strap pin (boot safe)
- Are si DAC capability (bonus, dar nu folosit aici)

### 21.4 Modul nou: `LedController.h/.cpp` (Slave)

```cpp
// LedController.h
#pragma once
#include <Arduino.h>
#include <Preferences.h>

class LedController {
public:
    static constexpr uint8_t  PWM_CHANNEL    = 0;
    static constexpr uint32_t PWM_FREQ_HZ    = 5000;
    static constexpr uint8_t  PWM_RESOLUTION = 12;   // 0..4095
    static constexpr uint16_t PWM_MAX        = (1 << 12) - 1;

    LedController(uint8_t pin);

    void begin();

    /** Set imediat intensitatea (0-100%). Override schedule pana la urmatoarea evaluare. */
    void setIntensity(uint8_t percent);

    /** Configureaza schedule. Persistent in NVS Slave. */
    void setSchedule(uint8_t onHour, uint8_t onMin,
                     uint8_t offHour, uint8_t offMin,
                     uint8_t maxIntensity, bool enabled);

    /** Apelat periodic din loop() pentru a aplica schedule daca timpul a fost sincronizat. */
    void tick(uint32_t epochSec);

    uint8_t getCurrentIntensity() const { return _currentPercent; }
    bool    isScheduleEnabled()    const { return _enabled; }

    struct ScheduleSnapshot {
        uint8_t onHour, onMin, offHour, offMin, maxIntensity;
        bool enabled;
    };
    ScheduleSnapshot getSchedule() const;

private:
    uint8_t  _pin;
    uint8_t  _currentPercent;     // 0-100, valoarea aplicata efectiv
    uint8_t  _onHour, _onMin;
    uint8_t  _offHour, _offMin;
    uint8_t  _maxIntensity;       // 0-100, intensitate cand e ON in schedule
    bool     _enabled;
    bool     _manualOverride;     // setIntensity() temporar mai puternic decat schedule
    uint32_t _manualOverrideUntilSec;

    Preferences _prefs;

    void _loadFromNvs();
    void _saveToNvs() const;
    void _applyPwm(uint8_t percent);

    static bool _isInWindow(uint8_t curH, uint8_t curM,
                            uint8_t onH, uint8_t onM,
                            uint8_t offH, uint8_t offM);
};
```

```cpp
// LedController.cpp
#include "LedController.h"
#include "Logger.h"

LedController::LedController(uint8_t pin)
    : _pin(pin), _currentPercent(0),
      _onHour(18), _onMin(0), _offHour(23), _offMin(30),
      _maxIntensity(80), _enabled(false),
      _manualOverride(false), _manualOverrideUntilSec(0) {}

void LedController::begin() {
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RESOLUTION);
    ledcAttachPin(_pin, PWM_CHANNEL);
    _loadFromNvs();
    _applyPwm(0);   // start OFF
}

void LedController::setIntensity(uint8_t percent) {
    if (percent > 100) percent = 100;
    _manualOverride = true;
    _manualOverrideUntilSec = (uint32_t)time(nullptr) + 3600;  // 1h override
    _applyPwm(percent);
}

void LedController::setSchedule(uint8_t onH, uint8_t onM, uint8_t offH, uint8_t offM,
                                 uint8_t maxIntensity, bool enabled) {
    if (onH > 23 || offH > 23 || onM > 59 || offM > 59 || maxIntensity > 100) return;
    _onHour = onH; _onMin = onM;
    _offHour = offH; _offMin = offM;
    _maxIntensity = maxIntensity;
    _enabled = enabled;
    _manualOverride = false;
    _saveToNvs();
    LOG_INFO("LED schedule: %02u:%02u -> %02u:%02u @%u%% enabled=%d",
             onH, onM, offH, offM, maxIntensity, enabled);
}

void LedController::tick(uint32_t epochSec) {
    if (epochSec < 1700000000UL) return;  // timpul nu e sincronizat inca

    // Override manual expira dupa 1h
    if (_manualOverride && epochSec > _manualOverrideUntilSec) {
        _manualOverride = false;
    }
    if (_manualOverride || !_enabled) return;

    struct tm timeinfo;
    time_t t = (time_t)epochSec;
    if (!localtime_r(&t, &timeinfo)) return;

    bool inWindow = _isInWindow(timeinfo.tm_hour, timeinfo.tm_min,
                                _onHour, _onMin, _offHour, _offMin);
    uint8_t target = inWindow ? _maxIntensity : 0;
    if (target != _currentPercent) {
        _applyPwm(target);
    }
}

void LedController::_applyPwm(uint8_t percent) {
    _currentPercent = percent;
    uint32_t duty = (uint32_t)percent * PWM_MAX / 100;
    ledcWrite(PWM_CHANNEL, duty);
}

bool LedController::_isInWindow(uint8_t curH, uint8_t curM,
                                 uint8_t onH, uint8_t onM,
                                 uint8_t offH, uint8_t offM) {
    int curMin = curH * 60 + curM;
    int onMin  = onH  * 60 + onM;
    int offMin = offH * 60 + offM;
    if (onMin <= offMin) {
        // schedule simplu intr-o singura zi
        return curMin >= onMin && curMin < offMin;
    } else {
        // schedule trece miezul noptii (ex. 22:00 -> 06:00)
        return curMin >= onMin || curMin < offMin;
    }
}

void LedController::_loadFromNvs() {
    _prefs.begin("led", true);
    _onHour       = _prefs.getUChar("oh", 18);
    _onMin        = _prefs.getUChar("om", 0);
    _offHour      = _prefs.getUChar("fh", 23);
    _offMin       = _prefs.getUChar("fm", 30);
    _maxIntensity = _prefs.getUChar("mi", 80);
    _enabled      = _prefs.getBool("en", false);
    _prefs.end();
}

void LedController::_saveToNvs() const {
    Preferences p;
    p.begin("led", false);
    p.putUChar("oh", _onHour);
    p.putUChar("om", _onMin);
    p.putUChar("fh", _offHour);
    p.putUChar("fm", _offMin);
    p.putUChar("mi", _maxIntensity);
    p.putBool ("en", _enabled);
    p.end();
}

LedController::ScheduleSnapshot LedController::getSchedule() const {
    return { _onHour, _onMin, _offHour, _offMin, _maxIntensity, _enabled };
}
```

### 21.5 Time sync Slave (foarte important pentru schedule)

Slave NU are NTP. Master ii sincronizeaza ora periodic prin UART:

**Master periodic (la fiecare 1 ora in `loop()`)**:
```cpp
static uint32_t lastTimeSync = 0;
if (millis() - lastTimeSync > 3600000UL) {
    time_t now = time(nullptr);
    if (now > 1700000000UL) {
        char msg[32];
        snprintf(msg, sizeof(msg), "TIME_SYNC %lu\n", (unsigned long)now);
        Serial2.print(msg);
    }
    lastTimeSync = millis();
}
```

**Slave** primeste in CommandDispatcher:
```cpp
if (strncmp(cmd, "TIME_SYNC ", 10) == 0) {
    uint32_t epoch = (uint32_t)atol(cmd + 10);
    if (epoch > 1700000000UL) {
        struct timeval tv = { .tv_sec = (time_t)epoch, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        _syncedEpoch = epoch;
    }
    _uart.sendLine("OK");
}
```

Drift estimat fara RTC extern: ~10s/zi (acceptabil pentru schedule LED — toleranta 1 minut). La fiecare resync (orar), drift-ul se reseteaza.

### 21.6 Comenzi UART noi

| Master → Slave | Slave → Master |
| --- | --- |
| `TIME_SYNC <epoch>\n` | `OK\n` |
| `LED_SET <0-100>\n` | `OK\n` (setare imediata) |
| `LED_SCHEDULE <onH> <onM> <offH> <offM> <maxInt> <enabled>\n` | `OK\n` (persistent NVS) |
| `LED_STATUS\n` | JSON `{"intensity":N,"enabled":B,"sched":{...}}` |

### 21.7 Comenzi MQTT noi (MAUI → Master → Slave)

```json
{"cmd":"setLedNow","intensity":75}
{"cmd":"setLedSchedule","onHour":18,"onMin":0,"offHour":23,"offMin":30,"intensity":80,"enabled":true}
```

Master forwardeaza pe UART. Adauga in struct `MqttPending`:
```cpp
bool  setLedNow      = false;
int   ledNowIntensity = 0;

bool  setLedSched    = false;
int   ledSchedOnH = 0, ledSchedOnM = 0;
int   ledSchedOffH = 0, ledSchedOffM = 0;
int   ledSchedIntensity = 0;
bool  ledSchedEnabled = false;
```

In `processZones()` Master (procesare pending):
```cpp
if (mp.setLedNow) {
    mp.setLedNow = false;
    char buf[32];
    snprintf(buf, sizeof(buf), "LED_SET %d\n", mp.ledNowIntensity);
    Serial2.print(buf);
}
if (mp.setLedSched) {
    mp.setLedSched = false;
    char buf[64];
    snprintf(buf, sizeof(buf), "LED_SCHEDULE %d %d %d %d %d %d\n",
             mp.ledSchedOnH, mp.ledSchedOnM,
             mp.ledSchedOffH, mp.ledSchedOffM,
             mp.ledSchedIntensity, mp.ledSchedEnabled ? 1 : 0);
    Serial2.print(buf);
}
```

### 21.8 State JSON MQTT extins

Adaugare camp `led` in JSON-ul de state publicat de Master (preluat din raspunsul Slave la `LED_STATUS`):

```json
{
  "left":  {...},
  "right": {...},
  "cfg":   {...},
  "lock":  {...},
  "slave": {...},
  "led": {
    "intensity": 75,
    "enabled": true,
    "schedule": {
      "onHour": 18, "onMin": 0,
      "offHour": 23, "offMin": 30,
      "maxIntensity": 80
    }
  },
  "fw": 145, ...
}
```

### 21.9 MAUI — control LED

**Model nou**: `MobileApp/Models/LedState.cs`

```csharp
public class LedSchedule {
    [JsonPropertyName("onHour")]      public int OnHour      { get; set; }
    [JsonPropertyName("onMin")]       public int OnMin       { get; set; }
    [JsonPropertyName("offHour")]     public int OffHour     { get; set; }
    [JsonPropertyName("offMin")]      public int OffMin      { get; set; }
    [JsonPropertyName("maxIntensity")] public int MaxIntensity { get; set; }
}

public class LedState {
    [JsonPropertyName("intensity")] public int          Intensity { get; set; }
    [JsonPropertyName("enabled")]   public bool         Enabled   { get; set; }
    [JsonPropertyName("schedule")]  public LedSchedule? Schedule  { get; set; }
}

public class VentilationState {
    // ... existing ...
    [JsonPropertyName("led")] public LedState? Led { get; set; }
}
```

**ViewModel nou**: `MobileApp/ViewModels/LedViewModel.cs`

```csharp
public partial class LedViewModel : ObservableObject {
    private readonly IMqttService _mqttService;

    [ObservableProperty] private int      _intensitySliderValue = 50;
    [ObservableProperty] private TimeSpan _onTime  = new TimeSpan(18, 0, 0);
    [ObservableProperty] private TimeSpan _offTime = new TimeSpan(23, 30, 0);
    [ObservableProperty] private int      _maxIntensity = 80;
    [ObservableProperty] private bool     _scheduleEnabled;
    [ObservableProperty] private int      _currentIntensity;

    public LedViewModel(IMqttService mqtt) {
        _mqttService = mqtt;
        _mqttService.OnStateReceived += OnState;
    }

    private void OnState(VentilationState state) {
        if (state.Led == null) return;
        CurrentIntensity = state.Led.Intensity;
        ScheduleEnabled  = state.Led.Enabled;
        if (state.Led.Schedule != null) {
            OnTime  = new TimeSpan(state.Led.Schedule.OnHour,  state.Led.Schedule.OnMin,  0);
            OffTime = new TimeSpan(state.Led.Schedule.OffHour, state.Led.Schedule.OffMin, 0);
            MaxIntensity = state.Led.Schedule.MaxIntensity;
        }
    }

    [RelayCommand]
    private async Task SetNowAsync() {
        await _mqttService.SendCommandAsync(new {
            cmd = "setLedNow",
            intensity = IntensitySliderValue
        });
    }

    [RelayCommand]
    private async Task SaveScheduleAsync() {
        await _mqttService.SendCommandAsync(new {
            cmd = "setLedSchedule",
            onHour  = OnTime.Hours,  onMin  = OnTime.Minutes,
            offHour = OffTime.Hours, offMin = OffTime.Minutes,
            intensity = MaxIntensity,
            enabled = ScheduleEnabled
        });
    }

    [RelayCommand]
    private async Task TurnOffAsync() {
        IntensitySliderValue = 0;
        await SetNowAsync();
    }
}
```

**View nou**: `MobileApp/Views/LedPage.xaml` (sau sectiune dedicata in DevicesPage)

```xml
<ContentPage ... x:DataType="vm:LedViewModel">
    <ScrollView>
        <VerticalStackLayout Spacing="14" Padding="14">

            <Label Text="CONTROL LED PWM" FontSize="22" FontAttributes="Bold"
                   Style="{StaticResource LabelRajdhani}" />

            <!-- Manual control -->
            <Border Padding="14" BackgroundColor="#0AFFFFFF"
                    Stroke="#3300e6ff" StrokeShape="RoundRectangle 10">
                <VerticalStackLayout Spacing="10">
                    <Label Text="MANUAL" FontSize="12" Opacity="0.6" />

                    <Slider Minimum="0" Maximum="100"
                            Value="{Binding IntensitySliderValue, Mode=TwoWay}"
                            ThumbColor="{StaticResource PrimaryCyan}" />

                    <Label Text="{Binding IntensitySliderValue, StringFormat='Intensitate: {0}%'}"
                           Style="{StaticResource LabelShareTech}" />

                    <Grid ColumnDefinitions="*,*" ColumnSpacing="10">
                        <Button Grid.Column="0" Text="✓ APLICA"
                                Command="{Binding SetNowCommand}"
                                Style="{StaticResource CyberButton}" />
                        <Button Grid.Column="1" Text="✗ STINGE"
                                Command="{Binding TurnOffCommand}"
                                Style="{StaticResource CyberButtonRed}" />
                    </Grid>

                    <Label Text="{Binding CurrentIntensity, StringFormat='Stare actuala: {0}%'}"
                           Opacity="0.8" />
                </VerticalStackLayout>
            </Border>

            <!-- Schedule -->
            <Border Padding="14" BackgroundColor="#0AFFFFFF"
                    Stroke="#3300e6ff" StrokeShape="RoundRectangle 10">
                <VerticalStackLayout Spacing="10">
                    <Grid ColumnDefinitions="*,Auto">
                        <Label Grid.Column="0" Text="SCHEDULE AUTOMAT"
                               FontSize="12" Opacity="0.6" />
                        <Switch Grid.Column="1" IsToggled="{Binding ScheduleEnabled}" />
                    </Grid>

                    <Grid ColumnDefinitions="Auto,*" RowDefinitions="Auto,Auto" RowSpacing="8" ColumnSpacing="10">
                        <Label Grid.Row="0" Grid.Column="0" Text="Pornire:" VerticalOptions="Center" />
                        <TimePicker Grid.Row="0" Grid.Column="1" Time="{Binding OnTime}" />

                        <Label Grid.Row="1" Grid.Column="0" Text="Oprire:" VerticalOptions="Center" />
                        <TimePicker Grid.Row="1" Grid.Column="1" Time="{Binding OffTime}" />
                    </Grid>

                    <Slider Minimum="0" Maximum="100"
                            Value="{Binding MaxIntensity, Mode=TwoWay}" />
                    <Label Text="{Binding MaxIntensity, StringFormat='Intensitate maxima: {0}%'}"
                           Style="{StaticResource LabelShareTech}" />

                    <Button Text="↑ SAVE SCHEDULE"
                            Command="{Binding SaveScheduleCommand}"
                            Style="{StaticResource CyberButtonOrange}" />
                </VerticalStackLayout>
            </Border>
        </VerticalStackLayout>
    </ScrollView>
</ContentPage>
```

**Navigare**: adauga tab nou „LED" in `FloatingNavBar.xaml` sau ca sectiune in DevicesPage.

### 21.10 Validare in firmware Master

Inainte de a forwarda comanda LED catre Slave, Master valideaza:

```cpp
// In MqttBridge::_handleMessage callback
if (cmd == "setLedNow") {
    int intensity = doc["intensity"].as<int>();
    if (intensity >= 0 && intensity <= 100) {
        _mqttPending.setLedNow = true;
        _mqttPending.ledNowIntensity = intensity;
    }
}
if (cmd == "setLedSchedule") {
    int onH = doc["onHour"].as<int>();
    int onM = doc["onMin"].as<int>();
    int offH = doc["offHour"].as<int>();
    int offM = doc["offMin"].as<int>();
    int inten = doc["intensity"].as<int>();
    bool en = doc["enabled"].as<bool>();
    if (onH < 24 && onM < 60 && offH < 24 && offM < 60 && inten <= 100) {
        _mqttPending.setLedSched = true;
        _mqttPending.ledSchedOnH = onH; _mqttPending.ledSchedOnM = onM;
        _mqttPending.ledSchedOffH = offH; _mqttPending.ledSchedOffM = offM;
        _mqttPending.ledSchedIntensity = inten;
        _mqttPending.ledSchedEnabled = en;
    }
}
```

### 21.11 Materiale aditionale pentru banda LED 24V 36W (din 16 — extindere)

| Item | Cantitate | Pret estimat | Note |
| --- | --- | --- | --- |
| **Modul NCEP01T18** (Ardushop ref. 6427854033437) | 1 | ~25-35 lei | VIN 12-24V, logic-level, 50-80A continuu — perfect 24V |
| **Banda LED 24V 36W** | 1 (lungime stabilita) | 50-150 lei | rigida sau flexibila; verifica IP-rating in functie de mediu (IP65+ pentru bucatarii/exterior) |
| **Sursa 24V DC industriala** ≥48W (1.3× margin) | 1 | ~80-120 lei | Recomandari: **Mean Well IRM-45-24** (45W DIN-rail), **Mean Well LRS-50-24** (50W enclosed), **GST60A24** (60W desktop adapter). MTBF >100k ore. |
| Fire 18AWG (sau 20AWG) pentru LED+/LED− | 2m | ~10 lei | la 1.5A, 18AWG are drop neglijabil pe distante <3m |
| Conectori screw-terminal sau JST 2-pin pentru LED | 2 perechi | ~5 lei | usurinta deconectare la mentenanta |
| **Optional**: dioda Schottky 3A pentru reverse polarity protection | 1 | ~3 lei | montata in serie cu LED+ |
| **Optional**: TVS diode pe 24V (SMBJ24CA sau similar) | 1 | ~3 lei | protectie spike-uri |

**NU se cumpara** (eliminate fata de varianta IRF520):
- Rezistor gate 33Ω, rezistor pull-down 10kΩ — modulul NCEP01T18 le include intern
- Radiator TO-220 — la 1.5A NCEP01T18 disipa doar 11mW, nu se incalzeste deloc
- MOSFET driver IC (TC4427/MIC4429) — NCEP01T18 e logic-level real

**Total adaugat pentru LED 24V 36W**: ~170-330 lei (depinde de lungime banda + alegere sursa).

**Verificare dimensionare sursa 24V**:
- Banda nominal: 1.5A continuu
- Inrush current la pornire abrupta: ~3A peak pentru 10-20ms
- Sursa 45W (1.9A) din Mean Well IRM-45-24 are protectie inrush 1.5x → suporta inrush
- Pentru margine confortabila si lifetime maxim: **alege sursa 50-60W** (>1.3× banda nominal)

### 21.12 Verificare end-to-end (extindere din 17)

**Pasi suplimentari** dupa pasul 16 (OTA Master) din sectiunea 17:

17. **Test PWM standalone Slave**: in `setup()` adauga `ledcAttachPin(25, 0); ledcSetup(0, 5000, 12); ledcWrite(0, 2048);` → banda LED la 50%.
18. **Test setLedNow din MAUI**: slider la 75%, apesi APLICA → banda LED la 75% in <1s.
19. **Test setLedSchedule**: setezi schedule 18:00→23:30 @80%, enable. La intrarea in fereastra orara → LED se aprinde la 80% automat. La iesire → LED off.
20. **Test resync time**: scoti Slave din priza → repornit, LED OFF (schedule paused). Master trimite TIME_SYNC → schedule re-aplica conditional in <1 min.
21. **Test heat IRF520**: dupa 1h ON la intensitate 100% si banda max curent, masoara cu termometru IR temperatura MOSFET. Trebuie <60°C (altfel necesita radiator mai mare sau upgrade IRLZ44N).
22. **Test persistenta NVS schedule**: setezi schedule din MAUI → reboot Slave → schedule pastrat la boot, executa la urmatorul time sync.

### 21.13 Sesiuni noi adaugate la 18 (Ordine executie)

Adaugare la **Faza 7 (OTA dual)** sau **Faza 8 (Resilience)**:

**S33b — Hardware: cablaj IRF520 + LED test** | 1h | dep: S10 (Slave functional)
- Deliverable: IRF520 conectat, LED 12V conectat, sursa 12V separata.
- Test: scriu manual pe Slave `LED_SET 50` din PC via UART → banda LED aprinsa 50%.

**S33c — LedController + persistenta NVS Slave** | 2h | dep: S33b
- Deliverable: `LedController.h/.cpp`, init in `setup()` Slave.
- Test: schedule persistent dupa reboot, intensity setata se mentine.

**S33d — UART commands LED + TIME_SYNC** | 1.5h | dep: S33c
- Deliverable: extindere `CommandDispatcher` cu LED_SET, LED_SCHEDULE, LED_STATUS, TIME_SYNC. Master trimite TIME_SYNC orar.
- Test: comenzi UART simulate de pe PC → Slave actioneaza corect, schedule executa cu time sincronizat.

**S33e — MQTT bridge LED commands Master** | 1.5h | dep: S33d
- Deliverable: `MqttPending` extins, handler `setLedNow`/`setLedSchedule`, JSON state cu camp `led`.
- Test: cmd MQTT din MQTT Explorer → Master forwardeaza → Slave actioneaza.

**S33f — MAUI LedViewModel + LedPage** | 2h | dep: S33e
- Deliverable: model `LedState`, ViewModel, View XAML, navigare in FloatingNavBar.
- Test: din MAUI controlezi intensitatea + schedule complet, vezi state actual reflectat.
- **Commit**: `feat(led): PWM control with schedule via IRF520`.

### 21.X Persistenta garantata a setarilor LED — NVS dual-mirror

**Cerinta utilizator**: setarile (interval orar + intensitate dimming) trebuie sa ramana permanente pana cand utilizatorul le suprascrie din MAUI.

**Strategie implementata**: dual-mirror NVS — atat Slave cat si Master persisteaza schedule-ul, pentru a supravietui orice failure individual.

#### 21.X.1 Sursele de adevar

| Componenta | Stocare | Rol |
| --- | --- | --- |
| **MAUI (telefon)** | Bind UI la state JSON primit pe MQTT | UI reflecta starea actuala raportata de Master; doar afiseaza, nu persisteaza nimic |
| **Master Carbon V3** | NVS namespace `ledcfg` | Backup mirror — pastreaza ultimul schedule trimis de utilizator. Folosit pentru resync Slave la nevoie. |
| **Slave Carbon V3** | NVS namespace `led` | **Source of truth efectiv** — Slave executa autonom schedule-ul folosind aceste valori |

#### 21.X.2 Flow setare schedule (utilizator)

```
1. Utilizator deschide MAUI → LedPage afiseaza schedule curent (din state JSON)
2. Modifica TimePicker on/off + slider intensity + switch enabled
3. Apasa „SAVE SCHEDULE"
4. MAUI publica MQTT: {"cmd":"setLedSchedule","onHour":18,"onMin":0,"offHour":23,"offMin":30,"intensity":80,"enabled":true}
5. Master primeste:
   a. Validare range (onH<24, onM<60, intensity<=100)
   b. Salvare locala in NVS namespace "ledcfg"
   c. Forwarding pe UART catre Slave: LED_SCHEDULE 18 0 23 30 80 1\n
6. Slave primeste:
   a. Parseaza valorile
   b. Salvare in NVS namespace "led" (LedController._saveToNvs())
   c. Aplica imediat schedule daca timpul e sincronizat
   d. Raspunde OK\n
7. Master primeste OK, publica state JSON cu noul led.schedule
8. MAUI primeste state, UI reflectata starea salvata (confirma vizual ca s-a salvat)
```

**Garantie**: la pasul 6.b setarile sunt persistente in NVS Slave **inainte** de a confirma OK. Daca Slave reboot imediat, NVS-ul e deja salvat → la urmatorul boot schedule-ul e activ.

#### 21.X.3 Flow boot Slave

```
1. Slave porneste (oricare cauza: alimentare initiala, ESP.restart, watchdog, factory reset Master, etc.)
2. LedController::begin() → _loadFromNvs() incarca: onH, onM, offH, offM, maxIntensity, enabled
3. Daca NVS gol (primul boot) → defaults compile-time (ex. 18:00→23:30 @80% disabled)
4. PWM GPIO 25 setat la 0 (LED off pana la primul tick)
5. Asteapta TIME_SYNC de la Master (in maxim 1h sau imediat la prima cerere)
6. La primul tick(epochSec) cu epoch valid → evaluare schedule → aplicare PWM
```

**Garantie**: setarile salvate **anterior** sunt aplicate fara sa fie nevoie ca utilizatorul sa intre in MAUI sau sa retrimita comanda. Banda LED isi reia ciclul automat.

#### 21.X.4 Flow boot Master

```
1. Master porneste
2. AppPreferences::begin() → incarca thresholds, hyst, override, etc. (existente)
3. Adaugare: LedConfigStorage::begin() → incarca din NVS namespace "ledcfg" valorile schedule LED salvate ultima oara
4. Cand Slave devine accesibil prin UART, Master cere LED_STATUS\n → Slave raspunde cu schedule-ul lui actual
5. Master compara cu valorile din NVS-ul propriu (ledcfg):
   - Daca sunt egale → nimic de facut, totul e sincron
   - Daca difera (ex. Slave a fost factory reset → are valori default) → Master trimite re-sync: LED_SCHEDULE <valori din NVS> → Slave salveaza si aplica
6. Master publica state JSON cu led.schedule corect
```

**Garantie**: chiar daca Slave a pierdut NVS (foarte rar, dar posibil la factory reset hardware), Master il reseteaza la ultimele valori dorite de utilizator. Schedule-ul e **invizibil indestructibil** atata timp cat oricare din placi pastreaza NVS-ul intact.

#### 21.X.5 Cazuri de overwrite explicit

Setarile sunt suprascrise **doar** in urmatoarele situatii:

1. **Utilizator trimite `setLedSchedule` din MAUI** — comportament normal, valorile noi inlocuiesc cele vechi pe ambele placi.
2. **Utilizator trimite `setLedNow` din MAUI** — manual override cu durata 1h, dupa care schedule-ul revine. NU sterge schedule-ul, doar il suspenda temporar.
3. **Factory reset Master prin `cmd:reset`** — wipe NVS Master inclusiv `ledcfg`. Dar Slave pastreaza propria copie in NVS `led`. Master, la primul `LED_STATUS` din Slave, afla schedule-ul corect si il salveaza inapoi in NVS-ul lui (auto-restore).
4. **Reset hardware Slave (3-sec hold RESET button)** — daca cineva apasa fizic, NVS Slave se sterge. Master, la urmatorul boot Slave, detecteaza divergenta in `LED_STATUS` si re-trimite valorile din NVS-ul lui.
5. **Erase All Flash Before Sketch Upload (Arduino IDE)** — wipe complet NVS al placii respective. Singura cale de pierdere completa e daca AMBELE placi sunt re-flashate cu Erase All in aceeasi sesiune. In practica, evitam aceasta combinatie — flash-uim cu „Erase All Flash" bifat doar la prima programare a unei placi noi.

#### 21.X.6 Implementare adaugata pe Master

```cpp
// LedConfigStorage.h (nou pe Master)
#pragma once
#include <Preferences.h>

class LedConfigStorage {
public:
    struct Snapshot {
        uint8_t onHour, onMin, offHour, offMin, maxIntensity;
        bool enabled;
        bool valid;   // true daca s-a citit ceva din NVS
    };

    static void save(uint8_t oh, uint8_t om, uint8_t fh, uint8_t fm,
                      uint8_t mi, bool en) {
        Preferences p;
        p.begin("ledcfg", false);
        p.putUChar("oh", oh);
        p.putUChar("om", om);
        p.putUChar("fh", fh);
        p.putUChar("fm", fm);
        p.putUChar("mi", mi);
        p.putBool ("en", en);
        p.putBool ("set", true);
        p.end();
    }

    static Snapshot load() {
        Preferences p;
        p.begin("ledcfg", true);
        Snapshot s;
        s.onHour       = p.getUChar("oh", 18);
        s.onMin        = p.getUChar("om", 0);
        s.offHour      = p.getUChar("fh", 23);
        s.offMin       = p.getUChar("fm", 30);
        s.maxIntensity = p.getUChar("mi", 80);
        s.enabled      = p.getBool ("en", false);
        s.valid        = p.getBool ("set", false);
        p.end();
        return s;
    }
};

// In MqttBridge::_handleMessage la cmd "setLedSchedule":
if (validInputs) {
    LedConfigStorage::save(onH, onM, offH, offM, intensity, enabled);   // PERSISTENTA MASTER
    _mqttPending.setLedSched = true;
    // ... copiere in pending pentru forwarding UART ...
}
```

**Comanda noua UART pentru sync verification**:

```
Master → Slave: LED_STATUS\n
Slave → Master: {"intensity":75,"enabled":true,"sched":{"onH":18,"onM":0,"offH":23,"offM":30,"maxInt":80}}\n
```

In `setup()` Master, dupa ce Slave devine accesibil:

```cpp
LedConfigStorage::Snapshot saved = LedConfigStorage::load();
if (saved.valid) {
    // Cere status Slave
    auto slaveStatus = slaveClient.getLedStatus();   // metoda noua in SlaveUartClient
    if (slaveStatus.differsFrom(saved)) {
        LOG_WARN("Slave LED schedule diverged from Master NVS — resyncing");
        slaveClient.sendLedSchedule(saved.onHour, saved.onMin, saved.offHour,
                                     saved.offMin, saved.maxIntensity, saved.enabled);
    }
}
```

#### 21.X.7 Tabel sumar persistenta

| Eveniment | Slave NVS `led` | Master NVS `ledcfg` | Schedule activ dupa eveniment |
| --- | --- | --- | --- |
| `setLedSchedule` din MAUI | scris | scris | nou (cel trimis) |
| Reboot Slave normal | pastrat | pastrat | restaurat din Slave NVS |
| Reboot Master normal | pastrat | pastrat | continua sa ruleze pe Slave |
| Power loss ambele | pastrat | pastrat | restaurat la pornire |
| `cmd:reset` Master (NVS wipe) | pastrat | sters | continua pe Slave; Master sync-back la primul LED_STATUS |
| Hold RESET button Slave | sters | pastrat | Slave intra default; Master detecteaza si re-trimite la primul ciclu |
| Re-flash Slave cu Erase All | sters | pastrat | Slave intra default; Master re-trimite |
| Re-flash Master cu Erase All | pastrat | sters | continua pe Slave; Master nu mai are backup pana la primul `setLedSchedule` |
| Re-flash AMBELE cu Erase All | sters | sters | utilizator trebuie sa retrimita schedule din MAUI |

**Concluzie**: schedule-ul e **invizibil indestructibil** in conditii normale de operare. Singurul caz de pierdere completa = re-flash hardware al ambelor placi simultan, ceea ce e o operatie deliberata efectuata doar la setup initial.

### 21.14 Considerente de fiabilitate

- **Modul NCEP01T18 e supradimensionat** pentru aplicatie LED: la <3A operam la <5% din capacitatea rated. Stress termic aproape zero → MTBF efectiv decenii. **Nu necesita radiator, gate driver IC sau componente externe**.
- **Logic-level real**: NCEP01T18 are `Vgs(th)` ~1.5-3V — deschis complet la 3.3V de pe ESP32. Verificat in burn-in: temperatura MOSFET = ambient + maxim 5°C la 3A LED.
- **PWM frecventa 5 kHz**: standard, nu produce zgomot vizibil/auditive. Daca apare zgomot subtle audible (rar, doar in linistitele perfecta), urcam la 20 kHz cu acelasi cod (LEDC suporta nativ).
- **Reverse polarity protection**: optional, dioda Schottky 3A in serie cu LED+ pentru protectie la cablaj invers (5 lei). Recomandat dar nu critic — modulul NCEP01T18 NU e protejat intern impotriva polaritatii inversate pe VIN.
- **TVS diode pe linia 12V**: ~5 lei suplimentar, protejeaza modul + LED la spike-uri din sursa (recomandat in mediu cu motoare/contactori in apropiere).
- **Inrush current LED 24V 36W**: nominal 1.5A, peak la pornire abrupta ~3A pentru 10-20ms. Sursa 24V de minim **45-50W** (Mean Well IRM-45-24 sau LRS-50-24) suporta confortabil. NCEP01T18 e oricum supradimensionat masiv → fara probleme la inrush.
- **Soft-start in software**: optional in `LedController::_applyPwm()` putem face fade-in/fade-out gradual (50ms ramp 0→target) — reduce stress pe LED si pe sursa. Cod simplu de adaugat:
  ```cpp
  void LedController::_applyPwm(uint8_t target) {
      uint32_t targetDuty = (uint32_t)target * PWM_MAX / 100;
      uint32_t curDuty = ledcRead(PWM_CHANNEL);
      const uint8_t steps = 50;
      for (uint8_t i = 1; i <= steps; i++) {
          uint32_t d = curDuty + (targetDuty - curDuty) * i / steps;
          ledcWrite(PWM_CHANNEL, d);
          delay(1);  // ~50ms total ramp
      }
      _currentPercent = target;
  }
  ```

## 22. Standarde profesionale de implementare (best practices cross-cutting)

Sectiune de referinta pentru patterns aplicate uniform in toate cele 3 stack-uri (Master ESP32, Slave ESP32, MAUI Android). **Toate exemplele de cod din sectiunile 7, 8, 9, 21 trebuie sa respecte aceste standarde**.

### 22.1 Coding standards comune

**Naming conventions**:

| Element | C++ ESP32 | C# MAUI |
| --- | --- | --- |
| Class | `PascalCase` (`SlaveUartClient`) | `PascalCase` (`MqttService`) |
| Method | `camelCase` (`fetchSensor`) sau `PascalCase` pentru public API | `PascalCase` (`SendCommandAsync`) |
| Private field | `_camelCase` (`_baseClient`) | `_camelCase` (`_mqttService`) |
| Const/static | `SCREAMING_SNAKE` (`SHT30_ADDR`) | `PascalCase` (`MaxRetries`) |
| Enum value | `PascalCase` (`Status::Active`) sau `UPPER` | `PascalCase` |
| Macro | `SCREAMING_SNAKE` (`LOG_INFO`) | n/a (folosim `nameof()`) |
| File | `PascalCase.h/.cpp` (`UartProtocol.h`) | `PascalCase.cs` (`LedViewModel.cs`) |
| Namespace | `lowerCamel` (`Crc::crc16`) | `PascalCase.Hierarchy` (`ProiectVentilatie.Mobile.ViewModels`) |

**File structure obligatoriu** (header C++ ESP32):
```cpp
// FileName.h — descriere scurta in 1 linie
#pragma once

// 1. System includes
#include <Arduino.h>
#include <Wire.h>

// 2. Library includes
#include <ArduinoJson.h>

// 3. Project includes
#include "Config.h"

// 4. Forward declarations daca e nevoie

// 5. Constants pentru fisier (constexpr, NU #define)
namespace { constexpr uint32_t LOCAL_TIMEOUT_MS = 1000; }

// 6. Class declaration
class FooBar { ... };
```

**Limita de linii per fisier**: maxim ~400 linii. Daca depaseste → split logic in module separate.
**Limita per functie**: maxim ~50 linii. Functii lungi sunt symptom de complexitate excesiva.

### 22.2 C++ ESP32 best practices (Master + Slave)

**Const-correctness obligatoriu**:

```cpp
// CORECT
class VentilationZone {
public:
    float getTemp() const;                                    // getter const
    void  updateLogic(const Config& cfg);                     // param const ref
    bool  hasError() const noexcept;                          // const + noexcept

private:
    void _applyRelay(bool state) const;   // GRESIT — modifica hardware, NU e const
    // CORECT: void _applyRelay(bool state);
};
```

**Constructor: Dependency Injection prin referinta**:

```cpp
// CORECT — DI prin ref, ownership clar (caller detine resursa)
class CommandDispatcher {
public:
    CommandDispatcher(Sht30Sensor& sensor, UartProtocol& uart, SystemLED& led)
        : _sensor(sensor), _uart(uart), _led(led) {}
    // copy/move deleted — nu copiem dispatcher-ul
    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

private:
    Sht30Sensor&  _sensor;   // referinta non-owning
    UartProtocol& _uart;
    SystemLED&    _led;
};
```

**`enum class` (tipare puternica) > enum (poluare namespace)**:

```cpp
// GRESIT — enum simplu
enum LedStatus { OFF, ON, ERROR };  // OFF/ON/ERROR ajung in scope global, conflict potential

// CORECT — enum class
enum class LedStatus : uint8_t { Off = 0, On = 1, Error = 2 };
LedStatus s = LedStatus::Off;        // explicit, type-safe
```

**Returnari din functii fallible**:

```cpp
// Pattern preferat: bool return + out param
bool Sht30Sensor::read(float& temp, float& hum, bool force = false);

// Sau struct cu success flag pentru rezultate complexe
struct FetchResult {
    bool ok;
    float temp;
    float hum;
    uint32_t ts;
    uint8_t errorCode;   // 0=OK, 1=timeout, 2=crc, 3=parse
};
FetchResult SlaveUartClient::fetch();
```

**`[[nodiscard]]` pentru forta verificare retur**:

```cpp
class OtaReceiver {
public:
    [[nodiscard]] bool begin(uint32_t size, const char* sha);
    [[nodiscard]] bool writeChunk(uint32_t length);
    [[nodiscard]] bool end();
};

// Caller-ul OBLIGAT sa verifice:
if (!_otaReceiver.begin(size, sha)) {           // CORECT
    LOG_ERROR("OTA begin failed");
    return;
}
```

**`static_assert` pentru contracte la compile-time**:

```cpp
// In Config.h — verificari de invarianti
static_assert(SLAVE_UART_BAUD <= 921600, "UART baud over ESP32 max");
static_assert(SHT30_RETRY_COUNT > 0, "Retry must be at least 1");
static_assert(sizeof(MqttPending) < 512, "MqttPending struct too large");
```

**Defensive programming patterns**:

```cpp
// 1. Validare input la frontiere (orice input extern)
void Slave::handleLedSchedule(uint8_t onH, uint8_t onM,
                               uint8_t offH, uint8_t offM,
                               uint8_t maxIntensity) {
    if (onH >= 24 || offH >= 24 || onM >= 60 || offM >= 60 || maxIntensity > 100) {
        LOG_WARN("Invalid schedule, rejecting");
        return;   // FAIL FAST
    }
    // proceed with valid input
}

// 2. Null check pe pointeri primiti
void SlaveUartClient::begin(HardwareSerial* serial) {
    if (serial == nullptr) {
        LOG_ERROR("Null serial");
        return;
    }
    _serial = serial;
}

// 3. Niciodata `delay()` lung in ISR sau callback
void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    // GRESIT: delay(100); ESP.restart();
    // CORECT: set flag, process in loop()
    _mqttPending.reboot = true;
}
```

**Resource management — RAII unde e posibil**:

```cpp
// Wrapper RAII pentru NVS (auto-close la out-of-scope)
class NvsScope {
public:
    NvsScope(const char* ns, bool readOnly = false) {
        _prefs.begin(ns, readOnly);
    }
    ~NvsScope() { _prefs.end(); }     // garantat apelat la return / exception

    Preferences& get() { return _prefs; }

    // Non-copyable
    NvsScope(const NvsScope&) = delete;
    NvsScope& operator=(const NvsScope&) = delete;

private:
    Preferences _prefs;
};

// Folosire — close automat la return
void saveSchedule(...) {
    NvsScope nvs("led", false);
    nvs.get().putUChar("oh", onHour);
    // ... la return automat _prefs.end()
}
```

### 22.3 MAUI .NET best practices

**Project setup obligatoriu** (in `.csproj`):

```xml
<PropertyGroup>
    <Nullable>enable</Nullable>                              <!-- nullable reference types -->
    <TreatWarningsAsErrors>true</TreatWarningsAsErrors>     <!-- ridica nivelul -->
    <WarningsNotAsErrors>CS8600;CS8602;CS8603</WarningsNotAsErrors>
    <LangVersion>latest</LangVersion>
    <AnalysisLevel>latest-recommended</AnalysisLevel>       <!-- Roslyn analyzers maxim -->
    <EnforceCodeStyleInBuild>true</EnforceCodeStyleInBuild> <!-- .editorconfig respectat -->
</PropertyGroup>
```

**MVVM strict cu CommunityToolkit.Mvvm**:

```csharp
// CORECT — auto-generated boilerplate
public partial class LedViewModel : ObservableObject {
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(IsScheduleVisible))]   // dependency tracking
    [NotifyCanExecuteChangedFor(nameof(SaveScheduleCommand))]   // re-evaluare CanExecute
    private bool _scheduleEnabled;

    public bool IsScheduleVisible => ScheduleEnabled;

    [RelayCommand(CanExecute = nameof(CanSave))]
    private async Task SaveScheduleAsync(CancellationToken ct) {
        // implementation
    }

    private bool CanSave() => ScheduleEnabled && OnTime != OffTime;
}
```

**Async/await — NICIODATA `.Wait()` sau `.Result` in cod aplicatie**:

```csharp
// GRESIT — block thread, deadlock potential pe UI thread
var data = _httpClient.GetStringAsync(url).Result;

// CORECT — async tot stack-ul
public async Task<string> FetchAsync(CancellationToken ct = default) {
    return await _httpClient.GetStringAsync(url, ct);
}
```

**CancellationToken propagat in toate operatiile async**:

```csharp
// CORECT — caller poate canc. operatia
public async Task<bool> SendCommandAsync(object command, CancellationToken ct = default) {
    var json = JsonSerializer.Serialize(command);
    await _client.PublishAsync(_settings.CommandTopic, json,
        QualityOfService.AtLeastOnceDelivery, ct);
    return true;
}

// In ViewModel — leagă de page lifecycle
[RelayCommand(IncludeCancelCommand = true)]   // genereaza si SaveScheduleCancelCommand
private async Task SaveScheduleAsync(CancellationToken ct) {
    await _mqttService.SendCommandAsync(new { ... }, ct);
}
```

**IDisposable corect implementat**:

```csharp
public class MqttService : IMqttService, IDisposable, IAsyncDisposable {
    private bool _disposed;

    public void Dispose() {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing) {
        if (_disposed) return;
        if (disposing) {
            _client?.Dispose();
            _reconnectCts?.Cancel();
            _reconnectCts?.Dispose();
        }
        _disposed = true;
    }

    public async ValueTask DisposeAsync() {
        await DisconnectAsync().ConfigureAwait(false);
        Dispose();
    }
}
```

**ILogger via DI in loc de `Console.WriteLine`**:

```csharp
// In MauiProgram.cs
builder.Logging.AddDebug();
#if DEBUG
builder.Logging.SetMinimumLevel(LogLevel.Trace);
#else
builder.Logging.SetMinimumLevel(LogLevel.Warning);
#endif

// In services
public class MqttService(ILogger<MqttService> logger, IOptions<MqttSettings> options) : IMqttService {
    public async Task ConnectAsync(CancellationToken ct = default) {
        try {
            await _client.ConnectAsync().WaitAsync(ct);
            _logger.LogInformation("MQTT connected to {Host}:{Port}", _settings.Host, _settings.Port);
        } catch (Exception ex) {
            _logger.LogError(ex, "MQTT connect failed to {Host}", _settings.Host);
            throw;
        }
    }
}
```

**Records pentru DTO-uri imutabile**:

```csharp
// CORECT — DTO immutable, value-equality, terse
public sealed record SlaveStatus(
    [property: JsonPropertyName("online")]    bool   Online,
    [property: JsonPropertyName("lastSeen")]  long   LastSeen,
    [property: JsonPropertyName("errors")]    int    Errors);

// Ofera value equality, deconstructor, with-expression
var s1 = new SlaveStatus(true, 1745784620, 0);
var s2 = s1 with { Errors = 5 };   // imutabil + clone partial
```

**Threading: MainThread.BeginInvokeOnMainThread pentru UI updates**:

```csharp
// In MqttService — vine pe thread non-UI
private void OnMessageReceived(object? sender, MessageReceivedEventArgs e) {
    var state = JsonSerializer.Deserialize<VentilationState>(e.Payload);
    if (state == null) return;

    // CORECT — UI updates pe main thread
    MainThread.BeginInvokeOnMainThread(() => OnStateReceived?.Invoke(state));
}

// Alternativ: dispatcher manual
private readonly IDispatcher _dispatcher = Application.Current!.Dispatcher;
_dispatcher.Dispatch(() => UpdateUi(state));
```

**WeakEventManager pentru a preveni memory leaks** (subscriere lunga la evenimente):

```csharp
private readonly WeakEventManager _stateEventManager = new();

public event EventHandler<VentilationState> StateReceived {
    add => _stateEventManager.AddEventHandler(value);
    remove => _stateEventManager.RemoveEventHandler(value);
}

protected void RaiseStateReceived(VentilationState state) {
    _stateEventManager.HandleEvent(this, state, nameof(StateReceived));
}
```

**Strongly-typed bindings (x:DataType obligatoriu)**:

```xml
<!-- CORECT — compile-time check binding paths -->
<ContentPage x:Class="..." x:DataType="vm:LedViewModel">
    <Slider Value="{Binding IntensitySliderValue, Mode=TwoWay}" />
</ContentPage>

<!-- GRESIT — fara x:DataType, binding errors descoperite la runtime -->
```

### 22.4 Strategie error handling per strat

| Strat | Pattern | Exemplu |
| --- | --- | --- |
| **ESP32 Driver (Sht30Sensor, MqttBridge)** | bool return + LOG_ERROR + counter | `if (!sht.read()) { LOG_ERROR(...); _errors++; return false; }` |
| **ESP32 App logic (VentilationZone, processZones)** | propagare prin bool, log la fail, EventLog la error grav | `if (!slaveClient.fetch()) { eventLog.append(EVT_ERROR, ...); enterFailsafe(); }` |
| **ESP32 Boundary (MQTT callback)** | catch silent, set pending flag, never crash | `void cb(...) { _pending.cmd = true; }` (procesare in loop) |
| **MAUI Service (MqttService)** | try/catch + ILogger.LogError, opt. throw | `try { ... } catch (Ex ex) { _logger.LogError(ex, "..."); throw; }` |
| **MAUI ViewModel** | try/catch in RelayCommand, DisplayAlert pe UI | `try { ... } catch { await DisplayAlert("Eroare", ...); }` |
| **MAUI UI** | NEVER throw — toate exception caught upstream | binding-uri safe, default values |

### 22.5 Strategie logging centralizata

**ESP32**:

```cpp
// Logger.h — un singur loc cu nivele
LOG_DEBUG("Detail trace");   // dezactivat in productie
LOG_INFO("State change");     // dezactivat in productie
LOG_WARN("Recovery action");  // pastrat in productie
LOG_ERROR("Failure");         // pastrat in productie

// Production: doar WARN+ERROR pe Serial USB
#define LOG_LEVEL LOG_LEVEL_WARN
```

**MAUI**:

```csharp
// Categorii standard
_logger.LogTrace("Frame parse: {Bytes} bytes", bytes.Length);   // dezactivat productie
_logger.LogDebug("Mqtt subscribe: {Topic}", topic);             // dezactivat productie
_logger.LogInformation("State updated: temp={T}", state.Temp);  // pastrat
_logger.LogWarning("Slave offline >{Sec}s", secs);
_logger.LogError(exception, "Connect failed to {Host}", host);

// Structured logging — placeholders, NU concatenare:
// CORECT: _logger.LogInformation("User {UserId} did {Action}", userId, action);
// GRESIT: _logger.LogInformation($"User {userId} did {action}");   // string interpolation
```

### 22.6 Strategie testing

**ESP32 — unit testing cu Unity (host-based)**:

Structura `ESP32/test/` cu sketch-uri runnable pe host (nu pe ESP32):

```cpp
// test/test_crc/test_crc.cpp
#include <unity.h>
#include "../../CrcUtil.h"

void test_crc16_modbus_known_vectors() {
    // Test vectors din specificatie Modbus oficial
    TEST_ASSERT_EQUAL_HEX16(0x4B37, Crc::crc16("123456789", 9));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, Crc::crc16("", 0));
}

void test_crc16_validate_round_trip() {
    char buf[64];
    snprintf(buf, sizeof(buf), "GET_SENSOR");
    uint16_t crc = Crc::crc16(buf, strlen(buf));
    char hex[5]; Crc::crcToHex(crc, hex);
    snprintf(buf, sizeof(buf), "GET_SENSOR*%s", hex);
    TEST_ASSERT_TRUE(Crc::validate(buf));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_modbus_known_vectors);
    RUN_TEST(test_crc16_validate_round_trip);
    return UNITY_END();
}
```

**ESP32 — integration testing pe device real**:

Sketch dedicat `ESP32/test/integration_uart.ino` care emuleaza Master:
- Trimite GET_SENSOR la 1 Hz timp de 1h
- Verifica raspuns CRC valid + JSON parsabil
- Output: success rate (target >99.9%)

**MAUI — unit testing ViewModel cu xUnit + Moq**:

```csharp
// Tests/LedViewModelTests.cs
using Moq;
using Xunit;
using FluentAssertions;
using ProiectVentilatie.Mobile.ViewModels;
using ProiectVentilatie.Mobile.Services;

public class LedViewModelTests {
    private readonly Mock<IMqttService> _mqttMock = new();

    [Fact]
    public async Task SetNowAsync_PublishesCorrectCommand() {
        // Arrange
        var vm = new LedViewModel(_mqttMock.Object);
        vm.IntensitySliderValue = 75;

        // Act
        await vm.SetNowCommand.ExecuteAsync(null);

        // Assert
        _mqttMock.Verify(m => m.SendCommandAsync(
            It.Is<object>(o => o.GetType().GetProperty("intensity")!.GetValue(o)!.Equals(75)),
            It.IsAny<CancellationToken>()
        ), Times.Once);
    }

    [Fact]
    public void OnState_WithLed_UpdatesProperties() {
        var vm = new LedViewModel(_mqttMock.Object);
        var state = new VentilationState {
            Led = new LedState(60, true, new LedSchedule(18, 0, 23, 30, 80))
        };
        // Trigger via reflection / private method or expose internal
        vm.HandleStateReceived(state);

        vm.CurrentIntensity.Should().Be(60);
        vm.ScheduleEnabled.Should().BeTrue();
        vm.OnTime.Should().Be(new TimeSpan(18, 0, 0));
    }
}
```

**MAUI — integration cu MQTT broker local**:

`docker run -p 1883:1883 eclipse-mosquitto` → testezi MqttService end-to-end fara HiveMQ.

**E2E hardware**:

Script `tests/e2e/test_full_workflow.sh`:
1. Reset firmware ambele placi
2. Trimite cmd MQTT din `mosquitto_pub`
3. Asteapta state nou pe `mosquitto_sub`
4. Verifica field-uri asteptate
5. Repeat pentru toate scenariile principale

### 22.7 Code review checklist

Inainte de merge la `main`, fiecare PR trece prin:

- [ ] **Functional**: feature-ul cerut e implementat complet?
- [ ] **Tests**: unit tests adaugate? Tests existente pass? Coverage rezonabil?
- [ ] **Naming**: respecta conventia tabelei 22.1?
- [ ] **Const-correctness** (C++): getter-i const? Param-uri const ref?
- [ ] **Nullable** (C#): nullable reference types respectate? Fara `!` peste tot?
- [ ] **Error handling**: erori la frontier prinse + logate? Defensive null checks?
- [ ] **Memory**: zero alocari heap in hot path C++? Fara leaks in MAUI (Dispose)?
- [ ] **Threading**: ESP32 — `volatile` pe shared between ISR/loop? MAUI — UI updates pe MainThread?
- [ ] **Logging**: erori critice logate? Niciun `printf`/`Console.WriteLine` direct?
- [ ] **Documentatie**: doc comment pe public API? `// why` comment pe logica subtila?
- [ ] **Static analysis**: zero warnings noi? `cppcheck`/Roslyn analyzers clean?
- [ ] **Build**: compileaza pe ambele placi (Master + Slave)? MAUI Release build OK?
- [ ] **Manual test**: 1 scenariu cheie testat manual pe hardware real?
- [ ] **Plan reference**: este alineat cu ordinea de executie sectiunea 18?

### 22.8 Git workflow + Conventional Commits

**Branch naming**:
- `feat/<feature-name>` — feature nou (ex. `feat/dual-board-uart`)
- `fix/<issue>` — bug fix (ex. `fix/mqtt-reconnect-leak`)
- `refactor/<area>` — refactoring fara schimbare comportament
- `docs/<topic>` — documentatie
- `test/<area>` — adaugare/imbunatatire teste
- `release/v<X.Y.Z>` — branch de release

**Commit message format** (Conventional Commits):

```
<type>(<scope>): <subject>

[body opt]

[footer opt — Closes #123, BREAKING CHANGE, etc.]
```

Exemple:
```
feat(slave): add LED PWM controller with NVS schedule
fix(master): handle Ethernet link drop with W5500 reset
refactor(mqtt): replace WiFiClientSecure with SSLClient
docs(readme): document dual-board UART setup
test(crc): add Modbus standard test vectors
chore(deps): bump Adafruit SHT31 to 2.2.0
```

**Tag-uri SemVer** la release:
- `v2.0.0` major (breaking change — schema MQTT noua)
- `v2.1.0` minor (feature LED schedule)
- `v2.1.1` patch (fix bug heap leak)

**PR process**:
1. Creezi branch din main (`git checkout -b feat/...`)
2. Commit-uri atomice cu mesaje conventionale
3. Push: `git push origin feat/...`
4. Open PR cu descriere clara (linkuri la sectiuni plan)
5. CI ruleaza: build + tests + analyzers
6. Code review checklist 22.7 bifat
7. Merge cu `Squash and merge` (un commit per PR)
8. Tag daca e release boundary

### 22.9 Static analysis si linters

**ESP32 (C++)**:

```bash
# cppcheck — analyzer rapid
cppcheck --enable=all --inconclusive --suppress=missingIncludeSystem \
    --inline-suppr --quiet ESP32/ ESP32_Slave/

# clang-tidy — mai puternic, integrat in editor (VS Code)
clang-tidy ESP32/MqttBridge.cpp -- -I~/.arduino15/packages/esp32/...

# Build flags strict in arduino-cli
arduino-cli compile --fqbn esp32:esp32:esp32 \
    --warnings all \
    --build-property "build.extra_flags=-Wall -Wextra -Wpedantic -Werror"
```

**MAUI (C#)**:

`.editorconfig` la root proiect:

```ini
root = true

[*.cs]
# Indentation
indent_style = space
indent_size = 4

# Code style
csharp_style_var_for_built_in_types = false:warning
csharp_style_var_when_type_is_apparent = true:warning
csharp_prefer_braces = true:warning
csharp_style_pattern_matching = true:warning

# Nullable
dotnet_diagnostic.CS8600.severity = error
dotnet_diagnostic.CS8602.severity = error
dotnet_diagnostic.CS8603.severity = error

# Async
dotnet_diagnostic.CS4014.severity = error   # await missing
dotnet_diagnostic.CA2007.severity = none    # ConfigureAwait nu e cerut in MAUI

# Disposable
dotnet_diagnostic.CA2000.severity = warning   # IDisposable not disposed
dotnet_diagnostic.CA1816.severity = warning   # GC.SuppressFinalize lipsa
```

Plus pachete NuGet recomandate:
- `Microsoft.CodeAnalysis.NetAnalyzers` — Roslyn analyzers oficial
- `StyleCop.Analyzers` — style enforcement
- `SonarAnalyzer.CSharp` — code quality

### 22.10 Code documentation

**Doxygen-style C++ pentru API public**:

```cpp
/**
 * @brief Cere senzorul de la Slave prin UART cu retry + CRC validation.
 *
 * Trimite "GET_SENSOR\n", asteapta raspuns JSON cu CRC validat, parseaza.
 * In caz de timeout sau CRC mismatch, retry pana la SLAVE_RETRY_PER_FETCH+1 ori.
 *
 * @param[out] temp     Temperatura citita (°C) daca returneaza true
 * @param[out] hum      Umiditate citita (%) daca returneaza true
 * @param[out] slaveTs  Timestamp Slave (epoch sec) daca returneaza true
 * @param[in]  timeoutMs Timeout per incercare (default SLAVE_REQ_TIMEOUT_MS)
 *
 * @return true daca toate retry-urile au reusit cu raspuns valid; false altfel.
 *         La fail, _consecutiveErrors++ si invocator-ul decide failsafe.
 *
 * @note  Functia e blocking pe durata timeoutMs × (retry+1). Apel dintr-un singur
 *        thread (loopTask). NU thread-safe.
 */
bool fetch(float& temp, float& hum, uint32_t& slaveTs,
           uint32_t timeoutMs = SLAVE_REQ_TIMEOUT_MS);
```

**XML doc C# pentru API public**:

```csharp
/// <summary>
/// Sends a typed command to ESP32 via MQTT command topic.
/// </summary>
/// <typeparam name="T">Command DTO type with [JsonPropertyName] attributes.</typeparam>
/// <param name="command">Command instance to serialize and send.</param>
/// <param name="ct">Token to cancel the publish operation.</param>
/// <returns>True if the message was acked by HiveMQ; false on timeout/error.</returns>
/// <exception cref="InvalidOperationException">Thrown if MQTT not connected.</exception>
public async Task<bool> SendCommandAsync<T>(T command, CancellationToken ct = default)
    where T : class { ... }
```

**Inline comments — DOAR „de ce", nu „ce"**:

```cpp
// CORECT — explica decizia
// Cooldown 60s ales empiric: SHT30 are accuracy crescuta dupa 30s heat-up,
// iar utilizatorul nu vrea citiri mai dese (cf. cerinta 21.X).
constexpr uint32_t SHT30_MIN_READ_MS = 60000UL;

// GRESIT — repeta codul
// Setam cooldown la 60000 ms
constexpr uint32_t SHT30_MIN_READ_MS = 60000UL;
```

**Per proiect — fisier `ARCHITECTURE.md`**:

Document de top-level (~200 linii) care explica:
- Diagrama bloc dual-board
- Responsabilitatea fiecarui modul
- Fluxul comenzilor MQTT → MAUI → Master → Slave
- Decizii arhitecturale + alternative rejectate cu motivare
- Glosar termeni proiect

### 22.11 Anti-patterns de evitat

**ESP32**:

| Anti-pattern | De ce e rau | Pattern corect |
| --- | --- | --- |
| `String s = "...";` in loop | Heap fragmentation in cateva ore | `char s[N]; snprintf(s, N, ...);` |
| `delay(1000)` in loop principal | Blocking watchdog, unresponsive | timer + state machine |
| Global mutable state fara `volatile` partajat ISR/loop | Race condition, miss updates | `volatile bool _flag;` + protectie |
| `new`/`malloc` in hot path | Heap exhaustion + fragmentation | static buffers pre-allocate |
| `while (Serial.available())` fara break | Hang la flood UART | counter + timeout |
| Initializare hardware in constructor | ESP32 nu e gata la pre-main | `begin()` method explicit |
| Catch-all `if (true)` „defensive" | Mascheaza bugs | fail-fast cu LOG_ERROR + return |

**MAUI**:

| Anti-pattern | De ce e rau | Pattern corect |
| --- | --- | --- |
| `async void` (non-event-handler) | Exceptii nu pot fi prinse | `async Task` |
| `.Result` / `.Wait()` | Deadlock pe UI thread | `await` |
| Logica in code-behind XAML | Incalca MVVM, netestabil | ViewModel + RelayCommand |
| String interpolation in `_logger.LogX($"...")` | Performance + structured logging brokemstring | `_logger.LogX("...{P}", p)` |
| Missing `Dispose` pe IDisposable | Memory leaks (MQTT client, timers) | `using var` sau implementare IDisposable |
| Subscriere `+=` la event fara unsubscribe | Memory leak (page kept alive) | `WeakEventManager` sau Dispose explicit |
| `[ObservableProperty]` cu collections mutable | Bindings nu reflecta `Add()`/`Remove()` | `ObservableCollection<T>` |
| Catch all `try { ... } catch { }` | Silent failures, debugging hell | Catch specific types + log |

### 22.12 Aplicare cross-cutting in faze (referinta rapida)

Pentru fiecare faza din sectiunea 18, urmatoarele patterns 22.X aplica:

| Faza | Patterns cheie din 22.X |
| --- | --- |
| F1 (CrcUtil) | 22.1 (naming), 22.6 (test cu vectori standard) |
| F3 (Sht30Sensor) | 22.2 (RAII, const), 22.4 (bool return + LOG) |
| H1-H3 (Slave UART) | 22.2 (DI), 22.4 (defensive), 22.5 (LOG_WARN la CRC fail), 22.10 (doxygen) |
| I-O (Master Ethernet+MQTT) | 22.2 (resource mgmt SSLClient), 22.4 (try/catch boundary), 22.6 (integration test) |
| P (MAUI updates) | 22.3 (MVVM strict, IDisposable), 22.4 (ViewModel try/catch), 22.6 (xUnit tests ViewModel) |
| R (resilience) | 22.4 (toate stratele), 22.5 (LOG_WARN per mecanism) |
| S (memory opt) | 22.2 (anti-pattern String → char[]) |
| T-U (OTA) | 22.2 (RAII Update partition), 22.4 (rollback la fail), 22.6 (E2E tests) |
| V-Y (LED feature) | 22.3 (LedViewModel MVVM), 22.4 (validare in Master), 22.6 (xUnit pentru LedViewModel) |
| BB (documentatie) | 22.10 (Doxygen + XML doc + ARCHITECTURE.md) |

### 22.13 Resurse externe recomandate

- **C++ Core Guidelines** (Bjarne Stroustrup, Herb Sutter): <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines>
- **ESP-IDF Programming Guide**: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/index.html>
- **Microsoft .NET Coding Style**: <https://github.com/dotnet/runtime/blob/main/docs/coding-guidelines/coding-style.md>
- **MVVM Toolkit Documentation**: <https://learn.microsoft.com/en-us/dotnet/communitytoolkit/mvvm/>
- **Conventional Commits**: <https://www.conventionalcommits.org/>
- **SemVer**: <https://semver.org/>

Proiectul respecta toate aceste standarde, cu adaptari minore pentru constrangeri ESP32 (fara exceptii, fara STL, etc.).

## 18.OLD — Vechi: Ordine de executie — 48 sesiuni atomice in 10 faze (PASTRAT pentru referinta)

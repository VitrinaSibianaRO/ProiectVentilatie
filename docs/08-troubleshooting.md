# 08 — Troubleshooting

Probleme comune și soluții.

## Cuprins

- [ESP32 nu se conectează la Wi-Fi](#esp32-nu-se-conectează-la-wi-fi)
- [ESP32 nu se conectează la HiveMQ](#esp32-nu-se-conectează-la-hivemq)
- [ESP32 nu se conectează la Blynk](#esp32-nu-se-conectează-la-blynk)
- [Senzori DHT22 dau erori](#senzori-dht22-dau-erori)
- [Releele nu pornesc](#releele-nu-pornesc)
- [Heap scădere progresivă](#heap-scădere-progresivă)
- [MAUI nu vede state retained](#maui-nu-vede-state-retained)
- [MAUI: butoanele Save sunt mereu disabled](#maui-butoanele-save-sunt-mereu-disabled)
- [Lock activ permanent](#lock-activ-permanent)
- [OTA failed](#ota-failed)
- [Override blocat ON > 2h](#override-blocat-on--2h)
- [Build fails: missing libraries](#build-fails-missing-libraries)
- [Build fails: build_number.txt errors](#build-fails-build_numbertxt-errors)
- [Comenzi diagnostic](#comenzi-diagnostic)

---

## ESP32 nu se conectează la Wi-Fi

**Simptom**: LED RGB rămâne albastru pulsat (portal WiFiManager activ).

**Cauze și soluții:**

1. **SSID/parolă greșite în WiFiManager**
   - Conectează-te la rețeaua `ESP32_Ventilatie` (open Wi-Fi creată de ESP32)
   - Browser → `http://192.168.4.1`
   - Configure WiFi → selectează SSID-ul tău + parolă → Save

2. **Wi-Fi-ul tău e 5GHz only**
   - ESP32-PICO-V3-02 suportă **doar 2.4GHz**
   - Activează banda 2.4GHz pe router

3. **Fără reset Wi-Fi**
   - Apasă butonul fizic GPIO 13 timp de **3 secunde** → LED alb → reset

## ESP32 nu se conectează la HiveMQ

**Simptom**: Serial: `[MQTT] Connect failed, rc=-2` sau `rc=-3`.

**Cauze și soluții:**

1. **Credențiale greșite în Config.h**
   ```cpp
   #define MQTT_USER "ventilatie_esp32"
   #define MQTT_PASS "PAROLA_CORECTĂ"
   ```
   Verifică în consola HiveMQ Cloud → Access Management.

2. **Cert TLS / data sistem greșită**
   - Cert ISRG Root X1 e valid până 2035, dar dacă data sistem ESP32 e <2026 → rejected
   - Verifică NTP sync: `Serial.println("[NTP] OK ...")`
   - Dacă NTP eșuează: forțează sync manual sau verifică firewall pe port 123 UDP

3. **Firewall blochează port 8883**
   - Test din alt device în aceeași rețea:
     ```bash
     nc -zv 1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud 8883
     ```
   - Dacă timeout → router/firewall blochează → schimbă rețeaua sau deschide portul

4. **Cluster URL greșit**
   - Verifică în Config.h: `MQTT_HOST` exact ca în consola HiveMQ

5. **PSRAM nu e activ**
   - TLS handshake necesită memorie. Dacă PSRAM e off, heap intern poate fi insuficient
   - Verifică `[Sistem] Heap intern liber: ...` — dacă <100KB, activează PSRAM

## ESP32 nu se conectează la Blynk

**Simptom**: LED RGB roșu/galben fix; Serial fără mesaje `[Blynk] Conectat`.

**Cauze:**

1. **`BLYNK_AUTH_TOKEN` invalid**
   - În Config.h, verifică token-ul (32 caractere)
   - În consola Blynk → Devices → device-ul tău → Auth Token

2. **Cont Blynk pe alt server**
   - Config.h folosește serverul default `blynk.cloud`
   - Dacă ai cont pe alt server (ex: `frankfurt.blynk.cloud`), adaugă:
     ```cpp
     Blynk.config(BLYNK_AUTH_TOKEN, "frankfurt.blynk.cloud", 80);
     ```

3. **Blynk down**
   - Verifică https://status.blynk.io
   - Sistemul funcționează autonom oricum (releele decise local)

## Senzori DHT22 dau erori

**Simptom**: `errs > 0` în state JSON; valori `temp` sau `hum` blocate sau NaN.

**Cauze:**

1. **Cablu DHT slăbit**
   - Verifică conectarea pe GPIO 19 (stânga) și GPIO 32 (dreapta)
   - Asigură-te că ai rezistor pull-up 10kΩ pe pinul DATA

2. **Senzor defect**
   - DHT22 are durată de viață ~3-5 ani
   - După `errs >= 5`, ESP32 publică event `sensor_error` și log NVS
   - Înlocuiește senzorul

3. **Cooldown insuficient**
   - DHT22 are nevoie de 2.1s între citiri
   - Codul respectă deja acest cooldown — dar nu apela `readSensor()` din alte locuri

## Releele nu pornesc

**Simptom**: V5/V6 (LED relay în Blynk) sunt OFF, dar ar trebui să fie ON conform pragurilor.

**Cauze:**

1. **Releele sunt active LOW**
   - Codul deja gestionează asta (`digitalWrite(pin, LOW)` = ON)
   - Verifică LED-ul fizic al releului — dacă e aprins dar releul nu comută, înlocuiește modulul

2. **Praguri prea mari**
   - Verifică `state.config.threshT` și `threshH`
   - Dacă T_curent < threshT și H_curent < threshH → releu OFF (corect, automat)

3. **Override OFF activ**
   - Verifică `state.left.override` / `state.right.override`
   - Dacă `true` și `relay: false` → e override forțat OFF
   - Trimite `{"cmd":"setOverride","zone":"left","value":2}` pentru clear

4. **Alimentare insuficientă**
   - Modulele relee 5V au nevoie de 80-100mA
   - ESP32 USB poate fi insuficient → folosește PSU extern 5V/1A

## Heap scădere progresivă

**Simptom**: Serial — `[Sistem] Heap liber:` scade de la 220KB la <100KB peste ore.

**Cauze și soluții:**

1. **Memory leak în `String` allocations**
   - Codul firmware actual NU folosește `String` în hot paths (verificat)
   - Dacă modifici cod și introduci `String` în loop → leak

2. **TLS reconnect leak**
   - PubSubClient + WiFiClientSecure pot avea leak la reconnect-uri repetate
   - Mitigare: restart preventiv la heap < 30KB (deja existent)

3. **JsonDocument dynamic alocat repetitiv**
   - Codul folosește `StaticJsonDocument` (alocare stack) — fără leak
   - Verifică dacă ai introdus `DynamicJsonDocument` undeva → înlocuiește cu Static

**Diagnostic:**

```cpp
// Adaugă în loop() la fiecare 60s
Serial.printf("Heap: %d, MinFree: %d\n",
    ESP.getFreeHeap(), ESP.getMinFreeHeap());
```

Dacă `MinFree` continuă să scadă → memory leak real.

## MAUI nu vede state retained

**Simptom**: Dashboard arată "fără date" sau gauge-urile la 0.

**Cauze:**

1. **`appsettings.json` nu e embedded resource**
   - Verifică `ProiectVentilatie.Mobile.csproj`:
     ```xml
     <ItemGroup>
       <EmbeddedResource Include="appsettings.json" />
     </ItemGroup>
     ```

2. **Credențiale `appsettings.json` greșite**
   - User: `ventilatie_app` (nu `ventilatie_esp32` — userii sunt diferiți)
   - Parolă corectă

3. **ESP32 nu publică state retained**
   - Verifică în HiveMQ Web Client → subscribe `ventilatie/state` → ar trebui să vezi imediat (chiar dacă ESP32 e offline acum)
   - Dacă nu vezi mesaj retained → ESP32 niciodată nu a publicat. Pornește ESP32, așteaptă heartbeat sau trimite cmd:refresh.

4. **Wi-Fi telefon firewall blochează 8883**
   - Wi-Fi public/corporate ar putea bloca
   - Test cu alt Wi-Fi (mobile hotspot)

5. **Force kill app + reopen**
   - Settings → Apps → Ventilație → Force Stop → reopen

## MAUI: butoanele Save sunt mereu disabled

**Cauze:**

1. **Lock activ pe alt canal**
   - Vezi banner "🔒 Control blocat (Blynk activ)"
   - Așteaptă <100ms pentru release automat
   - Dacă persistă → vezi [Lock activ permanent](#lock-activ-permanent)

2. **Nu sunt modificări** (Save diff-based)
   - Slider-ele afișează valorile actuale ale ESP32
   - Save e disabled până când modifici ceva
   - **Comportament corect, nu un bug**

3. **MAUI deconectat de broker**
   - Verifică label-ul de status conexiune
   - Pull down to refresh sau restart app

## Lock activ permanent

**Simptom**: `state.lock.owner` rămâne setat indefinit; banner persistent în UI.

**Cauze:**

1. **ESP32 a hang-uit între lock set și release**
   - Watchdog 60s ar trebui să prevină asta — dar dacă apare, restart manual
   - Apasă butonul fizic GPIO 13 timp de 3s → reset
   - Sau trimite `{"cmd":"reboot"}` din MAUI System

2. **Bug software**
   - Verifică în Serial dacă vezi `[Lock] released after processZones`
   - Raportează ca issue dacă reproductibil

## OTA failed

### `sha_mismatch`

**Cauze:**
- SHA-256 calculat greșit
- Fișier corupt în upload pe GitHub
- Server compromis (improbabil cu HTTPS)

**Soluție:**
- Recalculează: `sha256sum firmware.bin`
- Re-upload pe GitHub
- Asigură-te că copiezi 64 caractere hex (fără spații, fără numele fișierului)

### `download_error`

**Cauze:**
- URL invalid sau release inaccesibil
- Wi-Fi instabil în timpul download
- Server GitHub inaccesibil

**Soluție:**
- Verifică URL-ul în browser (poți descărca manual)
- Asigură-te că release-ul e public (nu draft)
- Reîncearcă după ~1min

### `url_not_allowed`

**Cauze:**
- URL nu începe cu `https://github.com/` sau `https://objects.githubusercontent.com/`

**Soluție:**
- Folosește exclusiv URL-uri de la GitHub releases
- Pentru hosting custom, modifică whitelist-ul în `OtaUpdater.cpp`

### `insufficient_space`

**Cauze:**
- Partition app prea mică (< dimensiunea firmware-ului)

**Soluție:**
- Verifică partition scheme: `Default 4MB with spiffs` (1.4MB APP) sau `Minimal SPIFFS` (1.9MB APP)
- Re-flash USB cu schema corectă

## Override blocat ON > 2h

**Simptom**: V11/V12 (override) rămâne 1 deși a trecut timeout-ul.

**Cauze:**

1. **`overrideTimeoutMin` e 0 sau foarte mare**
   - Verifică `state.config.ovrTimeout`
   - Default: 120 (minute)
   - Trimite `{"cmd":"reset"}` pentru a reveni la default

2. **NVS corupt**
   - Reset NVS: buton fizic GPIO 13 timp de 3s → reset complet (Wi-Fi + Prefs)

## Build fails: missing libraries

**Simptom Arduino**: `fatal error: PubSubClient.h: No such file or directory`

**Soluție:**
```bash
arduino-cli lib install "PubSubClient"
arduino-cli lib install "ArduinoJson"
arduino-cli lib install "Blynk"
arduino-cli lib install "WiFiManager"
arduino-cli lib install "DHT sensor library"
arduino-cli lib install "Adafruit NeoPixel"
```

**Simptom MAUI**: `error NU1101: Unable to find package`

**Soluție:**
```bash
cd MobileApp
dotnet restore
```

## Build fails: build_number.txt errors

**Simptom**: `cat: build_number.txt: No such file or directory`

**Soluție:**
- Creează manual: `echo "0" > ESP32/build_number.txt`
- Sau rulează `bump_build.sh` (creează automat dacă nu există)

**Simptom MSBuild**: `error MSB4044: The Target "IncrementBuildNumber" requires the property...`

**Soluție:**
- Verifică sintaxa target-ului în `ProiectVentilatie.Mobile.csproj` (vezi [06-versioning.md](06-versioning.md))
- Sau dezactivează target-ul temporar:
  ```xml
  <Target Name="IncrementBuildNumber" Condition="false" ... />
  ```

## Comenzi diagnostic

### Heap și uptime ESP32

```json
mosquitto_pub -t ventilatie/cmd -m '{"cmd":"refresh"}'
mosquitto_sub -t ventilatie/state
# Vezi: heap, uptimeSec
```

### Log evenimente ESP32

```json
mosquitto_pub -t ventilatie/cmd -m '{"cmd":"getLog"}'
mosquitto_sub -t ventilatie/log
# Sau în MAUI: tab Log → Reîncarcă
```

### Verifică conexiunea broker

```bash
# Test TLS handshake
openssl s_client -connect 1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud:8883 \
  -CApath /etc/ssl/certs

# Test cu mosquitto
mosquitto_sub -h ... -t '$SYS/broker/uptime' -v
```

### Reset complet ESP32

1. Conectează prin USB
2. `esptool.py --port /dev/ttyUSB0 erase_flash`
3. Re-flash firmware

### Logs MAUI Android

```bash
adb logcat -s "DOTNET:V" "Mono:V" "*:E"
```

---

Pentru orice altă problemă, deschide un issue pe GitHub repo.

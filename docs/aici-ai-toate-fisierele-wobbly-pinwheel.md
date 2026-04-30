# Plan: Sync complet Blynk ↔ ESP32 ↔ Android — fix override, reset, hysteresis

## Context

Trei probleme observate pe device:

1. **După `Reset Defaults` din Blynk, aplicația Android nu mai vede valorile senzorilor.** Cauza: `processZones()` la primirea unui pending Blynk reset apelează `prefs.resetToDefaults()` și apoi `mqtt.requestPublishNow()` ÎNAINTE să facă `readSensor()` în același ciclu. Plus, reset face `timerRebuildNeeded = true` care reconstruiește timer-ul cu noul interval (300s default vs 5s anterior din screenshot). State-ul publicat pe MQTT după reset are valorile vechi cached din zone, dar `lastSentTempL/R` și `lastSentHumL/R` rămân setate la valorile pre-reset → la următorul ciclu Blynk nu mai retrimite spre senzori (diff <0.5°C). Dacă MAUI deschis după reset citește retained state, primește snapshot vechi, iar până la heartbeat-ul de 1h sau următoarea schimbare nu mai vede update.

2. **Override (Vent stânga/dreapta) din Blynk nu funcționează.** Conform screenshot-urilor utilizatorului, datastreams-urile **V11/V12 sunt acum `Marja temperatura`/`Marja umiditate`** (Double 0/10, Integer 0/20) — adică hysteresis. Dar firmware-ul ESP32 are `BLYNK_WRITE(V11)`/`BLYNK_WRITE(V12)` mapate pe override (1=ON, 0=OFF, 2=clear). Toggle-urile **„Vent stanga"/„Vent dreapta"** din dashboard Blynk sunt pe alte pini (foarte probabil **V5/V6** — single integer 0/1 — folosite acum ca toggle bidirecțional pentru override). Firmware-ul nu are `BLYNK_WRITE(V5)`/`BLYNK_WRITE(V6)` — acei pini sunt strict read-only la ESP32 (publicare stare releu). Deci toggle-urile click → ajung pe V5/V6 → firmware ignoră → nimic nu se întâmplă.

3. **Hysteresis nu se poate seta din Blynk.** Aceeași cauză: V11/V12 sunt configurate în Blynk ca slidere pentru `Marja temperatura`/`Marja umiditate`, dar firmware-ul interpretează valorile ca override (1=ON). Lipsește handler BLYNK pentru hysteresis. Implementarea internă există deja (vezi [ESP32/AppPreferences.h:18-19](ESP32/AppPreferences.h#L18-L19) — `tempHyst`/`humHyst` salvate în NVS, [ESP32/VentilationZone.cpp](ESP32/VentilationZone.cpp) folosește hyst în `updateLogic`, [ESP32/MqttBridge.cpp:322-323](ESP32/MqttBridge.cpp#L322-L323) publică pe MQTT) — lipsește **doar puntea Blynk**.

În plus, „Threshold (Temp start vent / Umiditate start vent)" pe V7/V8 din Blynk pare că **funcționează** (BLYNK_WRITE există), dar user spune că nu merge. Posibil simptom secundar al lock-ului blocat sau pur și simplu confuzie cu sliderele V11/V12 care arată valori greșite. Va fi rezolvat colateral.

## Soluția

### A. Repunere pini virtuali Blynk (aliniere cu configurația actuală din consolă)

| Pin | Rol nou (firmware) | Tip Blynk | Observații |
|-----|---|---|---|
| V1-V4 | Senzori temp/hum (RO) | Double | Neschimbat |
| **V5** | **Toggle override stânga + display releu** | Integer 0/1 | RW: scriere = override toggle; citire = stare releu |
| **V6** | **Toggle override dreapta + display releu** | Integer 0/1 | RW: scriere = override toggle; citire = stare releu |
| V7 | Prag temperatură | Double | Neschimbat |
| V8 | Prag umiditate | Double | Neschimbat |
| V9 | Interval verificare | Integer | Neschimbat |
| V10 | Reset Defaults | Integer push | Neschimbat |
| **V11** | **Hysteresis temperatură (Marja)** | Double 0/10 | NOU — era override stânga |
| **V12** | **Hysteresis umiditate (Marja)** | Integer 0/20 | NOU — era override dreapta |
| V20 | Restart | Integer push | Neschimbat |
| V21 | Free heap KB | Integer RO | Neschimbat |
| V22 | Lock owner | Integer RO | Neschimbat |
| V23 | Firmware build | Integer RO | Neschimbat |

**Semantica nouă a override-ului V5/V6:**
- Toggle ON (1) → override forțat ON (releu pornește indiferent de senzor, expiră în 2h)
- Toggle OFF (0) → override clear (revine la control automat)
- Blynk afișează tot toggle-ul ca „pornit" când releul e ON (auto sau override) — vezi `pushRelayState()` care setează V5/V6 cu starea reală

Asta elimină modul cu 3 valori (1/0/2) și se aliniază cu UX-ul natural al toggle-ului.

### B. Modificări în firmware ESP32

#### 1. [ESP32/Config.h](ESP32/Config.h) — remap pini virtuali

```cpp
#define VP_RELAY_LEFT       V5    // RW: toggle override + display stare
#define VP_RELAY_RIGHT      V6    // RW: idem dreapta
// ELIMINĂ aliasurile vechi (păstrate pentru claritate, nu mai sunt folosite ca override pin):
// #define VP_OVERRIDE_LEFT  V11  ← șters
// #define VP_OVERRIDE_RIGHT V12  ← șters
#define VP_HYST_TEMP        V11   // NOU — hysteresis temperatură
#define VP_HYST_HUM         V12   // NOU — hysteresis umiditate
```

#### 2. [ESP32/ProiectVentilatie.ino](ESP32/ProiectVentilatie.ino) — handlere noi + remove vechi

**Șterge** `BLYNK_WRITE(VP_OVERRIDE_LEFT)` și `BLYNK_WRITE(VP_OVERRIDE_RIGHT)` (linii 502-533).

**Adaugă**:
```cpp
// Override stânga: toggle 0/1 pe V5
BLYNK_WRITE(VP_RELAY_LEFT) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_RELAY_LEFT, leftZone.getRelayState() ? 1 : 0);
        return;
    }
    mqtt.setLockOwner(LOCK_BLYNK);
    mqtt.requestPublishNow();
    int v = param.asInt();
    if (v == 1) {
        pending.overrideLeftSet = true;
        pending.overrideLeftVal = true;     // override ON
    } else {
        pending.overrideLeftClear = true;   // revenire auto
    }
}

BLYNK_WRITE(VP_RELAY_RIGHT) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_RELAY_RIGHT, rightZone.getRelayState() ? 1 : 0);
        return;
    }
    mqtt.setLockOwner(LOCK_BLYNK);
    mqtt.requestPublishNow();
    int v = param.asInt();
    if (v == 1) {
        pending.overrideRightSet = true;
        pending.overrideRightVal = true;
    } else {
        pending.overrideRightClear = true;
    }
}

BLYNK_WRITE(VP_HYST_TEMP) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_HYST_TEMP, prefs.tempHyst);
        return;
    }
    float v = param.asFloat();
    if (v >= MIN_TEMP_HYST && v <= MAX_TEMP_HYST) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        prefs.saveTempHyst(v);
        Serial.printf("[Blynk] Hysteresis temp: %.1f°C\n", v);
    }
}

BLYNK_WRITE(VP_HYST_HUM) {
    if (mqtt.getLockOwner() == LOCK_MQTT) {
        Blynk.virtualWrite(VP_HYST_HUM, prefs.humHyst);
        return;
    }
    float v = param.asFloat();
    if (v >= MIN_HUM_HYST && v <= MAX_HUM_HYST) {
        mqtt.setLockOwner(LOCK_BLYNK);
        mqtt.requestPublishNow();
        prefs.saveHumHyst(v);
        Serial.printf("[Blynk] Hysteresis hum: %.1f%%\n", v);
    }
}
```

**În `BLYNK_CONNECTED()` (linia 446)** — extinde sync inițial:
```cpp
Blynk.syncVirtual(VP_THRESH_TEMP, VP_THRESH_HUM, VP_INTERVAL,
                   VP_HYST_TEMP, VP_HYST_HUM, VP_RESET_DEFAULTS);
// trimite valorile curente hyst la conectare
Blynk.virtualWrite(VP_HYST_TEMP, prefs.tempHyst);
Blynk.virtualWrite(VP_HYST_HUM,  prefs.humHyst);
```

**În blocul `mp.setConfig` (linia 178-182)** — adaugă sync hyst la Blynk după primire MQTT:
```cpp
if (Blynk.connected()) {
    Blynk.virtualWrite(VP_THRESH_TEMP, prefs.tempThresh);
    Blynk.virtualWrite(VP_THRESH_HUM,  prefs.humThresh);
    Blynk.virtualWrite(VP_INTERVAL,    prefs.intervalSec);
    Blynk.virtualWrite(VP_HYST_TEMP,   prefs.tempHyst);   // NOU
    Blynk.virtualWrite(VP_HYST_HUM,    prefs.humHyst);    // NOU
}
```

**În blocul `mp.resetDefaults` MQTT (linia 187-202) și `pending.resetDefaults` Blynk (linia 277-293)** — extinde sync după reset + reset cache valori senzor pentru re-publicare imediată:
```cpp
if (Blynk.connected()) {
    Blynk.virtualWrite(VP_THRESH_TEMP, prefs.tempThresh);
    Blynk.virtualWrite(VP_THRESH_HUM,  prefs.humThresh);
    Blynk.virtualWrite(VP_INTERVAL,    prefs.intervalSec);
    Blynk.virtualWrite(VP_HYST_TEMP,   prefs.tempHyst);   // NOU
    Blynk.virtualWrite(VP_HYST_HUM,    prefs.humHyst);    // NOU
    Blynk.virtualWrite(VP_RESET_DEFAULTS, 0);
}
// Forțează re-trimiterea senzorilor la următorul ciclu (FIX: după reset Android nu mai vede valori)
lastSentTempL = -999.0f;
lastSentHumL  = -999.0f;
lastSentTempR = -999.0f;
lastSentHumR  = -999.0f;
```

#### 3. [ESP32/ProiectVentilatie.ino](ESP32/ProiectVentilatie.ino) — fix publicare state după reset

**Cauza root a problemei #1**: `processZones()` rulează în ordinea: pending Blynk → reset → `mqtt.requestPublishNow()` → DUPĂ care urmează `readSensor()` și `updateLogic()`. State-ul publicat conține senzori vechi (sau zero dacă tocmai s-a făcut boot).

**Fix**: mută al doilea apel `mqtt.requestPublishNow()` la sfârșitul `processZones()` dacă a fost vreun pending procesat (Blynk sau MQTT). Astfel state-ul publicat conține senzorii proaspăt citiți **plus** noile valori config.

```cpp
// La sfârșitul processZones(), DUPĂ pushRelayState():
if (blynkPendingProcessed || /* mqtt pending was processed */) {
    mqtt.requestPublishNow();   // republică state cu sensor data fresh
}
```

Cu o variabilă `mqttPendingProcessed` (similar cu `blynkPendingProcessed`) setată în blocul MQTT pending. Republicarea forțată după sensor read elimină și cazul în care MAUI deschis după reset vede stale data.

**Alternativ și mai robust** (recomandat): după `updateLogic()` (linia 349), dacă `blynkPendingProcessed || mqttPendingProcessed`, apelează `mqtt.requestPublishNow()`. Lock-ul deja a fost eliberat înainte.

### C. Modificări în Blynk Console (manual de către utilizator)

În tab-ul **Datastreams** al template-ului `TMPL42ximIY6M`:

1. **V5 — „Stare Vent Stanga"**: schimbă tipul în `Integer 0/1` cu **„Is Raw" UNCHECKED** și **„Is Reported" CHECKED** (deja așa). Asigură că are mod **bidirecțional** (default).
2. **V6 — „Stare Vent Dreapta"**: idem.
3. **V11 — „Marja temperatura"**: Double, min 0, max 10, default 2 (deja configurat).
4. **V12 — „Marja umiditate"**: Integer (sau Double), min 0, max 20, default 5 (deja configurat).

În **Web Dashboard / Mobile Dashboard**:
- Toggle-ul „Vent stanga" pe V5: tip Switch sau Stylized Switch, cu „On Value=1, Off Value=0".
- Toggle-ul „Vent dreapta" pe V6: idem.
- Slider „Marja temperatura" pe V11: pas 0.5.
- Slider „Marja umiditate" pe V12: pas 1.

(Dacă deja așa e configurat, nu trebuie schimbat nimic în consolă.)

### D. Verificare end-to-end

1. **Reset → senzori vizibili**: Apasă „Reset default" din Blynk → ESP32 logează `cmd:reset → defaults restaurate` → Blynk slidere V7/V8/V9/V11/V12 revin la valorile default → Android (MAUI) primește în <2s state nou cu senzori actuali (nu stale).
2. **Override din Blynk**: Toggle „Vent stanga" ON → releu pornește instant → Android afișează `LeftRelay=true LeftOverride=true` în <2s. Toggle OFF → revenire auto → Android afișează `LeftOverride=false`.
3. **Hysteresis din Blynk**: Mișcă slider „Marja temperatura" la 3°C → Blynk afișează 3.0°C → ESP32 logează `[Blynk] Hysteresis temp: 3.0°C` → următor state JSON pe MQTT include `cfg.hystT=3.0` → Android Settings page afișează 3.0°C în slider.
4. **Threshold din Blynk**: Mișcă „Temp start vent" la 50°C → ESP32 confirmă → Android afișează 50.0°C în Settings page.
5. **Bidirectional sync**: Modifică prag/hyst/override din Android → state nou → Blynk slidere/toggle reflectă schimbarea.
6. **Lock funcțional**: Click rapid în Blynk timp ce Android trimite o comandă → primești `cmd_rejected` event Blynk + UI Blynk revine la valoarea ESP32.
7. **Build**: `bash ESP32/scripts/bump_build.sh && arduino-cli compile ...` → 0 erori.

## De ce această abordare

- **Aliniere cu realitatea Blynk**: utilizatorul a reconfigurat deja consola — firmware-ul trebuie să se conformeze, nu invers (utilizatorul nu vrea să schimbe consola din nou).
- **Toggle 0/1 pe V5/V6 e UX standard**: toggle-uri „pornit/oprit" sunt mai intuitive decât 3-state (1/0/2). Pierdem capabilitatea de „force OFF override" (forțat oprit indiferent de senzor) — dar Android Dashboard oricum nu folosește această funcționalitate, doar ON/clear.
- **Reset cache `lastSentTemp/Hum`**: cea mai mică schimbare pentru a fixa „senzori dispar după reset" — alternativa ar fi să forțăm un publish complet la reset, dar cache reset acoperă atât Blynk (next cycle resends) cât și MQTT (republish trigger).
- **Republic MQTT post-readSensor**: garantează că orice pending procesat → state nou → sensor values proaspete în același mesaj.

## Risc

Mediu. Schimbăm semantica V5/V6 — dacă există clienți externi (alți developeri) care depind de modul read-only V5/V6, vor avea surpriză. În acest proiect single-user, fără risc.

Dependențe între pași: A→B sunt obligatorii împreună (firmware fără remap pini = handler-e nu se aleg). Reset cache (B fix #3) e independent și poate fi pus ca commit separat.

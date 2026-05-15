# Plan — Aliniere arhitectura ESP32 ↔ MAUI conform cerintelor

## Context

Cerintele user-ului (decizii arhitecturale):
1. **ESP32** = sursa de adevar pentru AUTOMATIZARE (praguri, histeresis, override, LED schedule salvate in NVS). Functioneaza independent de MAUI — confirmat (`VentilationZone::updateLogic()` ruleaza din `prefs`, fara dependinta MQTT).
2. **ESP32 publica MQTT** = DOAR senzori + system info (uptime/heap/fw/lock/slave-status/led intensity). Configul (praguri, hyst, interval, LED schedule) NU mai apare in publish-ul periodic.
3. **MAUI Settings UI** = sursa de adevar locala. Valorile raman la ce a setat user-ul, NU se suprascriu din MQTT.
4. **MAUI Dashboard** = afiseaza senzori real-time + relee + system info din MQTT state.
5. **Refresh button (Dashboard)** = trimite `cmd:refresh` la ESP32 + aplica LastState local imediat (zero flicker).
6. **Startup** = subscribe la `ventilatie/state` (retained) — primeste imediat ultimii senzori publicati. Nu e nevoie de comanda explicita la pornire.
7. **Niciodata MAUI nu trimite cmd config sau override automat.** Orice comanda care modifica state-ul ESP32 (setConfig, setLed, setLedSchedule, setOverride, reset, reboot, rebootSlave, update) se trimite EXCLUSIV la apasarea unui buton de catre user. Slidere/togle-uri ce modifica valori salveaza local in Preferences si seteaza `_dirty`; transmisia la ESP32 are loc doar pe Save. **Exceptie**: `cmd:refresh` (read-only, doar cere republish) — trimis la apasarea Refresh button din Dashboard sau System; respecta regula "user-triggered".

Problema actuala: `_publishStateNow()` din ESP32 include blocul `config` cu pragurile/hyst/interval + `led.schedEnabled`. MAUI `SettingsViewModel.OnStateReceived()` suprascrie UI-ul cu aceste valori, producand "revine la default" cand user-ul tocmai a editat ceva. Decuplam complet config-ul de stream-ul de stare.

---

## Fisiere de modificat

### ESP32 (Master)

**[ESP32/MqttBridge.cpp](ESP32/MqttBridge.cpp)** — `_publishStateNow()` (liniile 323–393):
- ELIMINA blocul `JsonObject cfg = doc["config"].to<JsonObject>()` cu `threshT, threshH, interval, ovrTimeout, hystT, hystH` (liniile 348–354)
- ELIMINA `led["schedEnabled"] = ledSchedEnabled;` (linia 365). PASTREAZA `led["intensity"]` (read-only display in MAUI Dashboard).
- Parametrul `bool ledSchedEnabled` din semnatura functiei: il pastram (apelantul tot il trimite, e simplu sa-l ignoram) sau il scoatem. Pastrarea = modificare minima.
- PASTREAZA: `left{temp,hum,relay,override,errs}`, `right{temp,hum,relay,override,errs,failsafe}`, `slave{online,errors,lastSeen}`, `led{intensity}`, `lock{owner,ageMs}` (omis cand fara lock), `fw`, `uptimeSec`, `heap`.

`publishStateIfNeeded()` (linia 285) si conditiile de publish (heartbeat / relayChanged / `_publishNow`) raman neschimbate.
Topicurile `/diag`, `/event`, `/log` raman neschimbate.
Toate comenzile MQTT acceptate (setOverride, setConfig, setLed, setLedSchedule, refresh, reset, reboot, rebootSlave, getLog) raman functionale — ESP32 continua sa execute + sa salveze in NVS.

### MAUI

**[MobileApp/Models/VentilationState.cs](MobileApp/Models/VentilationState.cs)**:
- `Config` proprietate (linia 17–18) → marcata cu `[JsonIgnore]` SAU stergi `ConfigState` din model. Aleg `[JsonIgnore]` pentru a evita ripple-effect in alte fisiere care eventual mai referentiaza tipul (verificare: doar SettingsViewModel.cs il foloseste).
- `LedState.SchedEnabled` (linia 92–93) → ramane pentru deserializare safe in cazul in care firmware vechi inca publica (default false). Nu mai e citita.

**[MobileApp/ViewModels/SettingsViewModel.cs](MobileApp/ViewModels/SettingsViewModel.cs)** — refactor:

**Eliminari (decuplare de MQTT state):**
- ELIMINA `_mqttService.OnStateReceived += OnStateReceived;` (linia 54)
- ELIMINA blocul `if (_mqttService.LastState != null) OnStateReceived(_mqttService.LastState);` (liniile 75–76)
- ELIMINA metoda `OnStateReceived(VentilationState state)` (liniile 79–114) complet
- ELIMINA campurile: `_lastReceivedConfig` (linia 11), `_lastReceivedLedIntensity` (linia 22), `_lastReceivedSchedEnabled` (linia 29)
- ELIMINA `_mqttService.OnStateReceived -= OnStateReceived;` din `Dispose()` (linia 290)

**Decuplare auto-send (LED intensity):**
- ELIMINA campul `_ledDebounceCts` (linia 130)
- `OnLedIntensityChanged()` (liniile 132–156) → rescris simplu:
  ```csharp
  partial void OnLedIntensityChanged(int value)
  {
      Preferences.Set(PrefLedIntensity, value);   // persist local imediat (acelasi pattern ca celelalte handlere LED)
      _ledIntensityDirty = true;
      RecomputeHasChanges();
  }
  ```
  **NU mai trimite `cmd:setLed` automat** — devine parte din Save.

**Dirty flags (inlocuiesc comparatia cu _lastReceivedConfig):**
- Introduce `private bool _threshDirty;` (pentru praguri+hyst+interval)
- Introduce `private bool _ledIntensityDirty;` (pentru sliderul LED intensity)
- Pastreaza `_ledScheduleDirty` (deja exista, linia 30) — pentru onTime/offTime/maxI/enabled
- Handler-ele praguri (liniile 123–127) — actualmente expression-bodied (`=> RecomputeHasChanges();`), trebuie convertite la block-body:
  ```csharp
  partial void OnTempThresholdChanged(float value)  { _threshDirty = true; RecomputeHasChanges(); }
  partial void OnHumThresholdChanged(float value)   { _threshDirty = true; RecomputeHasChanges(); }
  partial void OnIntervalSecChanged(int value)      { _threshDirty = true; RecomputeHasChanges(); }
  partial void OnTempHysteresisChanged(float value) { _threshDirty = true; RecomputeHasChanges(); }
  partial void OnHumHysteresisChanged(float value)  { _threshDirty = true; RecomputeHasChanges(); }
  ```
  Note intentional: persistenta Preferences pentru praguri ramane in `SaveAsync` (NU se salveaza in handler) — pragurile sunt commit-on-save. Slidere/togle-uri LED (intensity + onTime/offTime/maxI) salveaza imediat in Preferences pentru a pastra pozitia sliderului la navigare; ESP32 le primeste tot la Save.
- Handler-ele LED schedule (liniile 158–185) → ramane `_ledScheduleDirty = true;` (deja face asta)

**RecomputeHasChanges() (liniile 187–202) — simplificare:**
```csharp
private void RecomputeHasChanges()
{
    HasChanges = _threshDirty || _ledIntensityDirty || _ledScheduleDirty;
    SaveCommand.NotifyCanExecuteChanged();
}
```

**SaveAsync() (liniile 221–267) — rescris cu trimitere conditionata:**
```csharp
[RelayCommand(CanExecute = nameof(CanSave))]
private async Task SaveAsync()
{
    StatusMessage = "Trimit...";
    StatusColor = Colors.Orange;

    if (_threshDirty)
    {
        Preferences.Set(PrefThreshT,  TempThreshold);
        Preferences.Set(PrefThreshH,  HumThreshold);
        Preferences.Set(PrefInterval, IntervalSec);
        Preferences.Set(PrefHystT,    TempHysteresis);
        Preferences.Set(PrefHystH,    HumHysteresis);
        await _mqttService.SendCommandAsync(new {
            cmd = "setConfig", threshT = TempThreshold, threshH = HumThreshold,
            interval = IntervalSec, hystT = TempHysteresis, hystH = HumHysteresis
        });
        _threshDirty = false;
    }

    if (_ledScheduleDirty)
    {
        await _mqttService.SendCommandAsync(new {
            cmd = "setLedSchedule",
            onH = LedOnTime.Hours, onM = LedOnTime.Minutes,
            offH = LedOffTime.Hours, offM = LedOffTime.Minutes,
            maxI = LedMaxIntensity, enabled = LedScheduleEnabled
        });
        _ledScheduleDirty = false;
    }

    if (_ledIntensityDirty)
    {
        await _mqttService.SendCommandAsync(new { cmd = "setLed", percent = LedIntensity });
        _ledIntensityDirty = false;
    }

    RecomputeHasChanges();
    StatusMessage = "✓ Trimis";
    StatusColor = Colors.LimeGreen;
}
```

**ResetDefaultsAsync() (liniile 269–286):**
- Confirmat din `AppPreferences::resetToDefaults()` (ESP32) — reseteaza DOAR tempThresh/humThresh/tempHyst/humHyst/intervalSec + override. LED schedule e stocat separat (`LedConfigStorage`) si NU se reseteaza.
- Dupa `cmd:reset`, scope-ul reset in MAUI = doar threshold-uri:
  - Reseteaza Preferences `PrefThreshT, PrefThreshH, PrefInterval, PrefHystT, PrefHystH`
  - Reseteaza UI props: `TempThreshold=45, HumThreshold=60, IntervalSec=300, TempHysteresis=2, HumHysteresis=5`
  - Reseteaza `_threshDirty = false` (e in sync cu ESP32-NVS dupa reset)
  - LED schedule + intensity raman neatinse — sunt independente de `cmd:reset`

**Simplificare IsLocked (Blynk eliminat):**
- Campul `IsLocked` (linia 46) si `[ObservableProperty] private bool _isLocked;` — Blynk a fost eliminat din ESP32 (vezi `MqttBridge.cpp:171` "doar LOCK_MQTT acum (LOCK_BLYNK eliminat)"). MQTT lock-ul setat de ESP32 e doar pentru display Dashboard, NU pentru blocare comenzi.
- Optiune A (minimal): pastreaza campul dar `IsLocked = false` mereu (nimic nu il mai seteaza dupa eliminarea OnStateReceived). `CanSave() => HasChanges && IsConnected` (drop `!IsLocked`).
- Optiune B (curat): elimina complet `IsLocked` din ViewModel + binding XAML, simplifica `CanSave()`.

**[MobileApp/ViewModels/DashboardViewModel.cs](MobileApp/ViewModels/DashboardViewModel.cs)** — `Refresh()` (linii curente cu doar LastState):
- Modifica metoda Refresh inapoi la `async Task RefreshAsync()`:
  ```csharp
  [RelayCommand]
  private async Task RefreshAsync()
  {
      if (_mqttService.LastState != null)
          UpdateState(_mqttService.LastState);    // zero-flicker imediat din cache
      await _mqttService.SendCommandAsync(new { cmd = "refresh" }); // fresh publish ESP32
  }
  ```
- Acum e safe: Settings nu mai e abonat la state, deci publish-ul triggered de refresh NU mai afecteaza UI-ul Settings.

**[MobileApp/ViewModels/SystemViewModel.cs](MobileApp/ViewModels/SystemViewModel.cs)** — **NU necesita modificari**:
- `OnStateReceived` (liniile 112–123) citeste doar `Fw, UptimeSec, Heap, Left.Errs, Right.Errs` — toate sunt sensor/system, raman in noul publish.
- `RefreshAsync` (linia 235) trimite deja `cmd:refresh` — ramane functional.
- `ResetDefaultsAsync` (linia 241) — optional: la fel ca in SettingsViewModel, poate reseta si Preferences-urile MAUI dupa `cmd:reset` (consistency). Recomandat dar nu obligatoriu (Settings page e sursa primara).

---

## Comportament rezultat

| Scenariu | Curent | Dupa modificare |
|---|---|---|
| User editeaza prag T in Settings, soseste state ESP32 | UI revine la valoarea ESP32 | UI ramane la valoarea user-ului |
| Refresh in Dashboard | Doar LastState aplicat (no round-trip) | LastState aplicat + `cmd:refresh` la ESP32 → publish fresh; Settings neafectat |
| User reinstaleaza MAUI | Settings UI populat din state ESP32 | Settings UI = default-uri din cod; user re-seteaza manual; automatizarea ESP32 continua nederanjata |
| App pornit, ESP32 online | Retained state → Settings + Dashboard populate | Retained state → DOAR Dashboard populat (Settings = Preferences local) |
| User: Settings → schimba interval → Save | `setConfig` trimis, asteapta echo pentru "confirmare" | `setConfig` trimis (la Save), UI afiseaza imediat "✓ Trimis" (QoS1 = livrat la broker) |
| User misca slider LED intensity in Settings | `setLed` trimis automat dupa 300ms debounce (FARA buton) | NIMIC trimis. Valoarea salvata in Preferences local; setata `_dirty=true`. ESP32 primeste comanda doar la Save. |
| Override din Dashboard (toggle button) | Trimite `setOverride`, ESP32 reflecta in state | Identic — buton apasat de user, comanda trimisa. Override e relay-state, ramane in publish. |
| User: Settings → Reset Defaults | `cmd:reset` trimis, ESP32-NVS resetat (doar thresholds), MAUI-Preferences raman cu valori vechi | `cmd:reset` trimis + MAUI-Preferences reseteaza scope-ul corespunzator (doar thresholds, NU LED schedule) |
| Reboot ESP32 (curent oprit) | Pragurile persista in NVS, MAUI primeste config in state si arata aceleasi valori | Pragurile persista in NVS (automatizare continua), MAUI nu mai afla via state — dar oricum are valorile in Preferences local |

---

## Verificare end-to-end

1. **Build ESP32**: `cd ESP32 && ./build.sh` — fara erori, mesajul "[MQTT] State published (X bytes...)" arata X mai mic decat inainte (lipseste config block)
2. **Build MAUI**: `cd MobileApp && dotnet build -f net10.0-android -c Debug` — 0 errors
3. **Verificare JSON publicat** (optional, via mosquitto_sub sau MQTT Explorer pe `ventilatie/state`): payload-ul NU contine cheia `"config"` si nici `"led":{"schedEnabled":...}`
4. **Test scenarii**:
   - Conecteaza telefon → vezi senzori live in Dashboard (din retained state)
   - Settings: schimba T threshold 45 → 50 → Save → revin pe Dashboard → revin pe Settings: **50 e pastrat** (anterior: revenea la 45)
   - Refresh in Dashboard: senzori se updateaza (publish fresh ESP32), Settings neafectata
   - Inchide app, redeschide: Settings UI arata 50 (din Preferences), Dashboard primeste retained state cu senzori actuali
   - Reboot ESP32 (de la curent): la repornire, pragul 50 persista in NVS — verifica via Serial monitor (`[Prefs] tempThresh=50.0`)
   - Settings → Reset Defaults → confirma → Settings UI revine la 45/60/300, ESP32 publica state cu relay-state actualizat (config nu mai apare)
5. **Regresie**:
   - Override left/right din Dashboard merge in continuare (override face parte din relay state)
   - LED slider in Settings: misca-l fara sa apesi Save → MQTT inspect (mosquitto_sub pe `ventilatie/cmd`) NU arata `setLed` → apasi Save → `setLed` apare; valoarea se aplica pe slave (LED-ul fizic se schimba)
   - System page → Reboot, Reboot Slave, OTA — toate trimit comenzile corecte (la buton)
   - Senzorii continua sa fie actualizati in Dashboard la intervalul setat in Settings (default 300s) sau la apasarea Refresh
6. **Verificare "no auto-send"**: in Settings, modifica fiecare parametru (T threshold, H threshold, interval, hystT, hystH, LED intensity, LED schedule on/off/maxI, schedule enabled), monitorizeaza `ventilatie/cmd` cu mosquitto_sub. Niciun mesaj nu trebuie sa apara pana la apasarea Save. Apoi Save → exact 1× `setConfig` + (daca aplicabil) 1× `setLedSchedule` + (daca aplicabil) 1× `setLed`.

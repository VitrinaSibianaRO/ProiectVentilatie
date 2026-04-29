# Redesign UI: Industrial Cyberpunk — MAUI Android

Transformarea vizuală completă a aplicației MAUI pentru a reproduce prototipul React din `/VS/`.
**Funcționalitatea (ViewModels, MqttService, comenzi) rămâne neatinsă.**

---

## Design Reference

Prototipul de referință (`VS/Industrial Dashboard.html` + `VS/app-components.jsx` + `VS/app-pages.jsx`) definește:

| Element | Specificație |
|---|---|
| **Paleta** | Fundal `#050a14`, surface `rgba(0,18,46,0.93)`, accent cyan `#00e6ff`, verde `#00e87a`, portocaliu `#ff7a45`, roșu `#ff4422` |
| **Tipografie** | `Rajdhani` (titluri, etichete) + `Share Tech Mono` (valori numerice) |
| **Design Pattern** | Deep Dark + Glassmorphism + Glow Effects + micro-animații |
| **Ecran țintă** | 375×820 dp (mobil portrait) |

---

## Decizii User Review

> [!IMPORTANT]
> **Fonturi custom**: Utilizatorul a aprobat descărcarea și includerea fonturilor `Rajdhani` și `Share Tech Mono`. Acestea vor fi descărcate automat.

> [!IMPORTANT]
> **SkiaSharp**: Utilizatorul a aprobat adăugarea pachetului `SkiaSharp.Views.Maui.Controls` pentru animații avansate (HexGrid, Scanline, Fan spinning).

> [!IMPORTANT]
> **Pagina "Devices"**: Conținutul va fi mapat în paginile existente (**System** și **Dashboard**), fără a adăuga un tab nou.

---

## Proposed Changes

### Faza 1 — Design System & Fonturi

#### [MODIFY] App.xaml
- Înlocuiește toate culorile cu paleta cyberpunk.
- Adaugă stiluri globale pentru `Button`, `Label`, `Frame` (border cyan glow, fundal glassmorphism).
- Înregistrează fonturile `Rajdhani-Bold`, `Rajdhani-SemiBold`, `ShareTechMono-Regular`.

#### [MODIFY] ProiectVentilatie.Mobile.csproj
- Adaugă `<MauiFont Include="Resources\Fonts\*" />` pentru fonturile noi.
- Adaugă NuGet `SkiaSharp.Views.Maui.Controls` (dacă se aprobă mai sus).

#### [NEW] Resources/Fonts/
- `Rajdhani-SemiBold.ttf`
- `Rajdhani-Bold.ttf`
- `ShareTechMono-Regular.ttf`

---

### Faza 2 — Custom Controls (SkiaSharp)

#### [MODIFY] Controls/GaugeView.cs
Înlocuiește `GaugeDrawable` actual cu un design care reproduce `SliderRow` din prototip:
- **Track** mai subțire, cu gradient colorat (cyan → verde → portocaliu → roșu) bazat pe valoare.
- **Knob** cu border alb și glow color.
- **Label valoare** cu font `ShareTechMono`.
- **Badge status** (ex: "Ideal", "Ridicat") colorat dinamic.

#### [NEW] Controls/ThermometerView.cs *(SkiaSharp)*
SVG-style thermometer din `ThermometerSVG` (JS):
- Gradient vertical multicolor (albastru → cyan → verde → galben → roșu).
- Clippath animat pentru nivel.
- Bulb colorat la bază.

#### [NEW] Controls/DropletView.cs *(SkiaSharp)*
Droplet SVG din `DropletSVG` (JS):
- Formă de picătură cu umplere bazată pe `Humidity` (0–100).
- Gradient dinamic colorat (portocaliu uscat → verde ideal → mov umed).

#### [NEW] Controls/MiniFanView.cs *(SkiaSharp)*
Fan SVG din `MiniFanCard` (JS):
- 5 pale eliptice rotite la 72° una față de cealaltă.
- Rotație continuă când `IsOn = true` (animată cu `AnimationExtensions`).
- Border cyan cu glow când activ.

#### [NEW] Controls/HexGridView.cs *(SkiaSharp — opțional)*
Fundal decorativ hexagonal (36 hexagoane cu opacitate variabilă, fără interactivitate).

#### [NEW] Controls/ScanLineView.cs *(opțional)*
Linie animată care traversează ecranul de sus în jos (efect CRT).

---

### Faza 3 — AppShell & Navigare

#### [MODIFY] AppShell.xaml
- Tab bar reproiectat: fundal `#00081c`, border cyan subtle în vârf.
- Iconițe SVG inline (înlocuiesc PNG-urile generate anterior) cu accent cyan când activ.
- Indicator de linie activă animat deasupra iconului activ (reproducere `NavItem` din JS).
- Titlul Shell-ului ascuns (titlul e afișat în pagini, nu în shell).
- **Mapare tab-uri**:
  - `Dashboard` → `DashboardPage`
  - `Devices` → `SystemPage` (secțiunea Device Info) **sau** tab nou `DevicesPage`
  - `Settings` → `SettingsPage`
  - `Reports` → `LogPage`
  - `System` → `SystemPage` (OTA + Reboot)

#### [NEW] Controls/GlowTabBar.cs *(custom renderer opțional)*
Alternativă: custom `Shell.TabBar` renderer pentru Android care desenează indicatorul de linie animat.

---

### Faza 4 — Redesign Pagini

#### [MODIFY] Views/DashboardPage.xaml
Reproducere `PageDashboard` + `ZoneCard` din JS:

**Header area (sus)**:
- Badge Online/Offline cu inel pulsant (via `Animation` API sau SkiaSharp).
- Uptime counter în format `Xh XXm XXs`.
- Buton `REFRESH` cu iconița ↻ rotitoare când se execută.
- Lock banner (`LockBanner` din JS): fundal portocaliu `#ffaa00`, icon lacăt SVG.

**Zona Stânga / Zona Dreapta** (side-by-side):
Fiecare `ZoneCard` conține:
- **Corner brackets** decorative (4 borduri unghi în colțuri).
- Termometru vertical (`ThermometerView`) + valoare temp cu badge dinamic (Rece/Ideal/Cald/etc).
- Picătură umiditate (`DropletView`) + valoare % cu badge dinamic.
- `MiniFanView` (spinning când activ) + badge ON/OFF/Override.
- Buton override cu stare vizuală schimbată (Manual override vs Auto).

**Bindings păstrate** (fără modificări ViewModel):
- `LeftTemp`, `RightTemp`, `LeftHum`, `RightHum`
- `RelayLeftText`, `RelayRightText`
- `ToggleLeftCommand`, `ToggleRightCommand`
- `IsControlEnabled`, `LockBannerVisible`, `LockBannerText`
- `OnlineText`, `OnlineBadgeColor`, `LastUpdateText`

#### [MODIFY] Views/SettingsPage.xaml
Reproducere `PageSettings` + `SliderRow` din JS:

**Secțiunea "Praguri declanșare"** (accent portocaliu `#ff7a45`):
- `SliderRow` custom cu track gradient, knob cu glow, valoare numerică deasupra în font mono.
- Prag Temperatură (`TempThreshold`) cu descriere "Releul pornește când T ≥ prag".
- Prag Umiditate (`HumThreshold`) cu descriere similară.

**Secțiunea "Timing"** (accent cyan `#00e6ff`):
- Interval citire senzori (`IntervalSec`) cu slider.
- **Preset buttons** (10s, 1m, 5m, 15m, 1h) — butonașe mici sub slider.
- Timeout override (`OvrTimeout`) cu accent portocaliu `#ffaa44`.

**Butoane acțiuni**:
- `TRIMITE CONFIG` (principal, cyan) → `SaveCommand`.
- `RESET` (secundar, roșu cu confirmare dublu-tap) → `ResetDefaultsCommand`.
- Banner status (`StatusMessage`) colorat dinamic.

**Bindings păstrate**: `TempThreshold`, `HumThreshold`, `IntervalSec`, `SaveCommand`, `ResetDefaultsCommand`, `StatusMessage`, `IsLocked`.

> [!NOTE]
> `OvrTimeout` nu există în `SettingsViewModel` actual. Îl adăugăm ca proprietate nouă (local, fără afectare MQTT) sau îl omitem?

#### [MODIFY] Views/SystemPage.xaml
Reproducere `PageSystem` din JS:

**Info card**:
- `InfoRow`-uri: Firmware Build, Uptime, Heap liber (cu bară de progres colorată).
- Avertisment heap critic vizibil când heap < 30KB.

**Butoane**:
- `REBOOT ESP32`: cu confirmare dublu-tap (prima apăsare → "SIGUR REBOOT?", a doua execută).
- `RESET DEFAULT`: idem pattern dublu-tap.

**OTA Update card** (neschimbat funcțional):
- Câmpuri Entry stilizate cu border mono + placeholder.
- URL final compus afișat live.
- Progress bar cu gradient cyan animat.
- Buton `TRIMITE UPDATE`.

**Bindings păstrate**: Toate din `SystemViewModel` actual (BrokerHost, EspStatusText, OtaRepoUrl, etc).

#### [MODIFY] Views/LogPage.xaml
Reproducere `PageReports` din JS:

**Toolbar**:
- Buton `FETCH LOG` cu iconița ↓ (sau spinner când `loadingLog`).
- Counter intrări (ex: "10 intrări").

**Filter tabs** (4 butoane mici):
- Toate / Releu / Senzor / Override — stilizate cyberpunk, activ cu accent cyan.

**Log list**:
- Fiecare intrare: icon emoji în box colorat + tip + timestamp mono + zona (badge cyan) + mesaj.
- Animație `fadeInUp` la apariție.
- Empty state cu emoji 📋 și text descriptiv.

**Bindings păstrate**: `Entries`, `SelectedFilter`, `AvailableFilters`, `ReloadCommand`, `StatusMessage`.

---

### Faza 5 — Toast Notification System

#### [NEW] Controls/ToastOverlay.cs + ToastService.cs
Sistem de notificări overlay (top of screen) similar cu `Toast` din JS:
- Afișare mesaj cu fundal glassmorphism cyan sau roșu (eroare).
- Auto-dismiss după 2.5 secunde.
- Max 3 toast-uri simultan (stack vertical).
- Integrat în `App.xaml.cs` ca overlay global.

Înlocuiește mesajele `DisplayAlert` actuale cu `ToastService.Show()`.

---

## Verificare Plan

### Ce NU se modifică
- `MqttService.cs` — zero modificări.
- `IMqttService.cs` — zero modificări.
- `*ViewModel.cs` (toate 4) — zero modificări, cu excepția posibilei adăugări de `OvrTimeout`.
- `Models/` — zero modificări.
- `appsettings.json` — zero modificări.
- `MauiProgram.cs` — zero modificări (maxim înregistrare `ToastService`).

### Automated Verification
```
dotnet build -f net10.0-android -c Debug
```
Fără erori de compilare după fiecare fază.

### Manual Verification
1. **Dashboard**: Badge Online pulsant, ZoneCard cu termometre și ventilatoare, buton toggle override funcțional.
2. **Settings**: Slidere cu preset-uri, Save trimite comanda MQTT corect.
3. **System**: Heap bar, Reboot cu confirmare, OTA cu progress.
4. **Log**: Filter tabs, fetch log, animație intrări.
5. **Toast**: Apare la orice acțiune MQTT, dispare automat.

---

## Estimare Efort

| Fază | Complexitate | Timp estimat |
|---|---|---|
| 1 — Design System | Scăzut | ~30 min |
| 2 — Custom Controls | Ridicat | ~3–4h |
| 3 — AppShell | Mediu | ~1h |
| 4 — Redesign Pagini | Mediu | ~2–3h |
| 5 — Toast System | Scăzut | ~30 min |
| **Total** | | **~7–9h** |

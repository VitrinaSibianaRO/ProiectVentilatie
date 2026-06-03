# Plan: ProiectVentilatie.Web — Blazor WASM PWA pe Vercel

## Obiectiv & principiu

Al doilea **UI** peste același creier, nu a doua aplicație. Extragem logica C# într-o bibliotecă partajată (`Ventilatie.Core`) referită de MAUI **și** de Blazor. Firmware-ul ESP32 + contractul MQTT JSON rămân neatinse — sunt „backend-ul".

---

## 1. Structura soluției

```
ProiectVentilatie.sln
├── Ventilatie.Core/              (NOU — class library net10.0)
│   ├── Models/                   ← mutate din MobileApp
│   ├── ViewModels/               ← mutate din MobileApp
│   ├── Services/                 ← interfețe + logică agnostică
│   └── Abstractions/             ← ISettingsStore, IDialogService (NOI)
├── ProiectVentilatie.Mobile/     (EXISTENT — referă Core)
│   └── Platforms/...             ← impl MAUI: Preferences, HiveMqtt, LibVLC
└── ProiectVentilatie.Web/        (NOU — Blazor WASM)
    ├── wwwroot/ (manifest, sw, css, js interop)
    ├── Pages/ Components/         ← UI Razor
    ├── Platform/                  ← impl web: localStorage, mqtt.js, IMOU-proxy client
    └── api/                       ← funcții serverless TS (Vercel)
```

`/api/*.ts` stă la rădăcina deploy-ului Vercel (detectate automat ca serverless functions); Blazor publicat devine root-ul static.

---

## 2. Ventilatie.Core — ce mutăm + refactor

**Mutare directă (zero modificări de logică):**
- `Models/*` — VentilationState, TvState, LedState, MqttSettings, LedPattern, ImouCredentials/TokenCache
- `ViewModels/*` — `CommunityToolkit.Mvvm` (`[ObservableProperty]`/`[RelayCommand]`) rulează identic în WASM

**Namespace:** redenumire `ProiectVentilatie.Mobile.*` → `Ventilatie.Core.*` (find/replace o singură dată).

**MAUI-isme de scrubuit din ViewModels** (înlocuite cu abstracții injectate):

| MAUI-ism actual | Abstracție în Core | Impl MAUI | Impl Web |
|---|---|---|---|
| `Preferences.Get/Set` | `ISettingsStore` | wrap `Preferences` | `localStorage` (cache in-memory) |
| `Application.Current...DisplayAlertAsync` | `IDialogService.ConfirmAsync` | `DisplayAlert` | JS `confirm()` / modal |
| `Color`/`Colors` (`StatusColor`) | `string` hex sau enum status | → MAUI Color | → CSS class |
| `MainThread.BeginInvoke...` | rămâne **doar** în service-urile de platformă, nu în VM | — | `InvokeAsync(StateHasChanged)` |

**Notă `ISettingsStore`:** `Preferences` e sincron, `localStorage` în Blazor e async (JS interop). `WebSettingsStore` încarcă tot la init într-un dicționar in-memory → citiri sincrone din cache, scrieri write-through async. Astfel setterii ViewModel (`OnMorseTextChanged` etc.) rămân neschimbați.

---

## 3. ProiectVentilatie.Web — implementări de platformă

### `WebMqttService : IMqttService`
- JS interop peste **mqtt.js** (`wwwroot/js/mqtt-interop.js`)
- Conexiune: `wss://<cluster>.hivemq.cloud:8884/mqtt` (WSS, nu TCP 8883)
- Callback mesaje → .NET via `DotNetObjectReference`
- Aceleași topicuri și routing ca în `MqttService.cs`
- Marshaling UI: `InvokeAsync(StateHasChanged)` la nivel de componentă

### `WebSettingsStore : ISettingsStore`
- `Blazored.LocalStorage` + cache in-memory
- Init async la startup → citiri sincrone din cache

### `WebImouService : IImouCloudService`
- Client HTTP subțire spre `/api/imou/*` (same-origin → fără CORS)
- Nu semnează nimic, nu are secret — toată logica e în funcția TS

---

## 4. Proxy IMOU pe Vercel — funcția TS (camere must-have)

Rezolvă: **CORS** (apel same-origin) + secretul rămâne în env-ul funcției, niciodată în bundle.

```
/api/imou/[op].ts        (Node runtime, node:crypto pentru MD5)
  env: IMOU_APP_ID, IMOU_APP_SECRET, IMOU_REGION
```

**Logica de semnare** (replică `BuildRequest` din `ImouCloudService.cs`):
```
sign = MD5("time:{time},nonce:{nonce},appSecret:{secret}")
body = { system: { ver, sign, appId, time, nonce }, params, id }
```

**Endpoint-uri minime:**

| Endpoint | Rol |
|---|---|
| `POST /api/imou/token` | semnează `accessToken` → întoarce token clientului |
| `POST /api/imou/devices` | listă device-uri (cu token din client) |
| `POST /api/imou/live` | `bindDeviceLive` → `streams[0].hls` |

**Streaming:** URL HLS → **hls.js** + `<video>` (Safari/iOS redă HLS nativ).
Componentă: `HlsPlayer.razor` + `wwwroot/js/hls-interop.js`.

---

## 5. UI Razor — pagini, componente, temă

| MAUI (XAML) | Web (Razor) | Note |
|---|---|---|
| AppShell + TabBar | `MainLayout.razor` + `FloatingNavBar.razor` | `@page` routing |
| DashboardPage | `Dashboard.razor` | |
| SettingsPage (10 secțiuni LED) | `Settings.razor` | reutilizează `SettingsViewModel` din Core |
| CamerasPage / CameraSettings / CameraFullscreen | `Cameras.razor` etc. | + `HlsPlayer.razor` |
| TvPage | `Tv.razor` | |
| SystemPage | `System.razor` | |
| `CollapsibleSection.xaml` | `CollapsibleSection.razor` | `<details>` + CSS |
| `ScanLineView` (SkiaSharp) | **overlay CSS** (animație) | scanline trivial în CSS, fără Skia |
| Converters | metode C# / logică Razor inline | |
| Tema XAML (culori, fonturi) | **CSS** + Google Fonts | Rajdhani + ShareTech sunt Google Fonts |

**Legare VM↔Razor:** injectezi VM-ul, te abonezi la `PropertyChanged` → `InvokeAsync(StateHasChanged)`; `@bind-Value="VM.MorseText"` pentru input-uri.

---

## 6. PWA

Template Blazor WASM `--pwa` generează `manifest.json` + service worker (Workbox).

- `manifest.json`: `name`, `theme_color: #009688`, `display: standalone`, iconuri
- Service worker cache-uiește app shell + `_framework/*` → offline + load instant la revizitare
- iOS: `apple-touch-icon`, meta `apple-mobile-web-app-capable`

---

## 7. Vercel — deploy

### `vercel.json`
```json
{
  "rewrites": [
    { "source": "/api/(.*)", "destination": "/api/$1" },
    { "source": "/_framework/(.*)", "destination": "/_framework/$1" },
    { "source": "/(.*)", "destination": "/index.html" }
  ],
  "headers": [
    {
      "source": "/_framework/(.*)",
      "headers": [
        { "key": "Cache-Control", "value": "public, max-age=31536000, immutable" }
      ]
    },
    {
      "source": "/(.*)\\.wasm",
      "headers": [
        { "key": "Content-Type", "value": "application/wasm" }
      ]
    }
  ]
}
```

### Pipeline (GitHub Action — recomandat)
Imaginea de build Vercel nu are .NET SDK → buildăm în Action, deploy static cu `--prebuilt`:

```yaml
- name: Publish Blazor
  run: dotnet publish ProiectVentilatie.Web -c Release -o ./publish

- name: Deploy to Vercel
  run: vercel deploy --prebuilt ./publish/wwwroot
  env:
    VERCEL_TOKEN: ${{ secrets.VERCEL_TOKEN }}
```

Funcțiile `/api/*.ts` sunt detectate și deploy-ate automat.

---

## 8. Fișiere — sumar

| Locație | Acțiune |
|---|---|
| `Ventilatie.Core/Models/*` | **Mutate** din MobileApp |
| `Ventilatie.Core/ViewModels/*` | **Mutate** din MobileApp |
| `Ventilatie.Core/Abstractions/ISettingsStore.cs` | **NOU** |
| `Ventilatie.Core/Abstractions/IDialogService.cs` | **NOU** |
| `ProiectVentilatie.Mobile/*.csproj` | **Atins** — referință la Core + impl `MauiSettingsStore`/`MauiDialogService` |
| `ProiectVentilatie.Web/Program.cs` | **NOU** — DI wiring |
| `ProiectVentilatie.Web/Platform/WebMqttService.cs` | **NOU** |
| `ProiectVentilatie.Web/Platform/WebSettingsStore.cs` | **NOU** |
| `ProiectVentilatie.Web/Platform/WebImouService.cs` | **NOU** |
| `ProiectVentilatie.Web/Pages/*.razor` | **NOI** (×7 pagini) |
| `ProiectVentilatie.Web/Components/*.razor` | **NOI** (NavBar, Collapsible, HlsPlayer) |
| `ProiectVentilatie.Web/wwwroot/js/mqtt-interop.js` | **NOU** |
| `ProiectVentilatie.Web/wwwroot/js/hls-interop.js` | **NOU** |
| `ProiectVentilatie.Web/wwwroot/manifest.json` | **NOU** |
| `ProiectVentilatie.Web/wwwroot/css/app.css` | **NOU** |
| `api/imou/[op].ts` | **NOU** (serverless Vercel) |
| `vercel.json` | **NOU** |

---

## 9. Verificare end-to-end

1. **Local Blazor**: `dotnet watch` în Web → conectare live HiveMQ WSS → `ventilatie/state` se actualizează în Dashboard; Save în Settings → comanda apare pe MQTT Explorer
2. **Proxy local**: `vercel dev` rulează `/api` + static → testezi token + listă camere + HLS
3. **Camere**: stream HLS se redă în `<video>` pe desktop și mobil
4. **PWA**: deploy preview Vercel → „Add to Home Screen" pe Android Chrome + iOS Safari; load instant la a doua deschidere (service worker)
5. **Paritate funcțională**: aceeași comandă (ex. text Morse, control TV) din web și din MAUI → același efect pe ESP32

---

## 10. Faze de execuție

| Fază | Conținut | Validare |
|---|---|---|
| **1** | Extragere `Ventilatie.Core`, abstracții `ISettingsStore`/`IDialogService`, MAUI recompilează din Core | MAUI compilează și rulează identic |
| **2** | Schelet `ProiectVentilatie.Web` + `WebMqttService` + pagina Dashboard | Dashboard afișează state live MQTT-WS |
| **3** | Proxy IMOU TS (`/api/imou`) + `HlsPlayer.razor` | Stream cameră în browser |
| **4** | Rescriere UI Razor + temă CSS (restul paginilor) | Paritate vizuală + funcțională cu MAUI |
| **5** | PWA (`manifest.json` + SW) + pipeline Vercel (GitHub Action) | PWA instalabil, deploy automat pe push |

> **Ordine deliberată:** fiecare fază e testabilă independent. Faza 2 validează transportul MQTT înainte de a investi în UI.

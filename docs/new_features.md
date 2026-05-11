# Plan — Înlocuire secțiune "Devices" cu live viewer pentru camere IMOU

## Context

Aplicația MAUI [MobileApp/](MobileApp/) e un control panel pentru un sistem de ventilație (ESP32 + MQTT). Pagina actuală `Devices` afișează **read-only** stare hardware (broker MQTT, ESP32, locks, senzori DHT22) — nu mai e necesară.

Userul are camere IP IMOU (model PS8D, 5MP 3K, PoE, microfon, IP67), deja înregistrate în aplicația Imou Life. Vrea **ștergerea completă** a paginii `Devices` actuale și înlocuirea cu un **live video viewer** care:
- Afișează **oricâte camere** are (nu hardcoded la 5), cu nume custom
- Funcționează **de oriunde** (LAN local + remote prin internet)
- **Fără port forwarding** la router

### Decizii confirmate cu userul

| Aspect | Decizie |
|---|---|
| Conectivitate | **Cloud-first hibrid** — Cloud HLS oriunde (NAT traversal IMOU), RTSP local când telefonul e pe același LAN |
| IP discovery | **Auto din Cloud Open API** (`deviceBaseDetailList`) — userul nu mai introduce IP manual |
| Număr camere | **Nelimitat** — userul adaugă oricâte vrea, cu nume custom |
| Layout | **Grid responsive auto-fit** — celule de mărime fixă, n-coloane în funcție de lățimea ecranului, scroll vertical |
| Active streams | **Lazy loading** — doar tile-urile vizibile pe ecran rulează, cele off-screen sunt pause-uite |
| Features per cameră | Video + audio + snap photo + record local |
| Stocare credențiale | UI configurabil în Settings + **SecureStorage** (criptat) |
| Cloud setup | **Tot în MVP de la început** (necesar pentru discovery) |
| Date hardware vechi | **Ștergere completă** |
| Audio default | **Muted în grid**, unmute în fullscreen |
| Eticheta tab navbar | **"Camere"** (Română) |
| Player video | **LibVLCSharp.MAUI primary**, fallback `CommunityToolkit.Maui.MediaElement` dacă incompatibil cu .NET 10 |
| Recording lifecycle | **Doar cu app activă** (fără foreground service) |
| Snapshot | Doar din **fullscreen** (5MP main stream) |

### Constrângere tehnică majoră

- **Nu există SDK / NuGet IMOU pentru .NET / MAUI / Xamarin.** Soluția: `LibVLCSharp.MAUI` ca player video, REST direct la Open API IMOU (`openapi.easy4ip.com`).
- **Decizia "fără port forward + acces de oriunde"** elimină RTSP via internet. Single source de truth pentru remote = **HLS Cloud via Open API IMOU** (relay-urile IMOU rezolvă NAT traversal).

---

## Arhitectură

```
VIEWS                              VIEWMODELS                         SERVICES
─────                              ──────────                         ────────
CamerasPage (grid auto-fit) ─▶ CamerasViewModel (Singleton) ───▶ ICameraService ──▶ INetworkProbeService
   │                                │                                  │              (TCP probe LAN)
   ▼                                │                                  ├──▶ IImouCloudService
CameraFullscreenPage ──────▶ CameraFullscreenViewModel                │      (deviceBaseDetailList,
                                                                       │       liveList, accessToken,
                                                                       │       MD5 sign + clock skew)
CameraSettingsPage ────────▶ CameraSettingsViewModel                  │
   │ (Cloud setup +                                                    ├──▶ ICredentialStore
   │  cameras list editor)                                             │      (SecureStorage)
   ▼                                                                   │
CameraEditPopup ───────────▶ (binded la CameraSettingsVM)             ├──▶ ICameraConfigRepository
                                                                       │      (cameras.json + atomic)
                                  StreamCellViewModel (per tile)      │
                                  (1 IVideoPlayerHandle, lazy)         ├──▶ IRecordingService
                                                                       │      (LibVLC sout / Snapshot)
                                                                       │
                                                                       └──▶ IPlayerFactory
                                                                              (LibVLC primary,
                                                                               MediaElement fallback)
```

### Modele de date

- **`CameraConfig`**:
  `Id (Guid)`, `Name` (custom user), `DisplayOrder` (pentru sortare), `ImouDeviceId` (obligatoriu — primary key), `ImouChannelId` (default 1), `LocalIp` (auto-fill din `deviceBaseDetailList`, override manual posibil), `RtspPort` (default 554), `RtspUsername` (default "admin"), `PreferredScope` (Auto/LanOnly/CloudOnly, default Auto), `IsEnabled` (toggle vizibilitate), `CreatedAt`, `UpdatedAt`. Parola RTSP separat în SecureStorage cu key `cam_safety_{Id}`.
- **`ImouCredentials`** (obligatorii): `AppId`, `AppSecret`, `Region` (eu/us/ap) — în SecureStorage cu prefix `imou_`.
- **`ImouTokenCache`**: `AccessToken`, `IssuedAt`, `ExpiresAt`, `ServerTimeOffset` (clock skew detectat) — Preferences (TTL ~3 zile).
- **`ImouDiscoveredDevice`** (transient, neserializat): `DeviceId`, `Name` IMOU, `Channels`, `LocalIp`, `IsOnline`, `Model`, `Firmware` — folosit doar pentru pas de discovery în Settings.
- **`NetworkScope`** enum: `Auto`, `Lan`, `Cloud`.
- **`StreamCellState`**: `Scope`, `IsBuffering`, `LastError`, `IsAudioMuted` (default `true` în grid), `IsRecording`, `IsActive` (lazy loading flag — true doar când vizibil).

### Servicii (pattern din [Services/IMqttService.cs](MobileApp/Services/IMqttService.cs))

| Service | Responsabilitate |
|---|---|
| `IPlayerFactory` | Creează playere video — primary `LibVLCMediaPlayer`, fallback `MediaElementMediaPlayer` (auto-detect la startup) |
| `ICameraService` | URL builder: dat `CameraConfig` + `isFullscreen`, returnează stream URI (RTSP LAN sau HLS Cloud) |
| `INetworkProbeService` | TCP probe rapid (timeout 800ms) la `LocalIp:RtspPort` |
| `IImouCloudService` | Auth Open API, MD5 sign cu clock skew handling, cache token, endpoint-uri: `accessToken`, `deviceBaseDetailList` (discovery), `bindDeviceLive` + `getLiveStreamInfo` (HLS) |
| `ICredentialStore` | Wrap `SecureStorage.Default` (`cam_safety_{Id}`, `imou_app_*`) |
| `ICameraConfigRepository` | Read/write `cameras.json` în `FileSystem.AppDataDirectory` (atomic write) |
| `IRecordingService` | LibVLC `MediaPlayer.TakeSnapshot()` (fullscreen) + sout pipeline `mp4` cu `--sout-mp4-faststart` |

### Strategia player abstractizat

- **Interfață `IVideoPlayerHandle`** — comună (`Play`, `Stop`, `Pause`, `TakeSnapshot`, `StartRecord`, `StopRecord`, `IsMuted`, `Position`, ...).
- La startup, `MauiProgram` încearcă `LibVLC.Core.Initialize()` în try/catch:
  - **OK** → înregistrează `LibVLCPlayerFactory` Singleton.
  - **Eșuat** (incompatibilitate .NET 10) → înregistrează `MediaElementPlayerFactory` (fallback automat). Log clar.
- VM-urile bind doar la `IVideoPlayerHandle` — codul UI rămâne neutru de player.

### LibVLC lifecycle

- `LibVLC` global Singleton, `Core.Initialize()` în factory ctor (Android-specific).
- `MediaPlayer` per `StreamCellViewModel` — **lazy created** doar când tile-ul devine vizibil; dispose când iese din viewport (configurat prin `CollectionView` virtualization + custom viewport tracker).
- **Subtype în grid**: `subtype=1` (sub-stream ~640x480) pentru economie.
- **Subtype în fullscreen**: `subtype=0` (main 5MP) pentru calitate snapshot/recording.
- `App.xaml.cs OnSleep`: Stop() pe toate playerele active. `OnResume`: repornește pentru tile-urile vizibile.
- `CamerasViewModel` + `CamerasPage` ambele Singleton (excepție conștientă) — evită re-attach VideoView la tab switch.
- Configurare LibVLC `--rtsp-tcp` (TCP forțat).
- Logging filtrat să nu expună parola RTSP.

### Layout responsive (grid auto-fit)

- `CollectionView` cu `ItemsLayout=GridItemsLayout` și `Span` calculat dinamic:
  ```
  span = max(1, floor(availableWidth / minTileWidth))   // minTileWidth = 320 dp
  ```
  - Phone portrait (~360 dp) → span = 1 → listă verticală
  - Phone landscape (~720 dp) → span = 2
  - Tablet portrait (~768 dp) → span = 2
  - Tablet landscape (~1024+ dp) → span = 3
- Fiecare tile aspect ratio 16:9.
- Handler `MainDisplayInfoChanged` recalculează `Span` la rotire.
- `CollectionView` virtualization built-in + custom viewport tracker pentru lazy MediaPlayer.

### Lazy loading streamuri

Mecanism:
1. `StreamCellViewModel.IsActive` — flag setat de Page când tile devine vizibil pe ecran.
2. Page atașează handler la `CollectionView.Scrolled` event (sau folosește `EventToCommandBehavior`) pentru a calcula viewport.
3. Algoritm: per tile, dacă `tileBounds` se intersectează cu `viewportBounds` (cu un mic buffer ~100 px) → `IsActive = true`, altfel `false`.
4. La `IsActive=true` → `Play()`, la `false` → `Stop()` + `Dispose()` pe MediaPlayer (eliberare RAM).
5. Throttle 200ms să nu refresh la fiecare scroll event.

---

## Fișiere

### De șters

- [MobileApp/Views/DevicesPage.xaml](MobileApp/Views/DevicesPage.xaml) (229 linii)
- `MobileApp/Views/DevicesPage.xaml.cs`
- [MobileApp/ViewModels/DevicesViewModel.cs](MobileApp/ViewModels/DevicesViewModel.cs) (171 linii)

### De modificat

| Fișier | Modificare |
|---|---|
| [MobileApp/AppShell.xaml:16](MobileApp/AppShell.xaml#L16) | `DevicesPage` → `CamerasPage` |
| [MobileApp/MauiProgram.cs:55](MobileApp/MauiProgram.cs#L55), [:62](MobileApp/MauiProgram.cs#L62) | Șters DevicesViewModel/Page; adăugat noile + try/catch `LibVLC.Core.Initialize()` cu fallback |
| [MobileApp/Controls/FloatingNavBar.xaml:51-67](MobileApp/Controls/FloatingNavBar.xaml#L51-L67) | Text "Devices" → **"Camere"**, icon Path nou |
| `MobileApp/Controls/FloatingNavBar.xaml.cs` | `IsDevicesActive` → `IsCamerasActive`; route `//CamerasPage` |
| Toate paginile cu `CurrentPage="Devices"` | → `CurrentPage="Camere"` |
| [MobileApp/ProiectVentilatie.Mobile.csproj](MobileApp/ProiectVentilatie.Mobile.csproj) | + `LibVLCSharp.MAUI`, + `VideoLAN.LibVLC.Android` |
| [MobileApp/Platforms/Android/AndroidManifest.xml](MobileApp/Platforms/Android/AndroidManifest.xml) | + permisiuni (vezi mai jos) |
| `MobileApp/App.xaml.cs` | OnSleep stop all playere; OnResume repornește vizibilele |

### Noi (paths în [MobileApp/](MobileApp/))

**Models:** `CameraConfig.cs`, `ImouCredentials.cs`, `ImouTokenCache.cs`, `ImouDiscoveredDevice.cs`, `NetworkScope.cs`, `StreamCellState.cs`, `RecordingResult.cs`

**Services (interface + impl):** `IPlayerFactory.cs`/`LibVLCPlayerFactory.cs`/`MediaElementPlayerFactory.cs`, `IVideoPlayerHandle.cs` (+ wrappers), `ICameraService.cs`/`CameraService.cs`, `INetworkProbeService.cs`/`NetworkProbeService.cs`, `IImouCloudService.cs`/`ImouCloudService.cs`, `ICredentialStore.cs`/`CredentialStore.cs`, `ICameraConfigRepository.cs`/`CameraConfigRepository.cs`, `IRecordingService.cs`/`RecordingService.cs`

**ViewModels:** `CamerasViewModel.cs`, `StreamCellViewModel.cs`, `CameraFullscreenViewModel.cs`, `CameraSettingsViewModel.cs`

**Views:** `CamerasPage.xaml`(+`.cs`), `CameraFullscreenPage.xaml`(+`.cs`), `CameraSettingsPage.xaml`(+`.cs`), `CameraEditPopup.xaml`(+`.cs`), `CameraDiscoveryPopup.xaml`(+`.cs`) — popup pentru pasul "import camere din IMOU Cloud"

**Controls:** `CameraTileOverlay.xaml`(+`.cs`)

**Converters:** `NetworkScopeToColorConverter.cs`

---

## Pachete NuGet

| Pachet | Versiune | Motiv |
|---|---|---|
| `LibVLCSharp.MAUI` | latest stable | Player primary, RTSP/HLS |
| `VideoLAN.LibVLC.Android` | `>=3.5.x` | Native libs Android (~30-40 MB APK growth) |

`CommunityToolkit.Maui.MediaElement` deja prezent — fallback automat.

**Plan B**: dacă LibVLCSharp.MAUI nu compilează cu .NET 10, source-build din GitHub sau folosim doar MediaElement (HLS Cloud OK, RTSP poate fi instabil pe Android).

---

## Permisiuni Android

```xml
<uses-permission android:name="android.permission.INTERNET" />               <!-- existent -->
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />   <!-- existent -->
<uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />      <!-- detect SSID change -->
<uses-permission android:name="android.permission.WAKE_LOCK" />              <!-- redare continuă -->
<uses-permission android:name="android.permission.MODIFY_AUDIO_SETTINGS" />  <!-- audio focus -->
<uses-permission android:name="android.permission.READ_MEDIA_IMAGES" />      <!-- API 33+ snap -->
<uses-permission android:name="android.permission.READ_MEDIA_VIDEO" />       <!-- API 33+ record -->
<uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE"
                 android:maxSdkVersion="28" />
```

`RECORD_AUDIO` și `FOREGROUND_SERVICE` NU sunt necesare.

---

## Flow Cloud-first + LAN auto-detect

`CameraService.ResolveStreamUriAsync(config, isFullscreen)`:

1. `LanOnly` → RTSP URL doar (eroare clară dacă LAN unreachable).
2. `CloudOnly` → cere HLS via `IImouCloudService` (skip probe).
3. `Auto` (default):
   - `INetworkProbeService.IsLanReachableAsync(config.LocalIp, 554, timeoutMs:800)`.
   - Reușit → **RTSP LAN**: `rtsp://admin:{urlEncoded(safety)}@{IP}:554/cam/realmonitor?channel={ch}&subtype={1|0}` — latență <500ms, calitate maximă.
   - Eșuat → **HLS Cloud** via Open API — funcționează de oriunde (NAT traversal IMOU). Latență 6-15s.

Cache rezultat probe = 30s. Long-tap pe celulă → meniu manual "Forțează LAN/Cloud/Auto". Badge în CameraTileOverlay: cyan LAN / portocaliu CLOUD / roșu OFFLINE.

**Nota**: dacă userul e "de oriunde" (4G/5G, alt WiFi), probe la `LocalIp` eșuează automat → cad pe Cloud HLS fără config. Zero port forwarding.

---

## Cloud / Open API IMOU (Cloud-first flow)

### Setup inițial (one-time, în Settings)

1. **Pasul 1 — Credentiale developer**:
   - Userul creează cont developer pe https://open.imoulife.com (gratuit).
   - Creează app în consolă → primește `AppId` + `AppSecret`.
   - În aplicația **Imou Life**, pentru fiecare cameră → Settings → Open Platform → enable + bind to AppId.
   - În Settings page din MAUI app, introduce AppId/AppSecret/Region → Save.

2. **Pasul 2 — Discovery automat**:
   - Buton "🔍 IMPORTĂ CAMERE DIN CLOUD" în CameraSettingsPage.
   - Apelează `IImouCloudService.DiscoverDevicesAsync()` → `accessToken` → `deviceBaseDetailList`.
   - Returnează lista `ImouDiscoveredDevice` cu device ID, nume IMOU, IP local raportat (când camera e online), status, model.
   - `CameraDiscoveryPopup` afișează lista cu checkbox-uri.
   - User selectează ce camere vrea să importe + edit nume custom.
   - Pentru fiecare cameră selectată → introduce parolă RTSP (safety code de pe etichetă cameră).
   - Save → `CameraConfig`-uri create automat cu `LocalIp` auto-fill.

3. **Pasul 3 — Refresh manual oricând**:
   - Buton "↻ ACTUALIZEAZĂ IP-uri" în Settings → re-execută `deviceBaseDetailList` → update `LocalIp` per cameră dacă s-a schimbat (DHCP).

### Time sync (clock skew handling)

- La primul call cloud: HEAD request la `openapi.easy4ip.com`, citește header `Date`, calculează `serverTime - localTime` = offset → cache în `ImouTokenCache.ServerTimeOffset`.
- Toate sign-urile MD5 folosesc `localTime + offset`.
- Eroare sign mismatch → mesaj user-friendly *"Verifică ceasul telefonului"*.

### Endpoint-uri folosite

| Endpoint | Scop |
|---|---|
| `POST /openapi/accessToken` | Auth, MD5 sign, returnează token TTL ~3 zile |
| `POST /openapi/deviceBaseDetailList` | **Discovery** — listă camere user + metadata (IP local, status) |
| `POST /openapi/bindDeviceLive` | Returnează `liveToken` per cameră+stream |
| `POST /openapi/getLiveStreamInfo` | Returnează HLS URL bazat pe `liveToken` |

Cache HLS local 5 min. Retry exponential backoff (3 încercări 2s/8s/30s).

### Quota & risc

~1000 req/zi free tier. Cu lazy loading + cache HLS 5 min, chiar și 20 camere → ~100-200 req/zi → safe. Discovery rar (manual), nu drainuiește quota.

---

## Settings UI

### `CameraSettingsPage` — structură

1. **Section "Imou Cloud Credentials"** (obligatorie ca pas 1):
   - `AppId` text input
   - `AppSecret` password input (mask `•`)
   - `Region` picker (eu/us/ap)
   - Label "Last token issued: {date}"
   - Buton `💾 SAVE` + `🔄 TEST CONNECTION` (verifică token-ul)

2. **Section "Camere"** (după ce Cloud setup e valid):
   - Buton `🔍 IMPORTĂ CAMERE DIN CLOUD` — deschide `CameraDiscoveryPopup`
   - `CollectionView` cu camerele configurate (oricâte): card cu nume, IP local, scope badge, switch IsEnabled, butoane Edit/Delete
   - Buton `+ ADAUGĂ MANUAL` (pentru cazuri edge: cameră IMOU non-discovered, ne-IMOU)
   - Buton `↻ ACTUALIZEAZĂ IP-uri` (re-fetch din Cloud)

3. **Section "Backup configurări"** *(MVP-2 opțional)*:
   - Export JSON (fără parole), Import JSON.

### `CameraDiscoveryPopup` (CommunityToolkit Popup)

- Loading state cât durează discovery.
- Listă `ImouDiscoveredDevice`-uri cu checkbox-uri.
- Pentru fiecare bifat: input nume custom (default = nume IMOU), input parolă RTSP (cu hint "safety code de pe eticheta camerei").
- Buton `IMPORTĂ N CAMERE`.

### `CameraEditPopup` (edit existent)

| Câmp | Default | Validare |
|---|---|---|
| Name | nume custom | non-empty, max 64 |
| LocalIp | auto-fill din Cloud | `IPAddress.TryParse` (override OK) |
| RtspPort | 554 | 1–65535 |
| RtspUsername | "admin" | non-empty |
| RtspSafetyCode | (din SecureStorage) | non-empty (re-set OK) |
| ImouDeviceId | readonly | (din Cloud, neschimbabil) |
| ImouChannelId | 1 | număr |
| PreferredScope | Auto | Auto/LanOnly/CloudOnly |
| IsEnabled | true | toggle vizibilitate |
| DisplayOrder | next | sort manual (drag&drop MVP-2) |

Buton `🔌 TEST CONNECTION` → probe + try-open RTSP (5s timeout) → Toast OK/FAIL.

### Persistență

- `cameras.json` în `FileSystem.AppDataDirectory` (atomic write: temp + rename).
- **Parolele NU în JSON** — exclusiv `SecureStorage` (`cam_safety_{Id}`).

---

## Snap photo & Record

### Snapshot — **doar din fullscreen**

- API: `MediaPlayer.TakeSnapshot(num:0, path, width:0, height:0)` pe stream main 5MP.
- Path: `{AppDataDirectory}/Snapshots/cam{id}_{yyyyMMdd_HHmmss}.jpg`.
- Toast confirmare. Buton "Open" → `Launcher.Default.OpenAsync` cu FileProvider URI.

### Recording — **doar cu app activă**

- LibVLC sout: `:sout=#duplicate{dst=display,dst=std{access=file,mux=mp4,dst='{path}'}}` cu `--sout-mp4-faststart` (moov atom la început, robust la crash).
- Path: `{AppDataDirectory}/Recordings/cam{id}_{yyyyMMdd_HHmmss}.mp4`.
- Toggle: re-create Media cu/fără sout (~1s freeze acceptat).
- Indicator spațiu liber în UI; warning <500 MB; auto-stop <100 MB.
- `OnSleep` → recording stopped + fișier salvat (warning user).

---

## Riscuri și considerente

### Compatibilitate LibVLC
- Risc: LibVLCSharp.MAUI țintește .NET 8/9. Fallback automat la MediaElement.
- APK growth ~30-40 MB.

### Bug-uri / limitări IMOU
- **PS8D firmware <2.840**: RTSP poate eșua. Update obligatoriu.
- **RTSP local active după Imou Life launched**: pe unele firmware. Pre-validation cu VLC desktop OBLIGATORIE.
- **`deviceBaseDetailList` poate să nu returneze IP local pentru toate modelele** — fallback: user introduce manual sau folosim doar Cloud HLS.
- **HLS Cloud latency**: 6-15s. Documentat în UI cu badge CLOUD.

### Performanță (lazy loading ajută enorm)
- Pe orice device: doar 2-4 streamuri active simultan în viewport. CPU/baterie OK.
- 20 camere în repository → maximum 4 active în viewport → comparabil cu un singur stream pe entry-level phone.
- WiFi: 2-3 sub-streams ~2-4 Mbps.

### Securitate
- Parolă RTSP în `SecureStorage` Android KeyStore-backed.
- AppSecret IMOU în `SecureStorage`.
- HTTPS only Open API.
- LibVLC log filtrat să nu leak parola din URL.

### Lifecycle
- `IDisposable` pe toate VM-urile cu MediaPlayer.
- Ordine: `MediaPlayer.Dispose()` → `Media.Dispose()`.
- `CamerasViewModel` + `CamerasPage` Singleton (justificat).
- StreamCellViewModel `IDisposable` cu lazy create/dispose la viewport in/out.

### IP DHCP changes
- Auto-detect via probe + fallback Cloud — chiar dacă IP-ul s-a schimbat și `LocalIp` e stale, planul cade pe Cloud automat și user-ul nu observă degradare (decât latența).
- Buton "↻ Actualizează IP-uri" în Settings re-fetch din `deviceBaseDetailList`.

### Time sync
- Server time offset detect la primul call. Eroare clock skew → mesaj user-friendly.

---

## Verificare end-to-end

### Pre-integration: VLC desktop test (5 min, OBLIGATORIU)
1. Activat RTSP în Imou Life pentru fiecare cameră.
2. VLC desktop → `rtsp://admin:<safety>@192.168.x.x:554/cam/realmonitor?channel=1&subtype=1` → <3s, fără auth error.
3. Repetat cu `subtype=0`.
4. Dacă RTSP local nu funcționează → MVP doar cu Cloud HLS (decide cu user).

### Test E2E pe device Android real (NU emulator)

1. **Cloud setup**: în Settings introdus AppId/AppSecret/Region invalid → eroare clară; valid → Toast "Token issued, expires X".
2. **Discovery**: tap "Importă camere" → loading → afișează lista IMOU (ex 5 camere). Bifat 3, dat nume custom + parolă safety. Save → 3 carduri în lista din Settings.
3. **Layout responsive**: în CamerasPage:
   - Phone portrait → 1 col
   - Phone landscape → 2 col
   - Tablet landscape → 3 col
   - Rotire → reflow corect.
4. **Lazy loading**: cu 8+ camere configurate, scroll → primele 2-4 streamuri active, cele off-screen pause-uite. Adb logcat verifică Stop()/Play() corect.
5. **LAN/Cloud switch automat**:
   - Pe WiFi acasă → badge cyan LAN, latență <1s
   - Pe 4G (NU port forward) → badge portocaliu CLOUD, latență 6-15s, dar funcționează
   - Toggle WiFi off/on → comutare dinamică
6. **Fullscreen**: tap celulă → CameraFullscreenPage landscape, main subtype, audio activ default.
7. **Snap (doar fullscreen)**: tap snap → toast → fișier 5MP.
8. **Record**: tap → REC blink → 10s → stop → MP4 valid (faststart).
9. **App background**: streamurile oprite, recording stopped (warning).
10. **Settings persistență**: adăugat 3 camere → restart → toate apar. Edit/Delete OK. SafetyCode NU în JSON. SecureStorage rezistă restart.
11. **Tab navigation**: tab "Camere" → "Dashboard" → înapoi → streamurile vizibile încă rulează.
12. **Time skew**: ceas telefon +1 oră → cloud detect offset → continuă să meargă.
13. **Player fallback**: dacă LibVLC eșuează init, log clar, MediaElement preia (test forțat: rename libvlc.so).

---

## Sequencing execuție

1. **Prep manual obligatoriu**: firmware update camere, RTSP enable Imou Life, VLC desktop test; **userul creează cont developer + sharing camere**.
2. **Branch + deps setup**: NuGet packages + AndroidManifest + try/catch `LibVLC.Core.Initialize()` + `IPlayerFactory` schelet.
3. **Models + Services + Repository**: stub-uri + interfețe.
4. **`IImouCloudService`** (auth + `deviceBaseDetailList`) — testabil cu Postman/curl mai întâi.
5. **CameraSettingsPage + CameraDiscoveryPopup** — flow Cloud setup + import. Test cu cont real.
6. **`CameraService` + `INetworkProbeService`** — URL resolution.
7. **CamerasPage + grid auto-fit + lazy loading**: 1 stream → expand cu lazy.
8. **CameraFullscreenPage + Snap + Record**.
9. **Cleanup DevicesPage + Shell + NavBar** rebrand "Camere".
10. **App lifecycle** + spațiu disk indicator.
11. **E2E test** complet.
12. **Polish UI cyberpunk**.

## Critical files
- [MobileApp/MauiProgram.cs](MobileApp/MauiProgram.cs)
- [MobileApp/AppShell.xaml](MobileApp/AppShell.xaml)
- [MobileApp/Controls/FloatingNavBar.xaml](MobileApp/Controls/FloatingNavBar.xaml) + `.xaml.cs`
- [MobileApp/ProiectVentilatie.Mobile.csproj](MobileApp/ProiectVentilatie.Mobile.csproj)
- [MobileApp/Platforms/Android/AndroidManifest.xml](MobileApp/Platforms/Android/AndroidManifest.xml)
- `MobileApp/App.xaml.cs`

---

## Out of scope (MVP-2 / viitor)

- 2-way audio (talk-back) microfon telefon → cameră.
- Push notificări detecție AI (motion/persoană) — necesită cloud webhooks IMOU.
- mDNS/Bonjour discovery (alternativă la `deviceBaseDetailList`).
- PTZ controls (PS8D PoE bullet/turret nu are PTZ — non-applicable).
- Foreground service pentru recording în background.
- Galerie in-app cu thumbnails snapshots/recordings.
- Backup/restore configurări (export/import JSON, fără parole).
- iOS / Windows target.
- Drag&drop reorder pentru DisplayOrder.
- Suport camere ne-IMOU (ONVIF generic).

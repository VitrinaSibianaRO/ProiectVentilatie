# 06 — Strategie versionare

Atât firmware-ul ESP32 cât și aplicația MAUI au versionare automată la fiecare build.

## Cuprins

- [1. ESP32 firmware — build number simplu](#1-esp32-firmware--build-number-simplu)
- [2. MAUI — SemVer manual + build auto](#2-maui--semver-manual--build-auto)
- [3. Sincronizare versiune fw în UI](#3-sincronizare-versiune-fw-în-ui)
- [4. Bune practici](#4-bune-practici)

---

## 1. ESP32 firmware — build number simplu

### Mecanism

Un script bash (`ESP32/scripts/bump_build.sh`) incrementează un contor înainte de fiecare compilare:

```bash
#!/bin/bash
DIR="$(dirname "$0")/.."
FILE="$DIR/build_number.txt"
[ ! -f "$FILE" ] && echo "0" > "$FILE"
N=$(($(cat "$FILE") + 1))
echo "$N" > "$FILE"
cat > "$DIR/Version.h" << EOF
#pragma once
#define FW_BUILD_NUMBER $N
EOF
echo "[ESP32] Build #$N"
```

### Fișiere implicate

| Fișier | Status git | Conținut |
|---|---|---|
| `ESP32/scripts/bump_build.sh` | committed | Script-ul |
| `ESP32/build_number.txt` | **gitignored** | Un singur număr (init `0`) |
| `ESP32/Version.h` | **gitignored** | `#define FW_BUILD_NUMBER X` (auto-generat) |

### Workflow

```bash
# Pas manual înainte de compile
bash ESP32/scripts/bump_build.sh
# Output: [ESP32] Build #42

# Compile (folosește Version.h proaspăt generat)
arduino-cli compile --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M ESP32/
```

### Folosire în firmware

În `Config.h`:

```cpp
#include "Version.h"
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0    // fallback dacă Version.h nu există
#endif
```

Folosit pentru:
- Print la boot: `Serial.printf("[Boot] Firmware build #%d\n", FW_BUILD_NUMBER);`
- JSON state pe MQTT: `"fw":42`
- Blynk virtualWrite la `BLYNK_CONNECTED()`: `Blynk.virtualWrite(VP_FW_BUILD, FW_BUILD_NUMBER);`

### Integrare în `deploy.sh`

Modifică `deploy.sh` (existent) pentru a apela `bump_build.sh` înainte de `arduino-cli compile`:

```bash
#!/bin/bash
set -e
cd "$(dirname "$0")"

# 1. Bump version
bash ESP32/scripts/bump_build.sh

# 2. Compile
arduino-cli compile --upload --port "${1:-/dev/ttyUSB0}" \
  --fqbn esp32:esp32:pico32:PSRAM=enabled,FlashSize=8M \
  ESP32/

# (resto deploy.sh existent)
```

## 2. MAUI — SemVer manual + build auto

### Mecanism

Un MSBuild target în `ProiectVentilatie.Mobile.csproj` incrementează `ApplicationVersion` la fiecare `dotnet build`:

```xml
<Target Name="IncrementBuildNumber" BeforeTargets="BeforeBuild">
  <PropertyGroup>
    <BuildNumberFile>$(MSBuildProjectDirectory)/build_number.txt</BuildNumberFile>
  </PropertyGroup>
  <WriteLinesToFile Condition="!Exists('$(BuildNumberFile)')"
                    File="$(BuildNumberFile)" Lines="0" Overwrite="true" />
  <PropertyGroup>
    <_CurrentBuild>$([System.IO.File]::ReadAllText('$(BuildNumberFile)').Trim())</_CurrentBuild>
    <_NewBuild>$([MSBuild]::Add($(_CurrentBuild), 1))</_NewBuild>
  </PropertyGroup>
  <WriteLinesToFile File="$(BuildNumberFile)" Lines="$(_NewBuild)" Overwrite="true" />
  <PropertyGroup>
    <ApplicationVersion>$(_NewBuild)</ApplicationVersion>
  </PropertyGroup>
  <Message Importance="high" Text="[MAUI] Build #$(_NewBuild) (display $(ApplicationDisplayVersion))" />
</Target>
```

### Două nivele de versiune

| Câmp | Tip | Auto/Manual | Exemplu |
|---|---|---|---|
| `ApplicationDisplayVersion` | SemVer | **Manual** în csproj | `1.0`, `1.1`, `2.0` |
| `ApplicationVersion` | Integer | **Auto** la fiecare build | `1`, `2`, ... `42` |

`ApplicationDisplayVersion` se incrementează manual când marchezi un release semnificativ:

```xml
<PropertyGroup>
  <ApplicationDisplayVersion>1.1</ApplicationDisplayVersion>
  <!-- ApplicationVersion e setat dinamic de target -->
</PropertyGroup>
```

### Fișiere implicate

| Fișier | Status git | Conținut |
|---|---|---|
| `MobileApp/ProiectVentilatie.Mobile.csproj` | committed | Target MSBuild |
| `MobileApp/build_number.txt` | **gitignored** | Un singur număr (init `0`) |

### Folosire în app

În `SystemViewModel.cs`:

```csharp
public string AppVersion =>
    $"{AppInfo.Current.VersionString} (build #{AppInfo.Current.BuildString})";
// Output: "1.0 (build #42)"
```

Afișat pe System page.

## 3. Sincronizare versiune fw în UI

### În MAUI System page

```
┌─────────────────────────────────────┐
│ Versiune app:    1.0 (build #15)    │ ← AppInfo
│ Firmware ESP32:  build #42          │ ← din MQTT state.fw
└─────────────────────────────────────┘
```

Logică suplimentară (opțional): comparare cu ultima versiune salvată în Preferences:

```csharp
var lastKnownFw = Preferences.Get("LastKnownFwBuild", 0);
if (state.Fw < lastKnownFw) {
    StatusMessage = "⚠️ Firmware mai vechi decât anterior — verifică";
}
Preferences.Set("LastKnownFwBuild", Math.Max(lastKnownFw, state.Fw));
```

### În Blynk

V23 (FW Build) afișat pe Web Dashboard ca Label cu format `Build #{}`:

```
┌──────────────┐
│  Build #42   │
└──────────────┘
```

## 4. Bune practici

### .gitignore

Adaugă în `.gitignore`:

```
ESP32/build_number.txt
ESP32/Version.h
MobileApp/build_number.txt
```

> Cei doi developeri ar avea contoare diferite — în acest proiect single-user nu e o problemă.

### Inițializare la primul build

Scripturile creează automat fișierele dacă nu există (init `0`). Primul build va fi `#1`.

### Sincronizare cu git tags (opțional)

Pentru release-uri, poți alinia:
- Git tag: `v1.0-fw42` sau `app-v1.1`
- GitHub release tag: la fel
- `ApplicationDisplayVersion` în csproj: `1.1`

### Numele fișierelor binare

Pentru OTA (vezi [05-ota-update.md](05-ota-update.md)):

```bash
# Build → output în ESP32/build/firmware-build42.bin
arduino-cli compile --fqbn ... \
  --output-dir ./ESP32/build ESP32/

# Redenumește pentru claritate
mv ESP32/build/ProiectVentilatie.ino.bin ESP32/build/firmware-build$(cat ESP32/build_number.txt).bin
```

### Reset build number (opțional)

Dacă vrei să resetezi contorul (ex: după un release major):

```bash
echo "0" > ESP32/build_number.txt
echo "0" > MobileApp/build_number.txt
```

Următorul build va fi `#1`.

### Tracking în NVS pentru ESP32 (opțional)

Pentru a păstra istoric "firmware curent vs anterior", poți adăuga în NVS:
- `lastBootBuild` — actualizat la fiecare boot
- Comparație cu `FW_BUILD_NUMBER` curent → log eveniment dacă s-a schimbat

Nu e necesar pentru funcționarea de bază — doar diagnostic avansat.

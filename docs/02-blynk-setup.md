# 02 — Configurare Blynk app

Aplicația Blynk este deja conectată la firmware prin token (`BLYNK_AUTH_TOKEN` în `Config.h`). Acest document descrie cum se configurează datastreams-urile și widget-urile în consola Blynk pentru a se mapa pe virtual pinii folosiți de firmware.

## Cuprins

- [1. Acces consolă Blynk](#1-acces-consolă-blynk)
- [2. Datastreams](#2-datastreams)
- [3. Web Dashboard](#3-web-dashboard)
- [4. Mobile Dashboard](#4-mobile-dashboard)
- [5. Events (notificări)](#5-events-notificări)
- [6. Test rapid](#6-test-rapid)

---

## 1. Acces consolă Blynk

1. Browser → https://blynk.cloud
2. Login cu contul folosit la generarea token-ului `OSF1fWefKtBmV8c5QUDsgtpkMHafaB_I`
3. **Templates** → caută template-ul cu ID `TMPL42ximIY6M` (numele "Add agency")

> 💡 Dacă vrei să folosești alt cont/template: înlocuiește `BLYNK_TEMPLATE_ID`, `BLYNK_TEMPLATE_NAME` și `BLYNK_AUTH_TOKEN` în `ESP32/Config.h`.

## 2. Datastreams

În tab-ul **Datastreams** verifică/creează exact aceste virtual pini:

| Pin | Nume | Type | Min | Max | Default | Direcție |
|---|---|---|---|---|---|---|
| V1 | Temp Stânga | Double | -20 | 80 | 0 | RO |
| V2 | Hum Stânga | Double | 0 | 100 | 0 | RO |
| V3 | Temp Dreapta | Double | -20 | 80 | 0 | RO |
| V4 | Hum Dreapta | Double | 0 | 100 | 0 | RO |
| V5 | Releu Stânga | Integer | 0 | 1 | 0 | RO |
| V6 | Releu Dreapta | Integer | 0 | 1 | 0 | RO |
| V7 | Prag Temperatură | Double | 1 | 80 | 45 | RW |
| V8 | Prag Umiditate | Double | 0 | 100 | 60 | RW |
| V9 | Interval Citire | Integer | 10 | 3600 | 300 | RW |
| V10 | Reset Defaults | Integer | 0 | 1 | 0 | RW (push) |
| V11 | Override Stânga | Integer | 0 | 2 | 0 | RW |
| V12 | Override Dreapta | Integer | 0 | 2 | 0 | RW |
| V20 | Restart | Integer | 0 | 1 | 0 | RW (push) |
| V21 | Free Heap (KB) | Integer | 0 | 500 | 0 | RO |
| **V22** | **Lock Owner** | **Integer** | **0** | **2** | **0** | **RO (NOU)** |
| **V23** | **FW Build** | **Integer** | **0** | **9999** | **0** | **RO (NOU)** |

**Convenții override (V11, V12):**
- `0` = forțat OFF (releu oprit indiferent de senzori)
- `1` = forțat ON (releu pornit indiferent de senzori)
- `2` = clear (revine la modul automat, decis de praguri)
- Override expiră automat după 2h

**Convenție lock (V22):**
- `0` = niciun control activ (ambele canale pot scrie)
- `1` = Blynk activ (MAUI primește lock)
- `2` = MAUI/MQTT activ (Blynk primește `cmd_rejected`)

## 3. Web Dashboard

În tab-ul **Web Dashboard** organizează widget-urile în 3 secțiuni:

### Secțiunea Senzori (read-only)

| Widget | Pin | Setări |
|---|---|---|
| Gauge | V1 | min=-20, max=80, unit=°C, label="Temp Stânga" |
| Gauge | V2 | min=0, max=100, unit=%, label="Hum Stânga" |
| Gauge | V3 | min=-20, max=80, unit=°C, label="Temp Dreapta" |
| Gauge | V4 | min=0, max=100, unit=%, label="Hum Dreapta" |

### Secțiunea Stare (read-only)

| Widget | Pin | Setări |
|---|---|---|
| LED | V5 | Verde când ON, label="Releu Stânga" |
| LED | V6 | Verde când ON, label="Releu Dreapta" |
| Label | V21 | Format `{} KB`, label="Heap liber" |
| Label | V22 | Format custom (vezi mai jos), label="Control activ" |
| Label | V23 | Format `Build #{}`, label="Firmware" |

**Format custom V22 (Lock Owner):**
- `0` → `"Liber"`
- `1` → `"Blynk"`
- `2` → `"MAUI activ ⚠️"`

### Secțiunea Comenzi (read-write)

| Widget | Pin | Setări |
|---|---|---|
| Slider | V7 | min=1, max=80, step=0.5, unit=°C, label="Prag Temperatură" |
| Slider | V8 | min=0, max=100, step=1, unit=%, label="Prag Umiditate" |
| Numeric Input | V9 | min=10, max=3600, unit=sec, label="Interval Citire" |
| Segmented Switch | V11 | 3 valori: OFF(0), ON(1), AUTO(2), label="Override Stânga" |
| Segmented Switch | V12 | 3 valori: OFF(0), ON(1), AUTO(2), label="Override Dreapta" |
| Switch | V10 | Push (revine la 0), label="Reset Defaults" |
| Switch | V20 | Push (revine la 0), label="Restart Sistem" |

## 4. Mobile Dashboard

În aplicația Blynk pe telefon — aceeași structură ca Web Dashboard, dar widget-urile pot fi mai compacte:
- Senzori: 4× Value Display sau LCD widget
- Stare: 2× LED + 1× Value Display pentru heap/build/lock
- Comenzi: 2× Slider + 2× Segmented Switch (override) + 2× Button (reset/restart)

## 5. Events (notificări)

În tab-ul **Events**, configurează aceste evenimente cu notificare push:

| Event Code | Description | Severity | Notify |
|---|---|---|---|
| `sensor_error` | Eroare senzor DHT (5+ erori consecutive) | Warning | Email + Mobile |
| `override_expired` | Override anulat automat după 2h | Info | Mobile |
| `system_restart` | Restart sistem (manual/preventiv) | Info | Mobile |
| `cmd_rejected` | Comandă respinsă (lock activ) | Warning | Mobile |

## 6. Test rapid

După flash-ul firmware-ului ESP32 (vezi [03-esp32-build.md](03-esp32-build.md)):

1. Alimentează ESP32 → LED RGB albastru (boot) → albastru-pulsat (Wi-Fi config) → verde (conectat)
2. Web Dashboard refresh → toate gauge-urile V1-V4 trebuie să afișeze valori reale (după ~2 secunde)
3. V21 (heap KB) trebuie să arate **>200** (cu PSRAM, **>500**)
4. V23 (FW Build) trebuie să arate numărul curent (ex: `1`, `2`, `42`)
5. Modifică V11 (Override Stânga) → 1 (ON) → ascultă releu click → V5 (LED) devine verde
6. Așteaptă 2h sau scurtează timeout temporar pentru test → notificare push `override_expired`

Dacă oricare din pașii 2-4 eșuează, vezi [08-troubleshooting.md](08-troubleshooting.md).

## Sync bidirecțional Blynk ↔ MAUI

Modificările făcute în Blynk se reflectă automat în MAUI:
- Schimbi V7 (prag temp) → ESP32 procesează → publică state pe MQTT → MAUI primește în <1s

Modificările făcute în MAUI se sincronizează în Blynk:
- MAUI trimite `setConfig` → ESP32 aplică → `Blynk.virtualWrite(V7, ...)` → Blynk primește valoarea nouă instant

În timpul lock-ului (V22 ≠ 0), modificările făcute în canalul blocat sunt respinse cu eveniment `cmd_rejected`.

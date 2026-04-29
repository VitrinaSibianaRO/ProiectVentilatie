# 01 — Configurare HiveMQ Cloud

HiveMQ Cloud este broker-ul MQTT folosit pentru comunicația dintre ESP32 și aplicația MAUI. Aplicația Blynk folosește un canal separat (Blynk Cloud) și nu trece prin HiveMQ.

## Cuprins

- [1. Cont HiveMQ Cloud](#1-cont-hivemq-cloud)
- [2. Date cluster](#2-date-cluster)
- [3. Creare useri (Access Management)](#3-creare-useri-access-management)
- [4. ACL (Access Control List)](#4-acl-access-control-list)
- [5. Completare credențiale în cod](#5-completare-credențiale-în-cod)
- [6. Test conexiune cu Web Client](#6-test-conexiune-cu-web-client)

---

## 1. Cont HiveMQ Cloud

1. Accesează https://www.hivemq.com/mqtt-cloud-broker/
2. Click "Sign Up" → cont gratuit (Free tier oferă cluster cu 100 conexiuni simultane, suficient)
3. După confirmare email → login pe https://console.hivemq.cloud

## 2. Date cluster

Clusterul folosit de acest proiect:

```
Host:           1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud
Port TLS:       8883
Port WebSocket: 8884
```

Aceste valori sunt deja preconfigurate în:
- `ESP32/Config.h` (macro `MQTT_HOST`, `MQTT_PORT`)
- `MobileApp/appsettings.json` (`Mqtt.Host`, `Mqtt.Port`)

> ⚠️ Dacă folosești alt cluster, trebuie să actualizezi ambele fișiere.

## 3. Creare useri (Access Management)

În consolă: **Access Management** → **Add credentials**.

Creează **doi useri separați**:

### User 1: ESP32 firmware

| Câmp | Valoare |
|---|---|
| Username | `ventilatie_esp32` |
| Password | (alege parolă strong, minim 12 caractere) |
| Role | `Publish & Subscribe` |

### User 2: MAUI app

| Câmp | Valoare |
|---|---|
| Username | `ventilatie_app` |
| Password | (parolă diferită de cea pentru ESP32) |
| Role | `Publish & Subscribe` |

> 💡 **De ce doi useri?** Pentru audit (vezi în logs cine publică ce) și pentru a putea revoca accesul unui canal fără a-l afecta pe celălalt.

## 4. ACL (Access Control List)

Pentru fiecare user, configurează permisiunile:

| Permission | Topic | Notă |
|---|---|---|
| Publish | `ventilatie/#` | Subscribe la întreg subarbore |
| Subscribe | `ventilatie/#` | Idem |

**Pas cu pas în consola HiveMQ:**
1. Access Management → click pe userul `ventilatie_esp32`
2. Permissions → Add permission
3. Topic Filter: `ventilatie/#` (cu wildcard `#`)
4. Activity: bifează `Publish` și `Subscribe`
5. QoS: `0, 1, 2` (toate)
6. Retain: `Allowed`
7. Save → repetă pentru `ventilatie_app`

## 5. Completare credențiale în cod

### ESP32 — `ESP32/Config.h`

Editează după secțiunea Blynk:

```cpp
// HiveMQ Cloud
#define MQTT_HOST  "1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud"
#define MQTT_PORT  8883
#define MQTT_USER  "ventilatie_esp32"
#define MQTT_PASS  "PAROLA_ALEASA_PENTRU_ESP32"
```

> ⚠️ Nu commit-uia parola reală în git. Pentru proiect privat e acceptabil; pentru repo public folosește un fișier `Secrets.h` gitignored.

### MAUI — `MobileApp/appsettings.json`

```json
{
  "Mqtt": {
    "Host": "1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud",
    "Port": 8883,
    "Username": "ventilatie_app",
    "Password": "PAROLA_ALEASA_PENTRU_MAUI",
    "StateTopic":   "ventilatie/state",
    "CommandTopic": "ventilatie/cmd",
    "OnlineTopic":  "ventilatie/online",
    "EventTopic":   "ventilatie/event",
    "LogTopic":     "ventilatie/log"
  }
}
```

## 6. Test conexiune cu Web Client

HiveMQ oferă un client web pentru debugging:

1. În consolă, click pe cluster → **Web Client**
2. Conectare automată cu credențialele tale
3. Subscribe la `ventilatie/#` (cu wildcard)
4. Publică un mesaj test pe `ventilatie/test` → ar trebui să apară imediat în lista de mesaje recepționate

După ce flash-ezi firmware-ul ESP32 (vezi [03-esp32-build.md](03-esp32-build.md)), ar trebui să vezi automat:

```
ventilatie/online   →  "online"  (retained)
ventilatie/state    →  {"left":{"temp":24.5,...},...}  (retained)
```

dacă cele două apar — conexiunea funcționează.

## Probleme comune

Dacă întâmpini probleme, vezi [08-troubleshooting.md](08-troubleshooting.md#esp32-nu-se-conectează-la-hivemq).

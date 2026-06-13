#pragma once

// ============================================================
//  Config.h — Master (Carbon V3 #1)
//  Toate constantele de hardware, protocol, MQTT, timing.
//  ZERO magic numbers in alte fisiere.
// ============================================================

// ============================================================
//  HARDWARE PINS  (Carbon V3)
// ============================================================

// I2C (SHT30 stanga local)
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define I2C_FREQ_HZ 100000UL

// SHT30 senzor local (zona stanga)
#define SHT30_ADDR 0x44
#define SHT30_MIN_READ_MS 60000UL // 1 minut cooldown
#define SHT30_RETRY_COUNT 3

#define BOOTGUARD_SAFE_MODE_ENABLED 1

// UART2 catre Slave
#define SLAVE_UART_TX_PIN 19
#define SLAVE_UART_RX_PIN 32
#define SLAVE_UART_BAUD 115200UL
#define SLAVE_REQ_TIMEOUT_MS 1000
#define SLAVE_REBOOT_TIMEOUT_MS 500
#define SLAVE_RETRY_PER_FETCH 2
#define SLAVE_FAILSAFE_AFTER_FAILS 5

// Relee, butoane, LED
#define RELAY_LEFT_PIN 5
#define RELAY_RIGHT_PIN 8
#define RESET_BUTTON_PIN 26 // GPIO14 ocupat de SCK W5500
#define LED_PIN 2           // RGB LED onboard (Data)
#define LED_ENABLE_PIN 4    // RGB LED onboard (Power)
#define LED_COUNT 1

// ============================================================
//  DEFAULTS  (folosite doar la primul boot sau dupa factory reset)
// ============================================================
#define DEFAULT_TEMP_THRESH 45.0f
#define DEFAULT_HUM_THRESH 60.0f
#define DEFAULT_INTERVAL_SEC 300
#define DEFAULT_TEMP_HYST 2.0f
#define DEFAULT_HUM_HYST 5.0f

// ============================================================
//  LIMITE HARD
// ============================================================
#define MIN_INTERVAL_SEC 10
#define MAX_INTERVAL_SEC 3600
#define MIN_TEMP_HYST 0.0f
#define MAX_TEMP_HYST 10.0f
#define MIN_HUM_HYST 0.0f
#define MAX_HUM_HYST 20.0f
#define WDT_TIMEOUT_SEC 60

// ============================================================
//  ANTI-CHATTERING relee (protectie comutari rapide / oscilatii la prag)
// ============================================================
// Histerezis minim EFECTIV impus chiar daca userul seteaza H=0 din MAUI —
// banda moarta sub prag nu dispare niciodata.
#define MIN_EFFECTIVE_TEMP_HYST 0.5f
#define MIN_EFFECTIVE_HUM_HYST  2.0f
// Timp minim intre doua comutari ale aceluiasi releu (protejeaza contactele).
#define RELAY_MIN_SWITCH_MS 30000UL  // 30s

// ============================================================
//  HIVEMQ CLOUD (MQTT TLS 8883)
// ============================================================
#define MQTT_HOST "264f95b78b1d4733a57c7d0c6e045828.s1.eu.hivemq.cloud"
#define MQTT_PORT 8883
#define MQTT_USER "ventilatie_esp32"
#define MQTT_PASS "Esp32Ventil2024!"
#define MQTT_CLIENT_PREFIX "esp32-vent-"

// Timeouts TLS/TCP/socket — valori in SECUNDE (WiFiClientSecure/PubSubClient API).
// Bound blocarea loopTask la worst-case ~13s/incercare (8+5), sub WDT 60s.
#define MQTT_HANDSHAKE_TIMEOUT_S  8UL   // mbedTLS handshake (era 30000 = 30000s BUG)
#define MQTT_TCP_TIMEOUT_S        5UL   // TCP connect (WiFiClientSecure::setTimeout)
#define MQTT_SOCKET_TIMEOUT_S     4UL   // PubSubClient read (default 15s)

// Topic-uri
#define TOPIC_STATE "ventilatie/state"
#define TOPIC_CMD "ventilatie/cmd"
#define TOPIC_ONLINE "ventilatie/online"
#define TOPIC_EVENT "ventilatie/event"
#define TOPIC_LOG "ventilatie/log"
#define TOPIC_DIAG "ventilatie/diag"

// Buffer size pentru PubSubClient (default e doar 256B — prea mic pentru log).
#define MQTT_BUF_SIZE 4096
// Heartbeat: publicare state de liveness la fiecare interval
#define MQTT_HEARTBEAT_MS 240000UL // 4 min (era 1h) — dashboard mereu proaspat
// Throttle hard între publicări consecutive (anti-flood)
#define MQTT_PUBLISH_MIN_INTERVAL_MS 500UL
// Debounce publicari "la cerere": chiar cu pushNow, nu mai des de atat (anti-flood)
#define MQTT_ONDEMAND_MIN_MS 2000UL
// Reconnect MQTT (backoff)
// MAX mai mare: ESP32 facea ~360 tentative/ora la 10s max → rate-limit HiveMQ.
// La 60s max: ~60 tentative/ora, sub limita free tier (100 sesiuni/ora).
#define MQTT_RECONNECT_INITIAL_MS 5000UL
#define MQTT_RECONNECT_MAX_MS 60000UL

// ============================================================
//  NTP
// ============================================================
#define NTP_TIMEZONE "EET-2EEST,M3.5.0/3,M10.5.0/4" // Europa/București
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.google.com"
#define NTP_RESYNC_MS 86400000UL           // 24h
#define NTP_EPOCH_VALID_AFTER 1700000000UL // > 2023 = NTP sincronizat

// ============================================================
//  PSRAM (ESP32-PICO-V3-02 — 2MB SPI PSRAM)
// ============================================================
// Buffere mari alocate in PSRAM la setup() cu fallback pe heap intern.
#define PSRAM_STAT_BUF_SIZE 800 // MqttBridge state JSON
#define PSRAM_DIAG_BUF_SIZE 384 // MqttBridge diag JSON
#define PSRAM_LOG_BUF_SIZE 4096 // EventLog dump JSON

// ============================================================
//  FREERTOS DUAL-CORE
// ============================================================
// SlaveCommTask ruleaza pe Core 0 (Core 1 = loopTask: Ethernet/MQTT/NVS).
#define SLAVE_COMM_TASK_STACK 8192
#define SLAVE_COMM_TASK_PRIORITY 2
#define SLAVE_CMD_QUEUE_DEPTH 8
// Cadenta fetch Slave (ms) — mai rapida decat intervalul sensor (min 10min).
#define SLAVE_COMM_CADENCE_MS 500

// TvCommTask pe Core 0 (prio joasa — TV polling ~11s, non time-critical).
#define TV_COMM_TASK_STACK    6144
#define TV_COMM_TASK_PRIORITY 1

// taskNetwork pe Core 0 — WiFi + MQTT. TLS handshake cere stack generos
// (loopTask actual = 8192 si merge); luam headroom peste.
#define NET_TASK_STACK     12288
#define NET_TASK_PRIORITY  1          // sub SlaveCommTask(2); control = loopTask (Core 1)
#define CONTROL_LOOP_POLL_MS 50       // cadenta poll comenzi control (fara busy-spin)

// Adancime cozi control/TV (comenzi rare, declansate de user).
#define CONTROL_CMD_QUEUE_DEPTH 8
#define TV_CMD_QUEUE_DEPTH      8

// ============================================================
//  EVENT LOG
// ============================================================
#define EVENT_LOG_MAX_ENTRIES 5

// ============================================================
//  NVS NAMESPACES
// ============================================================
#define NVS_PREFS_NAMESPACE "ventilatie"
#define NVS_LOG_NAMESPACE "log"

// ============================================================
//  FIRMWARE VERSION
// ============================================================
// Version.h e auto-generat de scripts/bump_build.sh la fiecare build.
// Dacă fișierul lipsește (primul build sau build manual fără script),
// folosim fallback 0.
#if __has_include("Version.h")
#include "Version.h"
#endif
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif

// WiFi fallback
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_MAX_ATTEMPTS 5

// WiFi watchdog — reconectare periodica (vezi WifiWatchdog in Resilience.h).
// La fiecare INTERVAL, daca WiFi e jos: MAX_ATTEMPTS incercari de cate
// ATTEMPT_TIMEOUT fiecare. Esec → reia peste un INTERVAL, la infinit (fara reboot).
#define WIFI_WATCHDOG_INTERVAL_MS        300000UL  // 5 min intre verificari (era 10 min)
#define WIFI_WATCHDOG_MAX_ATTEMPTS       3
#define WIFI_WATCHDOG_ATTEMPT_TIMEOUT_MS 15000UL   // 15s / incercare

// Last-resort reboot dupa WiFi jos continuu — SINGURA cauza de reboot de retea.
// MQTT picat singur NU reporneste placa. 6h = suficient pentru recuperare normala.
#define WIFI_DOWN_REBOOT_MS  21600000UL  // 6h

// Global flag for WiFi availability
extern bool g_wifiAvailable;

// ============================================================
//  TV CONTROL (LG 75XS4P — RS-232 over TCP port 9761)
// ============================================================
#define TV_TCP_PORT         9761
#define TV_TCP_TIMEOUT_MS   1000
#define TV_SET_ID           1
#define TV_POLL_MS          360000UL   // 6 minute
#define TOPIC_TV_STATE      "ventilatie/tv/state"

// IP/MAC hardcoded TV. La boot, TvController::begin() TESTEAZA acest IP:
//   - daca TV-ul raspunde la TV_DEFAULT_IP (port 9761) → foloseste hardcoded;
//   - daca NU raspunde → foloseste IP/MAC setat din MAUI (salvat in NVS).
// MAC-ul hardcoded e folosit pentru Wake-on-LAN cand ruleaza pe hardcoded.
// Lasa TV_DEFAULT_IP gol ("") ca sa folosesti DOAR config-ul din MAUI.
#define TV_DEFAULT_IP       "192.168.1.131"
#define TV_DEFAULT_MAC      "00:A1:59:D1:3A:DC"

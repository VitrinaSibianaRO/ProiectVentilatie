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
#define I2C_SDA_PIN       21
#define I2C_SCL_PIN       22
#define I2C_FREQ_HZ       100000UL

// SHT30 senzor local (zona stanga)
#define SHT30_ADDR        0x44
#define SHT30_MIN_READ_MS 60000UL    // 1 minut cooldown
#define SHT30_RETRY_COUNT 3

// W5500 SPI (VSPI)
#define W5500_MOSI_PIN    23
#define W5500_MISO_PIN    19
#define W5500_SCK_PIN     18
#define W5500_CS_PIN      5
#define W5500_RST_PIN     33

// UART2 catre Slave
#define SLAVE_UART_TX_PIN        17
#define SLAVE_UART_RX_PIN        16
#define SLAVE_UART_BAUD          115200UL
#define OTA_UART_BAUD            460800UL   // baud inalt pentru OTA Slave
#define SLAVE_REQ_TIMEOUT_MS     1000
#define SLAVE_REBOOT_TIMEOUT_MS  500
#define SLAVE_RETRY_PER_FETCH    2
#define SLAVE_FAILSAFE_AFTER_FAILS 5

// Relee, butoane, LED
#define RELAY_LEFT_PIN    15
#define RELAY_RIGHT_PIN   26
#define RESET_BUTTON_PIN  13
#define LED_PIN           2
#define LED_ENABLE_PIN    4
#define LED_COUNT         1

// ============================================================
//  DEFAULTS  (folosite doar la primul boot sau dupa factory reset)
// ============================================================
#define DEFAULT_TEMP_THRESH         45.0f
#define DEFAULT_HUM_THRESH          60.0f
#define DEFAULT_INTERVAL_SEC        300
#define DEFAULT_OVERRIDE_TIMEOUT_MIN 120
#define DEFAULT_TEMP_HYST           2.0f
#define DEFAULT_HUM_HYST            5.0f

// ============================================================
//  LIMITE HARD
// ============================================================
#define MIN_INTERVAL_SEC  10
#define MAX_INTERVAL_SEC  3600
#define MIN_TEMP_HYST     0.0f
#define MAX_TEMP_HYST     10.0f
#define MIN_HUM_HYST      0.0f
#define MAX_HUM_HYST      20.0f
#define WDT_TIMEOUT_SEC   60

// ============================================================
//  HIVEMQ CLOUD (MQTT TLS 8883)
// ============================================================
#define MQTT_HOST           "3c03ab8cc05e43dfbada27542420c4fc.s1.eu.hivemq.cloud"
#define MQTT_PORT           8883
#define MQTT_USER           "ventilatie_esp32"
#define MQTT_PASS           "Ceparolasapun1"
#define MQTT_CLIENT_PREFIX  "esp32-vent-"

// Topic-uri
#define TOPIC_STATE   "ventilatie/state"
#define TOPIC_CMD     "ventilatie/cmd"
#define TOPIC_ONLINE  "ventilatie/online"
#define TOPIC_EVENT   "ventilatie/event"
#define TOPIC_LOG     "ventilatie/log"
#define TOPIC_DIAG    "ventilatie/diag"

// Buffer size pentru PubSubClient (default e doar 256B — prea mic pentru log).
#define MQTT_BUF_SIZE                4096
// Heartbeat: publicare state forțată la fiecare interval
#define MQTT_HEARTBEAT_MS            3600000UL   // 1h
// Throttle hard între publicări consecutive (anti-flood)
#define MQTT_PUBLISH_MIN_INTERVAL_MS 500UL
// Reconnect MQTT (backoff)
#define MQTT_RECONNECT_INITIAL_MS    5000UL
#define MQTT_RECONNECT_MAX_MS        60000UL

// ============================================================
//  ETHERNET
// ============================================================
#define ETH_DHCP_TIMEOUT_MS       15000
#define ETH_DOWN_RESTART_MS       600000UL  // 10 min link down → restart preventiv

// ============================================================
//  NTP
// ============================================================
#define NTP_TIMEZONE "EET-2EEST,M3.5.0/3,M10.5.0/4"   // Europa/București
#define NTP_SERVER1  "pool.ntp.org"
#define NTP_SERVER2  "time.google.com"
#define NTP_RESYNC_MS          86400000UL     // 24h
#define NTP_EPOCH_VALID_AFTER  1700000000UL   // > 2023 = NTP sincronizat

// ============================================================
//  OTA
// ============================================================
#define OTA_URL_WHITELIST1 "https://github.com/"
#define OTA_URL_WHITELIST2 "https://objects.githubusercontent.com/"

// ============================================================
//  EVENT LOG
// ============================================================
#define EVENT_LOG_MAX_ENTRIES 20

// ============================================================
//  NVS NAMESPACES
// ============================================================
#define NVS_PREFS_NAMESPACE "ventilatie"
#define NVS_LOG_NAMESPACE   "log"

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

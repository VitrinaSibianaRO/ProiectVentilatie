#pragma once

// ============================================================
//  BLYNK CREDENTIALS
// ============================================================
#define BLYNK_TEMPLATE_ID   "TMPL42ximIY6M"
#define BLYNK_TEMPLATE_NAME "Add agency"
#define BLYNK_AUTH_TOKEN    "OSF1fWefKtBmV8c5QUDsgtpkMHafaB_I"

// ============================================================
//  HARDWARE PINS  (Carbon V3)
// ============================================================
#define DHT_LEFT_PIN        19
#define DHT_RIGHT_PIN       32
#define RELAY_LEFT_PIN      15
#define RELAY_RIGHT_PIN     26
#define RESET_BUTTON_PIN    13
#define LED_PIN              2
#define LED_ENABLE_PIN       4
#define LED_COUNT            1

// ============================================================
//  DEFAULTS  (folosite doar la primul boot sau după factory reset)
// ============================================================
#define DEFAULT_TEMP_THRESH     45.0f
#define DEFAULT_HUM_THRESH      60.0f
#define DEFAULT_INTERVAL_SEC    300       // 5 minute
#define DEFAULT_OVERRIDE_TIMEOUT_MIN 120  // override expiră după 2 ore

// ============================================================
//  LIMITE HARD
// ============================================================
#define MIN_INTERVAL_SEC        10
#define MAX_INTERVAL_SEC        3600
#define DHT_MIN_READ_MS         2100      // cooldown minim DHT22
#define WDT_TIMEOUT_SEC         60

// ============================================================
//  BLYNK VIRTUAL PINS
// ============================================================
#define VP_TEMP_LEFT        V1
#define VP_HUM_LEFT         V2
#define VP_TEMP_RIGHT       V3
#define VP_HUM_RIGHT        V4
#define VP_RELAY_LEFT       V5   // read-only în Blynk (afișare stare)
#define VP_RELAY_RIGHT      V6   // read-only în Blynk (afișare stare)
#define VP_THRESH_TEMP      V7
#define VP_THRESH_HUM       V8
#define VP_INTERVAL         V9
#define VP_RESET_DEFAULTS   V10
#define VP_OVERRIDE_LEFT    V11  // override manual stânga (1=ON, 0=OFF, 2=clear)
#define VP_OVERRIDE_RIGHT   V12  // override manual dreapta
#define VP_FREE_HEAP        V21
#define VP_RESTART          V20
#define VP_LOCK_OWNER       V22  // 0=none, 1=blynk, 2=mqtt
#define VP_FW_BUILD         V23  // firmware build number (read-only)

// ============================================================
//  HIVEMQ CLOUD (MQTT)
// ============================================================
#define MQTT_HOST           "1e4444c383474a30b57cfe4f240e6122.s1.eu.hivemq.cloud"
#define MQTT_PORT           8883
#define MQTT_USER           "ventilatie_esp32"
#define MQTT_PASS           "PAROLA_DE_COMPLETAT"
#define MQTT_CLIENT_PREFIX  "esp32-vent-"

// Topic-uri
#define TOPIC_STATE         "ventilatie/state"
#define TOPIC_CMD           "ventilatie/cmd"
#define TOPIC_ONLINE        "ventilatie/online"
#define TOPIC_EVENT         "ventilatie/event"
#define TOPIC_LOG           "ventilatie/log"

// Buffer size pentru PubSubClient (default e doar 256B — prea mic pentru log).
#define MQTT_BUF_SIZE                4096

// Heartbeat: publicare state forțată la fiecare interval
#define MQTT_HEARTBEAT_MS            3600000UL    // 1h

// Throttle hard între publicări consecutive (anti-flood)
#define MQTT_PUBLISH_MIN_INTERVAL_MS 500UL

// Reconnect MQTT (backoff)
#define MQTT_RECONNECT_INITIAL_MS    5000UL
#define MQTT_RECONNECT_MAX_MS        60000UL

// WiFi: restart preventiv dacă suntem deconectați mai mult de N ms
#define WIFI_DOWN_RESTART_MS         600000UL     // 10 min

// WiFi: numele AP-ului pentru WiFiManager portal captiv
#define WIFI_AP_NAME                 "ESP32_Ventilatie"

// ============================================================
//  NTP
// ============================================================
#define NTP_TIMEZONE             "EET-2EEST,M3.5.0/3,M10.5.0/4"   // Europa/București
#define NTP_SERVER1              "pool.ntp.org"
#define NTP_SERVER2              "time.google.com"
#define NTP_RESYNC_MS            86400000UL   // 24h
#define NTP_EPOCH_VALID_AFTER    1700000000UL // > 2023 = NTP sincronizat

// ============================================================
//  OTA
// ============================================================
#define OTA_URL_WHITELIST1       "https://github.com/"
#define OTA_URL_WHITELIST2       "https://objects.githubusercontent.com/"

// ============================================================
//  EVENT LOG
// ============================================================
#define EVENT_LOG_MAX_ENTRIES     50

// ============================================================
//  NVS NAMESPACES
// ============================================================
#define NVS_PREFS_NAMESPACE      "ventilatie"
#define NVS_LOG_NAMESPACE        "log"

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

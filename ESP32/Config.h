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

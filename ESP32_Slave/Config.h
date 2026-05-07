// Config.h — Toate constantele proiectului Slave (Carbon V3 #2).
// NICIUN magic number in alte fisiere — toate valorile vin de aici.
#pragma once

// ============================================================
//  HARDWARE PINS
// ============================================================
constexpr uint8_t  I2C_SDA_PIN       = 21;
constexpr uint8_t  I2C_SCL_PIN       = 22;
constexpr uint32_t I2C_FREQ_HZ       = 100000UL;

constexpr uint8_t  SLAVE_UART_TX_PIN = 17;   // Slave TX → Master RX (GPIO 16)
constexpr uint8_t  SLAVE_UART_RX_PIN = 16;   // Slave RX ← Master TX (GPIO 17)
constexpr uint32_t SLAVE_UART_BAUD   = 115200UL;

constexpr uint8_t  LED_PIN           = 2;
constexpr uint8_t  LED_ENABLE_PIN    = 4;
constexpr uint8_t  LED_COUNT         = 1;

// LED PWM (banda 24V 36W prin NCEP01T18 module)
constexpr uint8_t  LED_PWM_PIN       = 25;
constexpr uint8_t  LED_PWM_CHANNEL   = 0;
constexpr uint32_t LED_PWM_FREQ_HZ   = 5000;
constexpr uint8_t  LED_PWM_BITS      = 12;   // rezolutie 12-bit: 0..4095

// ============================================================
//  SENZOR SHT30
// ============================================================
constexpr uint8_t  SHT30_ADDR        = 0x44;
// Cooldown 1 minut — SHT30 are acuratete buna dupa heat-up,
// iar citiri mai rapide nu aduc valoare pentru controlul ventilatie.
constexpr uint32_t SHT30_MIN_READ_MS = 60000UL;
constexpr uint8_t  SHT30_RETRY_COUNT = 3;

// ============================================================
//  PROTOCOL UART
// ============================================================
constexpr size_t   UART_BUFFER_SIZE  = 256;   // max bytes per linie (JSON poate fi ~200)
constexpr size_t   JSON_DOC_SIZE     = 256;   // StaticJsonDocument pentru raspuns senzor
constexpr uint32_t IDLE_WARN_MS      = 120000UL;  // >2min fara request → LED IDLE
constexpr uint32_t SELF_RESTART_IDLE_MS = 1800000UL;  // 30min fara request → restart preventiv

// ============================================================
//  LOGGING
// ============================================================
constexpr uint32_t LOG_BAUD = 115200UL;  // USB Serial pentru debug

// LOG_LEVEL: 0=DEBUG 1=INFO 2=WARN 3=ERROR
// Production: 2 (WARN+ERROR) — compile-out restul.
#ifndef LOG_LEVEL
  #define LOG_LEVEL 2
#endif

// ============================================================
//  PSRAM (ESP32-PICO-V3-02 — 2MB SPI PSRAM)
// ============================================================
constexpr size_t   OTA_CHUNK_BUF_SIZE    = 1024;   // OtaReceiver::_chunkBuf in PSRAM

// ============================================================
//  FREERTOS DUAL-CORE
// ============================================================
// SensorTask ruleaza pe Core 0 (Core 1 = loopTask: UART + LED + WDT).
constexpr size_t   SENSOR_TASK_STACK     = 4096;
constexpr uint8_t  SENSOR_TASK_PRIORITY  = 2;
// Perioada citire senzor (ms) — 30s; trezire anticipata posibila prin forceRead().
constexpr uint32_t SENSOR_READ_PERIOD_MS = 30000;

// ============================================================
//  TIMING
// ============================================================
constexpr uint32_t LOOP_TICK_MS      = 10;
constexpr uint32_t WDT_TIMEOUT_SEC   = 60;

// Baud rate inalt pentru OTA (optional — reduce transfer time ~4x)
constexpr uint32_t OTA_UART_BAUD     = 460800UL;
// Timeout citire chunk OTA (ms per chunk de 1024 bytes)
constexpr uint32_t OTA_CHUNK_TIMEOUT_MS = 2000;
// Dimensiune maxima firmware Slave (1.5 MB)
constexpr size_t   OTA_MAX_FW_SIZE   = 1500 * 1024;
// Dimensiune minima firmware valida (100 KB)
constexpr size_t   OTA_MIN_FW_SIZE   = 100 * 1024;

// ============================================================
//  FIRMWARE VERSION (auto-generat de scripts/bump_build.sh)
// ============================================================
#if __has_include("Version.h")
  #include "Version.h"
#endif
#ifndef FW_BUILD_NUMBER
  #define FW_BUILD_NUMBER 0
#endif

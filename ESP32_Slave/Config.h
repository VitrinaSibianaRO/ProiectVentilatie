// Config.h — Toate constantele proiectului Slave (Carbon S2, ESP32-S2FN4R2).
// NICIUN magic number in alte fisiere — toate valorile vin de aici.
#pragma once

// ============================================================
//  HARDWARE PINS
// ============================================================
constexpr uint8_t I2C_SDA_PIN = 21;
constexpr uint8_t I2C_SCL_PIN = 22;
constexpr uint32_t I2C_FREQ_HZ = 100000UL;

constexpr uint8_t SLAVE_UART_TX_PIN = 26; // Carbon S2 TX → Master RX
constexpr uint8_t SLAVE_UART_RX_PIN = 25; // Carbon S2 RX ← Master TX
// GPIO18/GPIO19 = USB D-/D+. CDC activ (CDCOnBoot=cdc) → GPIO18 ocupat.
// RX mutat pe GPIO3 ca USB Serial Monitor sa functioneze.
constexpr uint32_t SLAVE_UART_BAUD = 115200UL;

constexpr uint8_t LED_PIN = 2;
constexpr uint8_t LED_ENABLE_PIN = 4;
constexpr uint8_t LED_COUNT = 1;

// LED PWM (banda 24V 36W prin NCEP01T18 module)
constexpr uint8_t LED_PWM_PIN = 8;
constexpr uint8_t LED_PWM_CHANNEL = 0;
constexpr uint32_t LED_PWM_FREQ_HZ = 500;
constexpr uint8_t LED_PWM_BITS = 12; // rezolutie 12-bit: 0..4095

// ============================================================
//  SENZOR SHT30
// ============================================================
constexpr uint8_t SHT30_ADDR = 0x44;
// Cooldown 1 minut — SHT30 are acuratete buna dupa heat-up,
// iar citiri mai rapide nu aduc valoare pentru controlul ventilatie.
constexpr uint32_t SHT30_MIN_READ_MS = 60000UL;
constexpr uint8_t SHT30_RETRY_COUNT = 3;

// ============================================================
//  PROTOCOL UART
// ============================================================
constexpr size_t UART_BUFFER_SIZE =
    256; // max bytes per linie (JSON poate fi ~200)
constexpr size_t JSON_DOC_SIZE =
    256; // StaticJsonDocument pentru raspuns senzor
constexpr uint32_t IDLE_WARN_MS = 120000UL; // >2min fara request → LED IDLE
constexpr uint32_t SELF_RESTART_IDLE_MS =
    1800000UL; // 30min fara request → restart preventiv

// ============================================================
//  LOGGING
// ============================================================
constexpr uint32_t LOG_BAUD = 115200UL; // USB Serial pentru debug
constexpr bool SENSOR_SERIAL_TELEMETRY = true;

// Production: 1 (INFO+WARN+ERROR) — permite vizualizarea comenzilor LED PWM.
#ifndef LOG_LEVEL
#define LOG_LEVEL 1
#endif

// ============================================================
//  FREERTOS — ESP32-S2 SINGLE-CORE
// ============================================================
// SensorTask si loopTask ruleaza pe Core 0 prin time-slicing FreeRTOS.
// xTaskCreatePinnedToCore(..., 0) valid pe single-core.
constexpr size_t SENSOR_TASK_STACK = 4096;
constexpr uint8_t SENSOR_TASK_PRIORITY = 2;
// Perioada citire senzor (ms) — 30s; trezire anticipata posibila prin
// forceRead().
constexpr uint32_t SENSOR_READ_PERIOD_MS = 30000;

// ============================================================
//  TIMING
// ============================================================
constexpr uint32_t LOOP_TICK_MS = 10;
constexpr uint32_t WDT_TIMEOUT_SEC = 60;

// ============================================================
//  FIRMWARE VERSION (auto-generat de scripts/bump_build.sh)
// ============================================================
#if __has_include("Version.h")
#include "Version.h"
#endif
#ifndef FW_BUILD_NUMBER
#define FW_BUILD_NUMBER 0
#endif

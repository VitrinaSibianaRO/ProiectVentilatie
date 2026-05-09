// ESP32_Slave.ino — Entry point minimal. Toata logica e in module.
// Slave Carbon S2 (ESP32-S2FN4R2): SHT30 zona dreapta + UART responder + LED PWM 24V.
// FARA WiFi activ, FARA Ethernet, FARA MQTT, FARA cloud.
// Comunicare exclusiv prin UART2 (Serial2) catre Master.
//
// NOTE Carbon S2 vs Carbon V3:
//   - ESP32-S2 e single-core (Xtensa LX7) — SensorTask si loopTask ruleaza pe
//     acelasi core (Core 0) prin time-slicing FreeRTOS, fara separare fizica.
//   - Bluetooth nu exista pe ESP32-S2 — btStop() eliminat.
//   - USB CDC dezactivat in build (CDCOnBoot=default) ca sa elibereze GPIO18
//     (USB D-) pentru UART2 RX. Debug Serial = UART0 (GPIO1/TX GPIO3/RX).

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "Config.h"
#include "Logger.h"
#include "WatchdogManager.h"
#include "Sht30Sensor.h"
#include "SharedSensorData.h"
#include "SensorTask.h"
#include "SystemLED.h"
#include "SlaveResilience.h"
#include "LedController.h"
#include "UartProtocol.h"
#include "OtaReceiver.h"
#include "CommandDispatcher.h"

// ============================================================
//  SINGLETONS GLOBALE — ownership clar, initializate in setup()
// ============================================================
namespace {
    Sht30Sensor       g_sensor;
    SystemLED         g_led(LED_PIN, LED_ENABLE_PIN, LED_COUNT);
    LedController     g_ledCtrl;
    UartProtocol      g_uart(Serial1);
    OtaReceiver       g_ota(Serial1);
    CommandDispatcher g_dispatcher(g_sensor, g_uart, g_led, g_ledCtrl, g_ota);
}

// ============================================================
//  FREERTOS SHARED SENSOR DATA (Core 0 ↔ Core 1)
// ============================================================
SemaphoreHandle_t g_sensorMutex = nullptr;
SharedSensorData* g_sensorData  = nullptr;
SemaphoreHandle_t g_forceReadSem = nullptr;

#include <nvs_flash.h>
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

void setup() {
    // 1. USB Serial pentru debug (UART0 - GPIO 1/3)
    Logger::begin(LOG_BAUD);
    LOG_INFO("=== ESP32 Slave boot — fw build %d ===", FW_BUILD_NUMBER);

    // 2. NVS Flash Recovery
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) LOG_ERROR("NVS init failed!");

    // OTA rollback protection
    esp_ota_mark_app_valid_cancel_rollback();

    // 3. Oprim radio WiFi — economie ~80mA + zero interferenta.
    WiFi.mode(WIFI_OFF);
#ifndef CONFIG_IDF_TARGET_ESP32S2
    btStop();
#endif

    // 3. Status LED — arata booting
    g_led.begin();
    g_led.setStatus(SystemLED::Status::Booting);

    // 4. I2C bus pentru SHT30 (bus scurt ~10cm, perechi twisted pair nu necesare)
    //    Wire e folosit EXCLUSIV de SensorTask pe Core 0 dupa start() — nu e thread-safe.
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(I2C_FREQ_HZ);

    if (!g_sensor.begin(SHT30_ADDR)) {
        LOG_ERROR("SHT30 init FAIL la 0x%02X", SHT30_ADDR);
        g_led.setStatus(SystemLED::Status::SensorFail);
        // NU blocam boot — Slave continua sa functioneze si fara senzor
        // (va raporta ok=false la GET_SENSOR pana senzorul revine)
    } else {
        LOG_INFO("SHT30 OK la 0x%02X", SHT30_ADDR);
    }

    // 5. PSRAM + FreeRTOS primitives pentru shared sensor data
    // (ESP32-S2 single-core: SensorTask si loopTask pe Core 0 prin time-slicing)
    g_sensorData = (SharedSensorData*)ps_malloc(sizeof(SharedSensorData));
    if (!g_sensorData) g_sensorData = new SharedSensorData();
    memset(g_sensorData, 0, sizeof(SharedSensorData));

    g_sensorMutex = xSemaphoreCreateMutex();
    if (!g_sensorMutex) {
        LOG_ERROR("[FATAL] sensorMutex create failed");
        delay(500);
        ESP.restart();
    }

    LOG_INFO("[PSRAM] g_sensorData @ %p (%s)", g_sensorData,
             esp_ptr_in_psram(g_sensorData) ? "PSRAM" : "heap");

    // 6. Watchdog hardware 60s (loopTask pe Core 1; SensorTask se inregistreaza singur)
    WatchdogManager::begin(WDT_TIMEOUT_SEC);

    // 7. Pornire SensorTask pe Core 0 (citire SHT30 la fiecare 30s)
    SensorTask::start(g_sensor);

    // 8. LED PWM controller
    g_ledCtrl.begin();

    // 9. UART1 (Serial1) catre Master — ESP32-S2 are doar UART0+UART1
    g_uart.begin(SLAVE_UART_BAUD, SLAVE_UART_RX_PIN, SLAVE_UART_TX_PIN);
    LOG_INFO("UART1 ready: baud=%u TX=%d RX=%d",
             SLAVE_UART_BAUD, SLAVE_UART_TX_PIN, SLAVE_UART_RX_PIN);

    g_led.setStatus(SystemLED::Status::Ready);
    LOG_INFO("Ready — awaiting UART commands");
}

void loop() {
    WatchdogManager::feed();
    g_ledCtrl.tick();       // evalueaza schedule LED (no-op daca timp nesincronizat)
    g_dispatcher.tick();    // proceseaza UART + actualizeaza LED status
    
    // Resilience: watchdog idle & uptime monitor
    SlaveResilience::check(g_dispatcher.getLastRequestMs());

    delay(LOOP_TICK_MS);
}

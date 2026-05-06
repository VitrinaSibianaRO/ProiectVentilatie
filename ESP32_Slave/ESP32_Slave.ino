// ESP32_Slave.ino — Entry point minimal. Toata logica e in module.
// Slave Carbon V3 #2: SHT30 zona dreapta + UART responder + LED PWM 24V.
// FARA WiFi, FARA Ethernet, FARA MQTT, FARA cloud.
// Comunicare exclusiv prin UART2 (Serial2) catre Master.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>

#include "Config.h"
#include "Logger.h"
#include "WatchdogManager.h"
#include "Sht30Sensor.h"
#include "SystemLED.h"
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
    UartProtocol      g_uart(Serial2);
    OtaReceiver       g_ota(Serial2);
    CommandDispatcher g_dispatcher(g_sensor, g_uart, g_led, g_ledCtrl, g_ota);
}

void setup() {
    // 1. USB Serial pentru debug
    Logger::begin(LOG_BAUD);
    LOG_INFO("=== ESP32 Slave boot — fw build %d ===", FW_BUILD_NUMBER);

    // 2. Oprim radio WiFi + Bluetooth — economie ~80mA + zero interferenta
    WiFi.mode(WIFI_OFF);
    btStop();

    // 3. Status LED — arata booting
    g_led.begin();
    g_led.setStatus(SystemLED::Status::Booting);

    // 4. I2C bus pentru SHT30 (bus scurt ~10cm, perechi twisted pair nu necesare)
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

    // 5. LED PWM controller
    g_ledCtrl.begin();

    // 6. UART2 catre Master (TX=17, RX=16 pe Carbon V3)
    g_uart.begin(SLAVE_UART_BAUD, SLAVE_UART_RX_PIN, SLAVE_UART_TX_PIN);
    LOG_INFO("UART2 ready: baud=%u TX=%d RX=%d",
             SLAVE_UART_BAUD, SLAVE_UART_TX_PIN, SLAVE_UART_RX_PIN);

    // 7. Watchdog hardware 60s
    WatchdogManager::begin(WDT_TIMEOUT_SEC);

    g_led.setStatus(SystemLED::Status::Ready);
    LOG_INFO("Ready — awaiting UART commands");
}

void loop() {
    WatchdogManager::feed();
    g_ledCtrl.tick();       // evalueaza schedule LED (no-op daca timp nesincronizat)
    g_dispatcher.tick();    // proceseaza UART + actualizeaza LED status
    delay(LOOP_TICK_MS);
}

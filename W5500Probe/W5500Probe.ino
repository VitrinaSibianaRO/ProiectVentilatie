// W5500Probe.ino
// Test minimal pentru GroundStudio Carbon V3 + W5500 pe portul HSPI expus.
// Nu foloseste Ethernet.h, MQTT, task-uri sau codul proiectului principal.

#include <Arduino.h>
#include <SPI.h>

// Carbon V3 HSPI header
static constexpr uint8_t W5500_MISO_PIN = 12;
static constexpr uint8_t W5500_CS_PIN   = 15;
static constexpr uint8_t W5500_SCK_PIN  = 14;
static constexpr uint8_t W5500_MOSI_PIN = 13;
static constexpr uint8_t W5500_RST_PIN  = 33;

static SPIClass hspi(HSPI);

uint8_t readVersion(uint32_t hz, uint8_t spiMode) {
    hspi.beginTransaction(SPISettings(hz, MSBFIRST, spiMode));
    digitalWrite(W5500_CS_PIN, LOW);
    delayMicroseconds(2);

    hspi.transfer(0x00); // VERSIONR address high byte
    hspi.transfer(0x39); // VERSIONR address low byte
    hspi.transfer(0x00); // Common register block, read, variable length
    uint8_t value = hspi.transfer(0x00);

    delayMicroseconds(2);
    digitalWrite(W5500_CS_PIN, HIGH);
    hspi.endTransaction();
    return value;
}

void resetW5500() {
    digitalWrite(W5500_CS_PIN, HIGH);
    pinMode(W5500_RST_PIN, OUTPUT);
    digitalWrite(W5500_RST_PIN, LOW);
    delay(1000);
    digitalWrite(W5500_RST_PIN, HIGH);
    delay(2000);
    digitalWrite(W5500_CS_PIN, HIGH);
}

void runProbe() {
    const uint32_t speeds[] = {100000UL, 250000UL, 1000000UL, 4000000UL};
    const uint8_t modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};

    Serial.println();
    Serial.println("=== W5500 HSPI VERSIONR probe ===");
    Serial.printf("Pins: SCK=%u MISO=%u MOSI=%u CS=%u RST=%u\n",
                  W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN,
                  W5500_CS_PIN, W5500_RST_PIN);
    Serial.println("Expected W5500 VERSIONR: 0x04");

    for (uint32_t hz : speeds) {
        Serial.printf("Speed %lu Hz:", (unsigned long)hz);
        for (uint8_t mode : modes) {
            uint8_t value = readVersion(hz, mode);
            Serial.printf(" MODE%u=0x%02X", mode, value);
        }
        Serial.println();
    }
}

void setup() {
    Serial.begin(115200);
    delay(800);

    Serial.println();
    Serial.println("Booting W5500Probe...");

    pinMode(W5500_CS_PIN, OUTPUT);
    digitalWrite(W5500_CS_PIN, HIGH);
    delay(20);

    resetW5500();

    hspi.begin(W5500_SCK_PIN, W5500_MISO_PIN, W5500_MOSI_PIN, -1);
    Serial.println("HSPI initialized manually.");

    runProbe();
}

void loop() {
    delay(5000);
    runProbe();
}

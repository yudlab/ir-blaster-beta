#include <Arduino.h>
#include "config.h"
#include "app/Application.h"

Application app;

void setup() {
    // Serial0 is the hardware UART connected to the USB-to-UART bridge
    // (typically /dev/ttyUSB0). Keep Serial available for native USB CDC/HID.
    Serial0.begin(BoardConfig::SERIAL_BAUD);
    Serial.begin(BoardConfig::SERIAL_BAUD);

    const uint32_t serialWaitStart = millis();
    while (!Serial0 && millis() - serialWaitStart < 1500) {
        delay(10);
    }
    delay(100);

    Serial0.println();
    Serial0.println("================================================");
    Serial0.println("ESP IR Blaster booting");
    Serial0.printf("Board: %s\n", BoardConfig::NAME);
    Serial0.printf("CPU: %u MHz | Free heap: %u | PSRAM: %u\n",
                   getCpuFrequencyMhz(),
                   static_cast<unsigned>(ESP.getFreeHeap()),
                   static_cast<unsigned>(ESP.getFreePsram()));
    Serial0.println("Verbose UART logging: enabled (UART0 / Serial0)");
    Serial0.println("================================================");
    Serial0.flush();

    app.begin();
    Serial0.println("[BOOT] Application initialisation complete");
}

void loop() {
    app.update();
}
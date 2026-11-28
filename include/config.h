#pragma once

#include <Arduino.h>

// Target: GOOUUU ESP32-S3-CAM N16R8.
// Keep camera, SD, PSRAM and onboard LED pins reserved.

namespace BoardConfig {
    constexpr char NAME[] = "GOOUUU ESP32-S3-CAM N16R8";
    constexpr uint32_t SERIAL_BAUD = 115200;
}

namespace DisplayConfig {
    constexpr int WIDTH = 240;
    constexpr int HEIGHT = 320;
    constexpr int PIN_SCLK = 42;
    constexpr int PIN_MOSI = 41;
    constexpr int PIN_MISO = -1;
    constexpr int PIN_CS = 47;
    constexpr int PIN_DC = 45;
    // Hardware reset is tied high externally; use the controller's software reset.
    constexpr int PIN_RESET = -1;
    constexpr int PIN_BACKLIGHT = -1;
    constexpr bool BACKLIGHT_ACTIVE_HIGH = true;
}

// Onboard BOOT button (active LOW, with internal pull-up).
namespace InputConfig {
    constexpr int PIN_BOOT = 0;
    constexpr uint32_t DEBOUNCE_MS = 35;
    constexpr uint32_t LONG_PRESS_MS = 600;
    constexpr uint32_t VERY_LONG_PRESS_MS = 1800;
}

// I2C bus reserved for automatic SX1509 detection.
// GPIO14/21 are kept separate from the temporary direct trackball wiring.
namespace I2CConfig {
    constexpr int PIN_SDA = 14;
    constexpr int PIN_SCL = 21;
    constexpr uint32_t CLOCK_HZ = 400000;
    constexpr uint8_t SX1509_PRIMARY_ADDRESS = 0x3E;
    constexpr uint8_t SX1509_SECONDARY_ADDRESS = 0x3F;
}

// IR signals use native ESP32-S3 RMT GPIOs; do not route through SX1509.
namespace IRConfig {
    constexpr int PIN_TX = 1;
    constexpr int PIN_RX = 2;
    constexpr bool RX_INVERTED = true;
    constexpr uint32_t DEFAULT_CARRIER_HZ = 38000;
}

// BlackBerry Hall-effect trackball. Direction signals are on SX1509 #1.
// GPIO1/2 are reserved for IR TX/RX; BOOT remains the select button.
namespace TrackballConfig {
    constexpr int PIN_UP = -1;
    constexpr int PIN_DOWN = -1;
    constexpr uint8_t SX1509_PIN_UP = 0;
    constexpr uint8_t SX1509_PIN_DOWN = 1;
    constexpr uint32_t EXPANDER_POLL_MS = 2;
}

// GOOUUU onboard microSD slot: SD_MMC, 1-bit mode.
namespace SDConfig {
    constexpr int PIN_CMD = 38;
    constexpr int PIN_CLK = 39;
    constexpr int PIN_D0 = 40;
    constexpr char MOUNT_POINT[] = "/sdcard";
}

namespace CameraConfig {
    constexpr int PIN_SIOD = 4;
    constexpr int PIN_SIOC = 5;
    constexpr int PIN_VSYNC = 6;
    constexpr int PIN_HREF = 7;
    constexpr int PIN_Y2 = 11;
    constexpr int PIN_Y3 = 9;
    constexpr int PIN_Y4 = 8;
    constexpr int PIN_Y5 = 10;
    constexpr int PIN_Y6 = 12;
    constexpr int PIN_Y7 = 18;
    constexpr int PIN_Y8 = 17;
    constexpr int PIN_Y9 = 16;
    constexpr int PIN_PCLK = 13;
    constexpr int PIN_XCLK = 15;
    constexpr int PIN_PWDN = -1;
    constexpr int PIN_RESET = -1;
}

namespace ReservedPins {
    constexpr int CAMERA[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18};
    constexpr int SD_CARD[] = {38, 39, 40};
    constexpr int RGB_LED = 48;
    // GPIO26-37 are connected to flash/PSRAM resources on N16R8 modules.
}

#pragma once

#include <Arduino.h>

// Target: GOOUUU ESP32-S3-CAM N16R8.
// Keep camera, SD, PSRAM and onboard LED pins reserved.

namespace BoardConfig {
    constexpr char NAME[] = "GOOUUU ESP32-S3-CAM N16R8";
    constexpr uint32_t SERIAL_BAUD = 115200;
}

// Default panel: ST7789, 240x320. Pin map follows the carrier TFT header.
// GPIO0 is TFT reset and an ESP32-S3 strapping pin.
namespace DisplayConfig {
    constexpr int WIDTH = 240;
    constexpr int HEIGHT = 320;
    constexpr int PIN_SCLK = 42;
    constexpr int PIN_MOSI = 41;
    constexpr int PIN_MISO = -1;
    constexpr int PIN_CS = 47;
    constexpr int PIN_DC = 45;
    constexpr int PIN_RESET = 0;
    constexpr int PIN_BACKLIGHT = -1;
    constexpr bool BACKLIGHT_ACTIVE_HIGH = true;
}

namespace ReservedPins {
    constexpr int CAMERA[] = {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18};
    constexpr int SD_CARD[] = {38, 39, 40};
    constexpr int RGB_LED = 48;
    // GPIO26-37 are connected to flash/PSRAM resources on N16R8 modules.
}
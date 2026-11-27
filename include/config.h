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
    constexpr int PIN_RESET = 0;
    constexpr int PIN_BACKLIGHT = -1;
    constexpr bool BACKLIGHT_ACTIVE_HIGH = true;
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

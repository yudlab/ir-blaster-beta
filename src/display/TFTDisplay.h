#pragma once

#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <vector>

#include "config.h"

class TFTDisplay {
public:
    TFTDisplay();

    bool begin();
    void drawHome(int selectedItem, const String& status);
    void drawMessage(const String& title, const String& message);
    void drawSystemInfo();
    void drawList(const String& title, const std::vector<String>& items,
                  int selectedItem, const String& footer);
    void drawCamera(const String& status);
    void drawCameraPreview(const uint16_t* pixels, uint16_t width,
                           uint16_t height, const String& status);

private:
    Adafruit_ST7789 _tft;

    void drawHeader(const String& title);
    void drawMenuItem(int y, const String& label, bool selected);
    void drawText(const String& text, int16_t x, int16_t y, uint8_t size,
                  uint16_t foreground, uint16_t background);
};
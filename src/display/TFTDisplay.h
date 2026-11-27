#pragma once
#include <Arduino.h>

class TFTDisplay {
public:
    bool begin();
    void drawHome(int selectedItem, const String& status);
    void drawMessage(const String& title, const String& message);
    void drawSystemInfo();
    void _tft;

private:
    void drawHeader(const String& title);
    void drawMenuItem(int y, const String& label, bool selected);
};

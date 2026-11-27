#pragma once
#include <Arduino.h>
#include "display/TFTDisplay.h"

class Application {
public:
    void begin();
    void update();

private:
    enum class Screen { HOME, SYSTEM_INFO };

    TFTDisplay _display;
    
    Screen _screen = Screen::HOME;
    int _selectedItem = 0;
    uint32_t _lastRefresh = 0;
    bool _pressToExit = false;

    void render();
    void openSelectedItem();
    void returnHome();
};

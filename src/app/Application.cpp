#include "Application.h"

void Application::begin() {
    _display.begin();
    render();
}

void Application::update() {


    if (millis() - _lastRefresh >= 250) {
      
        _lastRefresh = millis();
    }
}

void Application::openSelectedItem() {
    switch (_selectedItem) {
        case 0:
            _screen = Screen::SYSTEM_INFO;
            render();
            break;
    }
}

void Application::returnHome() {
    _screen = Screen::HOME;
    _pressToExit = false;
    render();
}

void Application::render() {
    switch (_screen) {
        case Screen::HOME:
            _display.drawHome(_selectedItem, "Home > IR hardware configured");
            break;
        case Screen::SYSTEM_INFO:
            _display.drawSystemInfo();
            break;
    }
}
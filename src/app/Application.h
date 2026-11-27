#pragma once

#include <Arduino.h>
#include <vector>

#include "camera/CameraService.h"
#include "display/TFTDisplay.h"
#include "storage/StorageManager.h"

class Application {
public:
    void begin();
    void update();

    // Hardware input drivers (e.g. the trackball) call these three methods.
    void moveSelection(int direction);
    void select();
    void back();

private:
    enum class Screen { HOME, SD_BROWSER, CAMERA, SYSTEM_INFO, MESSAGE };

    TFTDisplay _display;
    StorageManager _storage;
    CameraService _camera;

    Screen _screen = Screen::HOME;
    int _selectedItem = 0;
    int _selectedFile = 0;
    uint32_t _lastRefresh = 0;
    String _currentPath = "/";
    String _cameraStatus = "Press to initialise camera";
    String _messageTitle;
    String _messageBody;
    std::vector<StorageEntry> _directoryEntries;

    void render();
    void openHomeItem();
    void refreshDirectory();
    void openSelectedFile();
    void returnHome();
};

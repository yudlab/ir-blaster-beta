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
    void moveSelection(int direction);
    void select();
    void back();

private:
    enum class Screen {
        HOME, SD_TOOLBOX, SD_BROWSER, SD_INFO, SD_RESULT,
        SD_FORMAT_CONFIRM, SD_DELETE_CONFIRM, CAMERA, IMAGE_VIEWER, SYSTEM_INFO, MESSAGE
    };

    TFTDisplay _display;
    StorageManager _storage;
    CameraService _camera;

    Screen _screen = Screen::HOME;
    int _selectedItem = 0;
    int _selectedFile = 0;
    int _viewerReturnSelection = 0;
    uint32_t _lastRefresh = 0;
    uint32_t _lastCameraPreview = 0;
    bool _buttonStableState = true;
    uint32_t _buttonLastAcceptedChange = 0;
    uint32_t _buttonPressedAt = 0;
    String _currentPath = "/";
    String _viewerReturnPath = "/";
    String _cameraStatus = "Starting camera...";
    String _viewingImagePath;
    Screen _resultReturnScreen = Screen::SD_TOOLBOX;
    int _resultReturnSelection = 0;
    String _messageTitle;
    String _messageBody;
    String _resultTitle;
    String _resultBody;
    String _pendingDeletePath;
    std::vector<StorageEntry> _directoryEntries;

    void render();
    void updateCameraPreview();
    void captureCameraImage();
    void openImageViewer(const String& path);
    void openHomeItem();
    void refreshDirectory();
    void openSelectedFile();
    void returnHome();
    void runSDAction(int action);
    void showResult(const String& title, const String& body);
    void pollExpanderTrackball();

    bool _sx1509PrimaryReady = false;
    bool _sx1509SecondaryReady = false;
    bool _trackballUsesExpander = false;
    uint8_t _expanderTrackballPrevious = 0xFF;
    uint32_t _lastExpanderPoll = 0;
};
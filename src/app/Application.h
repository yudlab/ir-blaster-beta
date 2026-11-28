#pragma once

#include <Arduino.h>
#include <vector>

#include "camera/CameraService.h"
#include "display/TFTDisplay.h"
#include "storage/StorageManager.h"
#include "ir/IRService.h"

class Application {
public:
    void begin();
    void update();
    void moveSelection(int direction);
    void select();
    void back();

private:
    enum class Screen {
        HOME, IR_MENU, IR_RUN_SOURCE, IR_SAVE_TARGET, IR_SAVE_WAIT, IR_STATUS, IR_SAVED_REMOTES,
        IR_SAVED_BUTTONS, IR_DATABASE, IR_SCRIPTS, IR_UNIVERSAL, IR_BUTTON_ACTION, IR_SHORTCUTS,
        SD_TOOLBOX, SD_BROWSER, SD_INFO, SD_RESULT,
        SD_FORMAT_CONFIRM, SD_DELETE_CONFIRM, CAMERA, IMAGE_VIEWER, SYSTEM_INFO, MESSAGE
    };

    TFTDisplay _display;
    StorageManager _storage;
    CameraService _camera;
    IRService _ir;

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
    std::vector<String> _irItems;
    String _selectedRemote;
    String _selectedDatabaseCode;
    String _selectedButton;
    String _universalBrand;
    String _universalCategory;
    String _universalFile;
    String _universalCsvPath;
    String _selectedUniversalLabel;
    size_t _selectedUniversalRow = 0;
    int _universalLevel = 0;
    bool _buttonActionIsUniversal = false;

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
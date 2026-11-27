#include "Application.h"

void Application::begin() {
    pinMode(InputConfig::PIN_BOOT, INPUT_PULLUP);
    _buttonLastReading = digitalRead(InputConfig::PIN_BOOT);
    _buttonStableState = _buttonLastReading;
    _buttonLastChange = millis();
    _buttonPressedAt = millis();

    _display.begin();
    _storage.begin();
    render();
}

void Application::update() {
    // BOOT button: short press advances; long press selects.
    // Actions occur on release so holding the button cannot repeat them.
    const uint32_t now = millis();
    const bool reading = digitalRead(InputConfig::PIN_BOOT);

    if (reading != _buttonLastReading) {
        _buttonLastReading = reading;
        _buttonLastChange = now;
    }

    if ((now - _buttonLastChange) >= InputConfig::DEBOUNCE_MS &&
        reading != _buttonStableState) {
        _buttonStableState = reading;

        if (reading == LOW) {
            _buttonPressedAt = now;
        } else {
            const uint32_t pressDuration = now - _buttonPressedAt;
            if (pressDuration >= InputConfig::VERY_LONG_PRESS_MS) {
                back();
            } else if (pressDuration >= InputConfig::LONG_PRESS_MS) {
                select();
            } else {
                moveSelection(1);
            }
        }
    }

    // Serial navigation remains available for debugging:
    // W = previous, S = next, E = select, B = back.
    while (Serial.available()) {
        const char key = static_cast<char>(Serial.read());
        if (key == 'w' || key == 'W') moveSelection(-1);
        else if (key == 's' || key == 'S') moveSelection(1);
        else if (key == 'e' || key == 'E' || key == '\n') select();
        else if (key == 'b' || key == 'B') back();
    }

    if (millis() - _lastRefresh >= 250) {
        _lastRefresh = millis();
    }
}

void Application::moveSelection(int direction) {
    if (_screen == Screen::HOME) {
        _selectedItem = (_selectedItem + direction + 6) % 6;
    } else if (_screen == Screen::SD_BROWSER) {
        const int count = static_cast<int>(_directoryEntries.size()) + 1; // parent entry
        if (count > 0) _selectedFile = (_selectedFile + direction + count) % count;
    }
    render();
}

void Application::select() {
    if (_screen == Screen::HOME) {
        openHomeItem();
        return;
    }

    if (_screen == Screen::SD_BROWSER) {
        openSelectedFile();
        return;
    }

    if (_screen == Screen::CAMERA) {
        if (!_camera.isReady()) {
            _cameraStatus = _camera.begin()
                ? "Camera ready. Press to capture."
                : "Camera init failed; check camera connection.";
        } else {
            if (!_storage.isMounted() && !_storage.begin()) {
                _cameraStatus = "SD card not available.";
            } else {
                String savedPath;
                _cameraStatus = _camera.captureAndSave(_storage, savedPath)
                    ? "Saved: " + savedPath
                    : "Capture/save failed. Check SD card.";
            }
        }
        render();
        return;
    }

    if (_screen == Screen::SYSTEM_INFO || _screen == Screen::MESSAGE) {
        returnHome();
    }
}

void Application::back() {
    if (_screen == Screen::SD_BROWSER && _currentPath != "/") {
        const int slash = _currentPath.lastIndexOf('/');
        _currentPath = slash <= 0 ? "/" : _currentPath.substring(0, slash);
        refreshDirectory();
        return;
    }
    returnHome();
}

void Application::openHomeItem() {
    switch (_selectedItem) {
        case 0:
            _messageTitle = "IR RECEIVER";
            _messageBody = "Receiver module not connected yet.";
            _screen = Screen::MESSAGE;
            break;
        case 1:
            _messageTitle = "IR TRANSMITTER";
            _messageBody = "Transmitter module not connected yet.";
            _screen = Screen::MESSAGE;
            break;
        case 2:
            _messageTitle = "TRACKBALL";
            _messageBody = "Trackball input is not wired yet.";
            _screen = Screen::MESSAGE;
            break;
        case 3:
            _currentPath = "/";
            _selectedFile = 0;
            if (!_storage.isMounted()) _storage.begin();
            refreshDirectory();
            _screen = Screen::SD_BROWSER;
            break;
        case 4:
            _screen = Screen::CAMERA;
            _cameraStatus = _camera.isReady()
                ? "Camera ready. Press to capture."
                : "Press to initialise camera.";
            break;
        case 5:
            _screen = Screen::SYSTEM_INFO;
            break;
    }
    render();
}

void Application::refreshDirectory() {
    _directoryEntries = _storage.listDirectory(_currentPath);
    const int maxSelection = static_cast<int>(_directoryEntries.size());
    if (_selectedFile > maxSelection) _selectedFile = maxSelection;
    if (_selectedFile < 0) _selectedFile = 0;
}

void Application::openSelectedFile() {
    if (_selectedFile == 0) {
        if (_currentPath == "/") return;
        back();
        return;
    }

    const StorageEntry& entry = _directoryEntries[_selectedFile - 1];
    if (entry.isDirectory) {
        _currentPath = entry.path;
        _selectedFile = 0;
        refreshDirectory();
        render();
        return;
    }

    _messageTitle = entry.name;
    _messageBody = String("File size: ") + String(entry.size) + " bytes";
    _screen = Screen::MESSAGE;
    render();
}

void Application::returnHome() {
    _screen = Screen::HOME;
    _selectedItem = 0;
    render();
}

void Application::render() {
    switch (_screen) {
        case Screen::HOME:
            _display.drawHome(_selectedItem,
                _storage.isMounted() ? "SD card mounted" : "SD card not mounted");
            break;

        case Screen::SD_BROWSER: {
            std::vector<String> labels;
            labels.push_back(_currentPath == "/" ? "[Root]" : "[..] Parent directory");
            for (const StorageEntry& entry : _directoryEntries) {
                labels.push_back(String(entry.isDirectory ? "[DIR] " : "[FILE] ") + entry.name);
            }
            const String footer = _storage.isMounted()
                ? String(_directoryEntries.size()) + " item(s)  " + _currentPath
                : "No SD card mounted";
            _display.drawList("SD CARD", labels, _selectedFile, footer);
            break;
        }

        case Screen::CAMERA:
            _display.drawCamera(_cameraStatus);
            break;

        case Screen::SYSTEM_INFO:
            _display.drawSystemInfo();
            break;

        case Screen::MESSAGE:
            _display.drawMessage(_messageTitle, _messageBody);
            break;
    }
}

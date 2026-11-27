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
    const uint32_t now = millis();
    const bool reading = digitalRead(InputConfig::PIN_BOOT);
    if (reading != _buttonLastReading) {
        _buttonLastReading = reading;
        _buttonLastChange = now;
    }
    if ((now - _buttonLastChange) >= InputConfig::DEBOUNCE_MS &&
        reading != _buttonStableState) {
        _buttonStableState = reading;
        if (reading == LOW) _buttonPressedAt = now;
        else {
            const uint32_t duration = now - _buttonPressedAt;
            if (_screen == Screen::CAMERA) {
                if (duration >= InputConfig::LONG_PRESS_MS) back();
                else captureCameraImage();
            } else if (duration >= InputConfig::VERY_LONG_PRESS_MS) back();
            else if (duration >= InputConfig::LONG_PRESS_MS) select();
            else moveSelection(1);
        }
    }

    while (Serial.available()) {
        const char key = static_cast<char>(Serial.read());
        if (key == 'w' || key == 'W') moveSelection(-1);
        else if (key == 's' || key == 'S') moveSelection(1);
        else if (key == 'e' || key == 'E' || key == '\n') {
            if (_screen == Screen::CAMERA) captureCameraImage();
            else select();
        }
        else if (key == 'b' || key == 'B') back();
    }

    if (_screen == Screen::CAMERA && _camera.isReady() &&
        millis() - _lastCameraPreview >= 100) {
        updateCameraPreview();
        _lastCameraPreview = millis();
    }
    if (millis() - _lastRefresh >= 250) _lastRefresh = millis();
}

void Application::moveSelection(int direction) {
    int count = 1;
    switch (_screen) {
        case Screen::HOME: count = 6; break;
        case Screen::SD_TOOLBOX: count = 6; break;
        case Screen::SD_BROWSER: count = static_cast<int>(_directoryEntries.size()) + 4; break;
        case Screen::SD_FORMAT_CONFIRM: case Screen::SD_DELETE_CONFIRM: count = 2; break;
        default: return;
    }
    _selectedItem = (_selectedItem + direction + count) % count;
    if (_screen == Screen::SD_BROWSER) _selectedFile = _selectedItem;
    render();
}

void Application::select() {
    if (_screen == Screen::HOME) { openHomeItem(); return; }
    if (_screen == Screen::SD_TOOLBOX) { runSDAction(_selectedItem); return; }
    if (_screen == Screen::SD_BROWSER) { _selectedFile = _selectedItem; openSelectedFile(); return; }
    if (_screen == Screen::SD_DELETE_CONFIRM) {
        if (_selectedItem == 1 && _storage.remove(_pendingDeletePath))
            showResult("FILE DELETED", _pendingDeletePath);
        else showResult("DELETE CANCELLED", "No file was deleted.");
        return;
    }
    if (_screen == Screen::SD_FORMAT_CONFIRM) {
        if (_selectedItem == 1) {
            String report;
            const bool ok = _storage.formatCard(report);
            showResult(ok ? "FORMAT COMPLETE" : "FORMAT FAILED", report);
        } else {
            _screen = Screen::SD_TOOLBOX;
            _selectedItem = 0;
            render();
        }
        return;
    }
    if (_screen == Screen::CAMERA) { captureCameraImage(); return; }
    if (_screen == Screen::SD_RESULT || _screen == Screen::SD_INFO ||
        _screen == Screen::SYSTEM_INFO || _screen == Screen::MESSAGE) {
        if (_screen == Screen::SD_RESULT || _screen == Screen::SD_INFO) {
            _screen = Screen::SD_TOOLBOX;
            _selectedItem = 0;
            render();
        } else returnHome();
    }
}

void Application::back() {
    if (_screen == Screen::CAMERA) {
        _camera.end();
        returnHome();
        return;
    }
    if (_screen == Screen::SD_BROWSER && _currentPath != "/") {
        const int slash = _currentPath.lastIndexOf('/');
        _currentPath = slash <= 0 ? "/" : _currentPath.substring(0, slash);
        refreshDirectory();
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::SD_BROWSER || _screen == Screen::SD_INFO ||
        _screen == Screen::SD_RESULT || _screen == Screen::SD_FORMAT_CONFIRM ||
        _screen == Screen::SD_DELETE_CONFIRM) {
        _screen = Screen::SD_TOOLBOX;
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::SD_TOOLBOX) { returnHome(); return; }
    returnHome();
}

void Application::openHomeItem() {
    switch (_selectedItem) {
        case 0: _messageTitle = "IR RECEIVER"; _messageBody = "Receiver module not connected yet."; _screen = Screen::MESSAGE; break;
        case 1: _messageTitle = "IR TRANSMITTER"; _messageBody = "Transmitter module not connected yet."; _screen = Screen::MESSAGE; break;
        case 2: _messageTitle = "TRACKBALL"; _messageBody = "Trackball input is not wired yet."; _screen = Screen::MESSAGE; break;
        case 3:
            if (!_storage.isMounted()) _storage.begin();
            _screen = Screen::SD_TOOLBOX;
            _selectedItem = 0;
            break;
        case 4:
            _screen = Screen::CAMERA;
            _cameraStatus = "Initialising camera...";
            render();
            if (_camera.begin()) {
                _cameraStatus = "Live preview | click to save";
                updateCameraPreview();
                _lastCameraPreview = millis();
            } else {
                _cameraStatus = "Camera init failed. Click to retry.";
                render();
            }
            return;
        case 5: _screen = Screen::SYSTEM_INFO; break;
    }
    render();
}

void Application::updateCameraPreview() {
    const uint16_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    if (_camera.getPreviewFrame(pixels, width, height)) {
        _display.drawCameraPreview(pixels, width, height, "CLICK: SAVE  |  HOLD: EXIT");
    } else {
        _cameraStatus = "Preview frame unavailable";
        _display.drawCamera(_cameraStatus);
    }
}

void Application::captureCameraImage() {
    if (!_camera.isReady()) {
        _cameraStatus = _camera.begin()
            ? "Camera ready; starting preview..."
            : "Camera init failed. Check serial log.";
        render();
        return;
    }

    if (!_storage.isMounted() && !_storage.begin()) {
        _cameraStatus = "SD unavailable - check card";
        render();
        return;
    }

    String savedPath;
    if (_camera.captureAndSave(_storage, savedPath)) {
        _cameraStatus = "Saved " + savedPath;
        Serial.println("[CAM] Capture complete; preview continues");
    } else {
        _cameraStatus = "Capture failed - see serial log";
    }
    // Capture feedback is shown immediately; the live preview resumes on the
    // next update tick without requiring another button press.
    const uint16_t* pixels = nullptr;
    uint16_t width = 0;
    uint16_t height = 0;
    if (_camera.getPreviewFrame(pixels, width, height)) {
        _display.drawCameraPreview(pixels, width, height, _cameraStatus);
    } else {
        _display.drawCamera(_cameraStatus);
    }
    _lastCameraPreview = millis();
}

void Application::runSDAction(int action) {
    if (!_storage.isMounted() && !_storage.begin()) {
        showResult("SD NOT READY", "Card could not be mounted. Check card and pins.");
        return;
    }

    if (action == 0) {
        _currentPath = "/";
        _selectedItem = 0;
        refreshDirectory();
        _screen = Screen::SD_BROWSER;
        render();
    } else if (action == 1) {
        _screen = Screen::SD_INFO;
        render();
    } else if (action == 2) {
        const SDBenchmarkResult r = _storage.runBenchmark();
        const double writeRate = r.writeMs ? (double)r.bytesWritten * 1000.0 / r.writeMs / 1048576.0 : 0;
        const double readRate = r.readMs ? (double)r.bytesRead * 1000.0 / r.readMs / 1048576.0 : 0;
        showResult("SD SPEED TEST",
            String("Write: ") + String(writeRate, 2) + " MB/s\nRead: " +
            String(readRate, 2) + " MB/s\nVerify: " +
            (r.success ? "PASS" : "FAIL"));
    } else if (action == 3) {
        String report;
        const bool ok = _storage.runCardTest(report);
        showResult(ok ? "CARD TEST PASS" : "CARD TEST FAILED", report);
    } else if (action == 4) {
        _screen = Screen::SD_FORMAT_CONFIRM;
        _selectedItem = 0;
        render();
    } else if (action == 5) {
        const bool ok = _storage.refresh();
        showResult(ok ? "SD REFRESHED" : "SD REFRESH FAILED",
                   ok ? "Card remounted successfully." : "Card could not be remounted.");
    }
}

void Application::refreshDirectory() {
    _directoryEntries = _storage.listDirectory(_currentPath);
    const int maxSelection = static_cast<int>(_directoryEntries.size());
    if (_selectedItem > maxSelection) _selectedItem = maxSelection;
    if (_selectedItem < 0) _selectedItem = 0;
}

void Application::openSelectedFile() {
    const int fileCount = static_cast<int>(_directoryEntries.size());
    if (_selectedFile == 0) {
        if (_currentPath != "/") back();
        else showResult("SD ROOT", _directoryEntries.empty()
            ? "Card is mounted. The root folder is empty."
            : String(fileCount) + " item(s) in root.");
        return;
    }
    if (_selectedFile == fileCount + 1) {
        String folder = _currentPath == "/" ? "/NewFolder" : _currentPath + "/NewFolder";
        int suffix = 2;
        while (_storage.exists(folder)) {
            folder = (_currentPath == "/" ? "/" : _currentPath + "/") + "NewFolder_" + String(suffix++);
        }
        const bool ok = _storage.createDirectory(folder);
        refreshDirectory();
        showResult(ok ? "FOLDER CREATED" : "CREATE FAILED", folder);
        return;
    }
    if (_selectedFile == fileCount + 2) {
        if (fileCount == 0) { showResult("NOTHING TO DELETE", "This directory has no files."); return; }
        _pendingDeletePath = _directoryEntries[fileCount - 1].path;
        _screen = Screen::SD_DELETE_CONFIRM;
        _selectedItem = 0;
        render();
        return;
    }
    if (_selectedFile == fileCount + 3) {
        if (fileCount == 0) { showResult("NOTHING TO RENAME", "This directory has no files."); return; }
        const StorageEntry& target = _directoryEntries[fileCount - 1];
        const int dot = target.name.lastIndexOf('.');
        const String base = dot > 0 ? target.name.substring(0, dot) : target.name;
        const String ext = dot > 0 ? target.name.substring(dot) : "";
        String renamed = base + "_renamed" + ext;
        String destination = _currentPath == "/" ? "/" + renamed : _currentPath + "/" + renamed;
        int suffix = 2;
        while (_storage.exists(destination)) {
            renamed = base + "_renamed_" + String(suffix++) + ext;
            destination = _currentPath == "/" ? "/" + renamed : _currentPath + "/" + renamed;
        }
        const bool ok = _storage.rename(target.path, destination);
        refreshDirectory();
        showResult(ok ? "FILE RENAMED" : "RENAME FAILED", destination);
        return;
    }
    const StorageEntry& entry = _directoryEntries[_selectedFile - 1];
    if (entry.isDirectory) {
        _currentPath = entry.path;
        _selectedItem = 0;
        refreshDirectory();
        render();
        return;
    }
    showResult(entry.name, String("File size: ") + String(entry.size) + " bytes\nPath: " + entry.path);
}

void Application::showResult(const String& title, const String& body) {
    _resultTitle = title;
    _resultBody = body;
    _screen = Screen::SD_RESULT;
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
            _display.drawHome(_selectedItem, _storage.isMounted() ? "SD card mounted" : "SD card not mounted");
            break;
        case Screen::SD_TOOLBOX: {
            const std::vector<String> items = {
                "File Manager", "Card Information", "Speed Test",
                "Card Integrity Test", "Format SD Card", "Refresh / Remount"
            };
            _display.drawList("SD TOOLBOX", items, _selectedItem,
                _storage.isMounted() ? "SD READY  " + String(_storage.freeBytes() / 1024 / 1024) + " MB free"
                                     : "SD NOT MOUNTED");
            break;
        }
        case Screen::SD_BROWSER: {
            std::vector<String> labels;
            labels.push_back(_currentPath == "/" ? "[ROOT] Open / status" : "[..] Parent directory");
            for (const StorageEntry& entry : _directoryEntries)
                labels.push_back(String(entry.isDirectory ? "[DIR] " : "[FILE] ") + entry.name);
            labels.push_back("[+] Create folder");
            labels.push_back("[-] Delete last listed item");
            labels.push_back("[R] Rename last listed item");
            const String directoryStatus = _directoryEntries.empty()
                ? "EMPTY  " + _currentPath
                : String(_directoryEntries.size()) + " item(s)  " + _currentPath;
            _display.drawList("FILE MANAGER", labels, _selectedItem, directoryStatus);
            break;
        }
        case Screen::SD_INFO: {
            const std::vector<String> info = {
                "Type: " + _storage.cardTypeName(),
                "Card: " + String(_storage.cardSize() / 1048576ULL) + " MB",
                "Volume: " + String(_storage.totalBytes() / 1048576ULL) + " MB",
                "Used: " + String(_storage.usedBytes() / 1048576ULL) + " MB",
                "Free: " + String(_storage.freeBytes() / 1048576ULL) + " MB",
                "Mode: SDMMC 1-bit"
            };
            _display.drawList("CARD INFORMATION", info, 0, "Long: return");
            break;
        }
        case Screen::SD_RESULT:
            _display.drawMessage(_resultTitle, _resultBody);
            break;
        case Screen::SD_FORMAT_CONFIRM: {
            const std::vector<String> choices = {"Cancel", "YES - FORMAT CARD"};
            _display.drawList("ERASE ALL DATA?", choices, _selectedItem,
                "Formatting deletes every file");
            break;
        }
        case Screen::SD_DELETE_CONFIRM: {
            const std::vector<String> choices = {"Cancel", "YES - DELETE"};
            _display.drawList("DELETE SELECTED?", choices, _selectedItem, _pendingDeletePath);
            break;
        }
        case Screen::CAMERA:
            if (!_camera.isReady()) _display.drawCamera(_cameraStatus);
            break;
        case Screen::SYSTEM_INFO: _display.drawSystemInfo(); break;
        case Screen::MESSAGE: _display.drawMessage(_messageTitle, _messageBody); break;
    }
}

#include "Application.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include <Wire.h>

namespace {
constexpr uint8_t SX1509_REG_DIR_B = 0x0E;
constexpr uint8_t SX1509_REG_DIR_A = 0x0F;
constexpr uint8_t SX1509_REG_PULLUP_A = 0x07;
constexpr uint8_t SX1509_REG_DATA_A = 0x11;

bool sx1509WriteRegister(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool sx1509ReadRegister(uint8_t address, uint8_t reg, uint8_t& value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(static_cast<int>(address), 1) != 1) return false;
    value = Wire.read();
    return true;
}

bool sx1509Responds(uint8_t address) {
    uint8_t value = 0;
    return sx1509ReadRegister(address, SX1509_REG_DATA_A, value);
}

bool sx1509ConfigureTrackball(uint8_t address) {
    // Configure the first four pins as inputs; enable internal pull-ups on
    // IO0/IO1 (UP/DOWN). The trackball module may also have onboard pull-ups.
    return sx1509WriteRegister(address, SX1509_REG_DIR_A, 0xFF) &&
           sx1509WriteRegister(address, SX1509_REG_DIR_B, 0xFF) &&
           sx1509WriteRegister(address, SX1509_REG_PULLUP_A, 0x0F);
}
}

namespace {
constexpr uint8_t BUTTON_QUEUE_SIZE = 16;
volatile uint8_t buttonQueueHead = 0;
volatile uint8_t buttonQueueTail = 0;
volatile uint32_t buttonQueueTimes[BUTTON_QUEUE_SIZE] = {};
volatile bool buttonQueueStates[BUTTON_QUEUE_SIZE] = {};
volatile bool buttonQueueOverflow = false;

// Hall-effect trackball outputs pulse LOW as the ball rotates.
// The SX1509 port is sampled frequently and falling transitions are queued.
portMUX_TYPE trackballMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t trackballUpEdges = 0;
volatile uint16_t trackballDownEdges = 0;
volatile uint16_t trackballLeftEdges = 0;
volatile uint16_t trackballRightEdges = 0;

void IRAM_ATTR onBootButtonEdge() {
    const uint8_t head = buttonQueueHead;
    const uint8_t next = static_cast<uint8_t>((head + 1) % BUTTON_QUEUE_SIZE);
    if (next == buttonQueueTail) {
        buttonQueueOverflow = true;
        return;
    }

    buttonQueueTimes[head] = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    buttonQueueStates[head] = gpio_get_level(static_cast<gpio_num_t>(InputConfig::PIN_BOOT)) != 0;
    buttonQueueHead = next;
}
}

void Application::begin() {
    Serial0.println("[APP] Application::begin() starting");
    pinMode(InputConfig::PIN_BOOT, INPUT_PULLUP);
    _buttonStableState = digitalRead(InputConfig::PIN_BOOT);
    _buttonLastAcceptedChange = millis();
    buttonQueueHead = 0;
    buttonQueueTail = 0;
    buttonQueueOverflow = false;
    attachInterrupt(digitalPinToInterrupt(InputConfig::PIN_BOOT), onBootButtonEdge, CHANGE);

    // Probe the shared I2C bus at boot. GPIO1/2 are reserved for IR RMT, so
    // trackball direction is available only when SX1509 #1 is connected.
    Wire.begin(I2CConfig::PIN_SDA, I2CConfig::PIN_SCL);
    Wire.setClock(I2CConfig::CLOCK_HZ);
    _sx1509PrimaryReady = sx1509Responds(I2CConfig::SX1509_PRIMARY_ADDRESS);
    _sx1509SecondaryReady = sx1509Responds(I2CConfig::SX1509_SECONDARY_ADDRESS);
    if (_sx1509PrimaryReady) {
        _trackballUsesExpander = sx1509ConfigureTrackball(I2CConfig::SX1509_PRIMARY_ADDRESS);
        if (_trackballUsesExpander) {
            sx1509ReadRegister(I2CConfig::SX1509_PRIMARY_ADDRESS,
                               SX1509_REG_DATA_A, _expanderTrackballPrevious);
            Serial0.println("[SX1509] Primary detected at 0x3E; trackball UP/DOWN/LEFT/RIGHT use IO0-IO3");
        } else {
            _sx1509PrimaryReady = false;
            Serial0.println("[SX1509] Primary detected but configuration failed; trackball direction disabled.");
        }
    }
    if (_sx1509SecondaryReady)
        Serial0.println("[SX1509] Secondary detected at 0x3F");

    trackballUpEdges = 0;
    trackballDownEdges = 0;
    trackballLeftEdges = 0;
    trackballRightEdges = 0;
    if (_trackballUsesExpander) {
        Serial0.println("[TRACKBALL] UP/DOWN/LEFT/RIGHT=SX1509 IO0/IO1/IO2/IO3; BOOT=SELECT");
    } else {
        Serial0.println("[TRACKBALL] SX1509 #1 not detected; direction inputs disabled. BOOT remains SELECT.");
    }
    Serial0.println("[APP] Initialising TFT display...");
    _display.begin();
    Serial0.println("[APP] Initialising SD storage...");
    _storage.begin();
    Serial0.printf("[APP] SD mounted: %s\n", _storage.isMounted() ? "yes" : "no");
    _ir.begin();
    Serial0.println("[IR] Type HELP in the USB serial monitor for remote/script commands.");
    render();
    Serial0.println("[APP] Initial screen rendered");
}

void Application::update() {
    _ir.poll();
    if (_screen == Screen::IR_SAVE_WAIT && _ir.captureFinished()) {
        _messageTitle = _ir.lastCaptureSucceeded() ? "IR SAVED" : "IR SAVE FAILED";
        _messageBody = _ir.lastCaptureSucceeded()
            ? _ir.lastCaptureLabel() + "\nSaved to SD card."
            : _ir.lastCaptureLabel() + "\nCheck receiver and SD card.";
        _screen = Screen::IR_STATUS;
        _selectedItem = 0;
        render();
    }
    if (_trackballUsesExpander) pollExpanderTrackball();

    // Drain trackball pulse counters outside the ISR. UP moves the selection
    // one item backwards; DOWN moves it one item forwards.
    uint16_t upEdges = 0, downEdges = 0, leftEdges = 0, rightEdges = 0;
    portENTER_CRITICAL(&trackballMux);
    upEdges = trackballUpEdges;
    downEdges = trackballDownEdges;
    leftEdges = trackballLeftEdges;
    rightEdges = trackballRightEdges;
    trackballUpEdges = trackballDownEdges = 0;
    trackballLeftEdges = trackballRightEdges = 0;
    portEXIT_CRITICAL(&trackballMux);

    // Apply the net movement in one redraw, even if several pulses arrived
    // while the display or camera was busy.
    const int trackballMovement = static_cast<int>(downEdges) - static_cast<int>(upEdges);
    if (trackballMovement != 0) {
        Serial.printf("[TRACKBALL] UP=%u DOWN=%u movement=%d\n",
                      upEdges, downEdges, trackballMovement);
        moveSelection(trackballMovement);
    }
    if (leftEdges) back();
    if (rightEdges) select();

    // Button edges are captured by an ISR, so short presses are not lost while
    // camera capture/preview or TFT rendering temporarily blocks this loop.
    while (buttonQueueTail != buttonQueueHead) {
        const uint8_t tail = buttonQueueTail;
        const uint32_t eventTime = buttonQueueTimes[tail];
        const bool reading = buttonQueueStates[tail];
        buttonQueueTail = static_cast<uint8_t>((tail + 1) % BUTTON_QUEUE_SIZE);

        if (reading == _buttonStableState ||
            (eventTime - _buttonLastAcceptedChange) < InputConfig::DEBOUNCE_MS) {
            continue;
        }

        _buttonStableState = reading;
        _buttonLastAcceptedChange = eventTime;
        if (reading == LOW) {
            _buttonPressedAt = eventTime;
            Serial.println("[BTN] BOOT pressed");
        } else {
            const uint32_t duration = eventTime - _buttonPressedAt;
            Serial.printf("[BTN] BOOT released after %lu ms\n",
                          static_cast<unsigned long>(duration));
            if (_screen == Screen::CAMERA) {
                if (duration >= InputConfig::LONG_PRESS_MS) back();
                else captureCameraImage();
            } else if (_screen == Screen::IMAGE_VIEWER) {
                if (duration >= InputConfig::LONG_PRESS_MS) back();
            } else if (duration >= InputConfig::VERY_LONG_PRESS_MS) back();
            else if (duration >= InputConfig::LONG_PRESS_MS) select();
            else moveSelection(1);
        }
    }

    if (buttonQueueOverflow) {
        buttonQueueOverflow = false;
        Serial.println("[BTN] Warning: button event queue overflowed");
    }

    // Uppercase commands are line-oriented; lowercase w/s/e/b remain quick
    // navigation shortcuts. Physical BOOT/trackball controls remain available.
    static String serialLine;
    while (Serial.available()) {
        const char key = static_cast<char>(Serial.read());
        if (key == '\r') continue;
        if (key == '\n') {
            if (serialLine.length()) _ir.handleCommand(serialLine, Serial);
            serialLine = "";
            continue;
        }
        if (!serialLine.length() && key == 'w') moveSelection(-1);
        else if (!serialLine.length() && key == 's') moveSelection(1);
        else if (!serialLine.length() && key == 'e') {
            if (_screen == Screen::CAMERA) captureCameraImage();
            else select();
        }
        else if (!serialLine.length() && key == 'b') back();
        else if (serialLine.length() < 160) serialLine += key;
    }

    if (_screen == Screen::CAMERA && _camera.isReady() &&
        millis() - _lastCameraPreview >= 100) {
        updateCameraPreview();
        _lastCameraPreview = millis();
    }
    if (millis() - _lastRefresh >= 250) _lastRefresh = millis();
}

void Application::pollExpanderTrackball() {
    if (millis() - _lastExpanderPoll < TrackballConfig::EXPANDER_POLL_MS) return;
    _lastExpanderPoll = millis();

    uint8_t current = 0xFF;
    if (!sx1509ReadRegister(I2CConfig::SX1509_PRIMARY_ADDRESS,
                            SX1509_REG_DATA_A, current)) {
        Serial0.println("[SX1509] Read failed; expander trackball temporarily unavailable");
        return;
    }

    // Hall outputs idle HIGH and pulse LOW. Count each HIGH-to-LOW transition.
    const uint8_t falling = static_cast<uint8_t>(_expanderTrackballPrevious & ~current);
    if (falling & (1U << TrackballConfig::SX1509_PIN_UP)) {
        portENTER_CRITICAL(&trackballMux);
        if (trackballUpEdges < 1000) ++trackballUpEdges;
        portEXIT_CRITICAL(&trackballMux);
    }
    if (falling & (1U << TrackballConfig::SX1509_PIN_DOWN)) {
        portENTER_CRITICAL(&trackballMux);
        if (trackballDownEdges < 1000) ++trackballDownEdges;
        portEXIT_CRITICAL(&trackballMux);
    }
    if (falling & (1U << TrackballConfig::SX1509_PIN_LEFT)) {
        portENTER_CRITICAL(&trackballMux);
        if (trackballLeftEdges < 1000) ++trackballLeftEdges;
        portEXIT_CRITICAL(&trackballMux);
    }
    if (falling & (1U << TrackballConfig::SX1509_PIN_RIGHT)) {
        portENTER_CRITICAL(&trackballMux);
        if (trackballRightEdges < 1000) ++trackballRightEdges;
        portEXIT_CRITICAL(&trackballMux);
    }
    _expanderTrackballPrevious = current;
}

void Application::moveSelection(int direction) {
    int count = 1;
    switch (_screen) {
        case Screen::HOME: count = 5; break;
        case Screen::IR_MENU: count = 5; break;
        case Screen::IR_RUN_SOURCE: count = 2; break;
        case Screen::IR_SAVE_TARGET: count = _irItems.empty() ? 1 : static_cast<int>(_irItems.size()); break;
        case Screen::IR_UNIVERSAL:
            count = static_cast<int>(_irItems.size()) +
                    (_universalOffset > 0 ? 1 : 0) + (_universalHasMore ? 1 : 0);
            if (count < 1) count = 1;
            break;
        case Screen::IR_BUTTON_ACTION: case Screen::IR_SHORTCUTS:
            count = _irItems.empty() ? 1 : static_cast<int>(_irItems.size());
            break;
        case Screen::IR_SAVED_REMOTES: case Screen::IR_SAVED_BUTTONS:
        case Screen::IR_DATABASE: case Screen::IR_SCRIPTS: count = _irItems.empty() ? 1 : static_cast<int>(_irItems.size()); break;
        case Screen::SD_TOOLBOX: count = 6; break;
        case Screen::SD_BROWSER:
            count = static_cast<int>(_directoryEntries.size()) + 4 +
                    (_directoryOffset > 0 ? 1 : 0) + (_directoryHasMore ? 1 : 0);
            break;
        case Screen::SD_FORMAT_CONFIRM: case Screen::SD_DELETE_CONFIRM: count = 2; break;
        default: return;
    }
    _selectedItem = (_selectedItem + direction + count) % count;
    if (_screen == Screen::SD_BROWSER) _selectedFile = _selectedItem;
    render();
}

void Application::select() {
    if (_screen == Screen::HOME) { openHomeItem(); return; }

    if (_screen == Screen::IR_MENU) {
        if (!_storage.isMounted() && !_storage.begin()) {
            _messageTitle = "SD NOT READY";
            _messageBody = "Insert a working SD card.";
            _screen = Screen::IR_STATUS;
            render();
            return;
        }
        if (_selectedItem == 0) {
            _ir.listSavedRemotes(_irItems);
            _irItems.push_back(" [ New remote ]");
            _screen = Screen::IR_SAVE_TARGET;
        } else if (_selectedItem == 1) {
            _universalLevel = 0;
            _universalOffset = 0;
            _ir.listUniversalBrands(_irItems, _universalOffset,
                                    UNIVERSAL_PAGE_SIZE, &_universalHasMore);
            _screen = Screen::IR_UNIVERSAL;
        } else if (_selectedItem == 2) {
            _ir.listShortcuts(_irItems);
            _screen = Screen::IR_SHORTCUTS;
        } else if (_selectedItem == 3) {
            _screen = Screen::IR_RUN_SOURCE;
        } else {
            _ir.listScripts(_irItems);
            _screen = Screen::IR_SCRIPTS;
        }
        _selectedItem = 0;
        render();
        return;
    }

    if (_screen == Screen::IR_RUN_SOURCE) {
        if (_selectedItem == 0) {
            _ir.listSavedRemotes(_irItems);
            _screen = Screen::IR_SAVED_REMOTES;
        } else {
            _ir.listDatabaseCodes(_irItems);
            _screen = Screen::IR_DATABASE;
        }
        _selectedItem = 0;
        render();
        return;
    }

    if (_screen == Screen::IR_SAVE_TARGET) {
        String remote;
        if (_selectedItem >= static_cast<int>(_irItems.size()) - 1) remote = _ir.nextRemoteName();
        else remote = _irItems[_selectedItem];
        _ir.beginScreenLearn(remote);
        _screen = _ir.captureFinished() ? Screen::IR_STATUS : Screen::IR_SAVE_WAIT;
        if (_screen == Screen::IR_STATUS) {
            _messageTitle = "IR SAVE FAILED";
            _messageBody = _ir.lastCaptureLabel();
        }
        _selectedItem = 0;
        render();
        return;
    }

    if (_screen == Screen::IR_SAVED_REMOTES) {
        if (_irItems.empty()) {
            _messageTitle = "NO SAVED REMOTES";
            _messageBody = "Record a remote first.";
            _screen = Screen::IR_STATUS;
            render();
            return;
        }
        _selectedRemote = _irItems[_selectedItem];
        _ir.listSavedButtons(_selectedRemote, _irItems);
        _selectedItem = 0;
        _screen = Screen::IR_SAVED_BUTTONS;
        render();
        return;
    }

    if (_screen == Screen::IR_SAVED_BUTTONS) {
        if (_irItems.empty()) {
            _messageTitle = "NO BUTTONS";
            _messageBody = "This remote has no saved buttons.";
            _screen = Screen::IR_STATUS;
            render();
            return;
        }
        _selectedButton = _irItems[_selectedItem];
        _buttonActionIsUniversal = false;
        _irItems = {"Send now", "Add to Remote Shortcuts"};
        _selectedItem = 0;
        _screen = Screen::IR_BUTTON_ACTION;
        render();
        return;
    }

    if (_screen == Screen::IR_UNIVERSAL) {
        const size_t itemCount = _irItems.size();
        const int previousPageIndex = static_cast<int>(itemCount);
        const int nextPageIndex = previousPageIndex + (_universalOffset > 0 ? 1 : 0);

        if (_universalOffset > 0 && _selectedItem == previousPageIndex) {
            _universalOffset = _universalOffset > UNIVERSAL_PAGE_SIZE
                ? _universalOffset - UNIVERSAL_PAGE_SIZE : 0;
        } else if (_universalHasMore && _selectedItem == nextPageIndex) {
            _universalOffset += UNIVERSAL_PAGE_SIZE;
        } else {
            if (_irItems.empty() || _selectedItem < 0 ||
                static_cast<size_t>(_selectedItem) >= itemCount) {
                _messageTitle = "NO IRDB DATA";
                _messageBody = "No entries here. Check the /codes folder on the SD card.";
                _screen = Screen::IR_STATUS;
                render();
                return;
            }

            if (_universalLevel == 0) {
                _universalBrand = _irItems[_selectedItem];
                _universalOffset = 0;
                _ir.listUniversalCategories(_universalBrand, _irItems,
                                            _universalOffset, UNIVERSAL_PAGE_SIZE,
                                            &_universalHasMore);
                _universalLevel = 1;
            } else if (_universalLevel == 1) {
                _universalCategory = _irItems[_selectedItem];
                _universalOffset = 0;
                _ir.listUniversalFiles(_universalBrand, _universalCategory, _irItems,
                                       _universalOffset, UNIVERSAL_PAGE_SIZE,
                                       &_universalHasMore);
                _universalLevel = 2;
            } else if (_universalLevel == 2) {
                _universalFile = _irItems[_selectedItem];
                _universalCsvPath = "/codes/" + _universalBrand + "/" +
                                    _universalCategory + "/" + _universalFile + ".csv";
                _universalOffset = 0;
                _ir.listUniversalButtons(_universalCsvPath, _irItems,
                                         _universalOffset, UNIVERSAL_PAGE_SIZE,
                                         &_universalHasMore);
                _universalLevel = 3;
            } else {
                _selectedUniversalRow = _universalOffset + static_cast<size_t>(_selectedItem);
                _selectedUniversalLabel = _irItems[_selectedItem];
                _buttonActionIsUniversal = true;
                _irItems = {"Send now", "Add to Remote Shortcuts"};
                _selectedItem = 0;
                _screen = Screen::IR_BUTTON_ACTION;
                render();
                return;
            }
        }
        _selectedItem = 0;
        render();
        return;
    }

    if (_screen == Screen::IR_BUTTON_ACTION) {
        const bool sendNow = (_selectedItem == 0);
        bool ok = false;
        if (_buttonActionIsUniversal) {
            if (sendNow) ok = _ir.sendUniversalButton(_universalCsvPath, _selectedUniversalRow);
            else ok = _ir.addUniversalShortcut(_universalCsvPath, _selectedUniversalRow,
                                               _selectedUniversalLabel);
        } else {
            if (sendNow) ok = _ir.sendSavedButton(_selectedRemote, _selectedButton);
            else ok = _ir.addCustomShortcut(_selectedRemote, _selectedButton);
        }
        _messageTitle = sendNow ? (ok ? "IR SENT" : "IR SEND FAILED")
                                : (ok ? "SHORTCUT SAVED" : "SHORTCUT SAVE FAILED");
        _messageBody = _buttonActionIsUniversal ? _selectedUniversalLabel
                                                : _selectedRemote + " " + _selectedButton;
        _screen = Screen::IR_STATUS;
        _selectedItem = 0;
        render();
        return;
    }

    if (_screen == Screen::IR_SHORTCUTS) {
        if (_irItems.empty()) {
            _messageTitle = "NO SHORTCUTS";
            _messageBody = "Add a button using Universal Remote or a saved remote.";
            _screen = Screen::IR_STATUS;
            render();
            return;
        }
        const String label = _irItems[_selectedItem];
        const bool ok = _ir.sendShortcut(static_cast<size_t>(_selectedItem));
        _messageTitle = ok ? "SHORTCUT SENT" : "SHORTCUT FAILED";
        _messageBody = label;
        _screen = Screen::IR_STATUS;
        render();
        return;
    }

    if (_screen == Screen::IR_DATABASE) {
        if (_irItems.empty()) {
            _messageTitle = "DATABASE EMPTY";
            _messageBody = "Put compatible .ir files in /IR/database on the SD card.";
            _screen = Screen::IR_STATUS;
            render();
            return;
        }
        const String code = _irItems[_selectedItem];
        const bool ok = _ir.sendDatabaseCode(String("/IR/database/") + code);
        _messageTitle = ok ? "DATABASE CODE SENT" : "SEND FAILED";
        _messageBody = code;
        _screen = Screen::IR_STATUS;
        render();
        return;
    }

    if (_screen == Screen::IR_SCRIPTS) {
        if (_irItems.empty()) {
            _messageTitle = "NO SCRIPTS";
            _messageBody = "No saved scripts found on SD.";
            _screen = Screen::IR_STATUS;
            render();
            return;
        }
        const String name = _irItems[_selectedItem];
        _ir.runSavedScript(name);
        _messageTitle = "SCRIPT FINISHED";
        _messageBody = name;
        _screen = Screen::IR_STATUS;
        render();
        return;
    }

    if (_screen == Screen::IR_STATUS) {
        _screen = Screen::IR_MENU;
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::IR_SAVE_WAIT) return;

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
    if (_screen == Screen::IMAGE_VIEWER) { back(); return; }
    if (_screen == Screen::SD_RESULT || _screen == Screen::SD_INFO ||
        _screen == Screen::SYSTEM_INFO || _screen == Screen::MESSAGE) {
        if (_screen == Screen::SD_RESULT) {
            _screen = _resultReturnScreen;
            _selectedItem = _resultReturnSelection;
            if (_screen == Screen::SD_BROWSER) refreshDirectory();
            render();
        } else if (_screen == Screen::SD_INFO) {
            _screen = Screen::SD_TOOLBOX;
            _selectedItem = 0;
            render();
        } else returnHome();
    }
}

void Application::back() {
    if (_screen == Screen::IR_STATUS || _screen == Screen::IR_SAVE_WAIT) {
        if (_screen == Screen::IR_SAVE_WAIT) _ir.cancelScreenLearn();
        _screen = Screen::IR_MENU;
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::IR_BUTTON_ACTION) {
        if (_buttonActionIsUniversal) {
            _ir.listUniversalButtons(_universalCsvPath, _irItems,
                                     _universalOffset, UNIVERSAL_PAGE_SIZE,
                                     &_universalHasMore);
            _screen = Screen::IR_UNIVERSAL;
        } else {
            _ir.listSavedButtons(_selectedRemote, _irItems);
            _screen = Screen::IR_SAVED_BUTTONS;
        }
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::IR_SAVED_BUTTONS) {
        _ir.listSavedRemotes(_irItems);
        _screen = Screen::IR_SAVED_REMOTES;
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::IR_UNIVERSAL) {
        _universalOffset = 0;
        if (_universalLevel == 3) {
            _ir.listUniversalFiles(_universalBrand, _universalCategory, _irItems,
                                   0, UNIVERSAL_PAGE_SIZE, &_universalHasMore);
            _universalLevel = 2;
        } else if (_universalLevel == 2) {
            _ir.listUniversalCategories(_universalBrand, _irItems,
                                        0, UNIVERSAL_PAGE_SIZE, &_universalHasMore);
            _universalLevel = 1;
        } else if (_universalLevel == 1) {
            _ir.listUniversalBrands(_irItems, 0, UNIVERSAL_PAGE_SIZE, &_universalHasMore);
            _universalLevel = 0;
        } else {
            _screen = Screen::IR_MENU;
        }
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::IR_SHORTCUTS) {
        _screen = Screen::IR_MENU;
        _selectedItem = 2;
        render();
        return;
    }
    if (_screen == Screen::IR_RUN_SOURCE || _screen == Screen::IR_SAVE_TARGET || _screen == Screen::IR_SAVED_REMOTES || _screen == Screen::IR_DATABASE ||
        _screen == Screen::IR_SCRIPTS) {
        _screen = Screen::IR_MENU;
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::IR_MENU) { returnHome(); return; }
    if (_screen == Screen::SD_RESULT) {
        _screen = _resultReturnScreen;
        _selectedItem = _resultReturnSelection;
        if (_screen == Screen::SD_BROWSER) refreshDirectory();
        render();
        return;
    }
    if (_screen == Screen::CAMERA) {
        _camera.end();
        returnHome();
        return;
    }
    if (_screen == Screen::IMAGE_VIEWER) {
        _currentPath = _viewerReturnPath;
        _screen = Screen::SD_BROWSER;
        refreshDirectory();
        _selectedItem = _viewerReturnSelection;
        const int maxSelection = static_cast<int>(_directoryEntries.size()) + 3 +
            (_directoryOffset > 0 ? 1 : 0) + (_directoryHasMore ? 1 : 0);
        if (_selectedItem > maxSelection) _selectedItem = maxSelection;
        if (_selectedItem < 0) _selectedItem = 0;
        _selectedFile = _selectedItem;
        render();
        return;
    }
    if (_screen == Screen::SD_BROWSER && _currentPath != "/") {
        const int slash = _currentPath.lastIndexOf('/');
        _currentPath = slash <= 0 ? "/" : _currentPath.substring(0, slash);
        _directoryOffset = 0;
        refreshDirectory();
        _selectedItem = 0;
        render();
        return;
    }
    if (_screen == Screen::SD_BROWSER || _screen == Screen::SD_INFO ||
        _screen == Screen::SD_FORMAT_CONFIRM || _screen == Screen::SD_DELETE_CONFIRM) {
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
        case 0: _screen = Screen::IR_MENU; _selectedItem = 0; break;
        case 1: _messageTitle = "TRACKBALL"; _messageBody = "UP / DOWN: move selection\nLEFT: back  RIGHT: open\nBOOT button: select"; _screen = Screen::MESSAGE; break;
        case 2:
            if (!_storage.isMounted()) _storage.begin();
            _screen = Screen::SD_TOOLBOX;
            _selectedItem = 0;
            break;
        case 3:
            Serial0.println("[APP] Camera menu selected");
            _screen = Screen::CAMERA;
            _cameraStatus = "Initialising camera...";
            render();
            Serial0.println("[APP] Calling CameraService::begin()");
            if (_camera.begin()) {
                Serial0.println("[APP] Camera initialised; requesting first preview frame");
                _cameraStatus = "Live preview | click to save";
                updateCameraPreview();
                _lastCameraPreview = millis();
            } else {
                Serial0.println("[APP] Camera initialisation failed");
                _cameraStatus = "Camera init failed. Click to retry.";
                render();
            }
            return;
        case 4: _screen = Screen::SYSTEM_INFO; break;
    }
    render();
}

void Application::updateCameraPreview() {
    Serial0.println("[APP] updateCameraPreview() called");
    if (_camera.drawPreview(_display, _cameraStatus)) {
        Serial0.println("[APP] Camera JPEG decoded directly to TFT");
    } else {
        Serial0.println("[APP] Preview frame unavailable; showing status screen");
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
    if (!_camera.drawPreview(_display, _cameraStatus)) {
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
        _directoryOffset = 0;
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
    _directoryEntries = _storage.listDirectory(
        _currentPath, _directoryOffset, DIRECTORY_PAGE_SIZE, &_directoryHasMore);
    const int maxSelection = static_cast<int>(_directoryEntries.size()) + 3 +
                             (_directoryOffset > 0 ? 1 : 0) +
                             (_directoryHasMore ? 1 : 0);
    if (_selectedItem > maxSelection) _selectedItem = maxSelection;
    if (_selectedItem < 0) _selectedItem = 0;
}

void Application::openSelectedFile() {
    const int fileCount = static_cast<int>(_directoryEntries.size());
    const int previousPageIndex = fileCount + 4;
    const int nextPageIndex = previousPageIndex + (_directoryOffset > 0 ? 1 : 0);
    if (_selectedFile == 0) {
        if (_currentPath != "/") back();
        else showResult("SD ROOT", _directoryEntries.empty()
            ? "Card is mounted. The root folder is empty."
            : String(fileCount) + " item(s) on this page.");
        return;
    }
    if (_directoryOffset > 0 && _selectedFile == previousPageIndex) {
        _directoryOffset = _directoryOffset > DIRECTORY_PAGE_SIZE
            ? _directoryOffset - DIRECTORY_PAGE_SIZE : 0;
        _selectedItem = 0;
        refreshDirectory();
        render();
        return;
    }
    if (_directoryHasMore && _selectedFile == nextPageIndex) {
        _directoryOffset += DIRECTORY_PAGE_SIZE;
        _selectedItem = 0;
        refreshDirectory();
        render();
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
        _directoryOffset = 0;
        _selectedItem = 0;
        refreshDirectory();
        render();
        return;
    }
    String lowerName = entry.name;
    lowerName.toLowerCase();
    if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) {
        openImageViewer(entry.path);
        return;
    }
    showResult(entry.name, String("File size: ") + String(entry.size) + " bytes\nPath: " + entry.path);
}

void Application::openImageViewer(const String& path) {
    _viewingImagePath = path;
    _viewerReturnPath = _currentPath;
    _viewerReturnSelection = _selectedItem;
    _screen = Screen::IMAGE_VIEWER;
    render();
}

void Application::showResult(const String& title, const String& body) {
    _resultReturnScreen = _screen;
    _resultReturnSelection = _selectedItem;
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
        case Screen::IR_MENU: {
            const std::vector<String> items = {"Save IR", "Universal Remote", "Remote Shortcuts", "Run IR Code", "Scripts"};
            _display.drawList("IR", items, _selectedItem, _ir.ready() ? "IR RX/TX READY" : "IR HARDWARE NOT READY");
            break;
        }
        case Screen::IR_RUN_SOURCE: {
            const std::vector<String> sources = {"Saved on SD", "IR Database"};
            _display.drawList("RUN IR CODE", sources, _selectedItem, "Choose code source");
            break;
        }
        case Screen::IR_SAVE_TARGET:
            _display.drawList("SAVE IR TO", _irItems, _selectedItem, "Choose remote or New remote");
            break;
        case Screen::IR_SAVE_WAIT:
            _display.drawMessage("RECORD IR", "Point remote at receiver.\nPress the remote button once.\nSaving to selected remote on SD...");
            break;
        case Screen::IR_STATUS:
            _display.drawMessage(_messageTitle, _messageBody);
            break;
        case Screen::IR_SAVED_REMOTES:
            _display.drawList("SAVED REMOTES", _irItems, _selectedItem, _irItems.empty() ? "No saved remotes" : "Select a remote");
            break;
        case Screen::IR_SAVED_BUTTONS:
            _display.drawList(_selectedRemote, _irItems, _selectedItem, _irItems.empty() ? "No saved buttons" : "Select to transmit");
            break;
        case Screen::IR_UNIVERSAL: {
            const String title = _universalLevel == 0 ? "IRDB BRANDS" :
                                 _universalLevel == 1 ? _universalBrand :
                                 _universalLevel == 2 ? _universalCategory : _universalFile;
            const String footer = _universalLevel == 0 ? "Browse /codes by brand" :
                                  _universalLevel == 1 ? "Choose device category" :
                                  _universalLevel == 2 ? "Choose CSV code set" : "Select a button";
            std::vector<String> labels = _irItems;
            if (_universalOffset > 0) labels.emplace_back("[<] Previous page");
            if (_universalHasMore) labels.emplace_back("[>] Next page");
            _display.drawList(title, labels, _selectedItem, footer);
            break;
        }
        case Screen::IR_BUTTON_ACTION:
            _display.drawList("BUTTON ACTION", _irItems, _selectedItem, "Send or add shortcut");
            break;
        case Screen::IR_SHORTCUTS:
            _display.drawList("REMOTE SHORTCUTS", _irItems, _selectedItem,
                              _irItems.empty() ? "No saved shortcuts" : "Select to transmit");
            break;
        case Screen::IR_DATABASE:
            _display.drawList("IR DATABASE", _irItems, _selectedItem, _irItems.empty() ? "Add .ir files to /IR/database" : "Select code to transmit");
            break;
        case Screen::IR_SCRIPTS:
            _display.drawList("IR SCRIPTS", _irItems, _selectedItem, _irItems.empty() ? "No saved scripts" : "Select script to run");
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
            if (_directoryOffset > 0) labels.push_back("[<] Previous page");
            if (_directoryHasMore) labels.push_back("[>] Next page");
            const String directoryStatus = String(_directoryOffset + 1) + "-" +
                String(_directoryOffset + _directoryEntries.size()) +
                (_directoryHasMore ? "+ items  " : " items  ") + _currentPath;
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
        case Screen::IMAGE_VIEWER:
            if (!_display.drawJpegFromFS(SD_MMC, _viewingImagePath)) {
                _display.drawMessage("IMAGE ERROR", "Could not decode JPEG.\nLong press to return.");
            }
            break;
        case Screen::SYSTEM_INFO: _display.drawSystemInfo(); break;
        case Screen::MESSAGE: _display.drawMessage(_messageTitle, _messageBody); break;
    }
}

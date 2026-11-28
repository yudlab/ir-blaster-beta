#pragma once

#include <Arduino.h>

class StorageManager;
class TFTDisplay;

// Camera frames remain JPEG in PSRAM. Preview decoding is streamed directly
// to the TFT, avoiding a second full-frame RGB565 allocation.
class CameraService {
public:
    bool begin();
    void end();
    bool isReady() const;
    bool drawPreview(TFTDisplay& display, const String& status);
    bool captureAndSave(StorageManager& storage, String& savedPath);

private:
    bool _ready = false;
};
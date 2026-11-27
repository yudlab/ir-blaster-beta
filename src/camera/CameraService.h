#pragma once

#include <Arduino.h>

class StorageManager;

// Camera capture is deliberately separate from storage. JPEG frames are saved
// through StorageManager; preview frames are decoded into a reusable PSRAM buffer.
class CameraService {
public:
    bool begin();
    void end();
    bool isReady() const;
    bool getPreviewFrame(const uint16_t*& pixels, uint16_t& width, uint16_t& height);
    bool captureAndSave(StorageManager& storage, String& savedPath);

private:
    bool _ready = false;
    uint16_t* _previewBuffer = nullptr;
    uint16_t _previewWidth = 320;
    uint16_t _previewHeight = 240;
};
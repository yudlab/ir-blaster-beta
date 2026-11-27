#pragma once

#include <Arduino.h>

class StorageManager;

// Camera capture is deliberately separate from storage. It hands JPEG bytes
// to StorageManager, so storage remains reusable by other modules.
class CameraService {
public:
    bool begin();
    bool isReady() const;
    bool captureAndSave(StorageManager& storage, String& savedPath);

private:
    bool _ready = false;
};

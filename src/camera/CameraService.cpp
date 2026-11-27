#include "CameraService.h"

#include <esp_camera.h>
#include "config.h"
#include "storage/StorageManager.h"

bool CameraService::begin() {
    if (_ready) return true;

    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = CameraConfig::PIN_Y2;
    config.pin_d1 = CameraConfig::PIN_Y3;
    config.pin_d2 = CameraConfig::PIN_Y4;
    config.pin_d3 = CameraConfig::PIN_Y5;
    config.pin_d4 = CameraConfig::PIN_Y6;
    config.pin_d5 = CameraConfig::PIN_Y7;
    config.pin_d6 = CameraConfig::PIN_Y8;
    config.pin_d7 = CameraConfig::PIN_Y9;

    config.pin_xclk = CameraConfig::PIN_XCLK;
    config.pin_pclk = CameraConfig::PIN_PCLK;
    config.pin_vsync = CameraConfig::PIN_VSYNC;
    config.pin_href = CameraConfig::PIN_HREF;
    config.pin_sccb_sda = CameraConfig::PIN_SIOD;
    config.pin_sccb_scl = CameraConfig::PIN_SIOC;
    config.pin_pwdn = CameraConfig::PIN_PWDN;
    config.pin_reset = CameraConfig::PIN_RESET;

    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    const esp_err_t result = esp_camera_init(&config);
    if (result != ESP_OK) {
        Serial.printf("[CAM] Initialisation failed: 0x%x\n", result);
        return false;
    }

    _ready = true;
    Serial.println("[CAM] Camera ready");
    return true;
}

bool CameraService::isReady() const {
    return _ready;
}

bool CameraService::captureAndSave(StorageManager& storage, String& savedPath) {
    if (!_ready || !storage.isMounted()) return false;

    camera_fb_t* frame = esp_camera_fb_get();
    if (!frame) {
        Serial.println("[CAM] Failed to acquire frame");
        return false;
    }

    if (!storage.exists("/DCIM")) {
        storage.createDirectory("/DCIM");
    }

    savedPath = "/DCIM/IMG_" + String(millis()) + ".jpg";
    File output = storage.openFile(savedPath, FILE_WRITE);
    if (!output) {
        esp_camera_fb_return(frame);
        Serial.println("[CAM] Could not create image file");
        return false;
    }

    const size_t written = output.write(frame->buf, frame->len);
    output.flush();
    output.close();
    const size_t expected = frame->len;
    esp_camera_fb_return(frame);

    if (written != expected) {
        storage.remove(savedPath);
        Serial.printf("[CAM] Incomplete write (%u/%u bytes)\n",
            static_cast<unsigned>(written), static_cast<unsigned>(expected));
        return false;
    }

    Serial.printf("[CAM] Saved %s (%u bytes)\n",
        savedPath.c_str(), static_cast<unsigned>(written));
    return true;
}

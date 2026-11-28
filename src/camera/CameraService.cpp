#include "CameraService.h"

#include <esp_camera.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "display/TFTDisplay.h"
#include "storage/StorageManager.h"

namespace {
void logMemory(const char* stage) {
    const size_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    const size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t internalFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    Serial0.printf("[MEM] %s | heap=%u | PSRAM total=%u free=%u | internal free=%u | ESP.freePSRAM=%u\n",
                   stage,
                   static_cast<unsigned>(ESP.getFreeHeap()),
                   static_cast<unsigned>(psramTotal),
                   static_cast<unsigned>(psramFree),
                   static_cast<unsigned>(internalFree),
                   static_cast<unsigned>(ESP.getFreePsram()));
}
}

bool CameraService::begin() {
    Serial0.println("[CAM] begin() called");
    Serial0.printf("[CAM] State: ready=%s\n", _ready ? "yes" : "no");
    if (_ready) {
        Serial0.println("[CAM] Already initialised; reusing camera");
        return true;
    }

    logMemory("before camera init");

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
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    Serial0.println("[CAM] Camera configuration:");
    Serial0.printf("[CAM]   SCCB SDA/SCL: %d/%d\n", config.pin_sccb_sda, config.pin_sccb_scl);
    Serial0.printf("[CAM]   XCLK/PCLK: %d/%d | VSYNC/HREF: %d/%d\n",
                   config.pin_xclk, config.pin_pclk, config.pin_vsync, config.pin_href);
    Serial0.printf("[CAM]   Data pins D0-D7: %d,%d,%d,%d,%d,%d,%d,%d\n",
                   config.pin_d0, config.pin_d1, config.pin_d2, config.pin_d3,
                   config.pin_d4, config.pin_d5, config.pin_d6, config.pin_d7);
    Serial0.println("[CAM]   Format: JPEG | Frame size: QVGA | Buffers: 1 | Location: PSRAM");
    logMemory("before esp_camera_init");
    Serial0.flush();

    Serial0.println("[CAM] Calling esp_camera_init()...");
    const uint32_t initStarted = millis();
    const esp_err_t result = esp_camera_init(&config);
    Serial0.printf("[CAM] esp_camera_init() returned 0x%x after %lu ms\n",
                   result, static_cast<unsigned long>(millis() - initStarted));
    if (result != ESP_OK) {
        Serial0.printf("[CAM] ERROR: Initialisation failed: 0x%x (%s)\n",
                       result, esp_err_to_name(result));
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor) {
        Serial0.printf("[CAM] Sensor detected; PID=0x%02X VER=0x%02X MIDH=0x%02X MIDL=0x%02X\n",
                       sensor->id.PID, sensor->id.VER, sensor->id.MIDH, sensor->id.MIDL);
    } else {
        Serial0.println("[CAM] WARNING: esp_camera_sensor_get() returned null");
    }

    _ready = true;
    logMemory("camera ready");
    return true;
}

void CameraService::end() {
    if (_ready) {
        esp_camera_deinit();
        _ready = false;
        Serial0.println("[CAM] Camera stopped");
    }
    logMemory("camera stopped");
}

bool CameraService::isReady() const {
    return _ready;
}

bool CameraService::drawPreview(TFTDisplay& display, const String& status) {
    Serial0.println("[CAM][PREVIEW] drawPreview() entered");
    if (!_ready) {
        Serial0.println("[CAM][PREVIEW] Aborted: camera is not ready");
        return false;
    }

    logMemory("before frame acquisition");
    const uint32_t frameStarted = millis();
    camera_fb_t* frame = esp_camera_fb_get();
    Serial0.printf("[CAM][PREVIEW] esp_camera_fb_get() returned %p after %lu ms\n",
                   frame, static_cast<unsigned long>(millis() - frameStarted));
    if (!frame) {
        Serial0.println("[CAM][PREVIEW] ERROR: camera returned a null frame");
        return false;
    }

    Serial0.printf("[CAM][PREVIEW] frame=%p buffer=%p length=%u format=%d size=%ux%u PSRAM=%s\n",
                   frame, frame->buf, static_cast<unsigned>(frame->len),
                   frame->format, frame->width, frame->height,
                   frame->buf && esp_ptr_external_ram(frame->buf) ? "yes" : "no");

    bool rendered = false;
    if (frame->buf && frame->len >= 4 &&
        frame->format == PIXFORMAT_JPEG &&
        frame->buf[0] == 0xFF && frame->buf[1] == 0xD8) {
        Serial0.println("[CAM][PREVIEW] Sending JPEG to block-based TFT decoder");
        Serial0.flush();
        const uint32_t decodeStarted = millis();
        rendered = display.drawCameraJpeg(frame->buf, frame->len, status);
        Serial0.printf("[CAM][PREVIEW] TFT JPEG decode result=%s after %lu ms\n",
                       rendered ? "success" : "failed",
                       static_cast<unsigned long>(millis() - decodeStarted));
    } else {
        Serial0.println("[CAM][PREVIEW] ERROR: invalid JPEG frame metadata/header");
    }

    esp_camera_fb_return(frame);
    Serial0.println("[CAM][PREVIEW] Camera framebuffer returned");
    logMemory("after preview");
    return rendered;
}

bool CameraService::captureAndSave(StorageManager& storage, String& savedPath) {
    if (!_ready || !storage.isMounted()) {
        Serial0.println("[CAM] Capture refused: camera or SD card is not ready");
        return false;
    }

    camera_fb_t* frame = esp_camera_fb_get();
    if (!frame) {
        Serial0.println("[CAM] Failed to acquire capture frame");
        return false;
    }

    if (!frame->buf || frame->format != PIXFORMAT_JPEG) {
        esp_camera_fb_return(frame);
        Serial0.println("[CAM] Capture refused: camera did not return a JPEG frame");
        return false;
    }

    if (!storage.exists("/DCIM") && !storage.createDirectory("/DCIM")) {
        esp_camera_fb_return(frame);
        Serial0.println("[CAM] Could not create /DCIM directory");
        return false;
    }

    savedPath = "/DCIM/IMG_" + String(millis()) + ".jpg";
    File output = storage.openFile(savedPath, FILE_WRITE);
    if (!output) {
        esp_camera_fb_return(frame);
        Serial0.println("[CAM] Could not create image file");
        return false;
    }

    const size_t expected = frame->len;
    const size_t written = output.write(frame->buf, expected);
    output.flush();
    output.close();
    esp_camera_fb_return(frame);

    if (written != expected) {
        storage.remove(savedPath);
        Serial0.printf("[CAM] Incomplete write (%u/%u bytes)\n",
            static_cast<unsigned>(written), static_cast<unsigned>(expected));
        return false;
    }

    Serial0.printf("[CAM] Saved %s (%u bytes)\n",
        savedPath.c_str(), static_cast<unsigned>(written));
    return true;
}
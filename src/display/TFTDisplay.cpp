#include "TFTDisplay.h"

#include <SPI.h>

TFTDisplay::TFTDisplay()
    : _tft(&SPI, DisplayConfig::PIN_CS, DisplayConfig::PIN_DC,
           DisplayConfig::PIN_RESET) {
}

bool TFTDisplay::begin() {
    SPI.begin(DisplayConfig::PIN_SCLK, DisplayConfig::PIN_MISO,
              DisplayConfig::PIN_MOSI, DisplayConfig::PIN_CS);
    _tft.init(DisplayConfig::WIDTH, DisplayConfig::HEIGHT);
    _tft.setRotation(0);
    _tft.setTextWrap(true);
    _tft.fillScreen(ST77XX_BLACK);
    drawMessage("ESP IR BLASTER", "Display initialised");
    return true;
}

void TFTDisplay::drawText(const String& text, int16_t x, int16_t y,
                          uint8_t size, uint16_t foreground,
                          uint16_t background) {
    _tft.setTextSize(size);
    _tft.setTextColor(foreground, background);
    _tft.setCursor(x, y);
    _tft.print(text);
}

void TFTDisplay::drawHeader(const String& title) {
    _tft.fillRect(0, 0, _tft.width(), 34, ST77XX_BLUE);
    drawText(title, 8, 9, 2, ST77XX_WHITE, ST77XX_BLUE);
    _tft.drawFastHLine(0, 34, _tft.width(), ST77XX_GREEN);
}

void TFTDisplay::drawMenuItem(int y, const String& label, bool selected) {
    const uint16_t background = selected ? ST77XX_RED : ST77XX_BLACK;
    _tft.fillRoundRect(8, y, _tft.width() - 16, 30, 5, background);
    drawText(String(selected ? "> " : "  ") + label, 16, y + 7, 2,
             ST77XX_WHITE, background);
}

void TFTDisplay::drawHome(int selectedItem, const String& status) {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader("ESP IR BLASTER");
    drawMenuItem(40, "IR Receiver", selectedItem == 0);
    drawMenuItem(76, "IR Transmitter", selectedItem == 1);
    drawMenuItem(112, "Trackball Test", selectedItem == 2);
    drawMenuItem(148, "SD Card Explorer", selectedItem == 3);
    drawMenuItem(184, "Camera", selectedItem == 4);
    drawMenuItem(220, "System Information", selectedItem == 5);
    drawText(status, 8, 265, 1, ST77XX_GREEN, ST77XX_BLACK);
    drawText("Use trackball: move / press", 8, 284, 1,
             ST77XX_WHITE, ST77XX_BLACK);
}

void TFTDisplay::drawList(const String& title, const std::vector<String>& items,
                          int selectedItem, const String& footer) {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader(title);

    const int maxVisible = 7;
    const int start = selectedItem >= maxVisible ? selectedItem - maxVisible + 1 : 0;
    for (int row = 0; row < maxVisible && start + row < static_cast<int>(items.size()); ++row) {
        const int y = 43 + row * 29;
        const bool selected = (start + row) == selectedItem;
        const uint16_t bg = selected ? ST77XX_RED : ST77XX_BLACK;
        _tft.fillRoundRect(6, y, _tft.width() - 12, 26, 4, bg);
        drawText(String(selected ? "> " : "  ") + items[start + row],
                 12, y + 6, 1, ST77XX_WHITE, bg);
    }
    drawText(footer, 8, 260, 1, ST77XX_GREEN, ST77XX_BLACK);
    drawText("Press: open  Back: return", 8, 281, 1,
             ST77XX_WHITE, ST77XX_BLACK);
}

void TFTDisplay::drawCamera(const String& status) {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader("CAMERA");
    drawText("OV2640 camera", 12, 58, 2, ST77XX_WHITE, ST77XX_BLACK);
    drawText(status, 12, 100, 1, ST77XX_YELLOW, ST77XX_BLACK);
    drawText("Press: capture JPEG", 12, 220, 1, ST77XX_GREEN, ST77XX_BLACK);
    drawText("Back: main menu", 12, 240, 1, ST77XX_WHITE, ST77XX_BLACK);
}

void TFTDisplay::drawMessage(const String& title, const String& message) {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader(title);
    drawText(message, 12, 70, 2, ST77XX_YELLOW, ST77XX_BLACK);
    drawText("Press to return", 12, 220, 1, ST77XX_GREEN, ST77XX_BLACK);
}

void TFTDisplay::drawSystemInfo() {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader("SYSTEM INFORMATION");
    drawText("Board: GOOUUU S3-CAM", 8, 50, 1, ST77XX_WHITE, ST77XX_BLACK);
    drawText("Flash: " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB",
             8, 78, 1, ST77XX_WHITE, ST77XX_BLACK);
    drawText("Free heap: " + String(ESP.getFreeHeap() / 1024) + " KB",
             8, 106, 1, ST77XX_WHITE, ST77XX_BLACK);
    drawText(String("PSRAM: ") + (psramFound() ? "Available" : "Not found"),
             8, 134, 1, ST77XX_WHITE, ST77XX_BLACK);
    drawText("Camera: OV2640", 8, 162, 1, ST77XX_WHITE, ST77XX_BLACK);
    drawText("SD: SD_MMC 1-bit", 8, 190, 1, ST77XX_WHITE, ST77XX_BLACK);
    drawText("Press to return", 8, 225, 1, ST77XX_GREEN, ST77XX_BLACK);
}

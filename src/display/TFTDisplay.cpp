#include "TFTDisplay.h"
#include "config.h"
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>


bool TFTDisplay::begin() {

    Adafruit_ST7789 _tft(
        &spi,
        TFT_CS,
        TFT_DC,
        TFT_RST
    );

    _tft.init(240, 320);
    _tft.setRotation(0);

    _tft.fillScreen(ST77XX_BLACK);

    _tft.setTextColor(ST77XX_WHITE);
    _tft.setTextSize(2);
    _tft.setCursor(20, 30);
    _tft.println("MiniDesk");
    drawMessage("ESP IR BLASTER", "Display initialised");
    return true;
}

void TFTDisplay::drawHeader(const String& title) {
    _tft.fillRect(0, 0, _tft.width(), 34, ST77XX_BLUE);
    _tft.setTextColor(ST77XX_WHITE, ST77XX_BLUE);
    _tft.drawString(title, 10, 9, 2);
    _tft.drawFastHLine(0, 34, _tft.width(), ST77XX_GREEN);
}

void TFTDisplay::drawMenuItem(int y, const String& label, bool selected) {
    const uint16_t bg = selected ? ST77XX_RED : ST77XX_BLACK;
    const uint16_t fg = selected ? ST77XX_WHITE : ST77XX_RED;
    _tft.fillRoundRect(8, y, _tft.width() - 16, 32, 5, bg);
    _tft.setTextColor(fg, bg);
    _tft.drawString(selected ? "> " + label : "  " + label, 16, y + 8, 2);
}

void TFTDisplay::drawHome(int selectedItem, const String& status) {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader("ESP IR BLASTER");
    drawMenuItem(43, "IR Receiver", selectedItem == 0);
    drawMenuItem(81, "IR Transmitter", selectedItem == 1);
    drawMenuItem(119, "Trackball Test", selectedItem == 2);
    drawMenuItem(157, "System Information", selectedItem == 3);
    _tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    _tft.drawString("SYSTEM", 10, 197, 2);
    _tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    _tft.drawString(status, 10, 216, 2);
    _tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    _tft.drawString("Move: navigate  Press: select", 10, 231, 1);
}


void TFTDisplay::drawMessage(const String& title, const String& message) {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader(title);
    _tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    _tft.drawString(message, 12, 70, 2);
    _tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    _tft.drawString("Press to return", 12, 220, 1);
}

void TFTDisplay::drawSystemInfo() {
    _tft.fillScreen(ST77XX_BLACK);
    drawHeader("SYSTEM INFORMATION");
    _tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    _tft.drawString("Board: ESP32-S3-CAM", 10, 50, 2);
    _tft.drawString("Flash: " + String(ESP.getFlashChipSize() / (1024 * 1024)) + " MB", 10, 78, 2);
    _tft.drawString("Free heap: " + String(ESP.getFreeHeap() / 1024) + " KB", 10, 106, 2);
    _tft.drawString(String("PSRAM: ") + (psramFound() ? "Available" : "Not found"), 10, 134, 2);
    _tft.drawString("Camera: reserved", 10, 162, 2);
    _tft.drawString("SD card: reserved", 10, 190, 2);
    _tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    _tft.drawString("Press to return", 10, 225, 1);
}

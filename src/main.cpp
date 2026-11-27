#include <Arduino.h>
#include "config.h"
#include "app/Application.h"

Application app;

void setup() {
    Serial.begin(BoardConfig::SERIAL_BAUD);
    delay(250);
    Serial.println();
    Serial.println("ESP IR Blaster booting...");
    app.begin();
}

void loop() {
    app.update();
}
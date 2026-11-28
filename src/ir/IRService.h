#pragma once

#include <Arduino.h>
#include <FS.h>
#include <vector>
#include <ESP32IRPulseKit.h>

class IRService {
public:
    bool begin();
    void poll();
    void handleCommand(const String& line, Print& output);
    bool ready() const { return _ready; }

private:
    esp32irpk::IRReceiver<> _rx;
    esp32irpk::IRSender _tx;
    bool _ready = false;
    bool _learning = false;
    String _learnRemote;
    String _learnButton;
    String _scriptPath;
    File _scriptWriter;

    static String safeName(const String& value);
    static String remotePath(const String& remote);
    static String buttonPath(const String& remote, const String& button);
    static String scriptPath(const String& name);
    bool ensureRemote(const String& remote);
    bool saveCapture(const esp32irpk::IRReceiveResult<>& result, const String& remote,
                     const String& button);
    bool loadAndSend(const String& remote, const String& button, Print& output);
    bool copyButton(const String& sourceRemote, const String& sourceButton,
                    const String& destinationRemote, const String& destinationButton);
    void listRemotes(Print& output);
    void runScript(const String& name, Print& output);
    void printHelp(Print& output);
};

#include "IRService.h"

#include <SD_MMC.h>
#include "config.h"
#include <algorithm>

namespace {
constexpr size_t MAX_CAPTURE_TICKS = 4096;

bool readLine(File& file, String& line) {
    line = file.readStringUntil('\n');
    line.trim();
    return line.length() > 0;
}

bool parseKeyValue(const String& line, const char* key, uint32_t& value) {
    const String prefix = String(key) + "=";
    if (!line.startsWith(prefix)) return false;
    value = static_cast<uint32_t>(strtoul(line.substring(prefix.length()).c_str(), nullptr, 10));
    return true;
}
}

IRService::IRService()
    : _rx(IRConfig::PIN_RX, IRConfig::RX_INVERTED), _tx(IRConfig::PIN_TX) {}

bool IRService::begin() {
    if (!SD_MMC.cardType()) {
        Serial0.println("[IR] SD card not mounted; IR storage unavailable");
    }
    _rx.setMaxRxSymbols(MAX_CAPTURE_TICKS);
    const bool rxOk = _rx.begin();
    const bool txOk = _tx.begin();
    _ready = rxOk && txOk;
    Serial0.printf("[IR] RMT receiver GPIO%d: %s | transmitter GPIO%d: %s\n",
                   IRConfig::PIN_RX, rxOk ? "ready" : "failed",
                   IRConfig::PIN_TX, txOk ? "ready" : "failed");
    return _ready;
}

String IRService::safeName(const String& value) {
    String result;
    result.reserve(value.length());
    for (size_t i = 0; i < value.length(); ++i) {
        const char c = value[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-') result += c;
    }
    if (result.length() > 32) result.remove(32);
    return result;
}

String IRService::remotePath(const String& remote) {
    return String("/IR/remotes/") + safeName(remote);
}
String IRService::buttonPath(const String& remote, const String& button) {
    return remotePath(remote) + "/" + safeName(button) + ".ir";
}
String IRService::scriptPath(const String& name) {
    return String("/IR/scripts/") + safeName(name) + ".irs";
}

bool IRService::ensureRemote(const String& remote) {
    const String name = safeName(remote);
    if (!name.length()) return false;
    if (!SD_MMC.exists("/IR")) SD_MMC.mkdir("/IR");
    if (!SD_MMC.exists("/IR/remotes")) SD_MMC.mkdir("/IR/remotes");
    if (!SD_MMC.exists("/IR/scripts")) SD_MMC.mkdir("/IR/scripts");
    if (!SD_MMC.exists("/IR/database")) SD_MMC.mkdir("/IR/database");
    const String path = remotePath(name);
    return SD_MMC.exists(path) || SD_MMC.mkdir(path);
}

bool IRService::saveCapture(const esp32irpk::IRReceiveResult<>& result,
                            const String& remote, const String& button) {
    if (!result.raw.ticks || result.raw.len == 0 || result.raw.len > MAX_CAPTURE_TICKS ||
        result.truncated() || !ensureRemote(remote)) return false;

    const String path = buttonPath(remote, button);
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) return false;

    const esp32irpk::IRDecodedBits* bits = result.bits();
    file.println("IRBLASTER1");
    file.printf("protocol=%u\n", bits ? static_cast<unsigned>(bits->protocol_id) : 0U);
    file.printf("bit_length=%u\n", bits ? bits->bit_length : 0U);
    file.printf("value=%llu\n", static_cast<unsigned long long>(bits ? bits->bits : 0ULL));
    file.printf("carrier=%lu\n", static_cast<unsigned long>(IRConfig::DEFAULT_CARRIER_HZ));
    file.printf("ticks=%u\n", static_cast<unsigned>(result.raw.len));
    for (size_t i = 0; i < result.raw.len; ++i) {
        if (i) file.print(',');
        file.print(result.raw.ticks[i]);
    }
    file.println();
    file.flush();
    const bool ok = file.getWriteError() == 0;
    file.close();
    return ok;
}

void IRService::poll() {
    if (!_ready) return;
    esp32irpk::IRReceiveResult<> result;
    if (!_rx.read(result)) return;

    const auto* candidate = result.candidate();
    Serial0.printf("[IR] Captured %u raw ticks, decoded=%s, truncated=%s\n",
                   static_cast<unsigned>(result.raw.len),
                   candidate ? candidate->name : "unknown",
                   result.truncated() ? "yes" : "no");
    if (_learning) {
        const bool saved = saveCapture(result, _learnRemote, _learnButton);
        _lastCaptureSucceeded = saved;
        _captureFinished = true;
        _lastCaptureLabel = _learnRemote + "/" + _learnButton;
        Serial0.printf("[IR] %s %s\\n", saved ? "Saved" : "Save failed",
                       _lastCaptureLabel.c_str());
        _learning = false;
        _learnRemote = "";
        _learnButton = "";
    }
}

bool IRService::loadAndSend(const String& remote, const String& button, Print& output) {
    return loadAndSendPath(buttonPath(remote, button), output);
}

bool IRService::loadAndSendPath(const String& path, Print& output) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) { output.println("Button not found."); return false; }

    String line;
    if (!readLine(file, line) || line != "IRBLASTER1") {
        file.close(); output.println("Invalid IR file."); return false;
    }
    uint32_t carrier = IRConfig::DEFAULT_CARRIER_HZ;
    uint32_t tickCount = 0;
    uint32_t ignored = 0;
    for (int i = 0; i < 5; ++i) {
        if (!readLine(file, line)) { file.close(); output.println("Corrupt IR metadata."); return false; }
        if (parseKeyValue(line, "carrier", carrier)) continue;
        if (parseKeyValue(line, "ticks", tickCount)) continue;
        parseKeyValue(line, "protocol", ignored);
        parseKeyValue(line, "bit_length", ignored);
        parseKeyValue(line, "value", ignored);
    }
    if (tickCount == 0 || tickCount > MAX_CAPTURE_TICKS || !readLine(file, line)) {
        file.close(); output.println("Invalid raw timing data."); return false;
    }

    std::vector<uint16_t> ticks;
    ticks.reserve(tickCount);
    size_t start = 0;
    while (start < line.length() && ticks.size() < tickCount) {
        const int comma = line.indexOf(',', start);
        const size_t end = comma < 0 ? line.length() : static_cast<size_t>(comma);
        const String token = line.substring(start, end);
        const long value = token.toInt();
        if (value <= 0 || value > 65535) { file.close(); output.println("Invalid timing value."); return false; }
        ticks.push_back(static_cast<uint16_t>(value));
        if (comma < 0) break;
        start = comma + 1;
    }
    file.close();
    if (ticks.size() != tickCount) { output.println("Incomplete raw timing data."); return false; }

    _tx.setCarrierHz(carrier);
    const bool ok = _tx.send({ticks.data(), ticks.size()});
    output.println(ok ? "IR transmitted (local TX complete)." : "IR transmission failed.");
    return ok;
}

bool IRService::copyButton(const String& sourceRemote, const String& sourceButton,
                           const String& destinationRemote, const String& destinationButton) {
    File source = SD_MMC.open(buttonPath(sourceRemote, sourceButton), FILE_READ);
    if (!source || !ensureRemote(destinationRemote)) { if (source) source.close(); return false; }
    File destination = SD_MMC.open(buttonPath(destinationRemote, destinationButton), FILE_WRITE);
    if (!destination) { source.close(); return false; }
    uint8_t buffer[256];
    while (source.available()) {
        const size_t count = source.read(buffer, sizeof(buffer));
        if (!count || destination.write(buffer, count) != count) {
            source.close(); destination.close(); return false;
        }
    }
    source.close();
    destination.flush();
    const bool ok = destination.getWriteError() == 0;
    destination.close();
    return ok;
}


void IRService::beginScreenLearn() {
    if (!_ready || !SD_MMC.cardType()) {
        _captureFinished = true;
        _lastCaptureSucceeded = false;
        _lastCaptureLabel = "IR or SD unavailable";
        return;
    }
    _captureFinished = false;
    _lastCaptureSucceeded = false;
    _learnRemote = "Remote_01";
    if (!ensureRemote(_learnRemote)) {
        _captureFinished = true;
        _lastCaptureSucceeded = false;
        _lastCaptureLabel = "Cannot create remote folder on SD";
        return;
    }
    for (unsigned index = 1; index <= 99; ++index) {
        char name[16];
        snprintf(name, sizeof(name), "Button_%02u", index);
        if (!SD_MMC.exists(buttonPath(_learnRemote, name))) {
            _learnButton = name;
            _learning = true;
            return;
        }
    }
    _captureFinished = true;
    _lastCaptureLabel = "Remote is full";
}

void IRService::listSavedRemotes(std::vector<String>& items) {
    items.clear();
    File root = SD_MMC.open("/IR/remotes");
    if (!root || !root.isDirectory()) return;
    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            const int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            items.push_back(name);
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

void IRService::listSavedButtons(const String& remote, std::vector<String>& items) {
    items.clear();
    File root = SD_MMC.open(remotePath(remote));
    if (!root || !root.isDirectory()) return;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = String(entry.name());
            const int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.endsWith(".ir")) name.remove(name.length() - 3);
            items.push_back(name);
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

void IRService::listDatabaseCodes(std::vector<String>& items) {
    items.clear();
    if (!SD_MMC.exists("/IR")) SD_MMC.mkdir("/IR");
    if (!SD_MMC.exists("/IR/database")) SD_MMC.mkdir("/IR/database");
    File root = SD_MMC.open("/IR/database");
    if (!root || !root.isDirectory()) return;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".ir")) {
            String name = String(entry.name());
            const int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            items.push_back(name);
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

void IRService::listScripts(std::vector<String>& items) {
    items.clear();
    File root = SD_MMC.open("/IR/scripts");
    if (!root || !root.isDirectory()) return;
    File entry = root.openNextFile();
    while (entry) {
        if (!entry.isDirectory() && String(entry.name()).endsWith(".irs")) {
            String name = String(entry.name());
            const int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            name.remove(name.length() - 4);
            items.push_back(name);
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();
}

bool IRService::sendSavedButton(const String& remote, const String& button) {
    return loadAndSend(remote, button, Serial0);
}

bool IRService::sendDatabaseCode(const String& path) {
    return loadAndSendPath(path, Serial0);
}

bool IRService::runSavedScript(const String& name) {
    runScript(name, Serial0);
    return true;
}

void IRService::listRemotes(Print& output) {
    File root = SD_MMC.open("/IR/remotes");
    if (!root || !root.isDirectory()) { output.println("No saved remotes yet."); return; }
    File remote = root.openNextFile();
    while (remote) {
        if (remote.isDirectory()) {
            output.print("REMOTE "); output.println(remote.name());
            File button = remote.openNextFile();
            while (button) {
                if (!button.isDirectory()) { output.print("  BUTTON "); output.println(button.name()); }
                button.close();
                button = remote.openNextFile();
            }
        }
        remote.close();
        remote = root.openNextFile();
    }
    root.close();
}

void IRService::runScript(const String& name, Print& output) {
    File file = SD_MMC.open(scriptPath(name), FILE_READ);
    if (!file) { output.println("Script not found."); return; }
    output.print("Running script "); output.println(name);
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (!line.length() || line.startsWith("#")) continue;
        if (line.startsWith("DELAY ")) {
            const uint32_t ms = static_cast<uint32_t>(line.substring(6).toInt());
            if (ms > 60000) { output.println("Delay rejected (>60000 ms)."); continue; }
            delay(ms);
        } else if (line.startsWith("SEND ")) {
            String args = line.substring(5);
            const int split = args.indexOf(' ');
            if (split <= 0) { output.println("Bad SEND line."); continue; }
            loadAndSend(args.substring(0, split), args.substring(split + 1), output);
        } else {
            output.print("Unknown script instruction: "); output.println(line);
        }
    }
    file.close();
    output.println("Script finished.");
}

void IRService::printHelp(Print& output) {
    output.println("IR commands (use uppercase):");
    output.println("  HELP");
    output.println("  LEARN <remote> <button>  - wait for one IR press and save its raw signal");
    output.println("  LIST                     - list saved remotes and buttons");
    output.println("  SEND <remote> <button>   - replay a saved button");
    output.println("  COPY <srcRemote> <srcButton> <dstRemote> <dstButton>");
    output.println("  SCRIPT <name>            - record script lines; finish with END");
    output.println("  Script lines: SEND <remote> <button>  |  DELAY <milliseconds>");
    output.println("  RUN <name>               - execute a saved script");
    output.println("  CANCEL                   - cancel learning or script recording");
}

void IRService::handleCommand(const String& input, Print& output) {
    String line = input;
    line.trim();
    if (!line.length()) return;

    if (_scriptWriter) {
        if (line == "END") {
            _scriptWriter.flush(); _scriptWriter.close();
            output.println("Script saved.");
        } else if (line == "CANCEL") {
            _scriptWriter.close();
            SD_MMC.remove(_scriptPath);
            _scriptPath = "";
            output.println("Script recording cancelled.");
        } else if (line.startsWith("SEND ") || line.startsWith("DELAY ") || line.startsWith("#")) {
            _scriptWriter.println(line);
            output.println("Added.");
        } else output.println("Only SEND, DELAY, comments, or END are allowed in scripts.");
        return;
    }

    if (line == "HELP") { printHelp(output); return; }
    if (line == "LIST") { listRemotes(output); return; }
    if (line == "CANCEL") {
        _learning = false; _learnRemote = ""; _learnButton = "";
        output.println("Pending learn cancelled."); return;
    }

    if (line.startsWith("LEARN ")) {
        String args = line.substring(6); args.trim();
        const int split = args.indexOf(' ');
        if (split <= 0 || split == static_cast<int>(args.length()) - 1) {
            output.println("Usage: LEARN <remote> <button>"); return;
        }
        _learnRemote = safeName(args.substring(0, split));
        _learnButton = safeName(args.substring(split + 1));
        if (!_learnRemote.length() || !_learnButton.length() || !ensureRemote(_learnRemote)) {
            output.println("Invalid remote/button name or SD card unavailable."); return;
        }
        _learning = true;
        output.println("Point the remote at the IR receiver and press the button once...");
        return;
    }

    if (line.startsWith("SEND ")) {
        String args = line.substring(5); args.trim();
        const int split = args.indexOf(' ');
        if (split <= 0) { output.println("Usage: SEND <remote> <button>"); return; }
        loadAndSend(args.substring(0, split), args.substring(split + 1), output);
        return;
    }

    if (line.startsWith("COPY ")) {
        String args = line.substring(5); args.trim();
        String parts[4]; int count = 0;
        while (args.length() && count < 4) {
            const int split = args.indexOf(' ');
            parts[count++] = split < 0 ? args : args.substring(0, split);
            if (split < 0) args = ""; else { args = args.substring(split + 1); args.trim(); }
        }
        if (count != 4 || args.length()) { output.println("Usage: COPY <srcRemote> <srcButton> <dstRemote> <dstButton>"); return; }
        output.println(copyButton(parts[0], parts[1], parts[2], parts[3]) ? "Button copied." : "Copy failed.");
        return;
    }

    if (line.startsWith("SCRIPT ")) {
        const String name = safeName(line.substring(7));
        if (!name.length()) { output.println("Usage: SCRIPT <name>"); return; }
        if (!SD_MMC.exists("/IR")) SD_MMC.mkdir("/IR");
        if (!SD_MMC.exists("/IR/scripts")) SD_MMC.mkdir("/IR/scripts");
        _scriptPath = scriptPath(name);
        SD_MMC.remove(_scriptPath);
        _scriptWriter = SD_MMC.open(_scriptPath, FILE_WRITE);
        if (_scriptWriter) output.println("Enter SEND/DELAY lines. Type END to save.");
        else output.println("Could not create script.");
        return;
    }

    if (line.startsWith("RUN ")) { runScript(line.substring(4), output); return; }
    output.println("Unknown command. Type HELP.");
}

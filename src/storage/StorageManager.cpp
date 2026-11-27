#include "StorageManager.h"
#include "config.h"

bool StorageManager::begin() {
    if (_mounted) {
        return true;
    }

    if (!SD_MMC.setPins(SDConfig::PIN_CLK, SDConfig::PIN_CMD, SDConfig::PIN_D0)) {
        Serial.println("[SD] Failed to assign SD_MMC pins");
        return false;
    }

    // The onboard GOOUUU slot is wired for SDMMC 1-bit mode.
    _mounted = SD_MMC.begin(SDConfig::MOUNT_POINT, true);
    if (!_mounted) {
        Serial.println("[SD] Mount failed");
        return false;
    }

    Serial.printf("[SD] Mounted: %llu MB total, %llu MB used\n",
        totalBytes() / (1024ULL * 1024ULL),
        usedBytes() / (1024ULL * 1024ULL));
    return true;
}

bool StorageManager::isMounted() const {
    return _mounted;
}

std::vector<StorageEntry> StorageManager::listDirectory(const String& path) {
    std::vector<StorageEntry> entries;
    if (!_mounted) return entries;

    File directory = SD_MMC.open(path);
    if (!directory || !directory.isDirectory()) {
        if (directory) directory.close();
        return entries;
    }

    File file = directory.openNextFile();
    while (file) {
        StorageEntry entry;
        entry.name = String(file.name());
        const int slash = entry.name.lastIndexOf('/');
        if (slash >= 0) entry.name = entry.name.substring(slash + 1);
        entry.path = path == "/" ? "/" + entry.name : path + "/" + entry.name;
        entry.isDirectory = file.isDirectory();
        entry.size = entry.isDirectory ? 0 : file.size();
        entries.push_back(entry);
        file.close();
        file = directory.openNextFile();
    }

    directory.close();
    return entries;
}

bool StorageManager::exists(const String& path) const {
    return _mounted && SD_MMC.exists(path);
}

bool StorageManager::createDirectory(const String& path) {
    return _mounted && SD_MMC.mkdir(path);
}

bool StorageManager::remove(const String& path) {
    if (!_mounted || !SD_MMC.exists(path)) return false;
    File item = SD_MMC.open(path);
    const bool isDirectory = item && item.isDirectory();
    if (item) item.close();
    return isDirectory ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
}

File StorageManager::openFile(const String& path, const char* mode) {
    if (!_mounted) return File();
    return SD_MMC.open(path, mode);
}

uint64_t StorageManager::totalBytes() const {
    return _mounted ? SD_MMC.totalBytes() : 0;
}

uint64_t StorageManager::usedBytes() const {
    return _mounted ? SD_MMC.usedBytes() : 0;
}

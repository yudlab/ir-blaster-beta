#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD_MMC.h>
#include <vector>

struct StorageEntry {
    String name;
    String path;
    bool isDirectory = false;
    size_t size = 0;
};

// Owns SD_MMC mounting and generic file operations. Camera and other features
// should use this class rather than mounting or configuring the card themselves.
class StorageManager {
public:
    bool begin();
    bool isMounted() const;

    std::vector<StorageEntry> listDirectory(const String& path);
    bool exists(const String& path) const;
    bool createDirectory(const String& path);
    bool remove(const String& path);
    File openFile(const String& path, const char* mode = FILE_READ);

    uint64_t totalBytes() const;
    uint64_t usedBytes() const;

private:
    bool _mounted = false;
};

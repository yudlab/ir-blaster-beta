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

struct SDBenchmarkResult {
    bool success = false;
    uint64_t bytesWritten = 0;
    uint64_t bytesRead = 0;
    uint32_t writeMs = 0;
    uint32_t readMs = 0;
    bool dataVerified = false;
};

class StorageManager {
public:
    bool begin();
    bool isMounted() const;
    bool refresh();

    std::vector<StorageEntry> listDirectory(const String& path);
    bool exists(const String& path) const;
    bool createDirectory(const String& path);
    bool remove(const String& path);
    bool rename(const String& from, const String& to);
    File openFile(const String& path, const char* mode = FILE_READ);

    uint64_t totalBytes() const;
    uint64_t usedBytes() const;
    uint64_t freeBytes() const;
    uint64_t cardSize() const;
    String cardTypeName() const;

    bool runCardTest(String& report);
    SDBenchmarkResult runBenchmark(uint32_t testSizeBytes = 512 * 1024);
    bool formatCard(String& report);

private:
    bool _mounted = false;
    bool ensureToolboxDirectory();
};
#include "StorageManager.h"
#include "config.h"

#include <esp_err.h>
#include <ff.h>
#include <cstdlib>

namespace {
constexpr char TOOLBOX_DIR[] = "/TOOLBOX";
constexpr char TEST_FILE[] = "/TOOLBOX/.sd_integrity.tmp";
constexpr char BENCH_FILE[] = "/TOOLBOX/.sd_benchmark.tmp";
constexpr size_t IO_CHUNK = 4096;

String formatRate(uint64_t bytes, uint32_t elapsedMs) {
    if (elapsedMs == 0) return "N/A";
    const double rate = (static_cast<double>(bytes) * 1000.0) /
                        static_cast<double>(elapsedMs) / (1024.0 * 1024.0);
    return String(rate, 2) + " MB/s";
}
}

bool StorageManager::begin() {
    if (_mounted) return true;

    if (!SD_MMC.setPins(SDConfig::PIN_CLK, SDConfig::PIN_CMD, SDConfig::PIN_D0)) {
        Serial.println("[SD] Failed to assign SD_MMC pins");
        return false;
    }

    _mounted = SD_MMC.begin(SDConfig::MOUNT_POINT, true, false);
    if (!_mounted) {
        Serial.println("[SD] Mount failed (formatting is never automatic)");
        return false;
    }

    if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("[SD] No card detected after mount");
        SD_MMC.end();
        _mounted = false;
        return false;
    }

    Serial.printf("[SD] Mounted: card=%llu MB filesystem=%llu MB used=%llu MB\n",
        cardSize() / (1024ULL * 1024ULL),
        totalBytes() / (1024ULL * 1024ULL),
        usedBytes() / (1024ULL * 1024ULL));
    return true;
}

bool StorageManager::isMounted() const { return _mounted; }

bool StorageManager::refresh() {
    if (_mounted) {
        SD_MMC.end();
        _mounted = false;
    }
    delay(100);
    return begin();
}

std::vector<StorageEntry> StorageManager::listDirectory(
    const String& path, size_t offset, size_t limit, bool* hasMore) {
    std::vector<StorageEntry> entries;
    if (hasMore) *hasMore = false;
    if (!_mounted || limit == 0) return entries;

    // Only retain one page in RAM. Large IRDB folders can contain hundreds
    // of entries; the TFT only displays seven rows at a time.
    entries.reserve(limit);
    File directory = SD_MMC.open(path);
    if (!directory || !directory.isDirectory()) {
        Serial.printf("[SD] Cannot open directory: %s\n", path.c_str());
        if (directory) directory.close();
        return entries;
    }

    size_t seen = 0;
    File file = directory.openNextFile();
    while (file) {
        if (seen++ < offset) {
            file.close();
            file = directory.openNextFile();
            if ((seen & 0x0F) == 0) yield();
            continue;
        }

        if (entries.size() >= limit) {
            if (hasMore) *hasMore = true;
            file.close();
            break;
        }

        StorageEntry entry;
        entry.name = file.name();
        const int slash = entry.name.lastIndexOf('/');
        if (slash >= 0) entry.name.remove(0, slash + 1);
        entry.path.reserve(path.length() + entry.name.length() + 2);
        entry.path = path;
        if (!entry.path.endsWith("/")) entry.path += '/';
        entry.path += entry.name;
        entry.isDirectory = file.isDirectory();
        entry.size = entry.isDirectory ? 0 : file.size();
        entries.emplace_back(std::move(entry));

        file.close();
        file = directory.openNextFile();
        if ((seen & 0x0F) == 0) yield();
    }
    directory.close();
    return entries;
}

bool StorageManager::exists(const String& path) const {
    return _mounted && SD_MMC.exists(path);
}

bool StorageManager::createDirectory(const String& path) {
    return _mounted && (SD_MMC.exists(path) || SD_MMC.mkdir(path));
}

bool StorageManager::remove(const String& path) {
    if (!_mounted || !SD_MMC.exists(path)) return false;
    File item = SD_MMC.open(path);
    const bool isDirectory = item && item.isDirectory();
    if (item) item.close();
    return isDirectory ? SD_MMC.rmdir(path) : SD_MMC.remove(path);
}

bool StorageManager::rename(const String& from, const String& to) {
    return _mounted && SD_MMC.exists(from) && !SD_MMC.exists(to) &&
           SD_MMC.rename(from, to);
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
uint64_t StorageManager::freeBytes() const {
    const uint64_t total = totalBytes();
    const uint64_t used = usedBytes();
    return total > used ? total - used : 0;
}
uint64_t StorageManager::cardSize() const {
    return _mounted ? SD_MMC.cardSize() : 0;
}
String StorageManager::cardTypeName() const {
    if (!_mounted) return "Not mounted";
    switch (SD_MMC.cardType()) {
        case CARD_MMC: return "MMC";
        case CARD_SD: return "SDSC";
        case CARD_SDHC: return "SDHC/SDXC";
        default: return "Unknown";
    }
}

bool StorageManager::ensureToolboxDirectory() {
    return _mounted && createDirectory(TOOLBOX_DIR);
}

bool StorageManager::runCardTest(String& report) {
    report = "Card test failed";
    if (!_mounted || !ensureToolboxDirectory()) {
        report = "SD not mounted / cannot create TOOLBOX";
        return false;
    }

    uint8_t* buffer = static_cast<uint8_t*>(malloc(IO_CHUNK));
    if (!buffer) {
        report = "Not enough RAM for test";
        return false;
    }

    for (size_t i = 0; i < IO_CHUNK; ++i) buffer[i] = static_cast<uint8_t>((i * 37U + 11U) & 0xFFU);

    File output = SD_MMC.open(TEST_FILE, FILE_WRITE);
    if (!output) {
        free(buffer);
        report = "Cannot create test file";
        return false;
    }
    output.seek(0);
    const size_t written = output.write(buffer, IO_CHUNK);
    output.flush();
    output.close();

    bool ok = written == IO_CHUNK;
    File input = SD_MMC.open(TEST_FILE, FILE_READ);
    if (!input) ok = false;
    size_t received = 0;
    if (input) {
        while (received < IO_CHUNK) {
            const size_t n = input.read(buffer + received, IO_CHUNK - received);
            if (n == 0) break;
            received += n;
        }
        input.close();
    }
    if (received != IO_CHUNK) ok = false;
    for (size_t i = 0; ok && i < IO_CHUNK; ++i) {
        if (buffer[i] != static_cast<uint8_t>((i * 37U + 11U) & 0xFFU)) ok = false;
    }

    free(buffer);
    SD_MMC.remove(TEST_FILE);
    report = ok ? "Write/read/verify: PASS" : "Write/read/verify: FAIL";
    Serial.printf("[SD] Integrity test: %s (written=%u read=%u)\n",
        ok ? "PASS" : "FAIL", static_cast<unsigned>(written),
        static_cast<unsigned>(received));
    return ok;
}

SDBenchmarkResult StorageManager::runBenchmark(uint32_t testSizeBytes) {
    SDBenchmarkResult result;
    if (!_mounted || !ensureToolboxDirectory()) return result;

    testSizeBytes = constrain(testSizeBytes, 64U * 1024U, 2U * 1024U * 1024U);
    uint8_t* buffer = static_cast<uint8_t*>(malloc(IO_CHUNK));
    if (!buffer) return result;
    for (size_t i = 0; i < IO_CHUNK; ++i) buffer[i] = static_cast<uint8_t>((i * 29U + 7U) & 0xFFU);

    SD_MMC.remove(BENCH_FILE);
    File output = SD_MMC.open(BENCH_FILE, FILE_WRITE);
    if (!output) {
        free(buffer);
        return result;
    }

    uint32_t start = millis();
    uint32_t remaining = testSizeBytes;
    bool writeOk = true;
    while (remaining > 0) {
        const size_t amount = remaining < IO_CHUNK ? remaining : IO_CHUNK;
        if (output.write(buffer, amount) != amount) {
            writeOk = false;
            break;
        }
        remaining -= amount;
        yield();
    }
    output.flush();
    result.writeMs = millis() - start;
    output.close();
    result.bytesWritten = testSizeBytes - remaining;

    File input = SD_MMC.open(BENCH_FILE, FILE_READ);
    if (!input) writeOk = false;
    start = millis();
    uint32_t totalRead = 0;
    bool verify = true;
    while (input && totalRead < result.bytesWritten) {
        const size_t amount = (result.bytesWritten - totalRead) < IO_CHUNK
            ? (result.bytesWritten - totalRead) : IO_CHUNK;
        const size_t n = input.read(buffer, amount);
        if (n == 0) break;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t expected = static_cast<uint8_t>(((totalRead + i) % IO_CHUNK) * 29U + 7U);
            if (buffer[i] != expected) verify = false;
        }
        totalRead += n;
        yield();
    }
    result.readMs = millis() - start;
    if (input) input.close();
    result.bytesRead = totalRead;
    result.dataVerified = verify && totalRead == result.bytesWritten;
    result.success = writeOk && result.dataVerified;
    SD_MMC.remove(BENCH_FILE);
    free(buffer);

    Serial.printf("[SD] Benchmark write=%s read=%s verify=%s bytes=%u\n",
        formatRate(result.bytesWritten, result.writeMs).c_str(),
        formatRate(result.bytesRead, result.readMs).c_str(),
        result.dataVerified ? "PASS" : "FAIL",
        static_cast<unsigned>(result.bytesWritten));
    return result;
}

bool StorageManager::formatCard(String& report) {
    report = "Format failed";
    if (!_mounted) {
        report = "Mount the card before formatting";
        return false;
    }

    // FatFs permits formatting a logical drive whether or not it is mounted.
    // Keep the SD_MMC disk-I/O driver registered while f_mkfs writes the new
    // partition table and FAT32 filesystem. SD_MMC.end() then performs the
    // normal VFS/FatFs unmount before begin() rebuilds the mount cleanly.
    BYTE* work = static_cast<BYTE*>(malloc(FF_MAX_SS));
    if (!work) {
        report = "Not enough RAM to format";
        return false;
    }

    // Force FAT32. FM_FAT alone requests FAT12/16 and can fail on larger SDHC
    // and SDXC cards.
    const MKFS_PARM options = { static_cast<BYTE>(FM_FAT32), 0, 0, 0, 0 };
    const FRESULT formatResult = f_mkfs("0:", &options, work, FF_MAX_SS);
    free(work);

    // Always close and reopen the SD_MMC mount, including after a format error,
    // so the filesystem driver does not retain stale FAT metadata.
    SD_MMC.end();
    _mounted = false;
    delay(150);
    const bool remounted = begin();

    if (formatResult != FR_OK) {
        report = String("Format failed: FatFs ") +
                 String(static_cast<int>(formatResult));
        if (!remounted) report += " / remount failed";
        Serial.printf("[SD] FAT32 format failed: FatFs=%d, remount=%s\n",
            static_cast<int>(formatResult), remounted ? "OK" : "FAILED");
        return false;
    }

    if (!remounted) {
        report = "FAT32 formatted, but SD remount failed";
        Serial.println("[SD] FAT32 format succeeded, but remount failed");
        return false;
    }

    // Verify that the newly mounted volume is usable, not merely that begin()
    // returned true.
    File root = SD_MMC.open("/");
    const bool verified = root && root.isDirectory();
    if (root) root.close();

    if (!verified) {
        report = "FAT32 formatted; filesystem verification failed";
        Serial.println("[SD] FAT32 format completed but root verification failed");
        return false;
    }

    report = "FAT32 format complete";
    Serial.printf("[SD] FAT32 format and remount verified (%llu MB)\n",
        totalBytes() / (1024ULL * 1024ULL));
    return true;
}

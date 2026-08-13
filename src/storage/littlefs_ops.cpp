// storage/littlefs_ops.cpp
#include "littlefs_ops.h"
#include <string.h>
#include <esp_littlefs.h>

namespace Storage {

static bool s_mounted = false;

const char* baseName(const char* path) {
    if (!path || !path[0]) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void copyName(const char* src, char* dst, size_t dstLen) {
    size_t i = 0;
    for (; i + 1 < dstLen && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

static uint16_t countFiles(const char* dir) {
    uint16_t n = 0;
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) n++;
        f.close();
        f = root.openNextFile();
    }
    root.close();
    return n;
}

static uint16_t listDir(const char* dir, char out[][FILE_NAME_MAX], uint16_t max) {
    uint16_t n = 0;
    if (!s_mounted) return 0;
    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    File f = root.openNextFile();
    while (f && n < max) {
        if (!f.isDirectory()) {
            copyName(baseName(f.name()), out[n], FILE_NAME_MAX);
            n++;
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
    return n;
}

static const char* FS_LABEL = "spiffs";
static const char* FS_BASE  = "/littlefs";

// Arduino LittleFS.format() calls disableCore0WDT(), which fails on
// unicore C3 and can leave a 2.4 MB format exposed to the loop WDT.
static bool formatPartition() {
    Serial.println("[FS] formatting partition (empty or corrupt)...");
    disableLoopWDT();
    esp_err_t err = esp_littlefs_format(FS_LABEL);
    enableLoopWDT();
    if (err != ESP_OK) {
        Serial.printf("[FS] format failed err=%d\n", (int)err);
        return false;
    }
    Serial.println("[FS] format ok");
    return true;
}

static bool mountFs() {
    return LittleFS.begin(false, FS_BASE, 10, FS_LABEL);
}

bool begin() {
    if (s_mounted) return true;

    if (!mountFs()) {
        Serial.println("[FS] mount failed (normal on first boot)");
        if (!formatPartition()) return false;
        if (!mountFs()) {
            Serial.println("[FS] begin failed after format");
            return false;
        }
    }
    s_mounted = true;
    // mkdir only - LittleFS.exists() logs a fake "no permits" error
    // when the directory is simply missing.
    LittleFS.mkdir(DIR_HANDSHAKES);
    LittleFS.mkdir(DIR_RESULTS);
    Serial.printf("[FS] mounted: total=%u used=%u\n",
                  (unsigned)LittleFS.totalBytes(),
                  (unsigned)LittleFS.usedBytes());
    return true;
}

bool ensureDir(const char* path) {
    if (!s_mounted || !path || !path[0]) return false;
    if (LittleFS.mkdir(path)) return true;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    bool ok = f.isDirectory();
    f.close();
    return ok;
}

bool removeFile(const char* path) {
    if (!s_mounted) return false;
    return LittleFS.remove(path);
}

bool fileExists(const char* path) {
    if (!s_mounted) return false;
    return LittleFS.exists(path);
}

size_t fileSize(const char* path) {
    if (!s_mounted) return 0;
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}

Stats stats() {
    Stats s{};
    if (!s_mounted) return s;
    s.total = LittleFS.totalBytes();
    s.used  = LittleFS.usedBytes();
    s.free  = (s.total > s.used) ? (s.total - s.used) : 0;
    s.handshakes = countFiles(DIR_HANDSHAKES);
    s.results    = countFiles(DIR_RESULTS);
    return s;
}

uint16_t forEachHandshake(FileVisitor fn, void* ctx) {
    uint16_t n = 0;
    if (!s_mounted || !fn) return 0;
    File root = LittleFS.open(DIR_HANDSHAKES);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }
    File f = root.openNextFile();
    while (f) {
        if (!f.isDirectory()) {
            char name[FILE_NAME_MAX];
            copyName(baseName(f.name()), name, sizeof(name));
            size_t sz = f.size();
            f.close();
            fn(name, sz, ctx);
            n++;
        } else {
            f.close();
        }
        f = root.openNextFile();
    }
    root.close();
    return n;
}

uint16_t listHandshakes(char out[][FILE_NAME_MAX], uint16_t max) {
    return listDir(DIR_HANDSHAKES, out, max);
}

uint16_t listResults(char out[][FILE_NAME_MAX], uint16_t max) {
    return listDir(DIR_RESULTS, out, max);
}

bool formatStorage() {
    if (s_mounted) {
        LittleFS.end();
        s_mounted = false;
    }
    if (!formatPartition()) return false;
    return begin();
}

} // namespace Storage

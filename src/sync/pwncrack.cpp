// sync/pwncrack.cpp
#include "pwncrack.h"
#include "../storage/littlefs_ops.h"
#include "../cap/hc22000.h"
#include "../net/ap_sta.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <LittleFS.h>
#include <ctype.h>
#include <string.h>
#include <esp_heap_caps.h>

static const char* PWN_HOST = "pwncrack.org";
static const char* PWN_UPLOAD_PATH = "/upload_handshake";
static const char* PWN_POTFILE_PATH = "/download_potfile_script";
static const size_t PWN_MAX_CACHE = 100;
static const uint8_t PWN_MAX_PENDING = 16;

bool Pwncrack::cacheLoaded = false;
char Pwncrack::lastError[64] = "";
volatile bool Pwncrack::busy = false;
std::vector<Pwncrack::CrackedEntry> Pwncrack::crackedCache;
std::vector<Pwncrack::UploadedEntry> Pwncrack::uploadedCache;

bool Pwncrack::isBusy() { return busy; }
const char* Pwncrack::getLastError() { return lastError; }

static bool writeAll(WiFiClient& c, const uint8_t* p, size_t n) {
    size_t off = 0;
    while (off < n) {
        size_t w = c.write(p + off, n - off);
        if (w == 0) return false;
        off += w;
        yield();
    }
    return true;
}

static bool writeStr(WiFiClient& c, const char* s) {
    return writeAll(c, reinterpret_cast<const uint8_t*>(s), strlen(s));
}

bool Pwncrack::hasApiKey(const char* key) {
    if (!key || !key[0]) return false;
    size_t n = strlen(key);
    if (n < 4 || n > 64) return false;
    for (size_t i = 0; i < n; i++) {
        if (key[i] < 0x20 || key[i] > 0x7E) return false;
    }
    return true;
}

bool Pwncrack::hasApiKey() {
    return hasApiKey(Net::cfg().pwncrackKey);
}

bool Pwncrack::canSync() {
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t freeH = ESP.getFreeHeap();
    if (largest < 8000 || freeH < 16000) {
        snprintf(lastError, sizeof(lastError), "low heap %u/%uK",
                 (unsigned)(largest / 1024), (unsigned)(freeH / 1024));
        return false;
    }
    lastError[0] = '\0';
    return true;
}

void Pwncrack::freeCacheMemory() {
    crackedCache.clear();
    crackedCache.shrink_to_fit();
    uploadedCache.clear();
    uploadedCache.shrink_to_fit();
    cacheLoaded = false;
}

bool Pwncrack::loadUploadedList() {
    uploadedCache.clear();
    if (!Storage::fileExists(Storage::FILE_PWNCRACK_UPLOADED)) return true;
    File f = LittleFS.open(Storage::FILE_PWNCRACK_UPLOADED, "r");
    if (!f) return false;
    char line[64];
    while (f.available() && uploadedCache.size() < PWN_MAX_CACHE) {
        size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[n] = '\0';
        while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
        if (n == 0) continue;
        UploadedEntry e{};
        strncpy(e.id, line, sizeof(e.id) - 1);
        uploadedCache.push_back(e);
    }
    f.close();
    return true;
}

bool Pwncrack::saveUploadedList() {
    Storage::ensureDir(Storage::DIR_RESULTS);
    File f = LittleFS.open(Storage::FILE_PWNCRACK_UPLOADED, "w");
    if (!f) return false;
    for (const auto& e : uploadedCache) f.println(e.id);
    f.close();
    return true;
}

bool Pwncrack::loadCache() {
    if (cacheLoaded) return true;
    crackedCache.clear();
    loadUploadedList();

    if (Storage::fileExists(Storage::FILE_PWNCRACK_RESULTS)) {
        File f = LittleFS.open(Storage::FILE_PWNCRACK_RESULTS, "r");
        if (f) {
            char line[160];
            while (f.available() && crackedCache.size() < PWN_MAX_CACHE) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                line[n] = '\0';
                while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
                if (n < 3) continue;

                char buf[160];
                strncpy(buf, line, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
                char* parts[8];
                int pc = 0;
                char* p = buf;
                while (pc < 8) {
                    parts[pc++] = p;
                    char* c = strchr(p, ':');
                    if (!c) break;
                    *c = '\0';
                    p = c + 1;
                }

                CrackedEntry e{};
                if (pc >= 5) {
                    strncpy(e.label, parts[3], sizeof(e.label) - 1);
                    strncpy(e.password, parts[4], sizeof(e.password) - 1);
                    strncpy(e.id, parts[3], sizeof(e.id) - 1);
                    if (strlen(parts[0]) == 12) {
                        bool hex = true;
                        for (int i = 0; i < 12; i++) {
                            if (!isxdigit((unsigned char)parts[0][i])) { hex = false; break; }
                        }
                        if (hex) {
                            for (int i = 0; i < 12; i++) {
                                e.id[i] = (char)toupper((unsigned char)parts[0][i]);
                            }
                            e.id[12] = '\0';
                        }
                    }
                } else if (pc >= 2) {
                    strncpy(e.label, parts[0], sizeof(e.label) - 1);
                    strncpy(e.password, parts[pc - 1], sizeof(e.password) - 1);
                    strncpy(e.id, parts[0], sizeof(e.id) - 1);
                } else {
                    continue;
                }
                if (e.password[0]) crackedCache.push_back(e);
            }
            f.close();
        }
    }
    cacheLoaded = true;
    return true;
}

bool Pwncrack::findUploaded(const char* id) {
    if (!id) return false;
    for (const auto& e : uploadedCache) {
        if (strcmp(e.id, id) == 0) return true;
    }
    return false;
}

bool Pwncrack::isCracked(const char* key) {
    if (!cacheLoaded) loadCache();
    if (!key) return false;
    for (const auto& e : crackedCache) {
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return true;
    }
    return false;
}

const char* Pwncrack::getPassword(const char* key) {
    if (!cacheLoaded) loadCache();
    if (!key) return "";
    for (const auto& e : crackedCache) {
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return e.password;
    }
    return "";
}

uint16_t Pwncrack::getCrackedCount() {
    if (!cacheLoaded) loadCache();
    return (uint16_t)crackedCache.size();
}

bool Pwncrack::isUploaded(const char* filename) {
    if (!cacheLoaded) loadCache();
    return findUploaded(filename);
}

void Pwncrack::markAsUploaded(const char* filename) {
    if (!filename || !filename[0] || findUploaded(filename)) return;
    if (uploadedCache.size() >= PWN_MAX_CACHE) return;
    UploadedEntry e{};
    strncpy(e.id, filename, sizeof(e.id) - 1);
    uploadedCache.push_back(e);
}

bool Pwncrack::uploadFile(const char* filepath, const char* apiKey) {
    if (!filepath || !apiKey) return false;
    File capFile = LittleFS.open(filepath, "r");
    if (!capFile) {
        snprintf(lastError, sizeof(lastError), "open fail");
        return false;
    }
    size_t fileSize = capFile.size();
    if (fileSize == 0 || fileSize > 200000) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "bad size");
        return false;
    }

    const char* filename = Storage::baseName(filepath);
    Serial.printf("[PWNCRACK] upload %s (%u B)\n", filename, (unsigned)fileSize);

    char boundary[32];
    snprintf(boundary, sizeof(boundary), "----Pwn%08lX", (unsigned long)millis());
    char keyPart[192];
    snprintf(keyPart, sizeof(keyPart),
             "--%s\r\nContent-Disposition: form-data; name=\"key\"\r\n\r\n%s\r\n",
             boundary, apiKey);
    char fileHead[256];
    snprintf(fileHead, sizeof(fileHead),
             "--%s\r\nContent-Disposition: form-data; name=\"handshake\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n",
             boundary, filename);
    char fileTail[48];
    snprintf(fileTail, sizeof(fileTail), "\r\n--%s--\r\n", boundary);
    size_t contentLength = strlen(keyPart) + strlen(fileHead) + fileSize + strlen(fileTail);

    WiFiClient client;
    client.setTimeout(15000);
    if (!client.connect(PWN_HOST, 80, 8000)) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "connect fail");
        return false;
    }

    char hdr[280];
    snprintf(hdr, sizeof(hdr),
             "POST %s HTTP/1.1\r\nHost: %s\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %u\r\nConnection: close\r\n\r\n",
             PWN_UPLOAD_PATH, PWN_HOST, boundary, (unsigned)contentLength);
    if (!writeStr(client, hdr) || !writeStr(client, keyPart) || !writeStr(client, fileHead)) {
        capFile.close();
        client.stop();
        snprintf(lastError, sizeof(lastError), "send hdr");
        return false;
    }

    uint8_t buf[512];
    size_t left = fileSize;
    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
        size_t rd = capFile.read(buf, chunk);
        if (rd == 0) break;
        if (!writeAll(client, buf, rd)) {
            capFile.close();
            client.stop();
            snprintf(lastError, sizeof(lastError), "send body");
            return false;
        }
        left -= rd;
        yield();
    }
    capFile.close();
    if (!writeStr(client, fileTail)) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "send tail");
        return false;
    }

    unsigned long t0 = millis();
    char status[80] = {0};
    size_t si = 0;
    while (millis() - t0 < 20000) {
        if (!client.available()) {
            if (!client.connected() && si > 0) break;
            delay(10);
            yield();
            continue;
        }
        char ch = (char)client.read();
        if (ch == '\n') break;
        if (ch != '\r' && si + 1 < sizeof(status)) status[si++] = ch;
    }
    status[si] = '\0';
    client.stop();
    Serial.printf("[PWNCRACK] %s\n", status);

    bool ok = strstr(status, "200") || strstr(status, "201") || strstr(status, "409");
    if (!ok) {
        if (strstr(status, "401") || strstr(status, "403")) {
            snprintf(lastError, sizeof(lastError), "bad key");
        } else if (strstr(status, "400")) {
            snprintf(lastError, sizeof(lastError), "rejected file");
        } else if (!status[0]) {
            snprintf(lastError, sizeof(lastError), "no reply");
        } else {
            snprintf(lastError, sizeof(lastError), "http fail");
        }
        return false;
    }
    lastError[0] = '\0';
    return true;
}

bool Pwncrack::downloadPotfile(const char* apiKey, uint16_t& newCracks) {
    newCracks = 0;
    if (!apiKey) return false;

    WiFiClient client;
    client.setTimeout(15000);
    if (!client.connect(PWN_HOST, 80, 8000)) {
        snprintf(lastError, sizeof(lastError), "pot connect");
        return false;
    }

    char req[256];
    snprintf(req, sizeof(req),
             "GET %s?key=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
             PWN_POTFILE_PATH, apiKey, PWN_HOST);
    if (!writeStr(client, req)) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot send");
        return false;
    }

    bool headersDone = false;
    bool statusOk = false;
    bool first = true;
    char line[200];
    size_t li = 0;
    unsigned long t0 = millis();

    Storage::ensureDir(Storage::DIR_RESULTS);
    File out = LittleFS.open(Storage::FILE_PWNCRACK_RESULTS, "w");
    if (!out) {
        client.stop();
        snprintf(lastError, sizeof(lastError), "pot save");
        return false;
    }

    size_t body = 0;
    while (millis() - t0 < 25000) {
        if (!client.available()) {
            if (!client.connected() && headersDone) break;
            delay(5);
            yield();
            continue;
        }
        int ch = client.read();
        if (ch < 0) continue;
        if (!headersDone) {
            if (ch == '\n') {
                line[li] = '\0';
                if (first) {
                    first = false;
                    statusOk = (strstr(line, "200") != nullptr);
                    Serial.printf("[PWNCRACK] pot %s\n", line);
                }
                if (li == 0 || (li == 1 && line[0] == '\r')) headersDone = true;
                li = 0;
            } else if (ch != '\r' && li + 1 < sizeof(line)) {
                line[li++] = (char)ch;
            }
        } else if (statusOk) {
            out.write((uint8_t)ch);
            body++;
            if (body > 80000) break;
        }
    }
    out.close();
    client.stop();

    if (!headersDone || !statusOk) {
        snprintf(lastError, sizeof(lastError), "pot http");
        return false;
    }

    uint16_t before = cacheLoaded ? (uint16_t)crackedCache.size() : 0;
    cacheLoaded = false;
    loadCache();
    uint16_t after = (uint16_t)crackedCache.size();
    newCracks = (after > before) ? (uint16_t)(after - before) : 0;
    Serial.printf("[PWNCRACK] pot %u B cracked=%u\n", (unsigned)body, after);
    lastError[0] = '\0';
    return true;
}

struct PwnScanCtx {
    struct Item {
        char path[48];
        char id[32];
    };
    Item items[PWN_MAX_PENDING];
    uint8_t count;
    uint8_t skipped;
};

static bool isHashName(const char* name) {
    size_t n = strlen(name);
    if (n > 6 && strcasecmp(name + n - 6, ".22000") == 0) return true;
    if (n > 8 && strcasecmp(name + n - 8, ".hc22000") == 0) return true;
    return false;
}

static void pwnCollect(const char* name, size_t size, void* raw) {
    PwnScanCtx* ctx = (PwnScanCtx*)raw;
    if (ctx->count >= PWN_MAX_PENDING) return;
    if (size == 0 || !isHashName(name)) return;
    if (Pwncrack::isUploaded(name)) {
        ctx->skipped++;
        return;
    }
    snprintf(ctx->items[ctx->count].path, sizeof(ctx->items[0].path),
             "%s/%s", Storage::DIR_HANDSHAKES, name);
    strncpy(ctx->items[ctx->count].id, name, sizeof(ctx->items[0].id) - 1);
    ctx->count++;
}

PwncrackSyncResult Pwncrack::syncCaptures(const char* apiKey, PwncrackProgressCallback cb) {
    PwncrackSyncResult result{};
    result.success = false;
    result.error[0] = '\0';

    if (busy) {
        strncpy(result.error, "already syncing", sizeof(result.error) - 1);
        return result;
    }
    if (!hasApiKey(apiKey)) {
        strncpy(result.error, "missing API key", sizeof(result.error) - 1);
        return result;
    }
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(result.error, "wifi not connected", sizeof(result.error) - 1);
        return result;
    }

    busy = true;
    freeCacheMemory();
    if (!canSync()) {
        strncpy(result.error, lastError[0] ? lastError : "low heap", sizeof(result.error) - 1);
        busy = false;
        return result;
    }

    if (cb) cb("Converting", 0, 1);
    uint16_t conv = Hc22000::convertAllPcaps();
    Serial.printf("[PWNCRACK] converted %u pcap->22000\n", (unsigned)conv);

    loadCache();
    PwnScanCtx scan{};
    Storage::forEachHandshake(pwnCollect, &scan);
    result.skipped = scan.skipped;

    if (cb) cb("Uploading", 0, scan.count);
    for (uint8_t i = 0; i < scan.count; i++) {
        if (cb) cb("Uploading", i + 1, scan.count);
        if (uploadFile(scan.items[i].path, apiKey)) {
            markAsUploaded(scan.items[i].id);
            result.uploaded++;
        } else {
            result.failed++;
        }
        delay(50);
        yield();
    }
    if (result.uploaded > 0) saveUploadedList();

    if (cb) cb("Potfile", scan.count, scan.count);
    uint16_t newCracks = 0;
    bool potOk = downloadPotfile(apiKey, newCracks);
    if (potOk) {
        result.newCracked = newCracks;
        result.cracked = getCrackedCount();
        result.success = true;
    } else {
        result.success = (result.uploaded > 0 || result.skipped > 0);
        if (!result.success) {
            strncpy(result.error, lastError[0] ? lastError : "pot fail", sizeof(result.error) - 1);
        } else {
            snprintf(result.error, sizeof(result.error), "pot: %s", lastError);
        }
    }

    busy = false;
    Serial.printf("[PWNCRACK] done up=%u fail=%u skip=%u cracked=%u\n",
                  result.uploaded, result.failed, result.skipped, result.cracked);
    return result;
}

// Pwncrack.org client — upload hashcat 22000 + download potfile
// API from official plugin: https://github.com/Terminatoror/pwncrack-addons

#include "pwncrack.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include "../core/wifi_utils.h"
#include "../core/heap_gates.h"
#include "../piglet/avatar.h"
#include "../piglet/mood.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <WiFi.h>
#include <SD.h>
#include <ctype.h>
#include <string.h>
#include <esp_heap_caps.h>

static const char* PWN_HOST = "pwncrack.org";
static const char* PWN_UPLOAD_PATH = "/upload_handshake";
static const char* PWN_POTFILE_PATH = "/download_potfile_script";
static const size_t PWN_MAX_CACHE = 400;

bool Pwncrack::cacheLoaded = false;
char Pwncrack::lastError[64] = "";
volatile bool Pwncrack::busy = false;
std::vector<Pwncrack::CrackedEntry> Pwncrack::crackedCache;
std::vector<Pwncrack::UploadedEntry> Pwncrack::uploadedCache;

bool Pwncrack::isBusy() { return busy; }
const char* Pwncrack::getLastError() { return lastError; }

bool Pwncrack::hasApiKey() {
    const char* key = Config::wifi().pwncrackKey;
    if (!key || key[0] == '\0') {
        // Try NVS again (RAM may have been wiped by config reload)
        if (Config::reloadPwncrackKeyFromNvs()) {
            key = Config::wifi().pwncrackKey;
        }
    }
    if (!key || key[0] == '\0') return false;
    size_t len = strlen(key);
    if (len < 4 || len > 64) return false;
    for (size_t i = 0; i < len; i++) {
        if (key[i] < 0x20 || key[i] > 0x7E) return false;
    }
    return true;
}

void Pwncrack::freeCacheMemory() {
    crackedCache.clear();
    crackedCache.shrink_to_fit();
    uploadedCache.clear();
    uploadedCache.shrink_to_fit();
    cacheLoaded = false;
    Serial.println("[PWNCRACK] Cache freed");
}

bool Pwncrack::loadUploadedList() {
    uploadedCache.clear();
    const char* path = SDLayout::pwncrackUploadedPath();
    if (!SD.exists(path)) return true;
    File f = SD.open(path, FILE_READ);
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
    const char* path = SDLayout::pwncrackUploadedPath();
    const char* dir = SDLayout::pwncrackDir();
    if (!SD.exists(dir)) SD.mkdir(dir);
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    for (const auto& e : uploadedCache) {
        f.println(e.id);
    }
    f.close();
    return true;
}

bool Pwncrack::findUploaded(const char* id) {
    if (!id) return false;
    for (const auto& e : uploadedCache) {
        if (strcmp(e.id, id) == 0) return true;
    }
    return false;
}

bool Pwncrack::isUploaded(const char* id) {
    if (!cacheLoaded) loadCache();
    return findUploaded(id);
}

void Pwncrack::markAsUploaded(const char* id) {
    if (!id || id[0] == '\0') return;
    if (findUploaded(id)) return;
    if (uploadedCache.size() >= PWN_MAX_CACHE) return;
    UploadedEntry e{};
    strncpy(e.id, id, sizeof(e.id) - 1);
    uploadedCache.push_back(e);
    saveUploadedList();
}

bool Pwncrack::loadCache() {
    crackedCache.clear();
    loadUploadedList();

    const char* path = SDLayout::pwncrackResultsPath();
    if (SD.exists(path)) {
        File f = SD.open(path, FILE_READ);
        if (f) {
            char line[160];
            while (f.available() && crackedCache.size() < PWN_MAX_CACHE) {
                size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
                line[n] = '\0';
                while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
                if (n < 3) continue;

                // potfile lines: flexible — plugin shows fields [3]=AP [4]=Pass when 5+ cols
                // Also accept "SSID:password" or "BSSID:SSID:password"
                char* parts[8];
                int pc = 0;
                char buf[160];
                strncpy(buf, line, sizeof(buf) - 1);
                buf[sizeof(buf) - 1] = '\0';
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
                    // Official plugin: fields[3]=AP, fields[4]=Pass
                    strncpy(e.label, parts[3], sizeof(e.label) - 1);
                    strncpy(e.password, parts[4], sizeof(e.password) - 1);
                    strncpy(e.id, parts[3], sizeof(e.id) - 1);
                    // If early field is 12 hex BSSID, prefer it as id for MAC match
                    auto isHex12 = [](const char* s) -> bool {
                        if (!s || strlen(s) != 12) return false;
                        for (int i = 0; i < 12; i++) {
                            char c = s[i];
                            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                                  (c >= 'A' && c <= 'F'))) return false;
                        }
                        return true;
                    };
                    if (isHex12(parts[0])) {
                        // normalize uppercase into id, keep label as AP/SSID
                        for (int i = 0; i < 12; i++) {
                            char c = parts[0][i];
                            e.id[i] = (c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
                        }
                        e.id[12] = '\0';
                    }
                } else if (pc >= 2) {
                    strncpy(e.label, parts[0], sizeof(e.label) - 1);
                    strncpy(e.password, parts[pc - 1], sizeof(e.password) - 1);
                    strncpy(e.id, parts[0], sizeof(e.id) - 1);
                } else {
                    continue;
                }
                if (e.password[0] == '\0') continue;
                crackedCache.push_back(e);
            }
            f.close();
        }
    }
    cacheLoaded = true;
    Serial.printf("[PWNCRACK] Cache: %u cracked, %u uploaded\n",
                  (unsigned)crackedCache.size(), (unsigned)uploadedCache.size());
    return true;
}

uint16_t Pwncrack::getCrackedCount() {
    if (!cacheLoaded) loadCache();
    return (uint16_t)crackedCache.size();
}

bool Pwncrack::isCracked(const char* key) {
    if (!key) return false;
    if (!cacheLoaded) loadCache();
    for (const auto& e : crackedCache) {
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return true;
    }
    return false;
}

const char* Pwncrack::getPassword(const char* key) {
    if (!key) return "";
    if (!cacheLoaded) loadCache();
    for (const auto& e : crackedCache) {
        if (strcasecmp(e.id, key) == 0 || strcasecmp(e.label, key) == 0) return e.password;
    }
    return "";
}

bool Pwncrack::canSync() {
    // Official path is plain HTTP — do NOT require TLS contiguous heap.
    // (Old FRAG:15KB/27KB gate blocked sync even when HTTP would work.)
    freeCacheMemory();
    size_t freeH = ESP.getFreeHeap();
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    // Soft floor: enough for WiFiClient + buffers (~8–12KB is plenty for HTTP)
    if (freeH < 12000 || largest < 6000) {
        snprintf(lastError, sizeof(lastError), "LOW HEAP %u/%uK",
                 (unsigned)(largest / 1024), (unsigned)(freeH / 1024));
        return false;
    }
    lastError[0] = '\0';
    return true;
}

// Official plugin uses HTTP only. Prefer HTTP (cheap), fall back to HTTPS.
// Short timeouts so UI does not freeze if host is blocked.
static bool connectPwn(WiFiClientSecure& tls, WiFiClient& plain, bool& useTls) {
    useTls = false;
    plain.setTimeout(8000);
    if (plain.connect(PWN_HOST, 80, 8000)) {
        return true;
    }
    useTls = true;
    tls.setInsecure();
    tls.setTimeout(10000);
    return tls.connect(PWN_HOST, 443, 10000);
}

static void diagAddLine(PwncrackDiagResult& d, const char* s) {
    if (d.lineCount >= 6 || !s) return;
    strncpy(d.lines[d.lineCount], s, sizeof(d.lines[0]) - 1);
    d.lines[d.lineCount][sizeof(d.lines[0]) - 1] = '\0';
    d.lineCount++;
}

PwncrackDiagResult Pwncrack::runDiagnostics(PwncrackProgressCallback cb) {
    PwncrackDiagResult d = {};
    d.wifiOk = d.dnsOk = d.tcp80Ok = d.httpOk = d.potOk = d.keyOk = false;
    d.httpCode = d.potCode = -1;
    d.potBytes = 0;
    d.tcp80Ms = d.httpMs = 0;
    d.ip[0] = '\0';
    d.detail[0] = '\0';
    d.lineCount = 0;
    busy = true;

    struct SceneGuard {
        SceneGuard() { Avatar::suspendScene(); }
        ~SceneGuard() { Avatar::resumeScene(); }
    } sceneGuard;

    d.keyOk = hasApiKey();
    diagAddLine(d, d.keyOk ? "1 KEY: OK" : "1 KEY: MISSING");

    if (WiFi.status() != WL_CONNECTED) {
        diagAddLine(d, "2 WIFI: NOT CONNECTED");
        strncpy(d.detail, "WIFI DOWN", sizeof(d.detail) - 1);
        busy = false;
        return d;
    }
    d.wifiOk = true;
    IPAddress ip = WiFi.localIP();
    snprintf(d.ip, sizeof(d.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    char wline[28];
    snprintf(wline, sizeof(wline), "2 WIFI: %s", d.ip);
    diagAddLine(d, wline);

    if (cb) cb("DNS...", 1, 5);

    // DNS resolve
    IPAddress resolved;
    uint32_t tDns = millis();
    bool dns = WiFi.hostByName(PWN_HOST, resolved);
    uint32_t dnsMs = millis() - tDns;
    if (!dns) {
        char line[28];
        snprintf(line, sizeof(line), "3 DNS: FAIL %ums", (unsigned)dnsMs);
        diagAddLine(d, line);
        strncpy(d.detail, "DNS BLOCK/FAIL", sizeof(d.detail) - 1);
        busy = false;
        return d;
    }
    d.dnsOk = true;
    {
        char line[28];
        snprintf(line, sizeof(line), "3 DNS: %u.%u.%u.%u",
                 resolved[0], resolved[1], resolved[2], resolved[3]);
        diagAddLine(d, line);
    }

    if (cb) cb("TCP:80...", 2, 5);

    // TCP :80
    WiFiClient plain;
    plain.setTimeout(8000);
    uint32_t t0 = millis();
    bool tcp = plain.connect(PWN_HOST, 80, 8000);
    d.tcp80Ms = millis() - t0;
    if (!tcp) {
        char line[28];
        snprintf(line, sizeof(line), "4 TCP80: FAIL %ums", (unsigned)d.tcp80Ms);
        diagAddLine(d, line);
        // Try :443 quickly
        if (cb) cb("TCP:443...", 3, 5);
        WiFiClientSecure tls;
        tls.setInsecure();
        tls.setTimeout(8000);
        t0 = millis();
        bool tlsOk = tls.connect(PWN_HOST, 443, 8000);
        uint32_t tlsMs = millis() - t0;
        if (tlsOk) {
            char l2[28];
            snprintf(l2, sizeof(l2), "4b TLS443: OK %ums", (unsigned)tlsMs);
            diagAddLine(d, l2);
            tls.stop();
            strncpy(d.detail, "HTTP BLOCKED? TLS OK", sizeof(d.detail) - 1);
        } else {
            char l2[28];
            snprintf(l2, sizeof(l2), "4b TLS443: FAIL %ums", (unsigned)tlsMs);
            diagAddLine(d, l2);
            strncpy(d.detail, "HOST UNREACHABLE", sizeof(d.detail) - 1);
        }
        busy = false;
        return d;
    }
    d.tcp80Ok = true;
    {
        char line[28];
        snprintf(line, sizeof(line), "4 TCP80: OK %ums", (unsigned)d.tcp80Ms);
        diagAddLine(d, line);
    }

    if (cb) cb("HTTP GET...", 3, 5);

    // Minimal HTTP GET /  (proves server talks HTTP, not just open port)
    t0 = millis();
    plain.print("GET / HTTP/1.1\r\nHost: pwncrack.org\r\nConnection: close\r\n\r\n");
    char statusLine[80] = {0};
    size_t si = 0;
    uint32_t wait0 = millis();
    while (millis() - wait0 < 10000) {
        if (plain.available()) {
            char ch = (char)plain.read();
            if (ch == '\n') break;
            if (ch != '\r' && si + 1 < sizeof(statusLine)) statusLine[si++] = ch;
        } else if (!plain.connected()) {
            break;
        } else {
            delay(5);
            yield();
        }
    }
    d.httpMs = millis() - t0;
    plain.stop();
    statusLine[si] = '\0';

    // parse code
    {
        const char* p = strchr(statusLine, ' ');
        if (p) d.httpCode = atoi(p + 1);
    }
    d.httpOk = (d.httpCode >= 200 && d.httpCode < 500);  // any real HTTP reply = reachability
    {
        char line[28];
        if (statusLine[0]) {
            snprintf(line, sizeof(line), "5 HTTP: %d %ums", d.httpCode, (unsigned)d.httpMs);
        } else {
            snprintf(line, sizeof(line), "5 HTTP: NO REPLY %ums", (unsigned)d.httpMs);
        }
        diagAddLine(d, line);
    }
    if (!d.httpOk) {
        strncpy(d.detail, statusLine[0] ? "HTTP BAD" : "HTTP NO REPLY", sizeof(d.detail) - 1);
        busy = false;
        return d;
    }

    // Potfile API (needs key) — proves API works like upload would
    if (cb) cb("POTFILE...", 4, 5);
    if (!d.keyOk) {
        diagAddLine(d, "6 POT: SKIP (NO KEY)");
        strncpy(d.detail, "NET OK - ADD KEY", sizeof(d.detail) - 1);
        busy = false;
        return d;
    }

    WiFiClient pot;
    pot.setTimeout(10000);
    if (!pot.connect(PWN_HOST, 80, 8000)) {
        diagAddLine(d, "6 POT: CONNECT FAIL");
        strncpy(d.detail, "POT CONNECT FAIL", sizeof(d.detail) - 1);
        busy = false;
        return d;
    }
    const char* key = Config::wifi().pwncrackKey;
    char req[192];
    snprintf(req, sizeof(req),
             "GET %s?key=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
             PWN_POTFILE_PATH, key, PWN_HOST);
    pot.print(req);

    char potStatus[80] = {0};
    si = 0;
    bool headersDone = false;
    bool first = true;
    size_t body = 0;
    wait0 = millis();
    char hline[8];
    size_t li = 0;
    while (millis() - wait0 < 15000) {
        if (!pot.available()) {
            if (!pot.connected() && headersDone) break;
            delay(5);
            yield();
            continue;
        }
        char ch = (char)pot.read();
        if (first) {
            if (ch == '\n') {
                potStatus[si] = '\0';
                first = false;
                const char* p = strchr(potStatus, ' ');
                if (p) d.potCode = atoi(p + 1);
                li = 0;
            } else if (ch != '\r' && si + 1 < sizeof(potStatus)) {
                potStatus[si++] = ch;
            }
            continue;
        }
        if (!headersDone) {
            if (ch == '\n') {
                if (li == 0 || (li == 1 && hline[0] == '\r')) headersDone = true;
                li = 0;
            } else {
                if (li + 1 < sizeof(hline)) hline[li++] = ch;
                else li = sizeof(hline) - 1;
            }
            continue;
        }
        body++;
        if (body > 4000) break;  // enough to prove download
    }
    pot.stop();
    d.potBytes = (uint16_t)body;
    d.potOk = (d.potCode == 200);
    {
        char line[28];
        snprintf(line, sizeof(line), "6 POT: %d %uB", d.potCode, (unsigned)d.potBytes);
        diagAddLine(d, line);
    }

    if (d.potOk) {
        strncpy(d.detail, "ALL OK - CAN SYNC", sizeof(d.detail) - 1);
    } else if (d.potCode == 401 || d.potCode == 403) {
        strncpy(d.detail, "NET OK - BAD KEY", sizeof(d.detail) - 1);
    } else if (d.potCode > 0) {
        snprintf(d.detail, sizeof(d.detail), "NET OK POT HTTP %d", d.potCode);
    } else {
        strncpy(d.detail, "NET OK POT FAIL", sizeof(d.detail) - 1);
    }

    Serial.printf("[PWNCRACK] Diag: wifi=%d dns=%d tcp80=%d http=%d pot=%d | %s\n",
                  d.wifiOk, d.dnsOk, d.tcp80Ok, d.httpOk, d.potOk, d.detail);
    busy = false;
    return d;
}

// pwncrack.org submit page: only files ending with .hc22000 are accepted
static void makeHc22000Name(const char* base, char* out, size_t outLen) {
    if (!base || !out || outLen < 12) return;
    out[0] = '\0';
    size_t n = strlen(base);
    // already .hc22000
    if (n > 8) {
        const char* t = base + n - 8;
        bool hc = true;
        const char* exp = ".hc22000";
        for (int i = 0; i < 8; i++) {
            char a = t[i], b = exp[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != b) { hc = false; break; }
        }
        if (hc) {
            strncpy(out, base, outLen - 1);
            out[outLen - 1] = '\0';
            return;
        }
    }
    // ends with .22000 → rewrite to .hc22000
    if (n > 6) {
        const char* t = base + n - 6;
        bool e = true;
        const char* exp = ".22000";
        for (int i = 0; i < 6; i++) {
            char a = t[i], b = exp[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != b) { e = false; break; }
        }
        if (e) {
            // stem + .hc22000
            size_t stem = n - 6;
            if (stem + 8 >= outLen) stem = outLen - 9;
            memcpy(out, base, stem);
            memcpy(out + stem, ".hc22000", 8);
            out[stem + 8] = '\0';
            return;
        }
    }
    // fallback: append .hc22000
    snprintf(out, outLen, "%s.hc22000", base);
}

bool Pwncrack::uploadFile(const char* filepath) {
    if (!filepath) return false;
    File capFile = SD.open(filepath, FILE_READ);
    if (!capFile) {
        snprintf(lastError, sizeof(lastError), "OPEN FAIL");
        return false;
    }
    size_t fileSize = capFile.size();
    if (fileSize == 0 || fileSize > 200000) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "BAD SIZE");
        return false;
    }

    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    // CRITICAL: site rejects non-.hc22000 names (content is still hashcat 22000)
    char uploadName[64];
    makeHc22000Name(filename, uploadName, sizeof(uploadName));
    Serial.printf("[PWNCRACK] Upload %s as %s (%u B)\n",
                  filename, uploadName, (unsigned)fileSize);

    const char* key = Config::wifi().pwncrackKey;
    if (!key || key[0] == '\0') {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "NO KEY");
        return false;
    }

    char boundary[32];
    snprintf(boundary, sizeof(boundary), "----Pwn%08lX", (unsigned long)millis());

    // multipart: key field + handshake file (same as official plugin)
    char keyPart[160];
    snprintf(keyPart, sizeof(keyPart),
             "--%s\r\nContent-Disposition: form-data; name=\"key\"\r\n\r\n%s\r\n",
             boundary, key);
    char fileHead[220];
    snprintf(fileHead, sizeof(fileHead),
             "--%s\r\nContent-Disposition: form-data; name=\"handshake\"; filename=\"%s\"\r\n"
             "Content-Type: application/octet-stream\r\n\r\n",
             boundary, uploadName);
    char fileTail[48];
    snprintf(fileTail, sizeof(fileTail), "\r\n--%s--\r\n", boundary);

    size_t contentLength = strlen(keyPart) + strlen(fileHead) + fileSize + strlen(fileTail);

    WiFiClientSecure tls;
    WiFiClient plain;
    bool useTls = false;
    if (!connectPwn(tls, plain, useTls)) {
        capFile.close();
        snprintf(lastError, sizeof(lastError), "CONNECT FAIL");
        return false;
    }
    Serial.printf("[PWNCRACK] Connected via %s\n", useTls ? "HTTPS" : "HTTP");

    auto writeAll = [&](const uint8_t* data, size_t n) -> bool {
        size_t off = 0;
        while (off < n) {
            size_t w = useTls ? tls.write(data + off, n - off) : plain.write(data + off, n - off);
            if (w == 0) return false;
            off += w;
            yield();
        }
        return true;
    };
    auto printStr = [&](const char* s) -> bool {
        return writeAll(reinterpret_cast<const uint8_t*>(s), strlen(s));
    };

    char hdr[280];
    snprintf(hdr, sizeof(hdr),
             "POST %s HTTP/1.1\r\nHost: %s\r\n"
             "Content-Type: multipart/form-data; boundary=%s\r\n"
             "Content-Length: %u\r\nConnection: close\r\n\r\n",
             PWN_UPLOAD_PATH, PWN_HOST, boundary, (unsigned)contentLength);
    if (!printStr(hdr) || !printStr(keyPart) || !printStr(fileHead)) {
        capFile.close();
        if (useTls) tls.stop(); else plain.stop();
        snprintf(lastError, sizeof(lastError), "SEND HDR");
        return false;
    }
    uint8_t buf[512];
    size_t left = fileSize;
    while (left > 0) {
        size_t chunk = left > sizeof(buf) ? sizeof(buf) : left;
        size_t rd = capFile.read(buf, chunk);
        if (rd == 0) break;
        if (!writeAll(buf, rd)) {
            capFile.close();
            if (useTls) tls.stop(); else plain.stop();
            snprintf(lastError, sizeof(lastError), "SEND BODY");
            return false;
        }
        left -= rd;
        yield();
    }
    capFile.close();
    if (!printStr(fileTail)) {
        if (useTls) tls.stop(); else plain.stop();
        snprintf(lastError, sizeof(lastError), "SEND TAIL");
        return false;
    }

    // Read status line + skip headers + sample body (JSON from server)
    char statusLine[96] = {0};
    char bodySample[96] = {0};
    uint32_t t0 = millis();
    size_t si = 0;
    bool gotStatus = false;
    bool headersDone = false;
    size_t bi = 0;
    char lineBuf[8];
    size_t li = 0;

    while (millis() - t0 < 20000) {
        int avail = useTls ? tls.available() : plain.available();
        if (avail <= 0) {
            if ((useTls && !tls.connected()) || (!useTls && !plain.connected())) break;
            delay(10);
            yield();
            continue;
        }
        char ch = useTls ? (char)tls.read() : (char)plain.read();
        if (!gotStatus) {
            if (ch == '\n') {
                statusLine[si] = '\0';
                gotStatus = true;
                li = 0;
            } else if (ch != '\r' && si + 1 < sizeof(statusLine)) {
                statusLine[si++] = ch;
            }
            continue;
        }
        if (!headersDone) {
            if (ch == '\n') {
                // empty line ends headers
                if (li == 0 || (li == 1 && lineBuf[0] == '\r')) {
                    headersDone = true;
                }
                li = 0;
            } else {
                if (li + 1 < sizeof(lineBuf)) lineBuf[li++] = ch;
                else li = sizeof(lineBuf) - 1;  // just track non-empty
            }
            continue;
        }
        // body sample
        if (bi + 1 < sizeof(bodySample) && ch >= 0x20 && ch < 0x7F) {
            bodySample[bi++] = ch;
            bodySample[bi] = '\0';
        } else if (bi + 1 < sizeof(bodySample) && (ch == '\n' || ch == '\r')) {
            // keep reading printable only
        }
        if (bi >= sizeof(bodySample) - 1) break;
    }
    if (useTls) tls.stop(); else plain.stop();

    Serial.printf("[PWNCRACK] Upload status: %s\n", statusLine);
    if (bodySample[0]) {
        Serial.printf("[PWNCRACK] Upload body: %s\n", bodySample);
    }

    bool httpOk = strstr(statusLine, "200") || strstr(statusLine, "201") ||
                  strstr(statusLine, "409");
    if (!httpOk) {
        // compact error for UI
        if (strstr(statusLine, "401") || strstr(statusLine, "403")) {
            snprintf(lastError, sizeof(lastError), "BAD KEY / AUTH");
        } else if (strstr(statusLine, "400")) {
            snprintf(lastError, sizeof(lastError), "BAD FILE/400");
        } else if (statusLine[0] == '\0') {
            snprintf(lastError, sizeof(lastError), "NO HTTP REPLY");
        } else {
            // show code if present
            const char* p = strstr(statusLine, " ");
            if (p && p[1]) {
                snprintf(lastError, sizeof(lastError), "HTTP %.12s", p + 1);
            } else {
                snprintf(lastError, sizeof(lastError), "HTTP FAIL");
            }
        }
        return false;
    }

    // Server may return 200 with error JSON — check common failure words
    // (plugin only checks status_code; we are a bit stricter)
    if (bodySample[0]) {
        // lowercase check via simple scan
        char low[96];
        size_t L = strlen(bodySample);
        if (L >= sizeof(low)) L = sizeof(low) - 1;
        for (size_t i = 0; i < L; i++) {
            char c = bodySample[i];
            low[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
        }
        low[L] = '\0';
        if (strstr(low, "invalid key") || strstr(low, "bad key") ||
            strstr(low, "\"error\"") || strstr(low, "unauthorized")) {
            snprintf(lastError, sizeof(lastError), "SERVER REJECT KEY");
            return false;
        }
        if (strstr(low, "invalid") && strstr(low, "file")) {
            snprintf(lastError, sizeof(lastError), "SERVER REJECT FILE");
            return false;
        }
    }

    lastError[0] = '\0';
    return true;
}

bool Pwncrack::downloadPotfile(uint16_t& newCracks) {
    newCracks = 0;
    const char* key = Config::wifi().pwncrackKey;

    WiFiClientSecure tls;
    WiFiClient plain;
    bool useTls = false;
    if (!connectPwn(tls, plain, useTls)) {
        snprintf(lastError, sizeof(lastError), "POT CONNECT");
        return false;
    }

    char req[192];
    snprintf(req, sizeof(req),
             "GET %s?key=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
             PWN_POTFILE_PATH, key, PWN_HOST);
    if (useTls) tls.print(req); else plain.print(req);

    const char* dir = SDLayout::pwncrackDir();
    if (!SD.exists(dir)) SD.mkdir(dir);
    const char* path = SDLayout::pwncrackResultsPath();

    bool headersDone = false;
    bool statusOk = false;
    char line[200];
    size_t li = 0;
    uint32_t t0 = millis();
    size_t bodyBytes = 0;
    bool firstHeader = true;

    // Buffer body to temp then commit only on HTTP 200
    File out = SD.open(path, FILE_WRITE);
    if (!out) {
        if (useTls) tls.stop(); else plain.stop();
        snprintf(lastError, sizeof(lastError), "POT SAVE");
        return false;
    }

    while (millis() - t0 < 25000) {
        int ch = -1;
        if (useTls) {
            if (tls.available()) ch = tls.read();
            else if (!tls.connected() && headersDone) break;
        } else {
            if (plain.available()) ch = plain.read();
            else if (!plain.connected() && headersDone) break;
        }
        if (ch < 0) {
            delay(5);
            yield();
            continue;
        }
        if (!headersDone) {
            if (ch == '\n') {
                line[li] = '\0';
                if (firstHeader) {
                    firstHeader = false;
                    statusOk = (strstr(line, "200") != nullptr);
                    Serial.printf("[PWNCRACK] Potfile status: %s\n", line);
                }
                if (li == 0 || (li == 1 && line[0] == '\r')) headersDone = true;
                li = 0;
            } else if (ch != '\r' && li + 1 < sizeof(line)) {
                line[li++] = (char)ch;
            }
        } else {
            if (statusOk) {
                out.write((uint8_t)ch);
                bodyBytes++;
                if (bodyBytes > 80000) break;
            }
        }
    }
    out.close();
    if (useTls) tls.stop(); else plain.stop();

    if (!headersDone) {
        snprintf(lastError, sizeof(lastError), "POT HEADERS");
        return false;
    }
    if (!statusOk) {
        snprintf(lastError, sizeof(lastError), "POT HTTP FAIL");
        return false;
    }

    uint16_t before = cacheLoaded ? (uint16_t)crackedCache.size() : 0;
    cacheLoaded = false;
    loadCache();
    uint16_t after = (uint16_t)crackedCache.size();
    newCracks = (after > before) ? (after - before) : 0;
    Serial.printf("[PWNCRACK] Potfile saved %u B, cracked=%u (+%u)\n",
                  (unsigned)bodyBytes, after, newCracks);
    return true;
}

PwncrackSyncResult Pwncrack::syncCaptures(PwncrackProgressCallback cb) {
    PwncrackSyncResult result = {};
    result.success = false;
    result.error[0] = '\0';
    busy = true;

    struct SceneGuard {
        SceneGuard() { Avatar::suspendScene(); }
        ~SceneGuard() { Avatar::resumeScene(); }
    } sceneGuard;

    if (!hasApiKey()) {
        strncpy(result.error, "NO PWNCRACK KEY", sizeof(result.error) - 1);
        busy = false;
        return result;
    }
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(result.error, "WIFI NOT CONNECTED", sizeof(result.error) - 1);
        busy = false;
        return result;
    }

    // Soft heap check only (HTTP path — no mbedTLS). Optional light cleanup.
    if (!canSync()) {
        if (cb) cb("FREEING HEAP...", 0, 0);
        freeCacheMemory();
        // Light condition without full TLS dance; HTTP needs far less contig
        if (!canSync()) {
            Mood::setStatusMessage("HEAP TIGHT");
            snprintf(result.error, sizeof(result.error), "%s", lastError[0] ? lastError : "LOW HEAP");
            busy = false;
            return result;
        }
    }

    loadCache();

    const char* hsDir = SDLayout::handshakesDir();
    if (!SD.exists(hsDir)) {
        strncpy(result.error, "NO HANDSHAKES DIR", sizeof(result.error) - 1);
        busy = false;
        return result;
    }

    // Collect .22000 / .hc22000 (same scan logic as menu / WPA-SEC style)
    struct Pending {
        char path[96];
        char id[48];
    };
    static Pending pending[24];
    uint8_t pendingCount = 0;

    auto ends22000 = [](const char* base) -> bool {
        size_t n = strlen(base);
        if (n > 6) {
            const char* t = base + n - 6;
            if (strcasecmp(t, ".22000") == 0) return true;
        }
        if (n > 8) {
            const char* t = base + n - 8;
            if (strcasecmp(t, ".hc22000") == 0) return true;
        }
        return false;
    };

    File dir = SD.open(hsDir);
    if (dir && dir.isDirectory()) {
        File file = dir.openNextFile();
        uint8_t scanned = 0;
        while (file && pendingCount < 24) {
            if (++scanned >= 10) { scanned = 0; yield(); }
            if (!file.isDirectory()) {
                const char* name = file.name();
                const char* base = strrchr(name, '/');
                base = base ? base + 1 : name;
                if (ends22000(base)) {
                    pending[pendingCount].id[0] = '\0';
                    pending[pendingCount].path[0] = '\0';
                    strncpy(pending[pendingCount].id, base, sizeof(pending[0].id) - 1);
                    if (name[0] == '/') {
                        strncpy(pending[pendingCount].path, name, sizeof(pending[0].path) - 1);
                    } else {
                        snprintf(pending[pendingCount].path, sizeof(pending[0].path),
                                 "%s/%s", hsDir, base);
                    }
                    pendingCount++;
                }
            }
            file.close();
            file = dir.openNextFile();
        }
        dir.close();
    }

    Serial.printf("[PWNCRACK] Found %u hash files in %s\n",
                  (unsigned)pendingCount, hsDir);

    if (pendingCount == 0) {
        strncpy(result.error, "NO .22000 FILES", sizeof(result.error) - 1);
        // Still try potfile download (maybe already uploaded elsewhere)
    }

    if (cb) cb("UPLOADING", 0, pendingCount);

    for (uint8_t i = 0; i < pendingCount; i++) {
        if (findUploaded(pending[i].id)) {
            result.skipped++;
            if (cb) cb("SKIP", i + 1, pendingCount);
            continue;
        }
        if (cb) {
            char st[24];
            snprintf(st, sizeof(st), "UP %u/%u", i + 1, pendingCount);
            cb(st, i + 1, pendingCount);
        }
        if (uploadFile(pending[i].path)) {
            markAsUploaded(pending[i].id);
            result.uploaded++;
        } else {
            result.failed++;
        }
        delay(50);
        yield();
    }

    if (cb) cb("POTFILE", pendingCount, pendingCount);
    uint16_t newCracks = 0;
    if (downloadPotfile(newCracks)) {
        result.newCracked = newCracks;
        result.cracked = getCrackedCount();
        result.success = true;
    } else {
        // uploads may still have worked
        result.success = (result.uploaded > 0 || result.skipped > 0);
        if (!result.success && result.error[0] == '\0') {
            strncpy(result.error, lastError[0] ? lastError : "POT FAIL", sizeof(result.error) - 1);
        }
    }

    if (result.success && result.error[0] == '\0') {
        // leave error empty on success
    }
    busy = false;
    return result;
}

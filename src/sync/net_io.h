#pragma once
#include "../storage/littlefs_ops.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

// Streamed from LittleFS — not held in RAM. Cap is bigger than the FS.
static constexpr size_t kHsUploadMax = 8u * 1024u * 1024u;

struct IoXfer {
    char phase[20];
    uint16_t file;
    uint16_t files;
    uint16_t ok;
    uint16_t fail;
    uint32_t sent;
    uint32_t size;
};

inline IoXfer& ioXfer() {
    static IoXfer s{};
    return s;
}

using IoPump = void (*)();
inline IoPump& ioPumpFn() {
    static IoPump p = nullptr;
    return p;
}
inline void ioPump() {
    if (ioPumpFn()) ioPumpFn()();
}

inline void ioXferClear() {
    IoXfer& x = ioXfer();
    memset(&x, 0, sizeof(x));
}

inline void ioXferPhase(const char* phase, uint16_t file, uint16_t files) {
    IoXfer& x = ioXfer();
    if (phase) {
        strncpy(x.phase, phase, sizeof(x.phase) - 1);
        x.phase[sizeof(x.phase) - 1] = '\0';
    }
    x.file = file;
    x.files = files;
    x.sent = 0;
    x.size = 0;
}

inline bool ioWriteAll(WiFiClient& c, const uint8_t* p, size_t n) {
    size_t off = 0;
    uint8_t spins = 0;
    while (off < n) {
        size_t w = c.write(p + off, n - off);
        if (w == 0) {
            if (!c.connected() || ++spins > 80) return false;
            delay(10);
            yield();
            continue;
        }
        spins = 0;
        off += w;
        yield();
        ioPump();
    }
    return true;
}

inline bool ioWriteAll(WiFiClient& c, const char* s) {
    if (!s) return true;
    return ioWriteAll(c, reinterpret_cast<const uint8_t*>(s), strlen(s));
}

inline bool ioTlsOpen(WiFiClientSecure& c, const char* host, uint16_t port = 443) {
    if (!host) return false;
    c.setInsecure();
    c.setTimeout(20000);
    if (c.connect(host, port, 12000)) return true;
    c.stop();
    delay(250);
    yield();
    return c.connect(host, port, 10000);
}

inline bool ioPwnOpen(WiFiClientSecure& tls, WiFiClient& plain, bool& useTls,
                      const char* host, bool forceTls = false) {
    useTls = false;
    if (!forceTls) {
        plain.setTimeout(12000);
        if (plain.connect(host, 80, 8000)) return true;
        plain.stop();
    }
    useTls = true;
    tls.setInsecure();
    tls.setTimeout(20000);
    for (uint8_t i = 0; i < 2; i++) {
        if (tls.connect(host, 443, 10000)) return true;
        tls.stop();
        delay(200);
        yield();
    }
    return false;
}

inline bool ioHttpOk(const char* status) {
    if (!status || !status[0]) return false;
    return strstr(status, "200") || strstr(status, "201") || strstr(status, "409");
}

inline bool ioHttpRedirect(const char* status) {
    if (!status) return false;
    return strstr(status, "301") || strstr(status, "302") ||
           strstr(status, "307") || strstr(status, "308");
}

inline void ioDrain(WiFiClient& c, uint32_t timeoutMs = 25000) {
    unsigned long t0 = millis();
    uint8_t junk[64];
    while ((uint32_t)(millis() - t0) < timeoutMs) {
        int n = c.available();
        if (n > 0) {
            if (n > (int)sizeof(junk)) n = (int)sizeof(junk);
            c.read(junk, (size_t)n);
            t0 = millis();
            yield();
            continue;
        }
        if (!c.connected()) break;
        delay(8);
        yield();
    }
}

inline bool ioReadStatusLine(WiFiClient& c, char* status, size_t statusLen,
                             uint32_t timeoutMs = 45000) {
    if (status && statusLen) status[0] = '\0';
    unsigned long t0 = millis();
    size_t si = 0;
    while ((uint32_t)(millis() - t0) < timeoutMs) {
        int avail = c.available();
        if (avail <= 0) {
            if (!c.connected()) break;
            delay(10);
            yield();
            continue;
        }
        int ch = c.read();
        if (ch < 0) continue;
        if (ch == '\n') {
            if (status && statusLen) status[si] = '\0';
            return status && status[0];
        }
        if (ch != '\r' && status && si + 1 < statusLen) status[si++] = (char)ch;
    }
    if (status && statusLen) status[si] = '\0';
    return status && status[0];
}

inline bool ioStreamFile(WiFiClient& c, File& f, size_t fileSize, char* err, size_t errLen) {
    IoXfer& x = ioXfer();
    x.size = (uint32_t)fileSize;
    x.sent = 0;
    uint8_t buf[512];
    size_t sent = 0;
    while (sent < fileSize) {
        size_t want = fileSize - sent;
        if (want > sizeof(buf)) want = sizeof(buf);
        size_t rd = f.read(buf, want);
        if (rd == 0) {
            if (err && errLen) snprintf(err, errLen, "read fail");
            f.close();
            return false;
        }
        if (!ioWriteAll(c, buf, rd)) {
            if (err && errLen) snprintf(err, errLen, "send body");
            f.close();
            c.stop();
            return false;
        }
        sent += rd;
        x.sent = (uint32_t)sent;
        yield();
        ioPump();
    }
    f.close();
    return true;
}

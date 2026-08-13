// cap/hc22000.cpp
#include "hc22000.h"
#include "../storage/littlefs_ops.h"
#include <LittleFS.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace Hc22000 {

static const uint8_t MAX_HS = 12;
static const uint16_t MAX_EAPOL = 192;

struct Hs {
    uint8_t bssid[6];
    uint8_t sta[6];
    uint8_t essid[32];
    uint8_t essidLen;
    uint8_t anonce[32];
    uint8_t pmkid[16];
    uint8_t m2[MAX_EAPOL];
    uint16_t m2Len;
    bool used;
    bool haveEssid;
    bool haveAnonce;
    bool havePmkid;
    bool haveM2;
    bool wrotePmkid;
    bool wroteEapol;
};

static Hs s_hs[MAX_HS];

static void hexEnc(const uint8_t* in, size_t n, char* out) {
    static const char* H = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = H[in[i] >> 4];
        out[i * 2 + 1] = H[in[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

static uint32_t s_lastM1Ms = 0;

static void makePath(const uint8_t* bssid, const char* suffix, char* path, size_t pathLen) {
    snprintf(path, pathLen,
             "%s/%02X-%02X-%02X-%02X-%02X-%02X%s",
             Storage::DIR_HANDSHAKES,
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
             suffix);
}

static Hs* slotFor(const uint8_t* bssid) {
    for (uint8_t i = 0; i < MAX_HS; i++) {
        if (s_hs[i].used && memcmp(s_hs[i].bssid, bssid, 6) == 0) return &s_hs[i];
    }
    for (uint8_t i = 0; i < MAX_HS; i++) {
        if (!s_hs[i].used) {
            memset(&s_hs[i], 0, sizeof(Hs));
            memcpy(s_hs[i].bssid, bssid, 6);
            s_hs[i].used = true;
            return &s_hs[i];
        }
    }
    memset(&s_hs[0], 0, sizeof(Hs));
    memcpy(s_hs[0].bssid, bssid, 6);
    s_hs[0].used = true;
    return &s_hs[0];
}

static bool writeLine(const uint8_t* bssid, const char* suffix, const char* line) {
    Storage::ensureDir(Storage::DIR_HANDSHAKES);
    char path[56];
    makePath(bssid, suffix, path, sizeof(path));
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    f.println(line);
    f.close();
    Serial.printf("[22000] wrote %s\n", Storage::baseName(path));
    return true;
}

static void maybeWrite(Hs* h) {
    if (!h || !h->haveEssid || h->essidLen == 0) return;
    char ap[13], sta[13], ess[65];
    hexEnc(h->bssid, 6, ap);
    hexEnc(h->sta, 6, sta);
    hexEnc(h->essid, h->essidLen, ess);

    // hashcat 22000 PMKID: WPA*01*PMKID*AP*STA*ESSID***01
    if (h->havePmkid && !h->wrotePmkid) {
        bool z = true;
        for (int i = 0; i < 16; i++) if (h->pmkid[i]) { z = false; break; }
        if (!z) {
            char pmk[33];
            hexEnc(h->pmkid, 16, pmk);
            char line[160];
            snprintf(line, sizeof(line), "WPA*01*%s*%s*%s*%s***01", pmk, ap, sta, ess);
            if (writeLine(h->bssid, ".22000", line)) h->wrotePmkid = true;
        }
    }

    // hashcat 22000 EAPOL: WPA*02*MIC*AP*STA*ESSID*ANONCE*EAPOL*00 (M1+M2)
    if (h->haveAnonce && h->haveM2 && !h->wroteEapol && h->m2Len >= 97) {
        uint16_t eapolLen = (uint16_t)((h->m2[2] << 8) | h->m2[3]);
        eapolLen = (uint16_t)(eapolLen + 4);
        if (eapolLen > h->m2Len) eapolLen = h->m2Len;
        if (eapolLen < 97 || eapolLen > MAX_EAPOL) return;
        uint8_t eapol[MAX_EAPOL];
        memcpy(eapol, h->m2, eapolLen);
        memset(eapol + 81, 0, 16);
        char mic[33], an[65];
        hexEnc(h->m2 + 81, 16, mic);
        hexEnc(h->anonce, 32, an);
        char ehex[MAX_EAPOL * 2 + 1];
        hexEnc(eapol, eapolLen, ehex);
        char line[768];
        snprintf(line, sizeof(line), "WPA*02*%s*%s*%s*%s*%s*%s*00",
                 mic, ap, sta, ess, an, ehex);
        if (writeLine(h->bssid, "_hs.22000", line)) h->wroteEapol = true;
    }
}

static uint16_t hdrLen80211(const uint8_t* f, uint16_t len) {
    if (len < 24) return 0;
    uint16_t off = 24;
    uint8_t type = (f[0] >> 2) & 0x03;
    if (type == 2 && (f[0] & 0x80)) off += 2; // QoS data
    if (f[1] & 0x80) off += 4;                 // HT ctrl / order
    if ((f[1] & 0x03) == 0x03) off += 6;       // 4-address
    return off;
}

static void parseBeacon(const uint8_t* f, uint16_t len) {
    if ((f[0] & 0xFC) != 0x80) return;
    const uint8_t* bssid = f + 16;
    uint16_t off = 24 + 12;
    if (off + 2 > len) return;
    while (off + 2 <= len) {
        uint8_t id = f[off];
        uint8_t l = f[off + 1];
        if (off + 2 + l > len) break;
        if (id == 0 && l > 0 && l <= 32) {
            Hs* h = slotFor(bssid);
            memcpy(h->essid, f + off + 2, l);
            h->essidLen = l;
            h->haveEssid = true;
            maybeWrite(h);
            return;
        }
        off = (uint16_t)(off + 2 + l);
    }
}

static bool findPmkidKde(const uint8_t* payload, uint16_t len, uint8_t out[16]) {
    // EAPOL key descriptor 0x02, key data at 99, PMKID KDE dd 14 00 0f ac 04
    if (len < 121 || payload[4] != 0x02) return false;
    uint16_t keyDataLen = (uint16_t)((payload[97] << 8) | payload[98]);
    if (keyDataLen < 22 || len < 99 + keyDataLen) return false;
    const uint8_t* keyData = payload + 99;
    for (uint16_t i = 0; i + 22 <= keyDataLen; i++) {
        if (keyData[i] == 0xdd && keyData[i + 1] == 0x14 &&
            keyData[i + 2] == 0x00 && keyData[i + 3] == 0x0f &&
            keyData[i + 4] == 0xac && keyData[i + 5] == 0x04) {
            const uint8_t* p = keyData + i + 6;
            bool z = true;
            for (int k = 0; k < 16; k++) if (p[k]) { z = false; break; }
            if (z) return false;
            memcpy(out, p, 16);
            return true;
        }
    }
    return false;
}

static void parseEapol(const uint8_t* f, uint16_t len) {
    uint16_t off = hdrLen80211(f, len);
    if (off + 8 + 4 > len) return;
    if (f[off] != 0xAA || f[off + 1] != 0xAA || f[off + 2] != 0x03) return;
    if (f[off + 6] != 0x88 || f[off + 7] != 0x8E) return;
    const uint8_t* e = f + off + 8;
    uint16_t elen = (uint16_t)(len - off - 8);
    if (elen < 99 || e[1] != 0x03) return;

    uint16_t body = (uint16_t)((e[2] << 8) | e[3]);
    uint16_t total = (uint16_t)(4 + body);
    if (total > elen) total = elen;
    if (total > MAX_EAPOL) total = MAX_EAPOL;

    // keyInfo at EAPOL payload[5..6]
    uint16_t ki = (uint16_t)((e[5] << 8) | e[6]);
    uint8_t install = (uint8_t)((ki >> 6) & 1);
    uint8_t keyAck = (uint8_t)((ki >> 7) & 1);
    uint8_t keyMic = (uint8_t)((ki >> 8) & 1);
    uint8_t secure = (uint8_t)((ki >> 9) & 1);

    uint8_t msg = 0;
    if (keyAck && !keyMic) msg = 1;
    else if (!keyAck && keyMic && !secure) msg = 2;
    else if (keyAck && keyMic && install) msg = 3;
    else if (!keyAck && keyMic && secure) msg = 4;
    if (msg == 0) return;

    const uint8_t* srcMac = f + 10;
    const uint8_t* dstMac = f + 4;
    uint8_t bssid[6], sta[6];
    if (msg == 1 || msg == 3) {
        memcpy(bssid, srcMac, 6);
        memcpy(sta, dstMac, 6);
    } else {
        memcpy(bssid, dstMac, 6);
        memcpy(sta, srcMac, 6);
    }

    Hs* h = slotFor(bssid);
    memcpy(h->sta, sta, 6);

    if (msg == 1) {
        memcpy(h->anonce, e + 17, 32);
        h->haveAnonce = true;
        s_lastM1Ms = millis();
        uint8_t pmk[16];
        if (findPmkidKde(e, total, pmk)) {
            memcpy(h->pmkid, pmk, 16);
            h->havePmkid = true;
        }
    } else if (msg == 3 && !h->haveAnonce) {
        memcpy(h->anonce, e + 17, 32);
        h->haveAnonce = true;
    } else if (msg == 2 && !h->haveM2) {
        memcpy(h->m2, e, total);
        h->m2Len = total;
        h->haveM2 = true;
    }
    maybeWrite(h);
}

void reset() {
    memset(s_hs, 0, sizeof(s_hs));
    s_lastM1Ms = 0;
}

bool shouldPauseDeauth() {
    return s_lastM1Ms != 0 && (millis() - s_lastM1Ms) < 1200;
}

void feed(const uint8_t* frame, uint16_t len) {
    if (!frame || len < 24) return;
    uint8_t type = (frame[0] >> 2) & 0x03;
    if (type == 0) parseBeacon(frame, len);
    else if (type == 2) parseEapol(frame, len);
}

uint16_t convertPcap(const char* pcapPath) {
    if (!pcapPath) return 0;
    File f = LittleFS.open(pcapPath, "r");
    if (!f) return 0;
    uint8_t fh[24];
    if (f.read(fh, 24) != 24) {
        f.close();
        return 0;
    }
    uint16_t n = 0;
    uint16_t before = 0;
    for (uint8_t i = 0; i < MAX_HS; i++) {
        if (s_hs[i].wroteEapol || s_hs[i].wrotePmkid) before++;
    }
    while (f.available()) {
        uint8_t ph[16];
        if (f.read(ph, 16) != 16) break;
        uint32_t incl = (uint32_t)ph[8] | ((uint32_t)ph[9] << 8) |
                        ((uint32_t)ph[10] << 16) | ((uint32_t)ph[11] << 24);
        if (incl < 8 || incl > 2048) break;
        uint8_t skip[8];
        if (f.read(skip, 8) != 8) break;
        uint32_t flen = incl - 8;
        if (flen > 400) {
            uint8_t dump[64];
            while (flen) {
                size_t c = flen > sizeof(dump) ? sizeof(dump) : flen;
                if (f.read(dump, c) != c) break;
                flen -= c;
            }
            continue;
        }
        uint8_t frame[400];
        if (f.read(frame, flen) != (int)flen) break;
        feed(frame, (uint16_t)flen);
        yield();
    }
    f.close();
    for (uint8_t i = 0; i < MAX_HS; i++) {
        if (s_hs[i].wroteEapol || s_hs[i].wrotePmkid) n++;
    }
    return (n > before) ? (uint16_t)(n - before) : 0;
}

struct ConvCtx {
    uint16_t n;
};

static void convOne(const char* name, size_t, void* raw) {
    size_t L = strlen(name);
    if (L < 6 || strcasecmp(name + L - 5, ".pcap") != 0) return;
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", Storage::DIR_HANDSHAKES, name);
    uint16_t add = convertPcap(path);
    ((ConvCtx*)raw)->n = (uint16_t)(((ConvCtx*)raw)->n + add);
}

uint16_t convertAllPcaps() {
    ConvCtx ctx{0};
    Storage::forEachHandshake(convOne, &ctx);
    return ctx.n;
}

} // namespace Hc22000

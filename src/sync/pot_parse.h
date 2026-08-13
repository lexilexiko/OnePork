// pot_parse.h - one potfile line -> BSSID / SSID / password
#pragma once

#include <string.h>
#include <ctype.h>

namespace Pot {

static inline bool hexDigit(char c) {
    return isxdigit((unsigned char)c) != 0;
}

static inline char hexVal(char c) {
    if (c >= '0' && c <= '9') return (char)(c - '0');
    if (c >= 'a' && c <= 'f') return (char)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (char)(c - 'A' + 10);
    return 0;
}

static inline void macPretty(const char* hex12, char out[18]) {
    for (int i = 0; i < 6; i++) {
        out[i * 3] = hex12[i * 2];
        out[i * 3 + 1] = hex12[i * 2 + 1];
        out[i * 3 + 2] = (i == 5) ? '\0' : ':';
    }
}

static inline bool macToHex12(const char* in, char out[13]) {
    size_t n = 0;
    if (!in) return false;
    for (const char* p = in; *p && n < 12; p++) {
        if (*p == ':' || *p == '-') continue;
        if (!hexDigit(*p)) return false;
        out[n++] = (char)toupper((unsigned char)*p);
    }
    out[n] = '\0';
    return n == 12;
}

static inline bool decodeHexStr(const char* hex, char* out, size_t outLen) {
    size_t hl = strlen(hex);
    if (hl < 2 || (hl & 1) || outLen < 2) return false;
    size_t n = hl / 2;
    if (n >= outLen) n = outLen - 1;
    for (size_t i = 0; i < n; i++) {
        if (!hexDigit(hex[i * 2]) || !hexDigit(hex[i * 2 + 1])) return false;
        out[i] = (char)((hexVal(hex[i * 2]) << 4) | hexVal(hex[i * 2 + 1]));
    }
    out[n] = '\0';
    return n > 0;
}

// Fills pretty BSSID (AA:BB:...), SSID, password. Returns false if unusable.
static inline bool parseLine(const char* line, char bssid[18], char ssid[33], char pass[64]) {
    if (!line || !bssid || !ssid || !pass) return false;
    bssid[0] = ssid[0] = pass[0] = '\0';
    if (line[0] == '#' || line[0] == '\0') return false;

    size_t len = strlen(line);
    if (len < 5) return false;

    // hashcat 22000 pot: WPA*01*...:password  or  WPA*02*...:password
    if (strncmp(line, "WPA*", 4) == 0) {
        const char* lastCol = strrchr(line, ':');
        if (!lastCol || lastCol[1] == '\0') return false;
        strncpy(pass, lastCol + 1, 63);
        pass[63] = '\0';
        // WPA*tt*mic*macap*macsta*essidhex*...
        char tmp[320];
        size_t hl = (size_t)(lastCol - line);
        if (hl >= sizeof(tmp)) hl = sizeof(tmp) - 1;
        memcpy(tmp, line, hl);
        tmp[hl] = '\0';
        char* parts[10];
        int pc = 0;
        char* p = tmp;
        while (pc < 10) {
            parts[pc++] = p;
            char* star = strchr(p, '*');
            if (!star) break;
            *star = '\0';
            p = star + 1;
        }
        if (pc >= 6 && macToHex12(parts[3], tmp)) {
            macPretty(tmp, bssid);
        }
        if (pc >= 6) decodeHexStr(parts[5], ssid, 33);
        return pass[0] != '\0';
    }

    // Classic wpa-sec: AABBCCDDEEFF:STA:SSID:pass  OR  AABBCCDDEEFF:SSID:pass
    char hex[13];
    if (!macToHex12(line, hex)) {
        // maybe first field is colon-separated MAC AA:BB:CC:DD:EE:FF:...
        const char* c = strchr(line, ':');
        if (!c) return false;
        // try 17-char MAC
        char macf[18];
        size_t ml = 0;
        const char* p = line;
        while (*p && ml < 17 && (hexDigit(*p) || *p == ':' || *p == '-')) {
            macf[ml++] = *p++;
        }
        macf[ml] = '\0';
        if (!macToHex12(macf, hex)) return false;
    }
    macPretty(hex, bssid);

    // Find first separator after the MAC field
    const char* rest = line;
    int macChars = 0;
    while (*rest && macChars < 12) {
        if (*rest != ':' && *rest != '-') macChars++;
        rest++;
    }
    if (*rest == ':') rest++;
    if (!*rest) return false;

    const char* last = strrchr(rest, ':');
    if (!last || last == rest) {
        strncpy(pass, rest, 63);
        pass[63] = '\0';
        return pass[0] != '\0';
    }

    // If next field is another 12-hex STA MAC, SSID starts after it
    char sta[13];
    const char* afterSta = rest;
    if (macToHex12(rest, sta)) {
        afterSta = rest;
        int n = 0;
        while (*afterSta && n < 12) {
            if (*afterSta != ':' && *afterSta != '-') n++;
            afterSta++;
        }
        if (*afterSta == ':') afterSta++;
        last = strrchr(afterSta, ':');
        if (!last) {
            strncpy(ssid, afterSta, 32);
            ssid[32] = '\0';
            return true;
        }
        rest = afterSta;
    }

    size_t sl = (size_t)(last - rest);
    if (sl >= 32) sl = 32;
    memcpy(ssid, rest, sl);
    ssid[sl] = '\0';
    strncpy(pass, last + 1, 63);
    pass[63] = '\0';
    return pass[0] != '\0';
}

} // namespace Pot

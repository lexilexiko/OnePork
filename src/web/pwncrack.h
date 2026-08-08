// Pwncrack.org distributed cracking client
// https://pwncrack.org/  — separate from WPA-SEC
#pragma once

#include <Arduino.h>
#include <vector>
#include "../core/heap_policy.h"

struct PwncrackSyncResult {
    bool success;
    uint8_t uploaded;
    uint8_t failed;
    uint8_t skipped;
    uint16_t cracked;
    uint16_t newCracked;
    char error[48];
};

// Step-by-step reachability / API probe (for T-key diagnostic)
struct PwncrackDiagResult {
    bool wifiOk;
    bool dnsOk;
    bool tcp80Ok;
    bool httpOk;
    bool potOk;
    bool keyOk;
    int httpCode;          // status from probe GET/POST path
    int potCode;
    uint16_t potBytes;
    uint32_t tcp80Ms;
    uint32_t httpMs;
    char ip[16];
    char detail[48];       // last failure reason / summary line
    char lines[6][28];     // short UI lines
    uint8_t lineCount;
};

typedef void (*PwncrackProgressCallback)(const char* status, uint8_t progress, uint8_t total);

class Pwncrack {
public:
    static bool isBusy();
    static bool hasApiKey();
    static bool canSync();
    static PwncrackSyncResult syncCaptures(PwncrackProgressCallback cb = nullptr);

    // Network + API self-test (no handshake file required)
    static PwncrackDiagResult runDiagnostics(PwncrackProgressCallback cb = nullptr);

    static bool loadCache();
    static void freeCacheMemory();
    static uint16_t getCrackedCount();
    static bool isCracked(const char* bssidOrSsid);
    static const char* getPassword(const char* bssidOrSsid);
    static bool isUploaded(const char* id);
    static void markAsUploaded(const char* id);

    static const char* getLastError();

private:
    static bool cacheLoaded;
    static char lastError[64];
    static volatile bool busy;

    struct CrackedEntry {
        char id[33];       // BSSID or SSID key
        char password[64];
        char label[33];    // display SSID/AP
    };
    struct UploadedEntry {
        char id[48];       // filename stem or bssid
    };
    static std::vector<CrackedEntry> crackedCache;
    static std::vector<UploadedEntry> uploadedCache;

    static bool loadUploadedList();
    static bool saveUploadedList();
    static bool uploadFile(const char* filepath);
    static bool downloadPotfile(uint16_t& newCracks);
    static bool findUploaded(const char* id);
};

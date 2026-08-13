// sync/pwncrack.h
// Pwncrack.org client: upload captures, download potfile.
// https://pwncrack.org/

#pragma once

#include <Arduino.h>
#include <vector>

struct PwncrackSyncResult {
    bool success;
    uint8_t uploaded;
    uint8_t failed;
    uint8_t skipped;
    uint16_t cracked;
    uint16_t newCracked;
    char error[48];
};

typedef void (*PwncrackProgressCallback)(const char* status, uint8_t progress, uint8_t total);

class Pwncrack {
public:
    static bool hasApiKey();
    static bool hasApiKey(const char* key);
    static bool canSync();
    static PwncrackSyncResult syncCaptures(const char* apiKey, PwncrackProgressCallback cb = nullptr);

    static bool loadCache();
    static void freeCacheMemory();
    static bool isCracked(const char* bssidOrSsid);
    static const char* getPassword(const char* bssidOrSsid);
    static uint16_t getCrackedCount();
    static bool isUploaded(const char* filename);
    static void markAsUploaded(const char* filename);

    static const char* getLastError();
    static bool isBusy();

private:
    static bool cacheLoaded;
    static char lastError[64];
    static volatile bool busy;

    struct CrackedEntry {
        char id[33];
        char password[64];
        char label[33];
    };
    struct UploadedEntry {
        char id[48];
    };
    static std::vector<CrackedEntry> crackedCache;
    static std::vector<UploadedEntry> uploadedCache;

    static bool findUploaded(const char* id);
    static bool loadUploadedList();
    static bool saveUploadedList();
    static bool uploadFile(const char* filepath, const char* apiKey);
    static bool downloadPotfile(const char* apiKey, uint16_t& newCracks);
};

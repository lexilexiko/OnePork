// sync/wpasec.h
// WPA-SEC client: upload .pcap, download potfile.
// https://wpa-sec.stanev.org/

#pragma once

#include <Arduino.h>
#include <vector>

struct WPASecSyncResult {
    bool success;
    uint8_t uploaded;
    uint8_t failed;
    uint8_t skipped;
    uint16_t cracked;
    uint16_t newCracked;
    char error[48];
};

typedef void (*WPASecProgressCallback)(const char* status, uint8_t progress, uint8_t total);

class WPASec {
public:
    static bool hasApiKey();
    static bool hasApiKey(const char* key);
    static bool canSync();
    static WPASecSyncResult syncCaptures(const char* apiKey, WPASecProgressCallback cb = nullptr);
    static bool pullPotfile(const char* apiKey, uint16_t& lines);

    static bool loadCache();
    static void freeCacheMemory();
    static bool isCracked(const char* bssid);
    static const char* getPassword(const char* bssid);
    static const char* getSSID(const char* bssid);
    static uint16_t getCrackedCount();
    static bool isUploaded(const char* bssid);
    static void markAsUploaded(const char* bssid);

    static const char* getLastError();
    static bool isBusy();

private:
    static bool cacheLoaded;
    static char lastError[64];
    static volatile bool busy;

    struct CrackedEntry {
        char bssid[13];
        char ssid[33];
        char password[64];
    };
    struct UploadedEntry {
        char bssid[13];
    };
    static std::vector<CrackedEntry> crackedCache;
    static std::vector<UploadedEntry> uploadedCache;

    static void normalizeBSSID(const char* input, char* output, size_t outLen);
    static const CrackedEntry* findCracked(const char* normalizedBssid);
    static bool findUploaded(const char* normalizedBssid);
    static bool loadUploadedList();
    static bool saveUploadedList();
    static bool uploadSingleCapture(const char* filepath, const char* bssid, const char* apiKey);
    static bool downloadPotfile(const char* apiKey, uint16_t& newCracks);
};

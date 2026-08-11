// Pwncrack menu — HASHES/WPA-SEC style list (SSID ST TYPE SIZE)
#pragma once

#include <Arduino.h>
#include <M5Unified.h>
#include <vector>
#include "../web/pwncrack.h"

// Same ST semantics as HASHES / WPA-SEC
enum class PwnCaptureStatus : uint8_t {
    LOCAL,      // Not uploaded yet
    UPLOADED,   // Uploaded, waiting for crack
    CRACKED     // Password found in potfile
};

struct PwnFileInfo {
    char filename[48];
    char path[96];
    char ssid[33];
    char bssid[18];     // AA:BB:CC:DD:EE:FF display
    char bssidHex[13];  // 12 hex chars for lookup
    uint32_t fileSize;
    bool isPMKID;
    PwnCaptureStatus status;
    char password[64];
};

// Lightweight metadata for the scan list (no status / password).
// ~96 bytes vs PwnFileInfo's ~280 bytes — lets us hold 200+ files cheaply.
struct PwnFileMeta {
    char filename[48];
    char ssid[33];
    char bssid[18];
    char bssidHex[13];
    uint32_t fileSize;
    bool isPMKID;
};

enum class PwnSyncState : uint8_t {
    IDLE,
    CONNECTING_WIFI,
    UPLOADING,
    RUNNING_DIAG,
    COMPLETE,
    ERROR,
    DIAG_DONE
};

class PwncrackMenu {
public:
    static void init();
    static void show();
    static void hide();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isActive() { return active; }
    static size_t getFileCount() { return metas.size(); }
    // Bottom bar hints (same pattern as HASHES/WPA-SEC)
    static const char* getBottomHint();

private:
    static bool active;
    static bool keyWasPressed;
    static bool syncModalActive;
    static bool detailViewActive;
    static PwnSyncState syncState;
    static char syncStatusText[48];
    static char syncError[48];
    static uint8_t syncUploaded;
    static uint8_t syncFailed;
    static uint8_t syncSkipped;
    static uint16_t syncCracked;
    static uint16_t syncNewCracked;
    static uint8_t scrollOffset;
    static uint8_t selectedIndex;
    static std::vector<PwnFileMeta> metas;
    static PwnFileInfo selectedInfo;     // Built lazily for the currently selected file
    static bool selectedInfoValid;
    static PwncrackDiagResult lastDiag;
    static uint8_t hintIndex;
    static const char* const HINTS[];
    static const uint8_t HINT_COUNT = 5;
    static const uint8_t VISIBLE_ITEMS = 5;

    static void handleInput();
    static void scanFiles();
    static void refreshSelected();
    static void startSync();
    static void startDiag();
    static void processSyncState();
    static void drawSyncModal(M5Canvas& canvas);
    static void drawDetailView(M5Canvas& canvas);
    static bool connectToWiFi();
    static void disconnectWiFi();
    static void onSyncProgress(const char* status, uint8_t progress, uint8_t total);
    static void ensureKeyLoaded();
    static void parseNameMeta(PwnFileInfo& info);
};

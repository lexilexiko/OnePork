// sync/sync_manager.h
// Async sync task manager. Handles WiFi mode switching + file upload + auto-return to AP.
// User presses "Sync WPA-Sec" -> this spawns a background task that:
// 1. Switches STA mode
// 2. Waits for WiFi connection
// 3. Uploads files
// 4. Switches back to AP mode

#pragma once

#include <Arduino.h>

namespace SyncManager {

enum SyncTarget {
    SYNC_WPASEC,
    SYNC_PWNCRACK
};

enum SyncStatus {
    SYNC_IDLE,
    SYNC_CONNECTING,      // switching to STA, waiting for WiFi
    SYNC_UPLOADING,       // connected, uploading files
    SYNC_DONE_SUCCESS,    // finished, switching back to AP
    SYNC_DONE_FAILURE,    // error occurred
    SYNC_RETURNING_TO_AP  // switching back to AP
};

struct SyncState {
    SyncStatus status;
    char message[128];    // human-readable status message
    int progress;         // 0-100 for progress bar (optional)
};

// Start a sync task. Target = SYNC_WPASEC or SYNC_PWNCRACK.
// apiKey = WPA-SEC API key (32 hex) or Pwncrack key
// Returns immediately. Status can be checked with getStatus().
// Must have already called Net::setSta(ssid, pass) before calling this.
void start(SyncTarget target, const char* apiKey);

// Get current sync status
SyncState getStatus();

// Call from loop() to service the async task
void loop();

// Stop/cancel current sync (for user abort)
void stop();

// Is sync currently running?
bool isRunning();

} // namespace SyncManager

// Pwncrack.org menu — same UX as HASHES (SSID / ST / TYPE / SIZE)

#include "pwncrack_menu.h"
#include "display.h"
#include "../web/pwncrack.h"
#include "../core/config.h"
#include "../core/sd_layout.h"
#include "../core/wifi_utils.h"
#include "../core/network_recon.h"
#include "../core/tls.h"
#include <M5Cardputer.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>
#include <ctype.h>

bool PwncrackMenu::active = false;
bool PwncrackMenu::keyWasPressed = false;
bool PwncrackMenu::syncModalActive = false;
bool PwncrackMenu::detailViewActive = false;
PwnSyncState PwncrackMenu::syncState = PwnSyncState::IDLE;
char PwncrackMenu::syncStatusText[48] = "";
char PwncrackMenu::syncError[48] = "";
uint8_t PwncrackMenu::syncUploaded = 0;
uint8_t PwncrackMenu::syncFailed = 0;
uint8_t PwncrackMenu::syncSkipped = 0;
uint16_t PwncrackMenu::syncCracked = 0;
uint16_t PwncrackMenu::syncNewCracked = 0;
uint8_t PwncrackMenu::scrollOffset = 0;
uint8_t PwncrackMenu::selectedIndex = 0;
std::vector<PwnFileMeta> PwncrackMenu::metas;
PwnFileInfo PwncrackMenu::selectedInfo = {};
bool PwncrackMenu::selectedInfoValid = false;
PwncrackDiagResult PwncrackMenu::lastDiag = {};
uint8_t PwncrackMenu::hintIndex = 0;

// Bottom bar — same style as HASHES (rotate on ; / .)
const char* const PwncrackMenu::HINTS[] = {
    "ENT:DET  S:SYNC  T:TEST",
    "R:KEY  C:CLR UPLOAD LOG",
    "S=SEND  T=NET CHECK",
    "OK=PASS  ..=SENT  --=LOC",
    "PWNCRACK != WPA-SEC"
};

static bool endsWithCI(const char* name, const char* suf) {
    if (!name || !suf) return false;
    size_t n = strlen(name), s = strlen(suf);
    if (n < s) return false;
    for (size_t i = 0; i < s; i++) {
        char a = name[n - s + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

static bool isAllHex(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

static void formatSize(char* out, size_t len, uint32_t bytes) {
    if (!out || len == 0) return;
    if (bytes < 1024) {
        snprintf(out, len, "%uB", (unsigned)bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(out, len, "%uKB", (unsigned)(bytes / 1024));
    } else {
        snprintf(out, len, "%uMB", (unsigned)(bytes / (1024 * 1024)));
    }
}

void PwncrackMenu::init() {
    active = false;
    syncModalActive = false;
    detailViewActive = false;
    syncState = PwnSyncState::IDLE;
    metas.clear();
    selectedInfoValid = false;
}

void PwncrackMenu::ensureKeyLoaded() {
    Config::reloadPwncrackKeyFromNvs();
    if (!Pwncrack::hasApiKey()) {
        Config::loadPwncrackKeyFromFile();  // key.txt or key.txt.imported
    }
}

// Parse SSID / BSSID from handshake filename (same dual-format as HASHES)
void PwncrackMenu::parseNameMeta(PwnFileInfo& info) {
    info.ssid[0] = '\0';
    info.bssid[0] = '\0';
    info.bssidHex[0] = '\0';
    info.isPMKID = false;

    const char* name = info.filename;
    size_t nameLen = strlen(name);
    const char* dot = strrchr(name, '.');
    size_t baseLen = dot ? (size_t)(dot - name) : nameLen;

    // Strip _hs / _pmkid suffix
    if (baseLen > 3 && strncmp(name + baseLen - 3, "_hs", 3) == 0) {
        baseLen -= 3;
    } else if (baseLen > 6 && strncmp(name + baseLen - 6, "_pmkid", 6) == 0) {
        baseLen -= 6;
        info.isPMKID = true;
    }

    if (baseLen == 12 && isAllHex(name, 12)) {
        // Legacy: BSSID only
        char hex[13];
        for (int i = 0; i < 12; i++) {
            char c = name[i];
            hex[i] = (c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
        }
        hex[12] = '\0';
        strncpy(info.bssidHex, hex, sizeof(info.bssidHex) - 1);
        snprintf(info.bssid, sizeof(info.bssid),
                 "%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
                 hex, hex + 2, hex + 4, hex + 6, hex + 8, hex + 10);
        strncpy(info.ssid, "[UNKNOWN]", sizeof(info.ssid) - 1);
    } else if (baseLen > 13 && name[baseLen - 13] == '_' &&
               isAllHex(name + baseLen - 12, 12)) {
        // New: SSID_BSSID
        const char* b = name + baseLen - 12;
        char hex[13];
        for (int i = 0; i < 12; i++) {
            char c = b[i];
            hex[i] = (c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
        }
        hex[12] = '\0';
        strncpy(info.bssidHex, hex, sizeof(info.bssidHex) - 1);
        snprintf(info.bssid, sizeof(info.bssid),
                 "%.2s:%.2s:%.2s:%.2s:%.2s:%.2s",
                 hex, hex + 2, hex + 4, hex + 6, hex + 8, hex + 10);
        size_t ssidLen = baseLen - 13;
        if (ssidLen > sizeof(info.ssid) - 1) ssidLen = sizeof(info.ssid) - 1;
        memcpy(info.ssid, name, ssidLen);
        info.ssid[ssidLen] = '\0';
    } else {
        // Unknown — show stem as SSID
        size_t copyLen = baseLen < sizeof(info.ssid) - 1 ? baseLen : sizeof(info.ssid) - 1;
        memcpy(info.ssid, name, copyLen);
        info.ssid[copyLen] = '\0';
        strncpy(info.bssid, "??", sizeof(info.bssid) - 1);
    }

    if (info.ssid[0] == '\0') {
        strncpy(info.ssid, "[UNKNOWN]", sizeof(info.ssid) - 1);
    }
}

// Forward declaration — see definition below
static PwnFileInfo materialize(const PwnFileMeta& m, const char* hsDir);

// Load status + password for the currently selected file (lazy).
// Called on scroll, on Enter (detail), and after upload sync.
void PwncrackMenu::refreshSelected() {
    if (metas.empty() || selectedIndex >= metas.size()) {
        selectedInfoValid = false;
        return;
    }
    selectedInfo = materialize(metas[selectedIndex], SDLayout::handshakesDir());
    selectedInfoValid = true;
}

// Convert a lightweight meta into a full PwnFileInfo (loads status + password).
// Used lazily for the currently selected file, and for the on-screen window.
static PwnFileInfo materialize(const PwnFileMeta& m, const char* hsDir) {
    PwnFileInfo info{};
    strncpy(info.filename, m.filename, sizeof(info.filename) - 1);
    snprintf(info.path, sizeof(info.path), "%s/%s", hsDir, m.filename);
    strncpy(info.ssid, m.ssid, sizeof(info.ssid) - 1);
    strncpy(info.bssid, m.bssid, sizeof(info.bssid) - 1);
    strncpy(info.bssidHex, m.bssidHex, sizeof(info.bssidHex) - 1);
    info.fileSize = m.fileSize;
    info.isPMKID = m.isPMKID;
    info.status = PwnCaptureStatus::LOCAL;
    info.password[0] = '\0';

    // Password lookup: BSSID hex, SSID, then filename
    const char* pw = "";
    if (m.bssidHex[0]) pw = Pwncrack::getPassword(m.bssidHex);
    if ((!pw || pw[0] == '\0') && m.ssid[0]) pw = Pwncrack::getPassword(m.ssid);
    if ((!pw || pw[0] == '\0') && m.filename[0]) pw = Pwncrack::getPassword(m.filename);
    if (pw && pw[0] != '\0') {
        info.status = PwnCaptureStatus::CRACKED;
        strncpy(info.password, pw, sizeof(info.password) - 1);
        info.password[sizeof(info.password) - 1] = '\0';
    } else if (Pwncrack::isUploaded(m.filename) ||
               (m.bssidHex[0] && Pwncrack::isUploaded(m.bssidHex))) {
        info.status = PwnCaptureStatus::UPLOADED;
    }
    return info;
}

void PwncrackMenu::scanFiles() {
    metas.clear();
    selectedInfoValid = false;

    // Heap gate: PwnFileMeta is ~120B (filename+ssid+bssid+bssidHex+size+isPMKID).
    // 200 metas = ~24KB, but reserve() needs the biggest single block.
    // Try 200 first, shrink if heap fragmented.
    constexpr uint16_t kMaxSlots = 200;
    uint16_t maxSlots = kMaxSlots;
    const size_t kPerSlot = 128;  // sizeof(PwnFileMeta) + allocator overhead

    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t freeHeap = ESP.getFreeHeap();

    // Adaptive: shrink the cap to what we can actually fit safely.
    while (maxSlots > 20 &&
           (largest < (size_t)maxSlots * kPerSlot / 2 ||
            freeHeap < (size_t)maxSlots * kPerSlot * 2)) {
        maxSlots = (uint16_t)(maxSlots * 3 / 4);  // geometric shrink
        if (maxSlots > 40) maxSlots -= 8;
    }

    if (freeHeap < 8 * 1024 || largest < 4 * 1024) {
        Serial.printf("[PWNCRACK] Scan skipped — heap too low: free=%u largest=%u\n",
                      (unsigned)freeHeap, (unsigned)largest);
        Display::showToast("LOW HEAP — RETRY", 1500);
        return;
    }

    Serial.printf("[PWNCRACK] scanFiles cap=%u (free=%u largest=%u)\n",
                  (unsigned)maxSlots, (unsigned)freeHeap, (unsigned)largest);

    metas.reserve(maxSlots);
    const char* hsDir = SDLayout::handshakesDir();
    if (!hsDir || !SD.exists(hsDir)) {
        Serial.printf("[PWNCRACK] No handshakes dir: %s\n", hsDir ? hsDir : "(null)");
        return;
    }

    File dir = SD.open(hsDir);
    if (!dir || !dir.isDirectory()) {
        if (dir) dir.close();
        return;
    }

    File entry = dir.openNextFile();
    uint16_t n = 0;
    while (entry && metas.size() < maxSlots) {
        if (!entry.isDirectory()) {
            const char* raw = entry.name();
            const char* slash = strrchr(raw, '/');
            const char* base = slash ? slash + 1 : raw;
            bool ok = endsWithCI(base, ".22000") || endsWithCI(base, ".hc22000");
            if (ok) {
                PwnFileMeta m{};
                strncpy(m.filename, base, sizeof(m.filename) - 1);
                m.fileSize = entry.size();

                // Reuse parser — it fills ssid/bssid/bssidHex/isPMKID
                PwnFileInfo tmp{};
                strncpy(tmp.filename, m.filename, sizeof(tmp.filename) - 1);
                parseNameMeta(tmp);
                strncpy(m.ssid, tmp.ssid, sizeof(m.ssid) - 1);
                strncpy(m.bssid, tmp.bssid, sizeof(m.bssid) - 1);
                strncpy(m.bssidHex, tmp.bssidHex, sizeof(m.bssidHex) - 1);
                m.isPMKID = tmp.isPMKID;

                metas.push_back(m);
                n++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
        if ((n % 16) == 0) yield();
    }
    dir.close();

    selectedInfoValid = false;
    Serial.printf("[PWNCRACK] Scan %s: %u hash files (cap=%u)\n",
                  hsDir, (unsigned)metas.size(), (unsigned)maxSlots);
}

void PwncrackMenu::show() {
    active = true;
    keyWasPressed = true;  // swallow key that opened menu
    scrollOffset = 0;
    selectedIndex = 0;
    syncModalActive = false;
    detailViewActive = false;
    syncState = PwnSyncState::IDLE;
    hintIndex = esp_random() % HINT_COUNT;
    selectedInfoValid = false;

    ensureKeyLoaded();
    Pwncrack::loadCache();
    scanFiles();
    refreshSelected();  // populate selectedInfo so first-row ST shows on first frame

    if (Pwncrack::hasApiKey()) {
        Display::showToast("PWNCRACK KEY OK", 1200);
    } else {
        Display::showToast("NO KEY - PUT key.txt", 2000);
    }
}

const char* PwncrackMenu::getBottomHint() {
    if (syncModalActive) {
        if (syncState == PwnSyncState::DIAG_DONE || syncState == PwnSyncState::COMPLETE ||
            syncState == PwnSyncState::ERROR) {
            return "ENTER/ESC = CLOSE";
        }
        if (syncState == PwnSyncState::RUNNING_DIAG) {
            return "TESTING pwncrack.org...";
        }
        return "SYNCING... WAIT";
    }
    if (detailViewActive) {
        return "ENTER/ESC = CLOSE DETAIL";
    }
    if (!Pwncrack::hasApiKey()) {
        return "R:LOAD KEY  T:TEST  ESC";
    }
    if (metas.empty()) {
        return "NO HASH  OINK FIRST  T:TEST";
    }
    return HINTS[hintIndex % HINT_COUNT];
}

void PwncrackMenu::hide() {
    active = false;
    syncModalActive = false;
    detailViewActive = false;
    syncState = PwnSyncState::IDLE;
    metas.clear();
    metas.shrink_to_fit();
    selectedInfoValid = false;
    Pwncrack::freeCacheMemory();
    if (WiFi.status() == WL_CONNECTED) {
        disconnectWiFi();
    }
}

void PwncrackMenu::onSyncProgress(const char* status, uint8_t progress, uint8_t total) {
    (void)total;
    if (status) {
        strncpy(syncStatusText, status, sizeof(syncStatusText) - 1);
        syncStatusText[sizeof(syncStatusText) - 1] = '\0';
    }
    syncUploaded = progress;  // step indicator while running
}

bool PwncrackMenu::connectToWiFi() {
    const char* ssid = Config::wifi().otaSSID;
    const char* password = Config::wifi().otaPassword;
    if (!ssid || ssid[0] == '\0') {
        strncpy(syncError, "NO WIFI SSID CONFIG", sizeof(syncError) - 1);
        return false;
    }
    strncpy(syncStatusText, "CONNECTING WIFI...", sizeof(syncStatusText) - 1);

    if (NetworkRecon::isRunning() || NetworkRecon::isPaused()) {
        NetworkRecon::stop();
    }
    WiFiUtils::stopPromiscuous();
    WiFiUtils::hardReset();
    WiFi.begin(ssid, password);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < 15000) {
        delay(100);
        yield();
    }
    if (WiFi.status() != WL_CONNECTED) {
        strncpy(syncError, "WIFI CONNECT FAILED", sizeof(syncError) - 1);
        WiFiUtils::shutdown();
        NetworkRecon::start();
        return false;
    }
    return true;
}

void PwncrackMenu::disconnectWiFi() {
    WiFiUtils::shutdown();
    NetworkRecon::start();
}

void PwncrackMenu::startSync() {
    ensureKeyLoaded();
    detailViewActive = false;
    syncModalActive = true;
    syncState = PwnSyncState::CONNECTING_WIFI;
    syncStatusText[0] = '\0';
    syncError[0] = '\0';
    syncUploaded = syncFailed = syncSkipped = 0;
    syncCracked = syncNewCracked = 0;

    if (!Pwncrack::hasApiKey()) {
        strncpy(syncError, "NO PWNCRACK KEY", sizeof(syncError) - 1);
        syncState = PwnSyncState::ERROR;
        Display::showToast("NO KEY", 1500);
        return;
    }
    if (metas.empty()) {
        scanFiles();
    }
    if (metas.empty()) {
        strncpy(syncError, "NO .22000 FILES", sizeof(syncError) - 1);
        syncState = PwnSyncState::ERROR;
        Display::showToast("NO .22000 IN HANDSHAKES", 2000);
        return;
    }
    Display::showToast("PWNCRACK SYNC...", 1000);
}

void PwncrackMenu::startDiag() {
    ensureKeyLoaded();
    detailViewActive = false;
    syncModalActive = true;
    syncState = PwnSyncState::CONNECTING_WIFI;
    // Reuse CONNECTING then branch via a flag in status text
    strncpy(syncStatusText, "DIAG:WIFI...", sizeof(syncStatusText) - 1);
    syncError[0] = '\0';
    lastDiag = {};
    // Special: mark we want diag after wifi by setting a sentinel in syncError
    strncpy(syncError, "__DIAG__", sizeof(syncError) - 1);
    Display::showToast("PWNCRACK TEST...", 1000);
}

void PwncrackMenu::processSyncState() {
    if (!syncModalActive) return;
    switch (syncState) {
        case PwnSyncState::CONNECTING_WIFI:
            strncpy(syncStatusText, "CONNECTING WIFI...", sizeof(syncStatusText) - 1);
            if (connectToWiFi()) {
                if (strcmp(syncError, "__DIAG__") == 0) {
                    syncError[0] = '\0';
                    syncState = PwnSyncState::RUNNING_DIAG;
                } else {
                    syncState = PwnSyncState::UPLOADING;
                }
            } else {
                syncState = PwnSyncState::ERROR;
            }
            break;
        case PwnSyncState::RUNNING_DIAG: {
            strncpy(syncStatusText, "TESTING pwncrack.org...", sizeof(syncStatusText) - 1);
            lastDiag = Pwncrack::runDiagnostics(onSyncProgress);
            if (lastDiag.detail[0]) {
                strncpy(syncError, lastDiag.detail, sizeof(syncError) - 1);
                syncError[sizeof(syncError) - 1] = '\0';
            }
            syncState = PwnSyncState::DIAG_DONE;
            disconnectWiFi();
            break;
        }
        case PwnSyncState::UPLOADING: {
            strncpy(syncStatusText, "UPLOAD + POTFILE...", sizeof(syncStatusText) - 1);
            // HTTP path — arena still helps if HTTPS fallback is used
            Tls::arenaBegin(Display::mainCanvasBuffer(), Display::mainCanvasBufferSize());
            PwncrackSyncResult r = Pwncrack::syncCaptures(onSyncProgress);
            Tls::arenaEnd();
            syncUploaded = r.uploaded;
            syncFailed = r.failed;
            syncSkipped = r.skipped;
            syncCracked = r.cracked;
            syncNewCracked = r.newCracked;
            if (r.error[0]) {
                strncpy(syncError, r.error, sizeof(syncError) - 1);
                syncError[sizeof(syncError) - 1] = '\0';
            }
            // Always COMPLETE so user sees counts (like WPA-SEC)
            syncState = PwnSyncState::COMPLETE;
            if (!r.success && r.uploaded == 0 && r.skipped == 0 && r.error[0]) {
                syncState = PwnSyncState::ERROR;
            }
            // Surface last HTTP detail if all failed
            if (r.uploaded == 0 && r.failed > 0 && Pwncrack::getLastError()[0]) {
                if (syncError[0] == '\0') {
                    strncpy(syncError, Pwncrack::getLastError(), sizeof(syncError) - 1);
                }
            }
            disconnectWiFi();
            scanFiles();  // refresh ST flags
            break;
        }
        default:
            break;
    }
}

void PwncrackMenu::handleInput() {
    bool anyPressed = M5Cardputer.Keyboard.isPressed();
    if (!anyPressed) {
        keyWasPressed = false;
        return;
    }
    if (keyWasPressed) return;
    keyWasPressed = true;

    auto keys = M5Cardputer.Keyboard.keysState();

    if (syncModalActive) {
        if (syncState == PwnSyncState::COMPLETE || syncState == PwnSyncState::ERROR ||
            syncState == PwnSyncState::DIAG_DONE) {
            if (keys.enter || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) ||
                M5Cardputer.Keyboard.isKeyPressed('`')) {
                syncModalActive = false;
                syncState = PwnSyncState::IDLE;
                Pwncrack::loadCache();
                refreshSelected();
            }
        }
        return;
    }

    // Detail view — Enter/back closes
    if (detailViewActive) {
        if (keys.enter || M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) ||
            M5Cardputer.Keyboard.isKeyPressed('`')) {
            detailViewActive = false;
        }
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE) ||
        M5Cardputer.Keyboard.isKeyPressed('`')) {
        hide();
        return;
    }

    // ; up  . down — rotate bottom hints like HASHES
    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        hintIndex = (hintIndex + 1) % HINT_COUNT;
        if (selectedIndex > 0) {
            selectedIndex--;
            if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
            refreshSelected();
        }
    }
    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        hintIndex = (hintIndex + 1) % HINT_COUNT;
        if (!metas.empty() && selectedIndex + 1 < metas.size()) {
            selectedIndex++;
            if (selectedIndex >= scrollOffset + VISIBLE_ITEMS) {
                scrollOffset = selectedIndex - VISIBLE_ITEMS + 1;
            }
            refreshSelected();
        }
    }

    // Enter → detail (password if [OK])
    if (keys.enter) {
        if (!metas.empty() && selectedIndex < metas.size()) {
            refreshSelected();  // make sure detail view shows current status
            detailViewActive = true;
        }
        return;
    }

    // S = sync to pwncrack.org
    if (M5Cardputer.Keyboard.isKeyPressed('s') || M5Cardputer.Keyboard.isKeyPressed('S')) {
        startSync();
        return;
    }

    // T = connectivity / API self-test (no upload required)
    if (M5Cardputer.Keyboard.isKeyPressed('t') || M5Cardputer.Keyboard.isKeyPressed('T')) {
        startDiag();
        return;
    }

    // R = reload key + rescan
    if (M5Cardputer.Keyboard.isKeyPressed('r') || M5Cardputer.Keyboard.isKeyPressed('R')) {
        ensureKeyLoaded();
        if (Config::loadPwncrackKeyFromFile()) {
            Display::showToast("PWN KEY LOADED", 2000);
        } else if (Pwncrack::hasApiKey()) {
            Display::showToast("KEY OK (NVS)", 1500);
        } else {
            Display::showToast("NO KEY FILE", 2000);
        }
        Pwncrack::loadCache();
        scanFiles();
        return;
    }

    // C = clear local "uploaded" markers so S will re-send everything
    // (useful if previous false success left ST=[..] but site empty)
    if (M5Cardputer.Keyboard.isKeyPressed('c') || M5Cardputer.Keyboard.isKeyPressed('C')) {
        const char* upPath = SDLayout::pwncrackUploadedPath();
        if (SD.exists(upPath)) {
            SD.remove(upPath);
        }
        Pwncrack::freeCacheMemory();
        Pwncrack::loadCache();
        refreshSelected();
        Display::showToast("UPLOAD LOG CLEARED", 2000);
        return;
    }
}

void PwncrackMenu::update() {
    if (!active) return;
    if (syncModalActive &&
        syncState != PwnSyncState::IDLE &&
        syncState != PwnSyncState::COMPLETE &&
        syncState != PwnSyncState::ERROR &&
        syncState != PwnSyncState::DIAG_DONE) {
        processSyncState();
    }
    handleInput();
}

void PwncrackMenu::drawSyncModal(M5Canvas& canvas) {
    const int boxW = 228, boxH = 118;
    const int boxX = (canvas.width() - boxW) / 2;
    const int boxY = (canvas.height() - boxH) / 2 - 2;
    canvas.fillRoundRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, 8, COLOR_BG);
    canvas.fillRoundRect(boxX, boxY, boxW, boxH, 8, COLOR_FG);
    canvas.setTextColor(COLOR_BG, COLOR_FG);
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(1);

    if (syncState == PwnSyncState::DIAG_DONE || syncState == PwnSyncState::RUNNING_DIAG) {
        canvas.drawString("PWNCRACK TEST", canvas.width() / 2, boxY + 4);
        if (syncState == PwnSyncState::RUNNING_DIAG) {
            canvas.drawString(syncStatusText[0] ? syncStatusText : "TESTING...",
                              canvas.width() / 2, boxY + 48);
        } else {
            // Show diagnostic lines
            canvas.setTextDatum(TL_DATUM);
            int y = boxY + 16;
            for (uint8_t i = 0; i < lastDiag.lineCount && i < 6; i++) {
                canvas.drawString(lastDiag.lines[i], boxX + 6, y);
                y += 12;
            }
            canvas.setTextDatum(TC_DATUM);
            if (lastDiag.detail[0]) {
                canvas.drawString(lastDiag.detail, canvas.width() / 2, boxY + boxH - 22);
            }
            canvas.drawString("ENTER/ESC", canvas.width() / 2, boxY + boxH - 10);
        }
        canvas.setTextDatum(TL_DATUM);
        return;
    }

    canvas.drawString("PWNCRACK.ORG SYNC", canvas.width() / 2, boxY + 6);

    if (syncState == PwnSyncState::ERROR) {
        canvas.drawString(syncError[0] ? syncError : "ERROR", canvas.width() / 2, boxY + 40);
        // If it was FRAG — explain it's fixed path / try T
        if (strstr(syncError, "FRAG") || strstr(syncError, "HEAP") ||
            strstr(syncError, "LOW HEAP")) {
            canvas.drawString("PRESS T = NET TEST", canvas.width() / 2, boxY + 58);
        }
        canvas.drawString("ENTER/ESC", canvas.width() / 2, boxY + boxH - 14);
    } else if (syncState == PwnSyncState::COMPLETE) {
        char line[48];
        snprintf(line, sizeof(line), "UP %u  SKIP %u  FAIL %u",
                 syncUploaded, syncSkipped, syncFailed);
        canvas.drawString(line, canvas.width() / 2, boxY + 28);
        snprintf(line, sizeof(line), "CRACKED %u (+%u)", syncCracked, syncNewCracked);
        canvas.drawString(line, canvas.width() / 2, boxY + 44);
        if (syncUploaded == 0 && syncSkipped > 0 && syncFailed == 0) {
            canvas.drawString("ALL SKIP=ALREADY SENT", canvas.width() / 2, boxY + 60);
            canvas.drawString("C=CLR THEN S", canvas.width() / 2, boxY + 74);
        } else if (syncFailed > 0 && syncError[0]) {
            canvas.drawString(syncError, canvas.width() / 2, boxY + 60);
            canvas.drawString("T=NET TEST", canvas.width() / 2, boxY + 74);
        } else if (syncUploaded > 0) {
            canvas.drawString("SENT OK (if site opens)", canvas.width() / 2, boxY + 60);
        }
        canvas.drawString("ENTER/ESC", canvas.width() / 2, boxY + boxH - 14);
    } else {
        canvas.drawString(syncStatusText[0] ? syncStatusText : "...", canvas.width() / 2, boxY + 48);
    }
    canvas.setTextDatum(TL_DATUM);
}

void PwncrackMenu::drawDetailView(M5Canvas& canvas) {
    if (selectedIndex >= metas.size()) return;
    const PwnFileInfo& f = selectedInfo;

    const int boxW = 220;
    const int boxH = 78;
    const int boxX = (canvas.width() - boxW) / 2;
    const int boxY = (canvas.height() - boxH) / 2 - 5;

    canvas.fillRoundRect(boxX - 2, boxY - 2, boxW + 4, boxH + 4, 8, COLOR_BG);
    canvas.fillRoundRect(boxX, boxY, boxW, boxH, 8, COLOR_FG);
    canvas.setTextColor(COLOR_BG, COLOR_FG);
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextSize(1);

    int centerX = canvas.width() / 2;

    // SSID uppercase
    char ssidLine[24];
    size_t pos = 0;
    const char* src = f.ssid;
    while (*src && pos + 1 < sizeof(ssidLine) && pos < 20) {
        ssidLine[pos++] = (char)toupper((unsigned char)*src++);
    }
    ssidLine[pos] = '\0';
    canvas.drawString(ssidLine, centerX, boxY + 4);
    canvas.drawString(f.bssid[0] ? f.bssid : "--", centerX, boxY + 16);

    if (f.status == PwnCaptureStatus::CRACKED && f.password[0]) {
        canvas.drawString("** CR4CK3D **", centerX, boxY + 32);
        char pwLine[24];
        size_t pwLen = strlen(f.password);
        if (pwLen > 20) {
            memcpy(pwLine, f.password, 18);
            pwLine[18] = '.';
            pwLine[19] = '.';
            pwLine[20] = '\0';
        } else {
            strncpy(pwLine, f.password, sizeof(pwLine) - 1);
            pwLine[sizeof(pwLine) - 1] = '\0';
        }
        canvas.drawString(pwLine, centerX, boxY + 48);
    } else if (f.status == PwnCaptureStatus::UPLOADED) {
        canvas.drawString("UPLOADED - WAITING", centerX, boxY + 36);
        canvas.drawString("S:SYNC FOR POTFILE", centerX, boxY + 52);
    } else {
        canvas.drawString("LOCAL - NOT SENT", centerX, boxY + 36);
        canvas.drawString("S:UPLOAD TO PWNCRACK", centerX, boxY + 52);
    }

    // tiny filename footer
    char shortn[28];
    strncpy(shortn, f.filename, sizeof(shortn) - 1);
    shortn[sizeof(shortn) - 1] = '\0';
    if (strlen(shortn) > 26) {
        shortn[23] = '.'; shortn[24] = '.'; shortn[25] = '.'; shortn[26] = '\0';
    }
    canvas.drawString(shortn, centerX, boxY + 64);
    canvas.setTextDatum(TL_DATUM);
}

void PwncrackMenu::draw(M5Canvas& canvas) {
    if (!active) return;

    uiListBackground(canvas);
    canvas.setTextColor(UiStyle::TEXT);
    canvas.setTextSize(1);

    if (!Config::isSDAvailable()) {
        canvas.setTextColor(UiStyle::RED);
        canvas.setCursor(4, 40);
        canvas.print("NO SD CARD");
        return;
    }

    if (syncModalActive) {
        drawSyncModal(canvas);
        return;
    }

    // Summary — like HASHES LOOT line. We only materialize visible window for status counts;
    // full scan would defeat the pagination purpose.
    uint16_t total = (uint16_t)metas.size();
    uint16_t cracked = 0, uploaded = 0, local = 0;
    const char* hsDir = SDLayout::handshakesDir();
    if (!metas.empty()) {
        // Count statuses in visible window + small buffer (fast, bounded heap)
        uint8_t scanEnd = (uint8_t)min((int)total, (int)(scrollOffset + VISIBLE_ITEMS + 4));
        for (uint8_t i = scrollOffset; i < scanEnd; i++) {
            PwnFileInfo tmp = materialize(metas[i], hsDir);
            if (tmp.status == PwnCaptureStatus::CRACKED) cracked++;
            else if (tmp.status == PwnCaptureStatus::UPLOADED) uploaded++;
            else local++;
        }
    }

    char summary[64];
    snprintf(summary, sizeof(summary), "PWN %u OK %u UP %u LOC %u",
             (unsigned)total, (unsigned)cracked, (unsigned)uploaded, (unsigned)local);
    canvas.setTextColor(UiStyle::GOLD);
    canvas.setCursor(4, 2);
    canvas.print(summary);

    // Key badge
    canvas.setTextColor(Pwncrack::hasApiKey() ? UiStyle::CYAN : UiStyle::RED);
    canvas.setCursor(canvas.width() - 42, 2);
    canvas.print(Pwncrack::hasApiKey() ? "KEY" : "NOKEY");

    if (metas.empty()) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.setCursor(4, 36);
        canvas.print("NO .22000 HASH FILES");
        canvas.setTextColor(UiStyle::TEXT);
        canvas.setCursor(4, 52);
        canvas.print("RUN OINK FIRST");
        canvas.setCursor(4, 68);
        canvas.print(SDLayout::handshakesDir());
        if (!Pwncrack::hasApiKey()) {
            canvas.setCursor(4, 88);
            canvas.print("KEY: pwncrack/key.txt");
            canvas.setCursor(4, 100);
            canvas.print("THEN PRESS R");
        }
        return;
    }

    // Column headers — same as HASHES
    canvas.setTextColor(UiStyle::CYAN);
    canvas.setCursor(4, 12);
    canvas.print("SSID");
    canvas.setCursor(120, 12);
    canvas.print("ST");
    canvas.setCursor(150, 12);
    canvas.print("TYPE");
    canvas.setCursor(190, 12);
    canvas.print("SIZE");

    int y = 22;
    const int lineHeight = 16;

    for (uint8_t i = scrollOffset; i < metas.size() && i < scrollOffset + VISIBLE_ITEMS; i++) {
        // Lazy materialize: builds a full PwnFileInfo with status+password just for drawing
        const PwnFileMeta& m = metas[i];
        PwnFileInfo f = materialize(m, SDLayout::handshakesDir());
        bool sel = (i == selectedIndex);
        uiListRow(canvas, y, lineHeight, sel, UiStyle::PINK);
        canvas.setTextColor(sel ? UiStyle::BG : UiStyle::TEXT);

        // SSID — uppercase, max 17
        canvas.setCursor(8, y + 1);
        char ssidBuf[20];
        size_t pos = 0;
        const char* ssidSrc = f.ssid;
        while (*ssidSrc && pos < 17) {
            ssidBuf[pos++] = (char)toupper((unsigned char)*ssidSrc++);
        }
        ssidBuf[pos] = '\0';
        if (*ssidSrc && pos >= 2) {
            ssidBuf[pos - 2] = '.';
            ssidBuf[pos - 1] = '.';
        }
        canvas.print(ssidBuf);

        // ST
        canvas.setCursor(120, y);
        if (f.status == PwnCaptureStatus::CRACKED) {
            canvas.print("[OK]");
        } else if (f.status == PwnCaptureStatus::UPLOADED) {
            canvas.print("[..]");
        } else {
            canvas.print("[--]");
        }

        // TYPE
        canvas.setCursor(150, y);
        canvas.print(f.isPMKID ? "PM" : "HS");

        // SIZE
        canvas.setCursor(190, y);
        char sizeBuf[12];
        formatSize(sizeBuf, sizeof(sizeBuf), f.fileSize);
        canvas.print(sizeBuf);

        y += lineHeight;
    }

    // Scroll arrows
    canvas.setTextColor(COLOR_FG);
    if (scrollOffset > 0) {
        canvas.setCursor(canvas.width() - 10, 22);
        canvas.print("^");
    }
    if (scrollOffset + VISIBLE_ITEMS < metas.size()) {
        canvas.setCursor(canvas.width() - 10, 22 + (VISIBLE_ITEMS - 1) * lineHeight);
        canvas.print("v");
    }

    // Controls live in bottom bar (Display::drawBottomBar → getBottomHint)

    if (detailViewActive) {
        drawDetailView(canvas);
    }
}

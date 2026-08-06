// SD + SPIFFS file browser — list, enter folders, delete files

#include "files_menu.h"
#include "display.h"
#include "../core/config.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <SPIFFS.h>
#include <FS.h>
#include <string.h>
#include <strings.h>
#include <algorithm>

bool FilesMenu::active = false;
FilesMenu::Source FilesMenu::source = FilesMenu::Source::SD_CARD;
FilesMenu::Phase FilesMenu::phase = FilesMenu::Phase::BROWSE;
char FilesMenu::cwd[96] = "/";
std::vector<FilesMenu::Entry> FilesMenu::entries;
uint8_t FilesMenu::selectedIndex = 0;
uint8_t FilesMenu::scrollOffset = 0;
bool FilesMenu::keyWasPressed = false;
char FilesMenu::statusMsg[40] = "";

void FilesMenu::init() {
    active = false;
    source = Source::SD_CARD;
    phase = Phase::BROWSE;
    setCwdRoot();
    entries.clear();
    selectedIndex = 0;
    scrollOffset = 0;
    keyWasPressed = false;
    statusMsg[0] = '\0';
}

fs::FS& FilesMenu::activeFs() {
    return (source == Source::SD_CARD) ? (fs::FS&)SD : (fs::FS&)SPIFFS;
}

bool FilesMenu::sourceReady() {
    if (source == Source::SD_CARD) return Config::isSDAvailable();
    // SPIFFS is mounted at boot in Config::init — assume available if begin already done
    return SPIFFS.totalBytes() > 0 || SPIFFS.begin(false);
}

void FilesMenu::setCwdRoot() {
    strncpy(cwd, "/", sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
}

void FilesMenu::joinPath(char* out, size_t outLen, const char* dir, const char* name) {
    if (!out || outLen == 0) return;
    out[0] = '\0';
    if (!dir || !name) return;
    if (strcmp(dir, "/") == 0) {
        snprintf(out, outLen, "/%s", name);
    } else {
        snprintf(out, outLen, "%s/%s", dir, name);
    }
}

void FilesMenu::formatSize(uint32_t bytes, char* out, size_t outLen) {
    if (!out || outLen == 0) return;
    if (bytes < 1024) {
        snprintf(out, outLen, "%luB", (unsigned long)bytes);
    } else if (bytes < 1024UL * 1024UL) {
        snprintf(out, outLen, "%luK", (unsigned long)((bytes + 512) / 1024));
    } else {
        snprintf(out, outLen, "%luM", (unsigned long)((bytes + 512UL * 1024UL) / (1024UL * 1024UL)));
    }
}

void FilesMenu::ensureVisible() {
    if (entries.empty()) {
        selectedIndex = 0;
        scrollOffset = 0;
        return;
    }
    if (selectedIndex >= entries.size()) selectedIndex = (uint8_t)(entries.size() - 1);
    if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
    else if (selectedIndex >= scrollOffset + VISIBLE) {
        scrollOffset = selectedIndex - VISIBLE + 1;
    }
    if (entries.size() <= VISIBLE) scrollOffset = 0;
    else if (scrollOffset + VISIBLE > entries.size()) {
        scrollOffset = (uint8_t)(entries.size() - VISIBLE);
    }
}

void FilesMenu::refresh() {
    entries.clear();
    entries.reserve(16);
    selectedIndex = 0;
    scrollOffset = 0;
    phase = Phase::BROWSE;
    statusMsg[0] = '\0';

    if (!sourceReady()) {
        strncpy(statusMsg, source == Source::SD_CARD ? "NO SD" : "NO SPIFFS",
                sizeof(statusMsg) - 1);
        return;
    }

    fs::FS& fs = activeFs();
    File dir = fs.open(cwd);
    if (!dir || !dir.isDirectory()) {
        strncpy(statusMsg, "OPEN FAIL", sizeof(statusMsg) - 1);
        if (dir) dir.close();
        // Try recover to root
        setCwdRoot();
        dir = fs.open(cwd);
        if (!dir || !dir.isDirectory()) {
            if (dir) dir.close();
            return;
        }
    }

    uint8_t yieldN = 0;
    while (entries.size() < MAX_ENTRIES) {
        File entry = dir.openNextFile();
        if (!entry) break;

        // Copy metadata BEFORE close — entry.name() is invalid after close()
        Entry e;
        memset(&e, 0, sizeof(e));
        e.isDir = entry.isDirectory();
        e.size = e.isDir ? 0 : (uint32_t)entry.size();

        char rawPath[96];
        rawPath[0] = '\0';
        const char* raw = entry.name();
        if (raw && raw[0]) {
            strncpy(rawPath, raw, sizeof(rawPath) - 1);
            rawPath[sizeof(rawPath) - 1] = '\0';
        }
        entry.close();
        if (!rawPath[0]) continue;

        // Basename only (SD may return full path)
        const char* base = strrchr(rawPath, '/');
        base = base ? base + 1 : rawPath;
        if (!base[0] || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) continue;

        strncpy(e.name, base, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        entries.push_back(e);

        if (++yieldN >= 12) {
            yieldN = 0;
            yield();
        }
    }
    dir.close();

    // Dirs first, then name
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir) return a.isDir && !b.isDir;
        return strcasecmp(a.name, b.name) < 0;
    });

    ensureVisible();
    Serial.printf("[FILES] %s cwd=%s count=%u\n",
                  source == Source::SD_CARD ? "SD" : "INT",
                  cwd, (unsigned)entries.size());
}

void FilesMenu::show() {
    active = true;
    source = Config::isSDAvailable() ? Source::SD_CARD : Source::INTERNAL;
    phase = Phase::BROWSE;
    setCwdRoot();
    keyWasPressed = true;  // ignore key that opened menu
    refresh();
    Display::clearBottomOverlay();
}

void FilesMenu::hide() {
    active = false;
    phase = Phase::BROWSE;
    entries.clear();
    Display::clearBottomOverlay();
}

void FilesMenu::goUp() {
    if (strcmp(cwd, "/") == 0) {
        hide();
        return;
    }
    // Strip last segment
    char* slash = strrchr(cwd, '/');
    if (!slash || slash == cwd) {
        setCwdRoot();
    } else {
        *slash = '\0';
        if (cwd[0] == '\0') setCwdRoot();
    }
    refresh();
}

void FilesMenu::openSelected() {
    if (entries.empty() || selectedIndex >= entries.size()) return;
    const Entry& e = entries[selectedIndex];
    if (!e.isDir) {
        // File: show size toast (no full viewer — keeps heap light)
        char sz[16];
        formatSize(e.size, sz, sizeof(sz));
        char msg[48];
        snprintf(msg, sizeof(msg), "%s  %s", e.name, sz);
        Display::showToast(msg, 1800);
        return;
    }

    char next[96];
    joinPath(next, sizeof(next), cwd, e.name);
    if (strlen(next) >= sizeof(cwd)) {
        Display::showToast("PATH TOO LONG");
        return;
    }
    strncpy(cwd, next, sizeof(cwd) - 1);
    cwd[sizeof(cwd) - 1] = '\0';
    refresh();
}

void FilesMenu::requestDelete() {
    if (entries.empty() || selectedIndex >= entries.size()) return;
    phase = Phase::CONFIRM_DEL;
}

void FilesMenu::confirmDelete() {
    if (entries.empty() || selectedIndex >= entries.size()) {
        phase = Phase::BROWSE;
        return;
    }
    const Entry& e = entries[selectedIndex];
    char path[128];
    joinPath(path, sizeof(path), cwd, e.name);

    fs::FS& fs = activeFs();
    bool ok = false;
    if (e.isDir) {
        // Only empty dirs (safe)
        File d = fs.open(path);
        bool empty = true;
        if (d && d.isDirectory()) {
            File child = d.openNextFile();
            if (child) {
                empty = false;
                child.close();
            }
            d.close();
        }
        if (!empty) {
            Display::showToast("DIR NOT EMPTY");
            phase = Phase::BROWSE;
            return;
        }
        ok = fs.rmdir(path);
    } else {
        ok = fs.remove(path);
    }

    if (ok) {
        Display::showToast("DELETED");
        snprintf(statusMsg, sizeof(statusMsg), "del ok");
    } else {
        Display::showToast("DELETE FAIL");
        snprintf(statusMsg, sizeof(statusMsg), "del fail");
    }
    phase = Phase::BROWSE;
    refresh();
}

void FilesMenu::handleInput() {
    bool any = M5Cardputer.Keyboard.isPressed();
    if (!any) {
        keyWasPressed = false;
        return;
    }
    if (keyWasPressed) return;
    keyWasPressed = true;

    auto keys = M5Cardputer.Keyboard.keysState();
    bool back = M5Cardputer.Keyboard.isKeyPressed(KEY_BACKSPACE);
    bool up = M5Cardputer.Keyboard.isKeyPressed(';');
    bool down = M5Cardputer.Keyboard.isKeyPressed('.');
    bool delKey = M5Cardputer.Keyboard.isKeyPressed('d') ||
                  M5Cardputer.Keyboard.isKeyPressed('D');
    bool tabSrc = M5Cardputer.Keyboard.isKeyPressed('t') ||
                  M5Cardputer.Keyboard.isKeyPressed('T');
    bool refreshKey = M5Cardputer.Keyboard.isKeyPressed('r') ||
                      M5Cardputer.Keyboard.isKeyPressed('R');

    if (phase == Phase::CONFIRM_DEL) {
        if (keys.enter || M5Cardputer.Keyboard.isKeyPressed('y') ||
            M5Cardputer.Keyboard.isKeyPressed('Y')) {
            confirmDelete();
            return;
        }
        if (back || M5Cardputer.Keyboard.isKeyPressed('n') ||
            M5Cardputer.Keyboard.isKeyPressed('N')) {
            phase = Phase::BROWSE;
            return;
        }
        return;
    }

    // BROWSE
    if (back) {
        goUp();
        return;
    }
    if (tabSrc) {
        // Toggle SD <-> internal SPIFFS
        if (source == Source::SD_CARD) {
            source = Source::INTERNAL;
        } else {
            source = Source::SD_CARD;
        }
        setCwdRoot();
        refresh();
        Display::showToast(source == Source::SD_CARD ? "SD CARD" : "INTERNAL");
        return;
    }
    if (refreshKey) {
        refresh();
        Display::showToast("REFRESH");
        return;
    }
    if (up && !entries.empty()) {
        if (selectedIndex == 0) selectedIndex = (uint8_t)(entries.size() - 1);
        else selectedIndex--;
        ensureVisible();
        return;
    }
    if (down && !entries.empty()) {
        selectedIndex = (uint8_t)((selectedIndex + 1) % entries.size());
        ensureVisible();
        return;
    }
    if (delKey) {
        requestDelete();
        return;
    }
    if (keys.enter) {
        openSelected();
        return;
    }
}

void FilesMenu::update() {
    if (!active) return;
    handleInput();
}

void FilesMenu::drawConfirm(M5Canvas& canvas) {
    canvas.fillSprite(UiStyle::BG);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::Font0);

    canvas.setTextColor(UiStyle::RED);
    canvas.drawString("DELETE?", 4, 8);

    if (!entries.empty() && selectedIndex < entries.size()) {
        const Entry& e = entries[selectedIndex];
        canvas.setTextColor(UiStyle::TITLE);
        char line[40];
        snprintf(line, sizeof(line), "%s%s", e.isDir ? "/" : "", e.name);
        canvas.drawString(line, 4, 28);
        canvas.setTextColor(UiStyle::DIM);
        canvas.drawString(cwd, 4, 44);
    }

    canvas.setTextColor(UiStyle::GOLD);
    canvas.drawString("ENT/Y = yes   N/BKSP = no", 4, MAIN_H - 12);
}

void FilesMenu::drawBrowse(M5Canvas& canvas) {
    canvas.fillSprite(UiStyle::BG);
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    canvas.setFont(&fonts::Font0);

    // Header: source + free space
    canvas.setTextColor(UiStyle::TITLE);
    const char* srcLabel = (source == Source::SD_CARD) ? "SD" : "INT";
    canvas.drawString(srcLabel, 4, 2);

    char spaceBuf[28];
    spaceBuf[0] = '\0';
    if (sourceReady()) {
        uint64_t total = 0, used = 0;
        if (source == Source::SD_CARD) {
            total = SD.totalBytes();
            used = SD.usedBytes();
        } else {
            total = SPIFFS.totalBytes();
            used = SPIFFS.usedBytes();
        }
        uint64_t freeB = (total > used) ? (total - used) : 0;
        char freeS[12], totS[12];
        formatSize((uint32_t)(freeB > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : freeB), freeS, sizeof(freeS));
        formatSize((uint32_t)(total > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : total), totS, sizeof(totS));
        snprintf(spaceBuf, sizeof(spaceBuf), "%s free", freeS);
    } else {
        strncpy(spaceBuf, "offline", sizeof(spaceBuf) - 1);
    }
    canvas.setTextColor(UiStyle::DIM);
    canvas.drawString(spaceBuf, 28, 2);

    // cwd (truncated)
    canvas.setTextColor(UiStyle::CYAN);
    char pathShow[36];
    if (strlen(cwd) > 32) {
        snprintf(pathShow, sizeof(pathShow), "..%s", cwd + strlen(cwd) - 30);
    } else {
        strncpy(pathShow, cwd, sizeof(pathShow) - 1);
        pathShow[sizeof(pathShow) - 1] = '\0';
    }
    canvas.drawString(pathShow, 4, 14);

    if (!sourceReady()) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.drawString(source == Source::SD_CARD ? "NO SD CARD" : "SPIFFS FAIL", 4, 40);
        canvas.setTextColor(UiStyle::DIM);
        canvas.drawString("T = switch source", 4, 56);
        canvas.setTextColor(UiStyle::GOLD);
        canvas.drawString("T source  BKSP exit", 4, MAIN_H - 10);
        return;
    }

    if (entries.empty()) {
        canvas.setTextColor(UiStyle::GOLD);
        canvas.drawString("(empty)", 4, 40);
        canvas.setTextColor(UiStyle::DIM);
        canvas.drawString("BKSP = up / exit", 4, 56);
    } else {
        const int rowH = 13;
        const int y0 = 28;
        for (uint8_t i = 0; i < VISIBLE && (scrollOffset + i) < entries.size(); i++) {
            uint8_t idx = scrollOffset + i;
            const Entry& e = entries[idx];
            int y = y0 + (int)i * rowH;
            bool sel = (idx == selectedIndex);

            if (sel) {
                canvas.fillRect(0, y - 1, DISPLAY_W, rowH, UiStyle::PINK);
                canvas.setTextColor(UiStyle::BG);
            } else {
                canvas.setTextColor(e.isDir ? UiStyle::CYAN : UiStyle::TEXT);
            }

            char line[42];
            if (e.isDir) {
                snprintf(line, sizeof(line), "/%-22s  <dir>", e.name);
            } else {
                char sz[10];
                formatSize(e.size, sz, sizeof(sz));
                snprintf(line, sizeof(line), " %-22s %5s", e.name, sz);
            }
            // clip name field manually if needed
            canvas.drawString(line, 2, y + 2);
        }

        if (scrollOffset > 0) {
            canvas.setTextColor(UiStyle::DIM);
            canvas.drawString("^", DISPLAY_W - 10, 28);
        }
        if (scrollOffset + VISIBLE < entries.size()) {
            canvas.setTextColor(UiStyle::DIM);
            canvas.drawString("v", DISPLAY_W - 10, 28 + (VISIBLE - 1) * 13);
        }
    }

    canvas.setTextColor(UiStyle::GOLD);
    canvas.drawString(";/.  ENT  D del  T src  BKSP", 4, MAIN_H - 10);
}

void FilesMenu::draw(M5Canvas& canvas) {
    if (!active) return;
    if (phase == Phase::CONFIRM_DEL) drawConfirm(canvas);
    else drawBrowse(canvas);
}

void FilesMenu::getStatusLine(char* out, size_t len) {
    if (!out || len == 0) return;
    if (phase == Phase::CONFIRM_DEL) {
        snprintf(out, len, "DEL? ENT=yes N=no");
        return;
    }
    if (!entries.empty() && selectedIndex < entries.size()) {
        const Entry& e = entries[selectedIndex];
        if (e.isDir) {
            snprintf(out, len, "/%s  ENT=open D=del", e.name);
        } else {
            char sz[12];
            formatSize(e.size, sz, sizeof(sz));
            snprintf(out, len, "%s %s  D=del", e.name, sz);
        }
    } else {
        snprintf(out, len, "%s %s  T=source",
                 source == Source::SD_CARD ? "SD" : "INT", cwd);
    }
}

// Simple SD + internal (SPIFFS) file browser with delete
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <M5Unified.h>
#include <vector>

class FilesMenu {
public:
    static void init();
    static void show();
    static void hide();
    static void update();
    static bool isActive() { return active; }
    static void draw(M5Canvas& canvas);
    static void getStatusLine(char* out, size_t len);

private:
    enum class Source : uint8_t { SD_CARD = 0, INTERNAL = 1 };
    enum class Phase : uint8_t { BROWSE = 0, CONFIRM_DEL = 1 };

    struct Entry {
        char name[36];
        bool isDir;
        uint32_t size;  // bytes; 0 for dirs
    };

    static bool active;
    static Source source;
    static Phase phase;
    static char cwd[96];
    static std::vector<Entry> entries;
    static uint8_t selectedIndex;
    static uint8_t scrollOffset;
    static bool keyWasPressed;
    static char statusMsg[40];

    static constexpr uint8_t VISIBLE = 5;
    static constexpr size_t MAX_ENTRIES = 64;

    static fs::FS& activeFs();
    static bool sourceReady();
    static void setCwdRoot();
    static void refresh();
    static void ensureVisible();
    static void goUp();
    static void openSelected();
    static void requestDelete();
    static void confirmDelete();
    static void joinPath(char* out, size_t outLen, const char* dir, const char* name);
    static void formatSize(uint32_t bytes, char* out, size_t outLen);
    static void drawBrowse(M5Canvas& canvas);
    static void drawConfirm(M5Canvas& canvas);
    static void handleInput();
};

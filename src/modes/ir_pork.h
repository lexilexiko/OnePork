// IR PORK — onboard IR power blast + custom SD packs
// Hotkey I from IDLE / ATTACK menu.  E = pick file.  R = NA/EU region.
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class IrPorkMode {
public:
    static void init();
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }

    static void getStatusLine(char* buf, size_t n);
    static void getTopBarLabel(char* buf, size_t n);
    static void onHotkeyAgain();
    // True while blasting — HUD can stay minimal
    static bool isBlasting() { return running && phase == Phase::BLAST; }

private:
    enum class Phase : uint8_t {
        READY = 0,
        BLAST,
        FILE_PICK,
        DONE
    };

    enum class Pack : uint8_t {
        BUILTIN = 0,  // compressed NA/EU power pack
        CUSTOM  = 1   // SD file lines
    };

    enum class Proto : uint8_t { NEC = 0, SAMSUNG, SONY };

    struct Code {
        Proto proto;
        uint16_t addr;
        uint16_t cmd;
        uint8_t bits;
        char name[18];
    };

    static constexpr uint8_t MAX_CODES = 48;
    static constexpr uint8_t MAX_FILES = 24;

    static bool running;
    static Phase phase;
    static Pack pack;
    static Code codes[MAX_CODES];
    static uint8_t codeCount;
    static uint8_t blastIndex;
    static uint8_t blastTotal;
    static uint32_t nextSendMs;
    static char packName[28];
    static char statusMsg[40];

    static char fileNames[MAX_FILES][28];
    static uint8_t fileCount;
    static uint8_t fileSel;
    static uint8_t fileScroll;

    static void loadBuiltinPack();
    static bool loadFile(const char* path);
    static void scanIrFiles();
    static void startBlast();
    static void handleInput();
    static void muteAudioForIr();
};

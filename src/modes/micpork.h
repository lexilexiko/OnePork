// MicPork Mode - Cardputer mic → pig dances as living spectrometer
// No bar UI: if pig doesn't dance, mic isn't hearing.
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class MicPorkMode {
public:
    static void init();
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }

    static uint8_t getLevelPct();  // 0-100 for bottom bar
    // Kept for UI compatibility (bands drive internal dance feel)
    static uint8_t getBandCount() { return 1; }
    static uint8_t getBand(uint8_t) { return getLevelPct(); }

private:
    static constexpr size_t SAMPLE_LEN = 256;
    static constexpr uint32_t SAMPLE_RATE = 16000;
    static constexpr uint8_t BUF_COUNT = 2;

    static bool running;
    static int16_t samples[BUF_COUNT][SAMPLE_LEN];
    static uint8_t writeBuf;
    static uint8_t readBuf;
    static bool haveAudio;
    static float overallLevel;   // 0..1 smoothed
    static float noiseFloor;     // auto-calibrated
    static float peakHold;
    static uint32_t lastWaveMs;
    static uint32_t lastLogMs;
    static uint32_t sessionStart;
    static uint8_t savedSoundLevel;
    static bool micOk;
    static uint32_t recordOkCount;
    static uint32_t recordFailCount;

    static void analyzeBuffer(const int16_t* data);
    static void handleInput();
    static bool startMicHardware();
};

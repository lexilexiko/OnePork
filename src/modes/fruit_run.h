// Fruit Run — collect 1000 fruits. 3 lives. Wolves get meaner as you climb.
// ,/ walk  ; jump  SPACE attack  . sit
#pragma once

#include <Arduino.h>
#include <M5Unified.h>

class FruitRunMode {
public:
    static void init();
    static void start();
    static void stop();
    static void update();
    static void draw(M5Canvas& canvas);
    static bool isRunning() { return running; }

    static uint16_t getScore() { return score; }
    static uint16_t getBest() { return bestScore; }
    static uint16_t getFruits() { return fruitsGot; }
    static uint8_t  getLives() { return lives; }
    static uint16_t getGoal() { return GOAL; }
    // Current wolf spawn chance 0–100 (for HUD)
    static uint8_t  getWolfChancePct();
    // Status line for bottom bar
    static void getStatusLine(char* buf, size_t n);
    // Compact top-bar label
    static void getTopBarLabel(char* buf, size_t n);

private:
    static constexpr uint16_t GOAL = 1000;
    static constexpr uint8_t  MAX_LIVES = 3;

    enum class Phase : uint8_t { TITLE, PLAY, DEAD, WIN };

    static bool running;
    static Phase phase;
    static uint16_t score;
    static uint16_t bestScore;
    static uint16_t fruitsGot;
    static uint8_t  lives;
    static uint32_t lastDropMs;
    static uint32_t lastTreeMs;
    static uint32_t lastMoveMs;
    static uint32_t lastWolfRollMs;
    static uint32_t phaseStartMs;
    static bool walking;
    static bool wonOnce;

    static void resetPlay();
    static void handleInput();
    static void drawHud(M5Canvas& canvas);
    static void trySpawnWolves(uint32_t now);
};

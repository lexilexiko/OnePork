// Fruit Run — same IDLE world. Walk L/R, jump, catch fruit from growing trees.
// No obstacles / no game over — chill collect-a-thon.
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

private:
    enum class Phase : uint8_t { TITLE, PLAY };

    static bool running;
    static Phase phase;
    static uint16_t score;
    static uint16_t bestScore;
    static uint16_t fruitsGot;
    static uint32_t lastDropMs;
    static uint32_t lastTreeMs;
    static uint32_t lastMoveMs;
    static uint32_t phaseStartMs;
    static bool walking;

    static void resetPlay();
    static void handleInput();
    static void drawHud(M5Canvas& canvas);
};

// Fruit Run — free roam collect-a-thon with rising wolf pressure.
// Goal 1000 fruits · 3 lives · wolf chance scales · dual wolves late game.

#include "fruit_run.h"
#include "../piglet/avatar.h"
#include "../piglet/weather.h"
#include "../piglet/seasonal_fx.h"
#include "../piglet/wolf.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include "../core/xp.h"
#include "../core/config.h"
#include <M5Cardputer.h>
#include <string.h>
#include <esp_random.h>

bool FruitRunMode::running = false;
FruitRunMode::Phase FruitRunMode::phase = FruitRunMode::Phase::TITLE;
uint16_t FruitRunMode::score = 0;
uint16_t FruitRunMode::bestScore = 0;
uint16_t FruitRunMode::fruitsGot = 0;
uint8_t  FruitRunMode::lives = MAX_LIVES;
uint32_t FruitRunMode::lastDropMs = 0;
uint32_t FruitRunMode::lastTreeMs = 0;
uint32_t FruitRunMode::lastMoveMs = 0;
uint32_t FruitRunMode::lastWolfRollMs = 0;
uint32_t FruitRunMode::phaseStartMs = 0;
bool FruitRunMode::walking = false;
bool FruitRunMode::wonOnce = false;

// Spawn chance by fruit count (user curve):
//  <20  → 0%   (safe intro)
//  20–99 → 15%
//  100+ → 40%
//  200+ → 80%
//  300+ → 95%
// Dual wolves from 500+, always dual after goal (1000)
uint8_t FruitRunMode::getWolfChancePct() {
    if (fruitsGot < 20)  return 0;
    if (fruitsGot < 100) return 15;
    if (fruitsGot < 200) return 40;
    if (fruitsGot < 300) return 80;
    return 95;
}

void FruitRunMode::getStatusLine(char* buf, size_t n) {
    if (!buf || n == 0) return;
    if (phase == Phase::TITLE) {
        snprintf(buf, n, "G:START  GOAL:%u  3 LIVES", (unsigned)GOAL);
        return;
    }
    if (phase == Phase::DEAD) {
        snprintf(buf, n, "GAME OVER  FRUIT:%u  BEST:%u",
                 (unsigned)fruitsGot, (unsigned)(bestScore / 10));
        return;
    }
    if (phase == Phase::WIN) {
        snprintf(buf, n, "WIN! 1000  KEEP GOING  2 WOLVES");
        return;
    }
    // PLAY — full game HUD on bottom bar (no heap / net junk)
    snprintf(buf, n, "F:%u/%u  HP:%u  WOLF:%u%%",
             (unsigned)fruitsGot, (unsigned)GOAL,
             (unsigned)lives, (unsigned)getWolfChancePct());
}

void FruitRunMode::getTopBarLabel(char* buf, size_t n) {
    if (!buf || n == 0) return;
    if (phase == Phase::DEAD) {
        snprintf(buf, n, "DEAD F:%u", (unsigned)fruitsGot);
        return;
    }
    if (phase == Phase::WIN) {
        snprintf(buf, n, "WIN F:%u", (unsigned)fruitsGot);
        return;
    }
    // Hearts as digits — compact for sky bar
    snprintf(buf, n, "F:%u/%u L:%u",
             (unsigned)fruitsGot, (unsigned)GOAL, (unsigned)lives);
}

void FruitRunMode::init() {
    running = false;
    phase = Phase::TITLE;
}

void FruitRunMode::resetPlay() {
    fruitsGot = 0;
    score = 0;
    lives = MAX_LIVES;
    wonOnce = false;
    lastDropMs = millis();
    lastTreeMs = millis();
    lastMoveMs = millis();
    lastWolfRollMs = millis();
    walking = false;

    Wolf::reset();
    Wolf::setAutoSpawn(false);
    Wolf::setMaxActive(1);

    Avatar::setState(AvatarState::HAPPY);
    Avatar::setJumpTuning(26, 500);
    Avatar::setGrassMoving(false, false, true);
    Avatar::setPlayDead(false);
    Avatar::showTree(7);
    Avatar::triggerTailWiggle();
}

void FruitRunMode::start() {
    running = true;
    phase = Phase::TITLE;
    phaseStartMs = millis();
    fruitsGot = 0;
    score = 0;
    lives = MAX_LIVES;
    wonOnce = false;
    Wolf::setAutoSpawn(false);
    Wolf::reset();
    Wolf::setMaxActive(1);
    resetPlay();
    SFX::play(SFX::MODE_ENTER);
    Display::notify(NoticeKind::STATUS, "FRUIT RUN  GOAL 1000", 2500, NoticeChannel::TOP_BAR);
}

void FruitRunMode::stop() {
    // Per-fruit XP already awarded via FRUIT_PICKED on collect
    if (fruitsGot > 0 && score > bestScore) bestScore = score;
    running = false;
    phase = Phase::TITLE;
    Wolf::setAutoSpawn(true);
    Wolf::setMaxActive(1);
    Wolf::reset();
    Avatar::setGrassMoving(false, false, true);
    Avatar::resetJumpTuning();
    Avatar::hideTree();
    Avatar::setPlayDead(false);
    Avatar::setState(AvatarState::NEUTRAL);
}

void FruitRunMode::handleInput() {
    bool left  = M5Cardputer.Keyboard.isKeyPressed(',');
    bool right = M5Cardputer.Keyboard.isKeyPressed('/');
    bool jumpKey = M5Cardputer.Keyboard.isKeyPressed(';');
    bool attackKey = M5Cardputer.Keyboard.isKeyPressed(' ');
    bool sitKey = M5Cardputer.Keyboard.isKeyPressed('.');

    static bool jumpWas = false;
    static bool attackWas = false;
    static bool startWas = false;
    bool jumpEdge = jumpKey && !jumpWas;
    bool attackEdge = attackKey && !attackWas;
    bool startEdge = (jumpKey || attackKey || left || right || sitKey) && !startWas;
    jumpWas = jumpKey;
    attackWas = attackKey;
    startWas = jumpKey || attackKey || left || right || sitKey;

    if (phase == Phase::TITLE || phase == Phase::DEAD || phase == Phase::WIN) {
        if (startEdge) {
            if (phase == Phase::WIN) {
                // Continue hard mode after win
                phase = Phase::PLAY;
                phaseStartMs = millis();
                Wolf::setMaxActive(2);
                Avatar::setState(AvatarState::EXCITED);
                Display::notify(NoticeKind::STATUS, "HARD MODE  2 WOLVES!", 2000, NoticeChannel::TOP_BAR);
            } else {
                resetPlay();
                phase = Phase::PLAY;
                phaseStartMs = millis();
                Avatar::setState(AvatarState::EXCITED);
                SFX::play(SFX::MODE_ENTER);
            }
        }
        return;
    }

    if (!Avatar::isPlayDead()) {
        Avatar::setSitting(sitKey && !left && !right && !jumpKey && !attackKey);
    }

    uint32_t now = millis();
    uint32_t tickMs = 18;
    if (now - lastMoveMs >= tickMs) {
        lastMoveMs = now;
        if (left && !right && !Avatar::isPlayDead()) {
            Avatar::playerWalkHold(-1);
            walking = true;
        } else if (right && !left && !Avatar::isPlayDead()) {
            Avatar::playerWalkHold(+1);
            walking = true;
        } else {
            Avatar::playerWalkHold(0);
            walking = false;
        }
    }

    if (jumpEdge && !Avatar::isJumping() && !Avatar::isAttackHopping()) {
        Avatar::setPlayDead(false);
        Avatar::setSitting(false);
        Avatar::cuteJump();
        Avatar::setState(AvatarState::EXCITED);
    }

    if (attackEdge && !Avatar::isAttackHopping() && !Avatar::isJumping()) {
        Avatar::setPlayDead(false);
        Avatar::setSitting(false);
        Avatar::attackHop();
        Avatar::setState(AvatarState::HUNTING);
    }
}

void FruitRunMode::trySpawnWolves(uint32_t now) {
    uint8_t chance = getWolfChancePct();
    if (chance == 0) return;

    // Dual wolves after 500, or always after beating goal
    bool dual = (fruitsGot >= 500) || wonOnce;
    Wolf::setMaxActive(dual ? 2 : 1);

    // Roll every ~3.5s when under max capacity
    if (now - lastWolfRollMs < 3500) return;
    lastWolfRollMs = now;

    uint8_t active = Wolf::getActiveCount();
    uint8_t cap = dual ? 2 : 1;
    if (active >= cap) return;

    // Probability roll
    if ((esp_random() % 100) >= chance) return;

    if (active == 0) {
        Wolf::spawnNow();
        // Late game: sometimes immediately second
        if (dual && (esp_random() % 100) < 50)
            Wolf::spawnSecond();
    } else if (dual) {
        Wolf::spawnSecond();
    }
}

void FruitRunMode::update() {
    if (!running) return;

    handleInput();
    if (phase != Phase::PLAY) return;

    uint32_t now = millis();

    // Keep ambient wolf off — we own spawns
    Wolf::setAutoSpawn(false);

    // Spawn tree when none
    if (!Avatar::isTreeVisible() && (now - lastTreeMs > 2200)) {
        Avatar::showTree((uint8_t)random(5, 9));
        lastTreeMs = now;
        lastDropMs = now + 600;
    }

    if (Avatar::isTreeVisible() && now - lastDropMs > 1000) {
        lastDropMs = now;
        Avatar::dropFruit();
        if (!Avatar::isTreeVisible()) lastTreeMs = now;
    }

    if (Avatar::tryStompTree()) {
        Display::notify(NoticeKind::STATUS, "STOMP!", 400, NoticeChannel::TOP_BAR);
    }

    // Jump / attack near wolf → scare
    if (Wolf::isActive()) {
        bool airborne = Avatar::isJumping() || Avatar::isAttackHopping();
        if (airborne) {
            int feetX = Avatar::getCurrentX() + 14 * 3;
            uint8_t before = Wolf::getActiveCount();
            Wolf::scareNear(feetX, 36);
            if (Wolf::getActiveCount() < before) {
                Avatar::triggerSparkles(5);
                Avatar::setState(AvatarState::HAPPY);
            }
        }
    }

    // Wolf bite → lose a life
    if (Wolf::consumeBiteEvent()) {
        if (lives > 0) lives--;
        SFX::play(SFX::OINK_SQUEAL);
        if (lives == 0) {
            phase = Phase::DEAD;
            phaseStartMs = now;
            if (score > bestScore) bestScore = score;
            Wolf::reset();
            Avatar::setState(AvatarState::SAD);
            SFX::play(SFX::YOU_DIED);
            Display::notify(NoticeKind::STATUS, "GAME OVER", 2500, NoticeChannel::TOP_BAR);
            return;
        }
        char msg[24];
        snprintf(msg, sizeof(msg), "BITE! LIVES %u", (unsigned)lives);
        Display::notify(NoticeKind::STATUS, msg, 1200, NoticeChannel::TOP_BAR);
        Avatar::setState(AvatarState::SAD);
    }

    // Rising wolf pressure
    trySpawnWolves(now);

    // Collect fruit
    const int PX = 3;
    int feetX = Avatar::getCurrentX() + 14 * PX;
    int lift = Avatar::getJumpLiftPx();
    int feetY = 106 - lift;
    int rad = (Avatar::isJumping() || Avatar::isAttackHopping()) ? 32 : 26;
    if (Avatar::tryCollectNearbyFruit(feetX, feetY, rad)) {
        fruitsGot++;
        score = fruitsGot * 10;
        if (score > bestScore) bestScore = score;
        Avatar::triggerSparkles(6);
        Avatar::triggerTailWiggle();
        Avatar::setState(AvatarState::HAPPY);
        SFX::play(SFX::MODE_ENTER);

        // Milestone pings
        if (fruitsGot == 20) {
            Display::notify(NoticeKind::STATUS, "WOLVES HUNTING!", 1500, NoticeChannel::TOP_BAR);
        } else if (fruitsGot == 100 || fruitsGot == 200 || fruitsGot == 500) {
            char m[28];
            snprintf(m, sizeof(m), "F:%u  WOLF %u%%", (unsigned)fruitsGot,
                     (unsigned)getWolfChancePct());
            Display::notify(NoticeKind::STATUS, m, 1200, NoticeChannel::TOP_BAR);
        }

        // Goal reached
        if (!wonOnce && fruitsGot >= GOAL) {
            wonOnce = true;
            phase = Phase::WIN;
            phaseStartMs = now;
            Wolf::setMaxActive(2);
            Avatar::setState(AvatarState::EXCITED);
            Avatar::triggerSparkles(12);
            SFX::play(SFX::ACHIEVEMENT);
            Display::notify(NoticeKind::STATUS, "1000! YOU WIN!", 3000, NoticeChannel::TOP_BAR);
            XP::addXPSilent(50);
        } else if ((fruitsGot % 25) == 0) {
            Avatar::setState(AvatarState::EXCITED);
        }
    }
}

void FruitRunMode::drawHud(M5Canvas& canvas) {
    // Main-canvas overlay only for title / end screens.
    // Live stats live on top+bottom bars so they stay readable.
    if (phase == Phase::TITLE) {
        canvas.fillRoundRect(20, 18, 200, 78, 4, 0x2104);
        canvas.drawRoundRect(20, 18, 200, 78, 4, 0xFDB5);
        canvas.setTextSize(1);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(0xFDB5);
        canvas.drawString("FRUIT RUN", 120, 24);
        canvas.setTextColor(0xFFFF);
        canvas.drawString("GOAL: 1000 FRUITS", 120, 38);
        canvas.drawString("3 LIVES  WOLF BITES KILL", 120, 50);
        canvas.drawString(",/ MOVE ; JUMP SPC ATK", 120, 62);
        canvas.setTextColor(0xFFE0);
        canvas.drawString("PRESS KEY TO START", 120, 76);
        return;
    }
    if (phase == Phase::DEAD) {
        canvas.fillRoundRect(28, 28, 184, 52, 4, 0x2104);
        canvas.drawRoundRect(28, 28, 184, 52, 4, 0xF800);
        canvas.setTextSize(1);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(0xF800);
        canvas.drawString("GAME OVER", 120, 34);
        canvas.setTextColor(0xFFFF);
        char b[40];
        snprintf(b, sizeof(b), "FRUIT %u   BEST %u",
                 (unsigned)fruitsGot, (unsigned)(bestScore / 10));
        canvas.drawString(b, 120, 48);
        canvas.setTextColor(0xAD55);
        canvas.drawString("KEY = RETRY", 120, 62);
        return;
    }
    if (phase == Phase::WIN) {
        canvas.fillRoundRect(28, 28, 184, 52, 4, 0x2104);
        canvas.drawRoundRect(28, 28, 184, 52, 4, 0xFFE0);
        canvas.setTextSize(1);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(0xFFE0);
        canvas.drawString("1000 FRUITS!", 120, 34);
        canvas.setTextColor(0xFFFF);
        canvas.drawString("HARD MODE: 2 WOLVES", 120, 48);
        canvas.setTextColor(0xAD55);
        canvas.drawString("KEY = CONTINUE", 120, 62);
        return;
    }

    // PLAY: tiny on-screen hearts (backup if bars busy)
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    for (uint8_t i = 0; i < MAX_LIVES; i++) {
        uint16_t c = (i < lives) ? 0xF800 : 0x4208;
        canvas.fillRect(4 + (int)i * 10, 2, 8, 7, c);
    }
}

void FruitRunMode::draw(M5Canvas& canvas) {
    Avatar::draw(canvas);
    Wolf::draw(canvas);
    Weather::drawBirds(canvas, 0xFFFF);
    Weather::draw(canvas, 0xFFFF, 0x0000);
    SeasonalFx::draw(canvas);
    drawHud(canvas);
}

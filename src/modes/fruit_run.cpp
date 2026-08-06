// Fruit Run — free roam in the real avatar world.
// ,/ walk   ; jump   SPACE attack-hop   . sit
// Stomp tree 3× to break it; jump on wolf to scare it; catch fruit.

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

bool FruitRunMode::running = false;
FruitRunMode::Phase FruitRunMode::phase = FruitRunMode::Phase::TITLE;
uint16_t FruitRunMode::score = 0;
uint16_t FruitRunMode::bestScore = 0;
uint16_t FruitRunMode::fruitsGot = 0;
uint32_t FruitRunMode::lastDropMs = 0;
uint32_t FruitRunMode::lastTreeMs = 0;
uint32_t FruitRunMode::lastMoveMs = 0;
uint32_t FruitRunMode::phaseStartMs = 0;
bool FruitRunMode::walking = false;

void FruitRunMode::init() {
    running = false;
    phase = Phase::TITLE;
}

void FruitRunMode::resetPlay() {
    score = fruitsGot;
    lastDropMs = millis();
    lastTreeMs = millis();
    lastMoveMs = millis();
    walking = false;

    Avatar::setState(AvatarState::HAPPY);
    Avatar::setJumpTuning(26, 500);
    Avatar::setGrassMoving(false, false, true);
    Avatar::showTree(7);
    Avatar::triggerTailWiggle();
}

void FruitRunMode::start() {
    running = true;
    phase = Phase::TITLE;
    phaseStartMs = millis();
    fruitsGot = 0;
    score = 0;
    resetPlay();
    SFX::play(SFX::MODE_ENTER);
    Display::notify(NoticeKind::STATUS, ",/ MOVE  ; JUMP  SPC ATK", 2800, NoticeChannel::TOP_BAR);
}

void FruitRunMode::stop() {
    if (fruitsGot > 0) {
        if (score > bestScore) bestScore = score;
        XP::addXPSilent((uint16_t)(fruitsGot > 15 ? 15 : fruitsGot));
    }
    running = false;
    phase = Phase::TITLE;
    Avatar::setGrassMoving(false, false, true);
    Avatar::resetJumpTuning();
    Avatar::hideTree();
    Avatar::setState(AvatarState::NEUTRAL);
}

void FruitRunMode::handleInput() {
    // Same arrow cluster as IDLE + space = attack hop
    //   , left    / right    ; jump    . sit    SPACE attack
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

    if (phase == Phase::TITLE) {
        if (startEdge) {
            resetPlay();
            phase = Phase::PLAY;
            phaseStartMs = millis();
            Avatar::setState(AvatarState::EXCITED);
            SFX::play(SFX::MODE_ENTER);
        }
        return;
    }

    if (!Avatar::isPlayDead()) {
        Avatar::setSitting(sitKey && !left && !right && !jumpKey && !attackKey);
    }

    uint32_t now = millis();
    // Fixed walk tick; world scroll uses SCENE → SCROLL SPD
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

    // ; = normal jump
    if (jumpEdge && !Avatar::isJumping() && !Avatar::isAttackHopping()) {
        Avatar::setPlayDead(false);
        Avatar::setSitting(false);
        Avatar::cuteJump();
        Avatar::setState(AvatarState::EXCITED);
    }

    // SPACE = attack hop (pounce)
    if (attackEdge && !Avatar::isAttackHopping() && !Avatar::isJumping()) {
        Avatar::setPlayDead(false);
        Avatar::setSitting(false);
        Avatar::attackHop();
        Avatar::setState(AvatarState::HUNTING);
    }
}

void FruitRunMode::update() {
    if (!running) return;

    handleInput();
    if (phase != Phase::PLAY) return;

    uint32_t now = millis();

    // Spawn tree when none (with fruit that will drop)
    if (!Avatar::isTreeVisible() && (now - lastTreeMs > 2200)) {
        Avatar::showTree((uint8_t)random(5, 9));
        lastTreeMs = now;
        lastDropMs = now + 600;  // start dropping soon after grow
    }

    // Living trees always rain fruit periodically
    if (Avatar::isTreeVisible() && now - lastDropMs > 1000) {
        lastDropMs = now;
        Avatar::dropFruit();
        if (!Avatar::isTreeVisible()) {
            lastTreeMs = now;
        }
    }

    // Stomp tree while airborne (jump or attack hop) — 3 hits breaks it
    if (Avatar::tryStompTree()) {
        // feedback already in tryStompTree; bonus score nudge
        Display::notify(NoticeKind::STATUS, "STOMP!", 500, NoticeChannel::TOP_BAR);
    }

    // Jump / attack on wolf → it flees
    if (Wolf::isActive()) {
        bool airborne = Avatar::isJumping() || Avatar::isAttackHopping();
        if (airborne) {
            int feetX = Avatar::getCurrentX() + 14 * 3;
            int wx = Wolf::getX();
            int dist = feetX - wx;
            if (dist < 0) dist = -dist;
            if (dist < 36) {
                Wolf::scareAway();
                Avatar::triggerSparkles(5);
                Avatar::setState(AvatarState::HAPPY);
                Display::notify(NoticeKind::STATUS, "WOLF FLED!", 900, NoticeChannel::TOP_BAR);
            }
        }
    }

    // Collect fallen / falling fruit near pig
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
        if ((fruitsGot % 5) == 0) {
            Avatar::setState(AvatarState::EXCITED);
            Display::notify(NoticeKind::STATUS, "YUM FRUIT!", 900, NoticeChannel::TOP_BAR);
        }
    }
}

void FruitRunMode::drawHud(M5Canvas& canvas) {
    canvas.setTextSize(1);
    canvas.setTextDatum(top_left);
    char buf[48];
    snprintf(buf, sizeof(buf), "FRUIT %u", (unsigned)fruitsGot);
    canvas.setTextColor(0x2104);
    canvas.drawString(buf, 5, 3);
    canvas.setTextColor(0xFFFF);
    canvas.drawString(buf, 4, 2);
    if (bestScore > 0) {
        snprintf(buf, sizeof(buf), "BEST %u", (unsigned)(bestScore / 10));
        canvas.setTextColor(0x2104);
        canvas.drawString(buf, 175, 3);
        canvas.setTextColor(0xFFE0);
        canvas.drawString(buf, 174, 2);
    }

    if (phase == Phase::TITLE) {
        canvas.fillRoundRect(28, 22, 184, 64, 4, 0x2104);
        canvas.drawRoundRect(28, 22, 184, 64, 4, 0xFDB5);
        canvas.setTextDatum(top_center);
        canvas.setTextColor(0xFDB5);
        canvas.drawString("FRUIT RUN", 120, 28);
        canvas.setTextColor(0xFFFF);
        canvas.drawString(",/ MOVE  ; JUMP  . SIT", 120, 42);
        canvas.drawString("SPC ATTACK  3x STOMP TREE", 120, 54);
        canvas.drawString("JUMP WOLF TO SCARE!", 120, 66);
    } else {
        canvas.setTextDatum(top_right);
        canvas.setTextColor(0xAD55);
        canvas.drawString("; JUMP SPC ATK", 236, 2);
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

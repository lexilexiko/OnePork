// Full-color chibi pixel pig (uint8 map matching classic pink piglet art)

#include "avatar.h"
#include "weather.h"
#include "seasonal_fx.h"
#include "trees.h"
#include "wolf.h"
#include "../ui/display.h"
#include "../core/config.h"
#include "../audio/sfx.h"
#include "../modes/fruit_run.h"
#include <time.h>
#include <math.h>

// Camera rails (~33% from each edge of roam range)
static constexpr int kCamXMin = 4;
static constexpr int kCamXMax = 155;
static constexpr int kCamLeftLine  = kCamXMin + (kCamXMax - kCamXMin) / 3;   // ~54
static constexpr int kCamRightLine = kCamXMax - (kCamXMax - kCamXMin) / 3;  // ~105

// Pig step on screen (fixed, comfortable) — not tied to scroll setting
static constexpr int kWalkStepPx = 4;

static int clampScrollSpd() {
    int s = (int)Config::personality().scrollSpeed;
    if (s < 1) s = 1;
    if (s > 10) s = 10;
    return s;
}

// World scroll only: lower ms = faster grass/trees at camera rails
static uint16_t scrollGrassSpeedMs() {
    // 1 → ~48ms/px (slow), 10 → ~6ms/px (very fast)
    int ms = 52 - clampScrollSpd() * 5;
    if (ms < 6) ms = 6;
    return (uint16_t)ms;
}

// Extra tree/grass scroll steps per update at high scroll speeds (1 at low, up to 3)
static int scrollStepsPerTick() {
    int s = clampScrollSpd();
    if (s >= 9) return 3;
    if (s >= 6) return 2;
    return 1;
}

// Static members
AvatarState Avatar::currentState = AvatarState::NEUTRAL;
bool Avatar::isBlinking = false;
bool Avatar::earsUp = true;
uint32_t Avatar::lastBlinkTime = 0;
uint32_t Avatar::blinkInterval = 3000;
int Avatar::moodIntensity = 0;  // Phase 8: -100 to 100

// Cute jump state
bool Avatar::jumpActive = false;
uint32_t Avatar::jumpStartTime = 0;
uint16_t Avatar::jumpDurationMs = 500;  // air time after crouch
int Avatar::jumpHeightPx = 16;          // higher hop (was 8)
// Brief crouch before springing up
static constexpr uint16_t JUMP_CROUCH_MS = 100;

// Attack hop state (multi-hop pounce for captures)
bool Avatar::attackHopActive = false;
uint32_t Avatar::attackHopStartTime = 0;
uint8_t Avatar::attackHopIndex = 0;
uint8_t Avatar::attackHopTotal = 0;
int16_t Avatar::attackHopOriginX = 0;
int16_t Avatar::attackHopTargets[5] = {0};
// Locked facing for whole attack hop (left hop was flipping to right at rail)
static bool s_attackHopFaceRight = true;

// Walk transition state
bool Avatar::transitioning = false;
uint32_t Avatar::transitionStartTime = 0;
int Avatar::transitionFromX = 2;
int Avatar::transitionToX = 2;
bool Avatar::transitionToFacingRight = true;
int Avatar::currentX = 2;

// Sniff animation state
bool Avatar::isSniffing = false;
static uint32_t sniffStartTime = 0;
static const uint32_t SNIFF_DURATION_MS = 600;  // 600ms for proper sniff cycle
static uint8_t sniffFrame = 0;  // Alternates between nose shapes (oo, oO, Oo)

// Walk transition timing
static const uint32_t TRANSITION_DURATION_MS = 1200;  // 1.2s slow relaxed walk (was 400ms - too hectic)

// Rest cooldown after grass stops - prevents immediate re-triggering
static uint32_t lastGrassStopTime = 0;
static const uint32_t GRASS_REST_COOLDOWN_MS = 3000;  // 3 second chill period after grass stops

// Ear twitch micro-animation state
bool Avatar::earTwitchActive = false;
uint32_t Avatar::earTwitchStart = 0;
uint32_t Avatar::nextEarTwitch = 0;

// Sparkle particle pool
Avatar::SparkleParticle Avatar::sparkles[MAX_SPARKLES] = {};

// Event reaction animation states
bool Avatar::perkUpActive = false;
uint32_t Avatar::perkUpStart = 0;
bool Avatar::flinchActive = false;
uint32_t Avatar::flinchStart = 0;
bool Avatar::spinActive = false;
uint32_t Avatar::spinStart = 0;
bool Avatar::pawScratchActive = false;
uint32_t Avatar::pawScratchStart = 0;
bool Avatar::tailWiggleActive = false;
uint32_t Avatar::tailWiggleStart = 0;

// Grass wander state (random roaming toward center while treadmill runs)
static uint32_t grassWanderTimer = 0;
static uint32_t grassWanderInterval = 4000;

// Attack shake state (visual feedback for captures)
static bool attackShakeActive = false;
static bool attackShakeStrong = false;
static uint32_t attackShakeRefreshTime = 0;

// Thunder flash state (weather effect - invert colors)
static bool thunderFlashActive = false;

// Live top sky color for top bar + blends
static constexpr uint16_t C_SKY_TOP = 0x3C3A;
static uint16_t s_skyTop = C_SKY_TOP;

// Smooth day ↔ dusk ↔ night (0 = day, 256 = night)
static uint16_t s_nightBlend = 0;
static uint32_t s_lastNightBlendMs = 0;
static uint16_t s_cachedNightTarget = 0;
static uint32_t s_lastNightTargetMs = 0;
static constexpr uint32_t NIGHT_BLEND_FULL_MS = 12000;

// Smooth clear → rain → storm sky darkening (0 clear, 160 rain, 256 storm)
static uint16_t s_weatherDark = 0;
static uint32_t s_lastWeatherDarkMs = 0;
static constexpr uint32_t WEATHER_DARK_FULL_MS = 4500;

// Scene palette (tree trunk/leaf/fruit colors live in trees.cpp now)
static constexpr uint16_t C_FRUIT   = 0xF800;
static constexpr uint16_t C_STAR    = 0xFFF0;
static constexpr uint16_t C_DUST    = 0xBDF3;
static constexpr uint16_t C_SPARK   = 0xFFE0;

uint16_t Avatar::getSkyColor() { return s_skyTop; }

static inline uint16_t maybeFlash(uint16_t c) {
    if (!thunderFlashActive) return c;
    uint16_t r = ((c >> 11) + 31) >> 1;
    uint16_t g = (((c >> 5) & 0x3F) + 63) >> 1;
    uint16_t b = ((c & 0x1F) + 31) >> 1;
    return (r << 11) | (g << 5) | b;
}

// Night sky star system state
Avatar::Star Avatar::stars[15] = {{0}};
uint8_t Avatar::starCount = 0;
uint32_t Avatar::lastStarSpawn = 0;
uint32_t Avatar::nextSpawnDelay = 2000;
bool Avatar::starsActive = false;

// Wave ripple state
WaveMode Avatar::waveMode = WaveMode::NONE;
uint32_t Avatar::waveBurstStart = 0;
uint32_t Avatar::waveBurstEnd = 0;
float Avatar::micDanceLevel = 0.0f;
static uint8_t waveIntensity = 3;  // 1-5 rings for INCOMING

// Legacy tree statics (kept for header linkage; logic lives in Trees module)
TreePhase Avatar::treePhase = TreePhase::HIDDEN;
float Avatar::treeGrowth = 0.0f;
uint32_t Avatar::treeAnimStart = 0;
Avatar::TreeTrunk Avatar::treeTrunk = {};
Avatar::TreeBranch Avatar::treeBranches[MAX_BRANCHES] = {};
uint8_t Avatar::treeBranchCount = 0;
Avatar::TreeLeafCluster Avatar::treeLeaves[MAX_LEAF_CLUSTERS] = {};
uint8_t Avatar::treeLeafCount = 0;
uint8_t Avatar::treeEndpointLeafCount = 0;
Avatar::TreeFruit Avatar::treeFruits[MAX_TREE_FRUITS] = {};
uint8_t Avatar::treeFruitCount = 0;
uint32_t Avatar::treeSeed = 0;
bool Avatar::treePendingHide = false;
bool Avatar::treePendingShow = false;
uint8_t Avatar::treePendingFruits = 0;
uint32_t Avatar::treeAliveStart = 0;
int16_t Avatar::treeScrollOffset = 0;

// Tree-pig collision → cool BONK (only FRUIT tree pushes; see Trees::checkFruitPush)
static bool treeColliding = false;
static bool wasTreeColliding = false;
static int8_t treeCollisionShake = 0;
// Bonk phases: 0 idle, 1 squash-in, 2 bounce-away, 3 dizzy settle
static uint8_t  treeBonkPhase = 0;
static uint32_t treeBonkStart = 0;
static int8_t   treeBonkDir = 1;   // +1 = tree is to the right of pig
static int16_t  treeBonkOriginX = 0;
static constexpr uint16_t BONK_SQUASH_MS = 120;
static constexpr uint16_t BONK_BOUNCE_MS = 220;
static constexpr uint16_t BONK_DIZZY_MS  = 350;

// Fruit splash particles (legacy draw path; Trees owns real drops now)
struct FruitSplash {
    float x, y;
    float vx, vy;
    uint8_t size;       // 1-3px radius
    uint32_t spawnTime;
    bool active;
};
static constexpr uint8_t FRUIT_SPLASH_COUNT = 8;
static FruitSplash fruitSplashes[FRUIT_SPLASH_COUNT] = {{0}};
static uint8_t fruitSplashIdx = 0;

// Fat pixel size = font scale factor (text size 3 = 3x3 blocks)
static constexpr int16_t PX = 3;

static inline int16_t snapPx(int16_t v) {
    return (v >= 0) ? (v / PX) * PX : ((v - 2) / PX) * PX;
}

// Iterative edge reflection — folds off-screen coordinates back on-screen.
// Returns reflected coordinate; increments `bounces` for each fold.
static inline int16_t reflectAxis(int16_t v, int16_t hi, uint8_t& bounces) {
    for (uint8_t i = 0; i < 4; i++) {
        if (v >= 0 && v <= hi) return v;
        if (v < 0) { v = -v;           bounces++; }
        else       { v = hi + hi - v;   bounces++; }
    }
    return (v < 0) ? 0 : (v > hi) ? hi : v;  // safety clamp
}

// Bresenham line on PX grid — stamps PX*PX blocks
static void fatLine(M5Canvas& canvas, int16_t x1, int16_t y1,
                    int16_t x2, int16_t y2, uint16_t color) {
    int gx1 = x1 / PX, gy1 = y1 / PX;
    int gx2 = x2 / PX, gy2 = y2 / PX;
    int dx = abs(gx2 - gx1), dy = abs(gy2 - gy1);
    int sx = (gx1 < gx2) ? 1 : -1, sy = (gy1 < gy2) ? 1 : -1;
    int err = dx - dy;
    while (true) {
        canvas.fillRect(gx1 * PX, gy1 * PX, PX, PX, color);
        if (gx1 == gx2 && gy1 == gy2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; gx1 += sx; }
        if (e2 < dx)  { err += dx; gy1 += sy; }
    }
}

// 1–2px Bresenham grass stroke (thickness mid-way: readable but not fat 3px blocks)
static void grassStroke(M5Canvas& canvas, int16_t x1, int16_t y1,
                        int16_t x2, int16_t y2, uint16_t color, uint8_t thick) {
    int dx = abs((int)x2 - (int)x1), dy = abs((int)y2 - (int)y1);
    int sx = (x1 < x2) ? 1 : -1, sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int x = x1, y = y1;
    for (;;) {
        if (x >= 0 && x < 240 && y >= 0 && y < 135) {
            canvas.drawPixel(x, y, color);
            if (thick >= 2 && x + 1 < 240) canvas.drawPixel(x + 1, y, color);
        }
        if (x == x2 && y == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx)  { err += dx; y += sy; }
    }
}

// Tapered stem: thick root/mid → thinner bright tip (thick 1..2 from grass style)
static void drawGrassStem(M5Canvas& canvas, int16_t bx, int16_t by,
                          int16_t tipX, int16_t tipY, uint16_t colBase,
                          uint16_t colMid, uint16_t colTip, uint8_t thick) {
    if (thick < 1) thick = 1;
    if (thick > 2) thick = 2;
    int16_t midX = (int16_t)(bx + (tipX - bx) / 2);
    int16_t midY = (int16_t)(by + (tipY - by) / 2);
    int16_t q3x  = (int16_t)(midX + (tipX - midX) / 2);
    int16_t q3y  = (int16_t)(midY + (tipY - midY) / 2);
    grassStroke(canvas, bx, by, midX, midY, colBase, thick);
    grassStroke(canvas, midX, midY, q3x, q3y, colMid, thick);
    grassStroke(canvas, q3x, q3y, tipX, tipY, colTip, (thick >= 2) ? 1 : 1);
    if (tipX >= 0 && tipX < 240 && tipY >= 0 && tipY < 135) {
        canvas.drawPixel(tipX, tipY, colTip);
        if (tipY > 0) canvas.drawPixel(tipX, tipY - 1, colTip);
    }
}

// Color helper for thunder flash inversion (matches Sirloin)
static uint16_t getDrawColor() {
    if (thunderFlashActive) {
        return getColorBG();  // Swap: draw with BG color during flash
    }
    return getColorFG();
}

static uint16_t getBGColor() {
    if (thunderFlashActive) {
        return getColorFG();  // Inverted while flashing
    }
    return getColorBG();
}

// Pig is drawn procedurally in drawPixelPigDetailed (no sprite sheet).
// Layout size in cells (trail / sparkle helpers) — matches old side pig span.
static constexpr int16_t PIG_LAYOUT_W = 34;
static constexpr int16_t PIG_LAYOUT_H = 18;

static bool s_walkKick = false;
static bool s_manualWalk = false;
static uint32_t s_manualWalkUntil = 0;
// Player free-roam world scroll (grass/trees) without parking pig at treadmill edges
static bool s_playerWalkScroll = false;
static bool s_sitting = false;
static bool s_playDead = false;
// Smooth pose blends (0..256) — avoid hard snaps that read as teleports
static uint16_t s_sitBlend = 0;
static uint16_t s_deadBlend = 0;
static constexpr uint16_t POSE_BLEND_STEP = 28;  // ~9 frames 0→256

// --- Smooth day / dusk / night ------------------------------------------------
static inline uint16_t lerp565(uint16_t a, uint16_t b, uint8_t num /*0..16*/) {
    int r0 = (a >> 11) & 0x1F, g0 = (a >> 5) & 0x3F, b0 = a & 0x1F;
    int r1 = (b >> 11) & 0x1F, g1 = (b >> 5) & 0x3F, b1 = b & 0x1F;
    int r = r0 + ((r1 - r0) * (int)num) / 16;
    int g = g0 + ((g1 - g0) * (int)num) / 16;
    int bb = b0 + ((b1 - b0) * (int)num) / 16;
    return (uint16_t)((r << 11) | (g << 5) | bb);
}

// Blend day → dusk (0..128) then dusk → night (128..256)
static uint16_t blendDayDuskNight(uint16_t dayC, uint16_t duskC, uint16_t nightC, uint16_t amt) {
    if (amt >= 256) return nightC;
    if (amt == 0) return dayC;
    if (amt <= 128) {
        return lerp565(dayC, duskC, (uint8_t)((amt * 16) / 128));
    }
    return lerp565(duskC, nightC, (uint8_t)(((amt - 128) * 16) / 128));
}

// Minutes-of-day → night amount with real dusk/dawn windows
// Dawn 05:00–07:00, day 07–18, dusk 18:00–20:00, night 20–05
static uint16_t nightAmountFromMinutes(int mins) {
    if (mins < 0) mins = 0;
    if (mins >= 24 * 60) mins %= (24 * 60);
    // Dawn 5:00–7:00 → 256→0
    if (mins >= 5 * 60 && mins < 7 * 60) {
        return (uint16_t)(256 - ((mins - 5 * 60) * 256) / 120);
    }
    // Day 7:00–18:00
    if (mins >= 7 * 60 && mins < 18 * 60) return 0;
    // Dusk 18:00–20:00 → 0→256
    if (mins >= 18 * 60 && mins < 20 * 60) {
        return (uint16_t)(((mins - 18 * 60) * 256) / 120);
    }
    // Night 20:00–05:00
    return 256;
}

// Synthetic 6-min cycle: day → dusk → night → dawn (no clock)
// 0–200s day, 200–240 dusk, 240–320 night, 320–360 dawn
static uint16_t nightAmountFromSynthetic(uint32_t secInCycle) {
    uint32_t sec = secInCycle % 360u;
    if (sec < 200u) return 0;
    if (sec < 240u) return (uint16_t)(((sec - 200u) * 256u) / 40u);
    if (sec < 320u) return 256;
    return (uint16_t)(256u - ((sec - 320u) * 256u) / 40u);
}

static uint16_t computeNightTarget(uint32_t now) {
    uint8_t sky = Config::personality().skyMode;
    if (sky == (uint8_t)SkyMode::DAY) return 0;
    if (sky == (uint8_t)SkyMode::NIGHT) return 256;

    // Cache target ~1s (RTC / time reads)
    if (s_lastNightTargetMs != 0 && (now - s_lastNightTargetMs) < 1000u) {
        return s_cachedNightTarget;
    }
    s_lastNightTargetMs = now;

    auto dt = M5.Rtc.getDateTime();
    if (dt.date.year >= 2024) {
        int mins = (int)dt.time.hours * 60 + (int)dt.time.minutes;
        s_cachedNightTarget = nightAmountFromMinutes(mins);
        return s_cachedNightTarget;
    }

    time_t unixNow = time(nullptr);
    if (unixNow >= 1700000000) {
        struct tm timeinfo;
        localtime_r(&unixNow, &timeinfo);
        int mins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
        s_cachedNightTarget = nightAmountFromMinutes(mins);
        return s_cachedNightTarget;
    }

    // No clock: living day/night cycle with dusk/dawn ramps
    s_cachedNightTarget = nightAmountFromSynthetic(now / 1000u);
    return s_cachedNightTarget;
}

static void approachU16(uint16_t& value, uint16_t target, uint32_t dt, uint32_t fullMs) {
    if (dt == 0) return;
    if (dt > 80u) dt = 80u;
    uint16_t step = (uint16_t)((dt * 256u) / fullMs);
    if (step < 1) step = 1;
    if (value < target) {
        uint16_t next = (uint16_t)(value + step);
        value = (next > target) ? target : next;
    } else if (value > target) {
        value = (value > step) ? (uint16_t)(value - step) : 0;
        if (value < target) value = target;
    }
}

static void updateNightBlend(uint32_t now) {
    uint16_t target = computeNightTarget(now);
    if (s_lastNightBlendMs == 0) {
        s_nightBlend = target;
        s_lastNightBlendMs = now;
        return;
    }
    uint32_t dt = now - s_lastNightBlendMs;
    s_lastNightBlendMs = now;
    approachU16(s_nightBlend, target, dt, NIGHT_BLEND_FULL_MS);
}

// Smooth sky darkening when rain/storm starts (not hard snap)
static void updateWeatherDark(uint32_t now) {
    uint16_t target = 0;
    if (Weather::isStorming()) target = 256;
    else if (Weather::isRaining()) target = 160;
    if (s_lastWeatherDarkMs == 0) {
        s_weatherDark = target;
        s_lastWeatherDarkMs = now;
        return;
    }
    uint32_t dt = now - s_lastWeatherDarkMs;
    s_lastWeatherDarkMs = now;
    approachU16(s_weatherDark, target, dt, WEATHER_DARK_FULL_MS);
}

// Living sky: day/dusk/night multi-stop gradient + Bayer dither
// Smooth night blend + smooth rain/storm darkening (no hard snap)
static void drawSkyBackdrop(M5Canvas& canvas) {
    uint32_t now = millis();
    updateNightBlend(now);
    updateWeatherDark(now);
    const uint16_t nb = s_nightBlend;  // 0 day .. 256 night
    const uint16_t wd = s_weatherDark; // 0 clear .. 160 rain .. 256 storm
    const bool raining = Weather::isRaining();
    const bool storm = Weather::isStorming();
    (void)storm;

    // Clear / rain / storm palettes × day-dusk-night, then cross-fade by wd
    // Clear
    const uint16_t CD0 = 0x3C3A, CD1 = 0x54BB, CD2 = 0x6D3C, CD3 = 0x8DDC;
    const uint16_t CK0 = 0x30A8, CK1 = 0x7150, CK2 = 0xD2C0, CK3 = 0xFD60;
    const uint16_t CN0 = 0x18A8, CN1 = 0x20CA, CN2 = 0x314C, CN3 = 0x49AE;
    // Rain (cooler / duller)
    const uint16_t RD0 = 0x31A8, RD1 = 0x4A6A, RD2 = 0x62EC, RD3 = 0x7B8E;
    const uint16_t RK0 = 0x3128, RK1 = 0x61A8, RK2 = 0xA2C8, RK3 = 0xC3A8;
    const uint16_t RN0 = 0x18A8, RN1 = 0x20CA, RN2 = 0x314C, RN3 = 0x49AE;
    // Storm (darker)
    const uint16_t SD0 = 0x2945, SD1 = 0x39A8, SD2 = 0x4A6A, SD3 = 0x5B0C;
    const uint16_t SK0 = 0x28A4, SK1 = 0x5126, SK2 = 0x8A28, SK3 = 0xB2C8;
    const uint16_t SN0 = 0x10A6, SN1 = 0x18C8, SN2 = 0x28EA, SN3 = 0x38EC;

    auto band = [&](uint16_t d, uint16_t k, uint16_t n) {
        return blendDayDuskNight(d, k, n, nb);
    };
    // clear → rain (0..160), rain → storm (160..256)
    auto wetMix = [&](uint16_t clearC, uint16_t rainC, uint16_t stormC) -> uint16_t {
        if (wd == 0) return clearC;
        if (wd <= 160) {
            return lerp565(clearC, rainC, (uint8_t)((wd * 16) / 160));
        }
        return lerp565(rainC, stormC, (uint8_t)(((wd - 160) * 16) / 96));
    };

    uint16_t S0 = wetMix(band(CD0, CK0, CN0), band(RD0, RK0, RN0), band(SD0, SK0, SN0));
    uint16_t S1 = wetMix(band(CD1, CK1, CN1), band(RD1, RK1, RN1), band(SD1, SK1, SN1));
    uint16_t S2 = wetMix(band(CD2, CK2, CN2), band(RD2, RK2, RN2), band(SD2, SK2, SN2));
    uint16_t S3 = wetMix(band(CD3, CK3, CN3), band(RD3, RK3, RN3), band(SD3, SK3, SN3));
    s_skyTop = S0;

    static const uint8_t bayer4[16] = {
        0,  8,  2, 10,
        12, 4, 14,  6,
        3, 11,  1,  9,
        15, 7, 13,  5
    };
    uint16_t samples[32];
    for (int i = 0; i < 32; i++) {
        int u = i;
        int u2 = (u * u) / 31;
        int u3 = 31 - ((31 - u) * (31 - u)) / 31;
        int e = (u2 + u3) / 2;
        if (e < 10)
            samples[i] = lerp565(S0, S1, (uint8_t)((e * 16) / 10));
        else if (e < 21)
            samples[i] = lerp565(S1, S2, (uint8_t)(((e - 10) * 16) / 11));
        else
            samples[i] = lerp565(S2, S3, (uint8_t)(((e - 21) * 16) / 10));
    }

    // Full sky to grass line — gradient meets turf cleanly
    for (int y = 0; y < 103; y++) {
        int si = (y * 31) / 102;
        if (si > 30) si = 30;
        uint16_t cA = samples[si];
        uint16_t cB = samples[si + 1];
        int sub = ((y * 31) % 102) * 16 / 102;
        if (cA == cB) {
            canvas.drawFastHLine(0, y, 240, maybeFlash(cA));
            continue;
        }
        for (int x = 0; x < 240; x += 2) {
            uint8_t thr = bayer4[((y & 3) << 2) | (x & 3)];
            uint16_t c = (sub > (int)thr) ? cB : cA;
            canvas.drawFastHLine(x, y, 2, maybeFlash(c));
        }
    }

    // Moon fades in with night (after mid-dusk); hides as sky wets
    if (nb > 100 && wd < 100) {
        int mx = 198, my = 22;
        // Brightness / size grow with night amount
        uint8_t t = (uint8_t)(((nb - 100) * 16) / 156);  // 0..16
        if (t > 16) t = 16;
        int rOuter = 6 + (4 * (int)t) / 16;   // 6..10
        int rMid   = 4 + (3 * (int)t) / 16;
        int rCore  = 3 + (3 * (int)t) / 16;
        uint16_t halo = lerp565(S0, 0x39E7, t);
        uint16_t body = lerp565(S1, 0xDEFB, t);
        uint16_t core = lerp565(0xC618, 0xFFFF, t);
        canvas.fillCircle(mx, my, rOuter, maybeFlash(halo));
        canvas.fillCircle(mx, my, rMid, maybeFlash(body));
        canvas.fillCircle(mx - 2, my - 1, rCore, maybeFlash(core));
        if (t > 10) {
            canvas.drawPixel(mx + 1, my + 1, maybeFlash(0xC618));
            canvas.drawPixel(mx - 1, my + 2, maybeFlash(0xC618));
        }
    }

    // Ambient FX: pollen (day) → fireflies (night); skip when sky is wet
    if (wd < 40 && !raining && !storm) {
        uint32_t phase = now % 16000;
        // Day pollen while still mostly day (nb < 80)
        if (nb < 80 && phase < 2800) {
            for (int i = 0; i < 7; i++) {
                uint32_t seed = now / 50 + (uint32_t)i * 97;
                if ((seed & 3) != 0) continue;
                int16_t px = (int16_t)((seed * 17 + i * 41) % 228) + 6;
                int16_t py = (int16_t)((seed * 13 + i * 53) % 48) + 6;
                py += (int16_t)(sinf((float)(now + i * 180) * 0.004f) * 3.0f);
                canvas.fillRect(px, py, 2, 2, maybeFlash(0xFFFE));
            }
        }
        // Fireflies once dusk is deep enough
        if (nb > 140 && phase >= 4000 && phase < 9000) {
            for (int i = 0; i < 5; i++) {
                uint32_t seed = now / 80 + (uint32_t)i * 131;
                if ((seed & 2) != 0) continue;
                int16_t px = (int16_t)((seed * 19 + i * 47) % 220) + 10;
                int16_t py = (int16_t)((seed * 11 + i * 29) % 40) + 30;
                bool on = ((now / 200 + i) & 1) != 0;
                if (on) canvas.fillRect(px, py, 2, 2, maybeFlash(0xFFE0));
            }
        }
        // Day streak only in clear day
        if (nb < 40 && phase >= 12000 && phase < 12500) {
            int t = (int)(phase - 12000);
            canvas.drawLine(40 + t, 12 + t / 4, 50 + t, 15 + t / 4, maybeFlash(0xFFFF));
        }
    }
}

// ---------------------------------------------------------------------------
// Pig draw — multi-tone procedural (ox,oy = feet center; local units * PX).
// ---------------------------------------------------------------------------

// Skin palettes: CLASSIC / BLUSH / HOG / ZOMBIE
static constexpr uint16_t PIG_OUT  = 0xE28C;
static constexpr uint16_t PIG_BODY = 0xFDB5;
static constexpr uint16_t PIG_HI   = 0xFDBF;
static constexpr uint16_t PIG_SH   = 0xE28C;
static constexpr uint16_t PIG_SN   = 0xFB92;
static constexpr uint16_t PIG_EAR  = 0xFA90;
static constexpr uint16_t PIG_IRIS = 0x9B48;
static constexpr uint16_t PIG_PUP  = 0x4185;
static constexpr uint16_t PIG_WHT  = 0xFFFF;
static constexpr uint16_t PIG_NOS  = 0xC24A;
static constexpr uint16_t PIG_HOOF = 0xD28A;
static constexpr uint16_t PIG_BLUSH= 0xF98C;

struct PigPalette {
    uint16_t out, body, hi, sh, sn, ear, iris, pup, wht, nos, hoof, blush;
};
static const PigPalette kPigPalettes[PIG_SKIN_COUNT] = {
    { PIG_OUT, PIG_BODY, PIG_HI, PIG_SH, PIG_SN, PIG_EAR, PIG_IRIS, PIG_PUP, PIG_WHT, PIG_NOS, PIG_HOOF, PIG_BLUSH },
    { 0xE28A, 0xFDB7, 0xFEDF, 0xE30C, 0xFC94, 0xFB12, 0x9B48, 0x5986, 0xFFFF, 0xC24A, 0xE28A, 0xF9AE },
    { 0xC208, 0xE34D, 0xFDB5, 0xB1E6, 0xD28A, 0xCA28, 0x6A46, 0x40C3, 0xFFFF, 0x8944, 0xA165, 0xE28C },
    { 0x3A08, 0x6B8C, 0x9D34, 0x4A69, 0x7BF0, 0x5AEB, 0x2DE4, 0xF800, 0x5FEA, 0x3205, 0x6B4D, 0x4A8A },
};
static const PigPalette& activePigPalette() {
    uint8_t s = Config::personality().pigSkin;
    if (s >= PIG_SKIN_COUNT) s = 0;
    return kPigPalettes[s];
}

// Blend two RGB565 colors (t=0..16 toward b)
static uint16_t pigMix(uint16_t a, uint16_t b, uint8_t t /*0..16*/) {
    int r0 = (a >> 11) & 0x1F, g0 = (a >> 5) & 0x3F, b0 = a & 0x1F;
    int r1 = (b >> 11) & 0x1F, g1 = (b >> 5) & 0x3F, b1 = b & 0x1F;
    int r = r0 + ((r1 - r0) * (int)t) / 16;
    int g = g0 + ((g1 - g0) * (int)t) / 16;
    int bb = b0 + ((b1 - b0) * (int)t) / 16;
    return (uint16_t)((r << 11) | (g << 5) | bb);
}

// ONE silhouette for ALL skins (BLUSH geometry). Only palette differs.
// Zombie = same pig, sick green/gray body; eyes green+red (not plain white).
// Sniff = bigger nostrils only (NO air puff / booger pixels in front of snout).
// Faces are state-aware: Happy ≠ Blink ≠ Sniff ≠ Jump ≠ Neutral, etc.
// Poses (cheap): s_playDead = flip same pig upside-down; sit = lower oy at call site.
static void drawPixelPigDetailed(M5Canvas& canvas, int16_t ox, int16_t oy,
                         AvatarState state, bool faceRight, bool blink, bool sniff,
                         uint8_t sniffPhase, bool earPerk, bool tailAlt, bool jumping,
                         uint16_t /*fg*/, uint16_t /*bg*/) {
    const int s = PX;
    // Play-dead: blend normal → upside-down (same silhouette, no hard snap)
    // a=0 normal: y = oy + ly*s
    // a=256 flip:  y = oy - (ly+18)*s
    const uint16_t deadA = s_deadBlend;  // 0..256
    auto P = [&](int lx, int ly, uint16_t col) {
        int x = faceRight ? (ox + lx * s) : (ox - (lx + 1) * s);
        int yN = oy + ly * s;
        int yD = oy - (ly + 18) * s;
        int y = yN + ((yD - yN) * (int)deadA) / 256;
        if (x < -s || x > 240 || y < -s || y > 135) return;
        canvas.fillRect(x, y, s, s, maybeFlash(col));
    };
    auto block = [&](int lx, int ly, int w, int h, uint16_t col) {
        for (int yy = 0; yy < h; yy++)
            for (int xx = 0; xx < w; xx++)
                P(lx + xx, ly + yy, col);
    };

    const bool isHappy   = (state == AvatarState::HAPPY);
    const bool isExcited = (state == AvatarState::EXCITED) || jumping;
    const bool isSad     = (state == AvatarState::SAD);
    const bool isSleepy  = (state == AvatarState::SLEEPY);
    const bool isAngry   = (state == AvatarState::ANGRY);
    const bool isHunt    = (state == AvatarState::HUNTING);
    // Happy squint only when calmly happy (not mid-jump / sniff / blink)
    const bool happySquint = isHappy && !jumping && !sniff && !blink;
    const int leg = (s_walkKick && tailAlt) ? 1 : 0;
    const int earY = earPerk ? -1 : 0;
    // subtle snout bob only — never draw floating pixels past the nose tip
    const int sniffPush = sniff ? ((sniffPhase % 3 == 1) ? 1 : 0) : 0;
    const int sniffDy   = sniff ? ((sniffPhase % 3 == 2) ? -1 : 0) : 0;

    const PigPalette& pal = activePigPalette();
    const uint8_t skin = Config::personality().pigSkin;
    const bool zombie = (skin == (uint8_t)PigSkin::ZOMBIE);

    const uint16_t O  = pal.out;
    const uint16_t B  = pal.body;
    const uint16_t H  = pal.hi;
    const uint16_t S  = pal.sh;
    const uint16_t SN = pal.sn;
    const uint16_t N  = pal.nos;
    const uint16_t F  = pal.hoof;
    const uint16_t A  = pal.ear;
    const uint16_t BL = pal.blush;
    const uint16_t Sb = pigMix(S, B, 10);
    const uint16_t Bh = pigMix(B, H, 10);
    const uint16_t SNh = pigMix(SN, H, 8);

    // Eye colors — soft brown for living pigs; zombie = green sclera + red pupil
    // (no plain white zombie eyes — undead glow)
    const uint16_t EW = zombie ? (uint16_t)0x5FEA : pal.wht;   // sclera
    const uint16_t EI = zombie ? (uint16_t)0x2DE4 : pal.iris;  // iris
    const uint16_t EP = zombie ? (uint16_t)0xF800 : pal.pup;   // pupil
    const uint16_t EG = zombie ? (uint16_t)0xFFE0 : pal.wht;   // glint

    const bool wet = Weather::isRaining();
    const int earFlat = wet ? 1 : 0;

    // soft shadow (fade out as play-dead flips)
    if (deadA < 200) {
        for (int dx = -7; dx <= 8; dx++) {
            if ((dx + 16) & 1) continue;
            int x = faceRight ? (ox + dx * s + s / 2) : (ox - dx * s - s / 2);
            if (x >= 0 && x < 240)
                canvas.drawPixel(x, oy + 1, maybeFlash(zombie ? 0x4208 : 0xE38E));
        }
    }

    // ========== BODY (same for every skin including ZOMBIE) ==========
    block(-7, -15, 14, 1, O);
    block(-8, -14, 1, 8, O);
    block(7, -14, 1, 8, O);
    block(-7, -6, 14, 1, O);
    P(-7, -14, O); P(6, -14, O);
    P(-7, -7, O);  P(6, -7, O);
    block(-7, -14, 14, 8, B);
    block(-6, -15, 12, 1, B);
    block(-6, -6, 12, 1, B);
    block(-9, -12, 2, 5, B);
    P(-9, -13, O); P(-10, -12, O); P(-10, -11, B); P(-10, -10, B);
    P(-10, -9, B); P(-9, -8, O);
    block(6, -13, 2, 5, B);
    block(-5, -13, 7, 2, Bh);
    block(-4, -14, 5, 1, H);
    block(-5, -8, 10, 2, Sb);
    block(-4, -7, 8, 1, S);

    // ========== HEAD ==========
    block(0, -17, 9, 1, O);
    block(-1, -16, 1, 7, O);
    block(9, -16, 1, 7, O);
    block(0, -9, 9, 1, O);
    block(0, -16, 9, 7, B);
    block(1, -17, 7, 1, B);
    block(0, -14, 4, 4, B);
    block(2, -13, 3, 2, Bh);
    // Cheeks — stronger blush when happy / excited / jump
    P(5, -11, BL); P(6, -11, BL);
    P(5, -12, BL); P(6, -10, BL);
    if (isHappy || isExcited || sniff) {
        P(4, -11, BL); P(7, -11, BL);
    }
    block(3, -10, 5, 1, S);

    // ========== EARS ==========
    int ey = -20 + earY + earFlat;
    P(0, ey + 1, O);
    P(1, ey, O);     P(2, ey, O);
    P(0, ey + 2, O); P(1, ey + 1, A); P(2, ey + 1, A); P(3, ey + 1, O);
    P(1, ey + 2, A); P(2, ey + 2, A); P(3, ey + 2, O);
    P(2, ey + 3, B); P(3, ey + 3, B);
    P(5, ey, O);     P(6, ey, O);
    P(4, ey + 1, O); P(5, ey + 1, A); P(6, ey + 1, A); P(7, ey + 1, O);
    P(5, ey + 2, A); P(6, ey + 2, A); P(7, ey + 2, O);
    P(5, ey + 3, B); P(6, ey + 3, B);
    if (!earPerk && !wet) { P(1, ey - 1, O); P(6, ey - 1, O); }
    else if (earPerk && !wet) { P(1, ey - 1, A); P(6, ey - 1, A); }

    // ========== EYES — soft round chibi (not angular cone/drop) ==========
    // Priority: blink > happy-squint > sniff focus > sleepy > sad > angry > hunt > excited > neutral
    if (blink) {
        // Soft closed curve (cute blink, not a hard slash)
        P(2, -13, O); P(3, -14, O); P(4, -14, O); P(5, -13, O);
        P(3, -13, B); P(4, -13, B);
    } else if (happySquint) {
        // ^_^ squint smile eyes — clearly "happy", not open scary whites
        P(2, -13, O); P(3, -14, O); P(4, -14, O); P(5, -13, O);
        P(3, -13, B); P(4, -13, B);
        // tiny glint on the arc
        P(3, -14, EG);
    } else if (sniff) {
        // Soft half-open, pupil shifted toward snout (curious sniff)
        P(2, -14, O); P(3, -14, O); P(4, -14, O); P(5, -14, O);
        P(2, -13, O); P(3, -13, EW); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
        // content brow
        P(2, -15, S); P(5, -15, S);
    } else if (isSleepy) {
        // Heavy lids, soft half-moon eye
        P(2, -14, O); P(3, -14, O); P(4, -14, O); P(5, -14, O);
        P(2, -13, O); P(3, -13, EW); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
    } else if (isSad) {
        // Small watery eyes + downturned brows
        P(2, -15, O); P(3, -15, S);                 // brow down-in
        P(2, -14, O); P(3, -14, EW); P(4, -14, EW); P(5, -14, O);
        P(2, -13, O); P(3, -13, EI); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
        // tear sparkle
        P(5, -12, zombie ? EG : EW);
    } else if (isAngry) {
        // Soft slant brows (cute mad, not demon cones) + focused round eye
        P(2, -15, O); P(3, -15, O); P(4, -16, O);   // / brow
        P(2, -14, O); P(3, -14, EW); P(4, -14, EI); P(5, -14, O);
        P(2, -13, O); P(3, -13, EI); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
    } else if (isHunt) {
        // Determined narrowed eye + slight brow
        P(2, -15, S); P(3, -15, S);
        P(2, -14, O); P(3, -14, O); P(4, -14, O); P(5, -14, O);
        P(2, -13, O); P(3, -13, EI); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
    } else if (isExcited) {
        // Big sparkly round eyes (jump / excited)
        P(2, -15, O); P(3, -15, EW); P(4, -15, EW); P(5, -15, O);
        P(2, -14, O); P(3, -14, EG); P(4, -14, EI); P(5, -14, O);
        P(2, -13, O); P(3, -13, EI); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
        // extra star glint
        P(3, -15, EG);
    } else {
        // NEUTRAL — soft round eye: outline circle, sclera, iris, pupil, glint
        P(2, -15, O); P(5, -15, O);
        P(3, -15, EW); P(4, -15, EW);
        P(2, -14, O); P(3, -14, EG); P(4, -14, EI); P(5, -14, O);
        P(2, -13, O); P(3, -13, EI); P(4, -13, EP); P(5, -13, O);
        P(3, -12, O); P(4, -12, O);
    }

    // ========== SNOUT — no floating "booger" puffs ==========
    int sx = 8 + sniffPush;
    int sy = -14 + sniffDy;

    P(sx + 1, sy, SN); P(sx + 2, sy, SN); P(sx + 3, sy, SN);
    block(sx, sy + 1, 5, 2, SN);
    P(sx + 1, sy + 3, SN); P(sx + 2, sy + 3, SN); P(sx + 3, sy + 3, SN);
    P(sx + 1, sy, O); P(sx + 2, sy, O); P(sx + 3, sy, O);
    P(sx, sy + 1, O); P(sx + 4, sy + 1, O);
    P(sx, sy + 2, O); P(sx + 4, sy + 2, O);
    P(sx + 2, sy + 3, O);
    // nostrils only (bigger when sniffing) — never draw beyond snout tip
    if (sniff) {
        P(sx + 1, sy + 1, N); P(sx + 1, sy + 2, N);
        P(sx + 3, sy + 1, N); P(sx + 3, sy + 2, N);
    } else {
        P(sx + 1, sy + 1, N);
        P(sx + 3, sy + 1, N);
    }
    P(sx + 2, sy + 2, SNh);

    // ========== MOUTH — unique per mood / FX ==========
    int my = sy + 4;
    if (sniff) {
        // tiny "o" focus on the sniff
        P(sx + 2, my, O);
    } else if (blink) {
        // gentle smile while blinking
        P(sx + 1, my, O); P(sx + 2, my, O); P(sx + 3, my, O);
    } else if (isExcited || jumping) {
        // open happy mouth
        P(sx + 1, my, O); P(sx + 2, my, O); P(sx + 3, my, O);
        P(sx, my - 1, O); P(sx + 4, my - 1, O);
        P(sx + 2, my + 1, O);
        if (!zombie) P(sx + 2, my, BL);  // soft tongue hint
    } else if (happySquint || isHappy) {
        // big U smile
        P(sx + 1, my, O); P(sx + 2, my, O); P(sx + 3, my, O);
        P(sx, my - 1, O); P(sx + 4, my - 1, O);
    } else if (isAngry) {
        // tight flat frown
        P(sx + 1, my, O); P(sx + 2, my, O); P(sx + 3, my, O);
        P(sx + 1, my - 1, O); P(sx + 3, my - 1, O);
    } else if (isHunt) {
        // determined little smirk
        P(sx + 1, my, O); P(sx + 2, my, O); P(sx + 3, my - 1, O);
    } else if (isSad) {
        // downturned
        P(sx + 1, my - 1, O); P(sx + 2, my - 1, O); P(sx + 3, my, O);
    } else if (isSleepy) {
        // tiny sleepy o
        P(sx + 2, my, O); P(sx + 1, my, O);
    } else {
        // neutral soft line
        P(sx + 1, my, O); P(sx + 2, my, O); P(sx + 3, my, O);
    }

    // ========== TAIL ==========
    int tw = (s_walkKick && leg) ? 1 : 0;
    block(-10, -11 + tw, 3, 3, B);
    P(-9, -12 + tw, B);
    P(-9, -9 + tw, B);
    P(-11, -11 + tw, B);
    P(-11, -12 + tw, O);
    P(-12, -11 + tw, B);
    P(-12, -12 + tw, O);
    P(-13, -11 + tw, B);
    P(-13, -10 + tw, B);
    P(-13, -9 + tw, O);
    P(-12, -9 + tw, B);
    P(-12, -8 + tw, O);
    P(-11, -8 + tw, B);
    P(-11, -9 + tw, S);
    P(-14, -10 + tw, O);
    P(-14, -11 + tw, O);

    // ========== LEGS ==========
    auto foot = [&](int lx, int lift) {
        block(lx, -5, 3, 3 + lift, B);
        block(lx, -2 + lift, 3, 1, F);
        P(lx, -1 + lift, O);
        P(lx + 2, -1 + lift, O);
    };
    foot(-7, 1 - leg);
    foot(-3, leg);
    foot(1, 1 - leg);
    foot(5, leg);
    block(-2, -6, 3, 1, B);
}

// ---------------------------------------------------------------------------
static void drawPixelPig(M5Canvas& canvas, int16_t ox, int16_t oy,
                         AvatarState state, bool faceRight, bool blink, bool sniff,
                         uint8_t sniffPhase, bool earPerk, bool tailAlt, bool jumping,
                         uint16_t fg, uint16_t bg) {
    drawPixelPigDetailed(canvas, ox, oy, state, faceRight, blink, sniff,
                         sniffPhase, earPerk, tailAlt, jumping, fg, bg);
}

// Grass animation state
bool Avatar::grassMoving = false;
bool Avatar::grassDirection = true;  // true = grass scrolls right
bool Avatar::pendingGrassStart = false;  // Wait for transition before starting grass
uint32_t Avatar::lastGrassUpdate = 0;
uint16_t Avatar::grassSpeed = 80;  // Default fast for OINK
Avatar::GrassBlade Avatar::grassBlades[GRASS_BLADE_COUNT] = {{0}};
int16_t Avatar::grassOffset = 0;
// Trail particle system (dust kicked up by running pig)
struct TrailParticle {
    float x, y;
    float vx, vy;
    float startX;
    float maxDist;     // 30-60px travel before vanishing
    uint8_t baseSize;  // 1-2 px radius
    bool active;
};
static const int TRAIL_COUNT = 10;
static TrailParticle trailParticles[TRAIL_COUNT] = {{0}};
static uint32_t lastTrailSpawn = 0;
static uint32_t lastTrailUpdate = 0;
static int trailSpawnIdx = 0;

// Internal state for looking direction
static bool facingRight = true;  // Default: pig looks right
static uint32_t lastFlipTime = 0;
static uint32_t flipInterval = 5000;

// Look behavior (stationary observation)
static uint32_t lastLookTime = 0;
static uint32_t lookInterval = 2000;  // Look around every 2-5s when stationary
bool Avatar::onRightSide = false;  // Track which side of screen pig is on (class static)

// Heavy-work scene gate (PigPass / EvilPig / TLS sync / Xfer). Refcounted.
static int s_sceneSuspendDepth = 0;
// Wolf bite stun — no walk/jump/attack until this millis
static uint32_t s_controlLockUntil = 0;
static constexpr uint32_t kWolfBiteLockMs = 10000;

void Avatar::suspendScene() {
    if (s_sceneSuspendDepth == 0) {
        // Freeze treadmill / walk / free-roam so we don't burn cycles in bg
        setPlayerWalkScroll(false, facingRight);
        setManualWalk(false);
        setGrassMoving(false);
        grassMoving = false;
        pendingGrassStart = false;
        s_playerWalkScroll = false;
        Wolf::reset();  // stop chase AI while scene is parked
        Serial.println("[AVATAR] Scene suspended (heavy work)");
    }
    if (s_sceneSuspendDepth < 32) s_sceneSuspendDepth++;
}

void Avatar::resumeScene() {
    if (s_sceneSuspendDepth <= 0) return;
    s_sceneSuspendDepth--;
    if (s_sceneSuspendDepth == 0) {
        Serial.println("[AVATAR] Scene resumed");
    }
}

bool Avatar::isSceneSuspended() {
    return s_sceneSuspendDepth > 0;
}

void Avatar::init() {
    s_sceneSuspendDepth = 0;
    s_controlLockUntil = 0;
    currentState = AvatarState::NEUTRAL;
    isBlinking = false;
    isSniffing = false;
    earsUp = true;
    lastBlinkTime = millis();
    blinkInterval = random(4000, 8000);
    earTwitchActive = false;
    nextEarTwitch = millis() + random(8000, 15001);

    // Init direction - start at LEFT or RIGHT edge (not center)
    // This ensures bubble can float beside pig from the start
    bool startRight = random(0, 2) == 0;
    onRightSide = startRight;
    currentX = startRight ? 108 : 20;  // Start at proper edge position
    facingRight = !startRight;  // Face toward center (more interesting)
    lastFlipTime = millis();
    flipInterval = random(25000, 50000);  // First walk: 25-50s
    lastLookTime = millis();
    lookInterval = random(3000, 8000);  // First look: 3-8s (let pig settle in)

    // Init grass blade system
    grassMoving = false;
    grassDirection = true;
    pendingGrassStart = false;
    grassSpeed = 80;
    lastGrassUpdate = millis();
    lastGrassStopTime = 0;  // No cooldown on fresh init
    grassOffset = 0;
    for (int i = 0; i < GRASS_BLADE_COUNT; i++) {
        // Classic fat turf mix — a bit taller so blades reach past pig ankles
        uint8_t r = (uint8_t)(esp_random() % 100);
        if (r < 10) {
            grassBlades[i].kind = 4;  // short stubble
            grassBlades[i].height = random(5, 9);
            grassBlades[i].width = 2;
        } else if (r < 55) {
            grassBlades[i].kind = 0;  // single blade
            grassBlades[i].height = random(11, 20);
            grassBlades[i].width = 2;
        } else if (r < 85) {
            grassBlades[i].kind = 1;  // tuft
            grassBlades[i].height = random(10, 18);
            grassBlades[i].width = 2;
        } else if (r < 95) {
            grassBlades[i].kind = 2;  // flower
            grassBlades[i].height = random(11, 16);
            grassBlades[i].width = 1;
        } else {
            grassBlades[i].kind = 3;  // pebble
            grassBlades[i].height = 2;
            grassBlades[i].width = 2;
        }
        grassBlades[i].lean = (int8_t)random(-3, 4);
        grassBlades[i].shade = (uint8_t)(esp_random() % 4);
    }

    // Legacy tree statics (unused; real state in Trees)
    treePhase = TreePhase::HIDDEN;
    treeGrowth = 0.0f;
    treeBranchCount = 0;
    treeLeafCount = 0;
    treeEndpointLeafCount = 0;
    treeFruitCount = 0;
    treePendingHide = false;
    treePendingShow = false;
    treePendingFruits = 0;
    treeAliveStart = 0;
    treeScrollOffset = 0;

    for (uint8_t i = 0; i < FRUIT_SPLASH_COUNT; i++) fruitSplashes[i].active = false;
    fruitSplashIdx = 0;

    // Multi-kind flora: (1) fruit on demand, (2) decor tree, (3) berry bush
    Trees::setPigHint(currentX, onRightSide);
    Trees::init();  // spawns DECOR + BERRY scenery

    // Init star system, dormant until night
    starsActive = false;
    starCount = 0;
    lastStarSpawn = 0;
    nextSpawnDelay = 2000;
    initStarPositions();
}

void Avatar::setState(AvatarState state) {
    currentState = state;
}

void Avatar::setMoodIntensity(int intensity) {
    moodIntensity = constrain(intensity, -100, 100);
}

bool Avatar::isFacingRight() {
    return facingRight;
}

bool Avatar::isOnRightSide() {
    return onRightSide;
}

bool Avatar::isTransitioning() {
    return transitioning || attackHopActive;
}

int Avatar::getCurrentX() {
    return currentX;
}

void Avatar::blink() {
    isBlinking = true;
    lastBlinkTime = millis();
}

void Avatar::wiggleEars() {
    earsUp = !earsUp;
}

void Avatar::sniff() {
    if (!isSniffing) {
        sniffFrame = 0;  // Reset frame on new sniff
    }
    isSniffing = true;
    sniffStartTime = millis();
}

void Avatar::cuteJump() {
    if (isControlLocked()) return;
    // Crouch → spring (higher hop). SFX on takeoff after crouch is handled in draw.
    s_sitting = false;
    s_playDead = false;
    jumpActive = true;
    jumpStartTime = millis();
    // Quiet prep — main JUMP tone fires when leaving crouch (see drawFrame)
}

int Avatar::getJumpLiftPx() {
    if (!jumpActive) return 0;
    uint32_t elapsed = millis() - jumpStartTime;
    if (elapsed < JUMP_CROUCH_MS) return 0;  // still on ground crouching
    elapsed -= JUMP_CROUCH_MS;
    if (elapsed >= jumpDurationMs) return 0;
    float t = (float)elapsed / (float)jumpDurationMs;
    float arc = 4.0f * t * (1.0f - t);
    return (int)(arc * (float)jumpHeightPx);
}

void Avatar::setJumpTuning(int heightPx, uint16_t durationMs) {
    if (heightPx < 4) heightPx = 4;
    if (heightPx > 48) heightPx = 48;
    if (durationMs < 200) durationMs = 200;
    if (durationMs > 1200) durationMs = 1200;
    jumpHeightPx = heightPx;
    jumpDurationMs = durationMs;
}

void Avatar::resetJumpTuning() {
    jumpHeightPx = 16;
    jumpDurationMs = 500;
}

void Avatar::attackHop() {
    if (attackHopActive) return;
    if (s_controlLockUntil != 0 && (int32_t)(millis() - s_controlLockUntil) < 0) return;
    // Forward multi-hop pounce — lands ahead (no snap-back teleport)
    s_sitting = false;
    s_playDead = false;
    jumpActive = false;
    attackHopActive = true;
    attackHopStartTime = millis();
    attackHopIndex = 0;
    attackHopOriginX = currentX;
    attackHopTotal = random(3, 5);  // 3-4 hops forward
    // Remember facing at start — left attacks must stay facing left even at rail
    s_attackHopFaceRight = facingRight;

    // Clamp hop path to camera rails. Going "further" at the rail = world scroll,
    // never plan X past the rail (that caused right-side teleport when targets
    // were re-applied / hop segments restarted from unpinned fromX).
    int16_t prevX = currentX;
    if (prevX < kCamLeftLine) prevX = kCamLeftLine;
    if (prevX > kCamRightLine) prevX = kCamRightLine;
    attackHopOriginX = prevX;
    // Soft-correct start if we were past rail
    currentX = prevX;

    int stepBoost = 6;
    for (uint8_t i = 0; i < attackHopTotal; i++) {
        int16_t offset = random(14 + stepBoost, 22 + stepBoost);
        if (!s_attackHopFaceRight) offset = -offset;
        int16_t target = prevX + offset;
        // Stay inside camera rails only
        if (target < kCamLeftLine) target = kCamLeftLine;
        if (target > kCamRightLine) target = kCamRightLine;
        attackHopTargets[i] = target;
        prevX = target;
    }
    facingRight = s_attackHopFaceRight;
    SFX::play(SFX::ATTACK_HOP);
}

bool Avatar::isAttackHopping() {
    return attackHopActive;
}

void Avatar::perkUp() {
    if (attackHopActive || spinActive) return;  // Don't interrupt bigger animations
    perkUpActive = true;
    perkUpStart = millis();
}

void Avatar::flinch() {
    if (attackHopActive || spinActive) return;
    flinchActive = true;
    flinchStart = millis();
}

void Avatar::spin() {
    if (attackHopActive) return;
    jumpActive = false;  // Cancel in-flight jump; spin owns the Y arc
    spinActive = true;
    spinStart = millis();
}

void Avatar::pawScratch() {
    if (attackHopActive || spinActive || perkUpActive || transitioning) return;
    pawScratchActive = true;
    pawScratchStart = millis();
}

void Avatar::triggerTailWiggle() {
    tailWiggleActive = true;
    tailWiggleStart = millis();
}

void Avatar::triggerSparkles(uint8_t count) {
    // Burst spawn sparkles from pig body center
    int cx = currentX + (PIG_LAYOUT_W * PX) / 2;
    int cy = (106 - PIG_LAYOUT_H * PX) + (PIG_LAYOUT_H * PX) / 2;
    for (uint8_t i = 0; i < MAX_SPARKLES && count > 0; i++) {
        if (sparkles[i].life == 0) {
            sparkles[i].x = cx + random(-10, 11);
            sparkles[i].y = cy + random(-10, 11);
            sparkles[i].vx = random(-3, 4);
            sparkles[i].vy = random(-4, 1);  // Bias upward
            sparkles[i].life = random(10, 18);  // ~330-600ms at 30fps
            count--;
        }
    }
}

void Avatar::updateAndDrawSparkles(M5Canvas& canvas) {
    uint16_t col = maybeFlash(C_SPARK);
    for (uint8_t i = 0; i < MAX_SPARKLES; i++) {
        if (sparkles[i].life == 0) continue;
        sparkles[i].x += sparkles[i].vx;
        sparkles[i].y += sparkles[i].vy;
        sparkles[i].life--;
        int16_t sx = snapPx(sparkles[i].x);
        int16_t sy = snapPx(sparkles[i].y);
        if (sparkles[i].life > 6) {
            canvas.fillRect(sx, sy, PX, PX, col);
            canvas.fillRect(sx - PX, sy, PX, PX, col);
            canvas.fillRect(sx + PX, sy, PX, PX, col);
            canvas.fillRect(sx, sy - PX, PX, PX, col);
            canvas.fillRect(sx, sy + PX, PX, PX, col);
        } else {
            canvas.fillRect(sx, sy, PX, PX, col);
        }
    }
}

void Avatar::draw(M5Canvas& canvas) {
    // Parked during heavy ops — don't burn CPU on trees/wolf/ambient drops
    if (s_sceneSuspendDepth > 0) {
        canvas.fillSprite(getBGColor());
        return;
    }

    uint32_t now = millis();

    // Sniff animation times out after SNIFF_DURATION_MS
    // Update sniff frame for animation (cycle every 100ms)
    if (isSniffing) {
        if (now - sniffStartTime > SNIFF_DURATION_MS) {
            isSniffing = false;
            sniffFrame = 0;
        } else {
            sniffFrame = ((now - sniffStartTime) / 100) % 3;  // 3 frames: oo, oO, Oo
        }
    }

    // Event reaction animation timeouts
    if (perkUpActive && (now - perkUpStart >= PERK_UP_DURATION_MS)) {
        perkUpActive = false;
    }
    if (flinchActive && (now - flinchStart >= FLINCH_DURATION_MS)) {
        flinchActive = false;
    }
    if (spinActive && (now - spinStart >= SPIN_DURATION_MS)) {
        spinActive = false;
    }
    if (pawScratchActive && (now - pawScratchStart >= PAW_SCRATCH_DURATION_MS)) {
        pawScratchActive = false;
    }

    // Handle walk transition animation
    if (transitioning) {
        uint32_t elapsed = now - transitionStartTime;
        if (elapsed >= TRANSITION_DURATION_MS) {
            // Transition complete
            transitioning = false;
            currentX = transitionToX;
            facingRight = transitionToFacingRight;
            onRightSide = (currentX > 60);  // Track which side we're on

            // Start grass now if it was pending
            if (pendingGrassStart) {
                grassMoving = true;
                pendingGrassStart = false;
                facingRight = !grassDirection;  // Face opposite to grass movement
            } else if (!grassMoving) {
                // === POST-WALK RANDOM BEHAVIOR ===
                // After arriving somewhere, pig might do something interesting
                int arrivalRoll = random(0, 100);
                if (arrivalRoll < 20) {
                    // 20%: Look around after arriving (curious)
                    facingRight = !facingRight;
                } else if (arrivalRoll < 35) {
                    // 15%: Sniff the new area
                    sniff();
                } else if (arrivalRoll < 45) {
                    // 10%: Quick ear wiggle (settling in)
                    wiggleEars();
                } else if (arrivalRoll < 55) {
                    // 10%: Turn around completely (changed mind)
                    facingRight = !transitionToFacingRight;
                }
                // 45%: Just face the direction we were walking
            }

            // Reset look timer for new position with random delay
            lastLookTime = now;
            lookInterval = random(1500, 6000);  // More variable
        } else {
            // Animate X position (ease in-out)
            float t = (float)elapsed / TRANSITION_DURATION_MS;
            // Heavy quintic ease: 6t^5 - 15t^4 + 10t^3 (more inertia than smooth step)
            // Flatter acceleration/deceleration = heavier feel
            float smoothT = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            currentX = transitionFromX + (int)((transitionToX - transitionFromX) * smoothT);
        }
    }

    // Phase 8: Mood intensity affects animation timing
    // High positive = excited (faster blinks, more looking around)
    // High negative = lethargic (slower blinks, less movement)

    // Calculate intensity-adjusted blink interval
    // Base: 4000-8000ms, excited (-50%): 2000-4000ms, sad (+50%): 6000-12000ms
    float blinkMod = 1.0f - (moodIntensity / 200.0f);  // 0.5 to 1.5
    uint32_t minBlink = (uint32_t)(4000 * blinkMod);
    uint32_t maxBlink = (uint32_t)(8000 * blinkMod);

    // Check if we should blink (single frame blink)
    if (now - lastBlinkTime > blinkInterval) {
        isBlinking = true;
        lastBlinkTime = now;
        blinkInterval = random(minBlink, maxBlink);
    }

    // Ear twitch micro-animation: random brief ear perk
    if (earTwitchActive) {
        if (now - earTwitchStart >= EAR_TWITCH_DURATION_MS) {
            earTwitchActive = false;
            nextEarTwitch = now + random(8000, 15001);
        }
    } else if (now >= nextEarTwitch && !transitioning && !attackHopActive) {
        earTwitchActive = true;
        earTwitchStart = now;
    }

    // Calculate intensity-adjusted intervals
    // Excited pig looks around more, sad pig stares
    // NOTE: Reduced mood effect (was /150, now /300) to prevent frantic movement at high happiness
    float flipMod = 1.0f - (moodIntensity / 300.0f);  // ~0.66 to ~1.33
    uint32_t minWalk = (uint32_t)(30000 * flipMod);   // Walk timer: 30s base
    uint32_t maxWalk = (uint32_t)(75000 * flipMod);   // Walk timer: 75s base
    uint32_t minLook = (uint32_t)(4000 * flipMod);    // Look timer: 4s base (more frequent looks)
    uint32_t maxLook = (uint32_t)(15000 * flipMod);   // Look timer: 15s base

    // === ORGANIC RANDOM BEHAVIORS ===
    // Disable while treadmill / hop / player poses (sit & dead would fight free-roam X)
    if (!transitioning && !grassMoving && !pendingGrassStart && !attackHopActive &&
        !s_sitting && !s_playDead && s_sitBlend < 32 && s_deadBlend < 32) {

        // --- LOOK BEHAVIOR: Random glances with personality ---
        if (now - lastLookTime > lookInterval) {
            int lookRoll = random(0, 100);

            if (lookRoll < 35) {
                // 35%: Simple head turn
                facingRight = !facingRight;
            } else if (lookRoll < 55) {
                // 20%: Look one way, then back (curious double-take)
                facingRight = !facingRight;
                // Schedule a quick look-back by shortening next interval
                lookInterval = random(800, 1500);  // Quick follow-up
                lastLookTime = now;
                goto skip_look_reset;  // Don't reset with normal interval
            } else if (lookRoll < 72) {
                // 17%: Sniff while looking
                facingRight = random(0, 2) == 0;
                sniff();
            } else if (lookRoll < 84) {
                // 12%: Ear wiggle
                wiggleEars();
                if (random(0, 3) == 0) triggerTailWiggle();
            } else if (lookRoll < 92) {
                // 8%: Soft blink
                blink();
            } else if (lookRoll < 96) {
                // 4%: Cute idle hop
                cuteJump();
            }
            // 4%: chill

            lastLookTime = now;
            // Vary the interval more - sometimes rapid, sometimes long pauses
            if (random(0, 5) == 0) {
                lookInterval = random(1500, 4000);  // 20% chance: quick succession
            } else {
                lookInterval = random(minLook, maxLook);
            }
        }
        skip_look_reset:

        // --- WALK BEHAVIOR: Always stay at LEFT/RIGHT edges for bubble positioning ---
        // Pig should ALWAYS be at edges where bubble can float beside, never in center
        if (now - lastFlipTime > flipInterval) {
            int walkRoll = random(0, 100);
            int targetX;

            // Define edge zones (bubble floats beside pig, not above)
            const int LEFT_EDGE = 20;   // Left rest position
            const int RIGHT_EDGE = 108; // Right rest position

            if (walkRoll < 50) {
                // 50%: Walk to opposite edge (primary behavior)
                targetX = onRightSide ? LEFT_EDGE : RIGHT_EDGE;
            } else if (walkRoll < 85) {
                // 35%: Walk to random edge (left or right)
                targetX = random(0, 2) == 0 ? LEFT_EDGE : RIGHT_EDGE;
            } else if (walkRoll < 95) {
                // 10%: Short shuffle within current edge zone
                if (onRightSide) {
                    targetX = random(85, 108);  // Stay in right zone
                } else {
                    targetX = random(20, 45);   // Stay in left zone
                }
            } else {
                // 5%: Stay put, just turn around (fake walk)
                facingRight = !facingRight;
                lastFlipTime = now;
                flipInterval = random(minWalk / 2, maxWalk / 2);
                goto skip_walk;
            }

            // Only walk if destination is different enough (avoid micro-movements)
            if (abs(targetX - currentX) > 15) {
                bool goingRight = targetX > currentX;

                transitioning = true;
                transitionStartTime = now;
                transitionFromX = currentX;
                transitionToX = targetX;
                transitionToFacingRight = goingRight;  // Face direction of travel

                lastFlipTime = now;
                // Vary wait time more randomly
                if (random(0, 4) == 0) {
                    flipInterval = random(15000, 30000);  // 25% chance: shorter wait
                } else {
                    flipInterval = random(minWalk, maxWalk);
                }
            } else {
                // Destination too close, just look that way instead
                facingRight = targetX > currentX;
                lastFlipTime = now;
                flipInterval = random(minWalk / 3, minWalk);
            }
        }
        skip_walk:;
    }

    // === GRASS WANDER: Random roaming toward center while treadmill runs ===
    if (!transitioning && grassMoving && !attackHopActive) {
        if (now - grassWanderTimer > grassWanderInterval) {
            int homeX = grassDirection ? 108 : 20;
            int centerX = DISPLAY_W / 2;  // 120 = screen center limit
            int distFromHome = abs(currentX - homeX);

            if (distFromHome < 20) {
                // Near home - chance to wander toward center
                if (random(0, 100) < 35) {
                    int lo = (homeX < centerX) ? homeX + 10 : centerX;
                    int hi = (homeX < centerX) ? centerX : homeX - 10;
                    int target = random(lo, hi + 1);
                    if (abs(target - currentX) > 10) {
                        transitioning = true;
                        transitionStartTime = now;
                        transitionFromX = currentX;
                        transitionToX = target;
                        transitionToFacingRight = !grassDirection;  // Keep treadmill facing
                    }
                }
            } else {
                // Away from home - maybe return (or stay and chill)
                if (random(0, 100) < 45) {
                    transitioning = true;
                    transitionStartTime = now;
                    transitionFromX = currentX;
                    transitionToX = homeX;
                    transitionToFacingRight = !grassDirection;  // Restore treadmill facing
                }
            }

            grassWanderTimer = now;
            grassWanderInterval = random(3000, 8000);
        }
    }

    // Blink holds ~140ms so it reads as a real blink (was 1 frame → looked wrong)
    static constexpr uint32_t BLINK_HOLD_MS = 140;
    bool shouldBlink = false;
    if (isBlinking && currentState != AvatarState::SLEEPY) {
        if ((now - lastBlinkTime) < BLINK_HOLD_MS) {
            shouldBlink = true;
        } else {
            isBlinking = false;
        }
    }

    if (jumpActive && (now - jumpStartTime > (uint32_t)JUMP_CROUCH_MS + jumpDurationMs)) {
        jumpActive = false;
    }

    drawFrame(canvas, shouldBlink, facingRight, isSniffing);
}

void Avatar::drawFrame(M5Canvas& canvas, bool blink, bool faceRight, bool sniff) {
    // Z-order (back → front):
    //   sky → stars/moon → clouds → season backdrop (lightning) → tree → pig → grass
    // Clouds MUST be before the tree (were drawn after Avatar in Display → tree behind clouds).
    drawSkyBackdrop(canvas);
    updateStars();
    drawStars(canvas);
    Weather::drawClouds(canvas, getDrawColor());
    SeasonalFx::drawBackdrop(canvas);  // spring bolts sit in the sky
    // Back grass first — trees sit on top of turf (foreground of grass)
    drawGrass(canvas, false);

    uint32_t now = millis();

    // Watchdog: if caller stops refreshing attack shake, auto-disable after 250ms
    if (attackShakeRefreshTime == 0 || (now - attackShakeRefreshTime) > 250) {
        attackShakeActive = false;
        attackShakeStrong = false;
    }

    // Handle cute jump timeout (crouch + air)
    if (jumpActive && (now - jumpStartTime > (uint32_t)JUMP_CROUCH_MS + jumpDurationMs)) {
        jumpActive = false;
    }

    // === Attack hop animation update ===
    if (attackHopActive) {
        uint32_t hopElapsed = now - attackHopStartTime;
        uint32_t totalHopTime = (uint32_t)attackHopTotal * ATTACK_HOP_MS;
        // Always keep start facing (toX==fromX at rail used to force face-right)
        facingRight = s_attackHopFaceRight;

        if (hopElapsed >= totalHopTime) {
            // Animation complete — keep last interpolated X (already rail-safe)
            attackHopActive = false;
            if (attackHopTotal > 0) {
                // Use planned end only if it matches rail clamps (no snap past rail)
                int16_t endX = attackHopTargets[attackHopTotal - 1];
                if (endX < kCamLeftLine) endX = kCamLeftLine;
                if (endX > kCamRightLine) endX = kCamRightLine;
                currentX = endX;
            }
            facingRight = s_attackHopFaceRight;  // stay facing attack direction
            onRightSide = (currentX > 60);
            // Stop hop-driven scroll; hold-to-walk re-enables if key still down
            if (s_playerWalkScroll) setPlayerWalkScroll(false, s_attackHopFaceRight);
        } else {
            uint8_t hopIdx = hopElapsed / ATTACK_HOP_MS;
            if (hopIdx >= attackHopTotal) hopIdx = attackHopTotal - 1;
            attackHopIndex = hopIdx;

            float hopT = (float)(hopElapsed - hopIdx * ATTACK_HOP_MS) / (float)ATTACK_HOP_MS;
            float smoothT = hopT * hopT * (3.0f - 2.0f * hopT);
            int16_t fromX = (hopIdx == 0) ? attackHopOriginX : attackHopTargets[hopIdx - 1];
            int16_t toX = attackHopTargets[hopIdx];
            // Rail-safe endpoints (defensive)
            if (fromX < kCamLeftLine) fromX = kCamLeftLine;
            if (fromX > kCamRightLine) fromX = kCamRightLine;
            if (toX < kCamLeftLine) toX = kCamLeftLine;
            if (toX > kCamRightLine) toX = kCamRightLine;

            currentX = fromX + (int)((toX - fromX) * smoothT);

            // At camera rail: scroll world so attack "continues" into the scene
            if (s_attackHopFaceRight && currentX >= kCamRightLine - 1) {
                currentX = kCamRightLine;
                setPlayerWalkScroll(true, true);
            } else if (!s_attackHopFaceRight && currentX <= kCamLeftLine + 1) {
                currentX = kCamLeftLine;
                setPlayerWalkScroll(true, false);
            }
            onRightSide = (currentX > 60);
        }
    }

    // Trees never push/bonk — walk through freely; break only by jump/stomp
    treeColliding = false;
    treeCollisionShake = 0;
    wasTreeColliding = false;
    treeBonkPhase = 0;
    int16_t tbxNow = 0;
    (void)tbxNow;

    // Airborne stomp works in every mode (idle jump, fruit-run, attack hop)
    tryStompTree();

    // Jump / attack hop near wolf → scare it away (IDLE + all avatar scenes)
    if (Wolf::isActive() && !isControlLocked()) {
        bool airborne = attackHopActive || (jumpActive && getJumpLiftPx() > 3);
        if (airborne) {
            int feetX = currentX + 14 * (int)PX;
            uint8_t before = Wolf::getActiveCount();
            Wolf::scareNear(feetX, 42);  // 1–2 wolves
            if (Wolf::getActiveCount() < before) {
                triggerSparkles(5);
                triggerTailWiggle();
                setState(AvatarState::HAPPY);
                SFX::play(SFX::OINK_HAPPY);
            }
        }
    }

    // Clear wolf bite stun when timer expires
    if (s_controlLockUntil != 0 && (int32_t)(millis() - s_controlLockUntil) >= 0) {
        s_controlLockUntil = 0;
        if (s_playDead) setPlayDead(false);
        setState(AvatarState::NEUTRAL);
    }

    // Ambient fruit trees + auto-collect fallen fruit/berries near pig
    {
        int feet = currentX + 14 * PX;
        int lift = getJumpLiftPx();
        int feetY = 106 - lift;
        if (Trees::updateAmbient(feet, feetY, currentX, onRightSide)) {
            triggerSparkles(5);
            triggerTailWiggle();
            SFX::play(SFX::MODE_ENTER);
            if (currentState != AvatarState::HUNTING)
                setState(AvatarState::HAPPY);
        }
    }

    // Trees in front of back grass, behind pig
    drawTree(canvas);

    // Calculate vertical shake/jump offset
    int shakeY = 0;
    int startX = currentX;

    if (treeBonkPhase != 0) {
        uint32_t be = now - treeBonkStart;
        if (treeBonkPhase == 1) {
            // SQUASH into tree: compress toward trunk, duck
            float t = (float)be / (float)BONK_SQUASH_MS;
            startX += (int)(treeBonkDir * 6 * t);   // lean into impact
            shakeY = (int)(4 * t);                  // squash down
            facingRight = (treeBonkDir > 0);        // look at tree
        } else if (treeBonkPhase == 2) {
            // BOUNCE away: hop back with arc
            float t = (float)be / (float)BONK_BOUNCE_MS;
            float arc = 4.0f * t * (1.0f - t);
            startX = treeBonkOriginX - (int)(treeBonkDir * 14 * t);
            shakeY = -(int)(arc * 12);              // nice hop
            facingRight = (treeBonkDir < 0);        // face away while bouncing
        } else {
            // DIZZY settle: gentle wobble + stars already spawned
            float t = (float)be / (float)BONK_DIZZY_MS;
            float damp = 1.0f - t;
            startX = treeBonkOriginX - (int)(treeBonkDir * 14)
                     + (int)(sin((float)be * 0.04f) * 3.0f * damp);
            shakeY = (int)(sin((float)be * 0.05f) * 2.0f * damp);
            // face away from tree while recovering
            facingRight = (treeBonkDir < 0);
        }
    } else if (attackHopActive) {
        uint32_t hopElapsed = now - attackHopStartTime;
        uint32_t hopLocal = hopElapsed - (uint32_t)attackHopIndex * ATTACK_HOP_MS;
        float t = (float)hopLocal / (float)ATTACK_HOP_MS;
        float arc = 4.0f * t * (1.0f - t);
        shakeY = -(int)(arc * ATTACK_HOP_HEIGHT);
    } else if (jumpActive) {
        uint32_t elapsed = now - jumpStartTime;
        if (elapsed < JUMP_CROUCH_MS) {
            // Crouch prep: sink down, then spring
            float t = (float)elapsed / (float)JUMP_CROUCH_MS;
            // ease-in crouch (heavier at end)
            float e = t * t;
            shakeY = (int)(5.0f * e);  // +Y = down
        } else {
            // Play takeoff SFX once when leaving crouch
            static uint32_t s_lastJumpSfxMs = 0;
            if (jumpStartTime != s_lastJumpSfxMs) {
                s_lastJumpSfxMs = jumpStartTime;
                SFX::play(SFX::JUMP);
            }
            float t = (float)(elapsed - JUMP_CROUCH_MS) / (float)jumpDurationMs;
            if (t > 1.0f) t = 1.0f;
            float arc = 4.0f * t * (1.0f - t);
            shakeY = -(int)(arc * jumpHeightPx);  // -Y = up
        }
    } else if (attackShakeActive) {
        const int amp = attackShakeStrong ? 6 : 4;
        shakeY = (esp_random() % 2 == 0) ? amp : -amp;
    } else if (spinActive) {
        uint32_t elapsed = now - spinStart;
        float t = (float)elapsed / (float)SPIN_DURATION_MS;
        float arc = 4.0f * t * (1.0f - t);
        shakeY = -(int)(arc * 8);  // spin always uses default hop height
        uint8_t flipPhase = elapsed / (SPIN_DURATION_MS / SPIN_FLIPS);
        facingRight = (flipPhase % 2 == 0);
    } else if (perkUpActive) {
        uint32_t elapsed = now - perkUpStart;
        float t = (float)elapsed / (float)PERK_UP_DURATION_MS;
        float arc = 4.0f * t * (1.0f - t);
        shakeY = -(int)(arc * PERK_UP_HEIGHT);
    } else if (flinchActive) {
        uint32_t elapsed = now - flinchStart;
        if (elapsed < 150) {
            shakeY = 3;
        } else {
            shakeY = (esp_random() % 2 == 0) ? 2 : -2;
        }
    } else if (pawScratchActive) {
        // X only
    } else if (transitioning || grassMoving) {
        // Smooth walk bob (sin-ish, game-like)
        float phase = (float)(now % 320) / 320.0f * 6.28318f;
        shakeY = -(int)((sinf(phase) * 0.5f + 0.5f) * 3.0f);
    } else {
        // Idle breathe — slower, living
        float phase = (float)(now % 2800) / 2800.0f * 6.28318f;
        shakeY = -(int)((sinf(phase) * 0.5f + 0.5f) * 2.0f);
    }

    // MicPork: pig IS the spectrometer — bounce/sway with sound energy
    if (micDanceLevel > 0.02f && treeBonkPhase == 0) {
        float l = micDanceLevel;
        if (l > 1.0f) l = 1.0f;
        // Faster beat when louder
        float hz = 2.0f + l * 10.0f;
        float phase = (float)(now % 1000) / 1000.0f * 6.28318f * hz;
        int bounce = (int)((sinf(phase) * 0.5f + 0.5f) * (3.0f + l * 14.0f));
        shakeY -= bounce;
        // Side sway like spectrum peak
        startX += (int)(sinf(phase * 1.7f) * l * 6.0f);
        // Ears perk with energy
        if (l > 0.15f) earTwitchActive = true;
        // Tail thrash when loud
        if (l > 0.25f) {
            tailWiggleActive = true;
            tailWiggleStart = now;
        }
    }

    if (pawScratchActive && treeBonkPhase == 0) {
        uint32_t elapsed = now - pawScratchStart;
        startX += ((elapsed / 100) % 2 == 0) ? 2 : -2;
    }

    // Feet on ground (RatOs-style origin). startX is left anchor of pig zone.
    int feetX = startX + 14 * PX;
    int feetY = 106 + shakeY;

    if (s_manualWalk && millis() > s_manualWalkUntil) s_manualWalk = false;
    s_walkKick = (grassMoving || transitioning || attackHopActive || s_manualWalk) &&
                 (treeBonkPhase == 0);

    // Winter: pig walks through drifts → melt a trampled path (протоптанная тропинка)
    if (s_walkKick && shakeY >= -2)
        SeasonalFx::trampleSnow(feetX);

    bool tailAlt = false;
    if (tailWiggleActive) {
        if ((now - tailWiggleStart) < TAIL_WIGGLE_DURATION_MS) {
            tailAlt = ((now / 120) % 2) != 0;
        } else {
            tailWiggleActive = false;
        }
    } else if (s_walkKick) {
        tailAlt = ((now / 100) % 2) != 0;
    }

    bool earPerk = earTwitchActive || perkUpActive || (treeBonkPhase == 1);

    // Dizzy stars during bonk
    if (treeBonkPhase == 3 && ((now / 80) % 2) == 0) {
        int16_t hx = feetX;
        int16_t hy = feetY - 14 * PX;
        float ang = (float)(now % 1000) * 0.00628f;
        for (int s = 0; s < 3; s++) {
            float a = ang + s * 2.1f;
            int16_t sx = snapPx(hx + (int16_t)(cosf(a) * 12));
            int16_t sy = snapPx(hy + (int16_t)(sinf(a) * 6));
            if (sx >= 0 && sx < 240 && sy >= 0 && sy < 107)
                canvas.fillRect(sx, sy, PX, PX, maybeFlash(C_SPARK));
        }
    }

    // Waves: startX/Y interpreted as feet center (see drawWaveRipples)
    drawWaveRipples(canvas, faceRight, feetX, feetY);

    // Contact shadow under feet (on turf; trees already drawn behind pig)
    // Pig between tree layer and front grass ankles
    // Contact shadow under feet — pig grounded
    if (!jumpActive && !s_playDead) {
        int16_t shY = 105;
        for (int dx = -16; dx <= 18; dx++) {
            int16_t sx = (int16_t)(feetX + dx);
            if (sx < 0 || sx >= 240) continue;
            uint32_t h = (uint32_t)(sx + feetX) * 1103515245u;
            // denser darker oval under belly/feet
            int adx = dx < 0 ? -dx : dx;
            if (adx <= 14) {
                canvas.drawPixel(sx, shY, maybeFlash(0x3180));
                if ((h & 3) == 0) canvas.drawPixel(sx, shY - 1, maybeFlash(0x4200));
            } else if (adx <= 18 && (h & 1)) {
                canvas.drawPixel(sx, shY, maybeFlash(0x4200));
            }
        }
    }

    // Smooth sit / play-dead blends (no hard Y snap between poses)
    {
        uint16_t sitT = (s_sitting && !jumpActive && !s_playDead) ? 256 : 0;
        uint16_t deadT = s_playDead ? 256 : 0;
        if (s_sitBlend < sitT) {
            s_sitBlend = (uint16_t)((s_sitBlend + POSE_BLEND_STEP > sitT) ? sitT : s_sitBlend + POSE_BLEND_STEP);
        } else if (s_sitBlend > sitT) {
            s_sitBlend = (s_sitBlend > POSE_BLEND_STEP) ? (uint16_t)(s_sitBlend - POSE_BLEND_STEP) : 0;
        }
        if (s_deadBlend < deadT) {
            s_deadBlend = (uint16_t)((s_deadBlend + POSE_BLEND_STEP > deadT) ? deadT : s_deadBlend + POSE_BLEND_STEP);
        } else if (s_deadBlend > deadT) {
            s_deadBlend = (s_deadBlend > POSE_BLEND_STEP) ? (uint16_t)(s_deadBlend - POSE_BLEND_STEP) : 0;
        }
    }

    // Pig BETWEEN grass layers
    // Sit = same pig, sunk into turf (blend 0..~9px)
    int16_t sitSink = (int16_t)((3 * PX * (int)s_sitBlend) / 256);
    int16_t drawFeetY = (int16_t)(feetY + sitSink);
    // Play-dead: blend flip inside drawPixelPig via s_deadBlend
    AvatarState drawState = (s_deadBlend > 128) ? AvatarState::SLEEPY : currentState;
    bool drawBlink = (s_deadBlend > 128) ? true : blink;
    drawPixelPig(canvas, (int16_t)feetX, drawFeetY,
                 drawState, faceRight, drawBlink, sniff,
                 sniffFrame, earPerk, tailAlt, jumpActive,
                 getDrawColor(), getBGColor());

    updateAndDrawSparkles(canvas);

    // Grass IN FRONT of feet/ankles only (not a wall covering the body)
    drawGrass(canvas, true);

    // Draw falling drops/splashes in the foreground so they appear above grass
    // (moved from Trees::draw to ensure correct Z-order and collectability).
    Trees::drawDropsForeground(canvas);
}

void Avatar::setGrassMoving(bool moving, bool directionRight, bool force) {
    // Mode treadmill takes over from free-roam scroll
    if (moving) s_playerWalkScroll = false;

    // Early exit if already in requested state (prevents per-frame overhead)
    if (moving && (grassMoving || pendingGrassStart) && !force) {
        return;  // Already moving or pending - don't interrupt
    }
    if (!moving && !grassMoving && !pendingGrassStart) {
        return;  // Already stopped
    }
    // Force restart: clear flags so path below can re-arm treadmill
    if (force && moving) {
        grassMoving = false;
        pendingGrassStart = false;
        lastGrassStopTime = 0;
    }

    if (moving) {
        // COOLDOWN CHECK: Don't start grass if we just stopped
        // This prevents rapid on/off/on/off state changes from causing macarena
        uint32_t now = millis();
        if (!force && lastGrassStopTime > 0 && (now - lastGrassStopTime) < GRASS_REST_COOLDOWN_MS) {
            return;  // Still in cooldown period - pig needs rest
        }

        grassDirection = directionRight;

        // Calculate correct treadmill position based on direction
        // Grass RIGHT: pig at X=108 (tail margin on right)
        // Grass LEFT: pig at X=20 (tail margin on left: 20-18=2)
        int targetX = directionRight ? 108 : 20;

        if (transitioning) {
            // Check if this is a coast-back transition (pig returning to rest at X=20)
            // Don't interrupt coast-back with grass start - let the pig chill first
            // This prevents the "macarena" bug where rapid state changes cause endless back-and-forth
            if (transitionToX == 20) {
                return;  // Coast-back in progress - pig needs a break
            }
            // Already sliding to grass position - queue grass
            pendingGrassStart = true;
            grassMoving = false;
        } else if (currentX != targetX) {
            // Not at correct treadmill position - slide there first
            startWindupSlide(targetX, directionRight);  // face direction of travel
            pendingGrassStart = true;
            grassMoving = false;
        } else {
            // Already at correct position - start grass immediately
            facingRight = !directionRight;
            grassMoving = true;
            pendingGrassStart = false;
        }

        // Clear cooldown since we successfully started
        lastGrassStopTime = 0;
        grassWanderTimer = millis();
        grassWanderInterval = random(3000, 8000);  // Initial delay before first wander
    } else {
        // Stop grass and coast back to resting position
        bool wasPlayer = s_playerWalkScroll;
        s_playerWalkScroll = false;
        grassMoving = false;
        pendingGrassStart = false;

        // Start cooldown timer - pig needs rest before grass can start again
        lastGrassStopTime = millis();

        // Reset walk timer to prevent immediate post-coast walk trigger
        lastFlipTime = millis();

        // Free-roam player walk: stay put (no edge park). Modes still coast home.
        if (!wasPlayer) {
            startWindupSlide(20, false);  // X=20, face left when done
        }
    }
}

void Avatar::setGrassSpeed(uint16_t ms) {
    grassSpeed = ms;
}

void Avatar::resetGrass() {
    grassOffset = 0;
    for (int i = 0; i < GRASS_BLADE_COUNT; i++) {
        uint8_t r = (uint8_t)(esp_random() % 100);
        if (r < 10) {
            grassBlades[i].kind = 4;
            grassBlades[i].height = random(5, 9);
            grassBlades[i].width = 2;
        } else if (r < 55) {
            grassBlades[i].kind = 0;
            grassBlades[i].height = random(11, 20);
            grassBlades[i].width = 2;
        } else if (r < 85) {
            grassBlades[i].kind = 1;
            grassBlades[i].height = random(10, 18);
            grassBlades[i].width = 2;
        } else if (r < 95) {
            grassBlades[i].kind = 2;
            grassBlades[i].height = random(11, 16);
            grassBlades[i].width = 1;
        } else {
            grassBlades[i].kind = 3;
            grassBlades[i].height = 2;
            grassBlades[i].width = 2;
        }
        grassBlades[i].lean = (int8_t)random(-3, 4);
        grassBlades[i].shade = (uint8_t)(esp_random() % 4);
    }
}

void Avatar::updateGrass() {
    if (!grassMoving) return;

    uint32_t now = millis();
    // Pixel-level scroll: shift 1px every grassSpeed/GRASS_STRIDE ms
    uint32_t pixelInterval = grassSpeed / GRASS_STRIDE;
    if (pixelInterval < 1) pixelInterval = 1;
    if (now - lastGrassUpdate < pixelInterval) return;
    lastGrassUpdate = now;

    // Player free-roam: multi-step scroll from SCROLL SPD (world only)
    int steps = s_playerWalkScroll ? scrollStepsPerTick() : 1;

    // Smooth pixel scroll (+ all flora kinds)
    for (int n = 0; n < steps; n++) {
        if (grassDirection) {
            grassOffset++;
            Trees::scroll(+1);
            SeasonalFx::scroll(+1);  // snow banks ride the same treadmill
            if (grassOffset >= GRASS_STRIDE) {
                grassOffset = 0;
                GrassBlade last = grassBlades[GRASS_BLADE_COUNT - 1];
                for (int i = GRASS_BLADE_COUNT - 1; i > 0; i--) {
                    grassBlades[i] = grassBlades[i - 1];
                }
                grassBlades[0] = last;
            }
        } else {
            grassOffset--;
            Trees::scroll(-1);
            SeasonalFx::scroll(-1);
            if (grassOffset < 0) {
                grassOffset = GRASS_STRIDE - 1;
                GrassBlade first = grassBlades[0];
                for (int i = 0; i < GRASS_BLADE_COUNT - 1; i++) {
                    grassBlades[i] = grassBlades[i + 1];
                }
                grassBlades[GRASS_BLADE_COUNT - 1] = first;
            }
        }
    }

    // Organic mutation
    if (random(0, 30) == 0) {
        int idx = random(0, GRASS_BLADE_COUNT);
        uint8_t r = (uint8_t)(esp_random() % 100);
        if (r < 10) {
            grassBlades[idx].kind = 4;
            grassBlades[idx].height = random(5, 9);
            grassBlades[idx].width = 2;
        } else if (r < 55) {
            grassBlades[idx].kind = 0;
            grassBlades[idx].height = random(11, 20);
            grassBlades[idx].width = 2;
        } else if (r < 85) {
            grassBlades[idx].kind = 1;
            grassBlades[idx].height = random(10, 18);
            grassBlades[idx].width = 2;
        } else if (r < 95) {
            grassBlades[idx].kind = 2;
            grassBlades[idx].height = random(11, 16);
            grassBlades[idx].width = 1;
        } else {
            grassBlades[idx].kind = 3;
            grassBlades[idx].height = 2;
            grassBlades[idx].width = 2;
        }
        grassBlades[idx].lean = (int8_t)random(-3, 4);
        grassBlades[idx].shade = (uint8_t)(esp_random() % 4);
    }
}

void Avatar::drawGrass(M5Canvas& canvas, bool frontLayer) {
    // Only scroll/update once (back pass)
    if (!frontLayer) updateGrass();

    uint32_t now = millis();
    const int16_t baseY = 106;  // Ground line — pig hooves sit here

    bool shakeActive = Display::isShaking();
    float shakeDecay = shakeActive ? Display::getShakeDecay() : 0.0f;
    uint8_t shakeIntensity = shakeActive ? Display::getShakeIntensity() : 0;

    // Season drives palette (classic fat geometry always)
    Season season = Weather::getActiveSeason();
    const bool isSpring = (season == Season::SPRING);
    const bool isAutumn = (season == Season::AUTUMN);
    const bool isWinter = (season == Season::WINTER);
    // SUMMER = classic summer greens

    const bool wetGrass = Weather::isRaining();
    const uint16_t dirtMid  = maybeFlash(0x8A40);
    const uint16_t dirtDark = maybeFlash(isWinter ? 0x6B6D : 0x5140);
    const uint16_t dirtLite = maybeFlash(isWinter ? 0xC618 : 0xA3E0);

    // Seasonal fat-blade palettes (same geometry as classic)
    // SUMMER = deep classic green; SPRING = fresh yellow-lime + flower carpet
    static const uint16_t PAL_SUMMER_BASE[4] = { 0x2C60, 0x3D00, 0x4DA0, 0x65C0 };
    static const uint16_t PAL_SUMMER_TIP[4]  = { 0x5DE0, 0x7E60, 0x9F00, 0xBFE0 };
    // Spring: chartreuse / young-shoot yellow-green (clearly not summer)
    static const uint16_t PAL_SPRING_BASE[4] = { 0x4C80, 0x6DA0, 0x8F00, 0xAFE0 };
    static const uint16_t PAL_SPRING_TIP[4]  = { 0xBFE0, 0xDFF2, 0xEFF8, 0xF7FE };
    static const uint16_t PAL_AUTUMN_BASE[4] = { 0x5A20, 0x8AC0, 0xB340, 0xC300 };
    static const uint16_t PAL_AUTUMN_TIP[4]  = { 0xD280, 0xE2C0, 0xFBE0, 0xFD20 };
    // Winter: frosty blue-gray grass with ice tips (the look you liked)
    static const uint16_t PAL_WINTER_BASE[4] = { 0x3C8E, 0x4D10, 0x5DB4, 0x7C9A };
    static const uint16_t PAL_WINTER_TIP[4]  = { 0x9CF3, 0xBDF7, 0xDEFB, 0xFFFF };

    const uint16_t* GRASS_BASE = PAL_SUMMER_BASE;
    const uint16_t* GRASS_TIP  = PAL_SUMMER_TIP;
    uint16_t turf0 = maybeFlash(wetGrass ? 0x2C40 : 0x3480);
    uint16_t turf1 = maybeFlash(wetGrass ? 0x3C60 : 0x4D00);
    uint16_t turf2 = maybeFlash(wetGrass ? 0x2C20 : 0x45A0);
    static const uint16_t FLOWER_SUMMER[4] = { 0xF800, 0xFFE0, 0xFD1F, 0xFFFF };
    // Spring blooms: white daisy, yellow dandelion, pink, violet
    static const uint16_t FLOWER_SPRING[4] = { 0xFFFF, 0xFFE0, 0xFD1F, 0xB01F };
    static const uint16_t FLOWER_AUTUMN[4] = { 0xFBE0, 0xFD20, 0xD280, 0xC300 };  // gold/orange
    static const uint16_t FLOWER_WINTER[4] = { 0xFFFF, 0xDEFB, 0xBDF7, 0x9CF3 };  // frost
    const uint16_t* FLOWER_COLS = FLOWER_SUMMER;

    if (isSpring) {
        GRASS_BASE = PAL_SPRING_BASE;
        GRASS_TIP  = PAL_SPRING_TIP;
        // Lush lime turf carpet (yellower than summer)
        turf0 = maybeFlash(wetGrass ? 0x4C60 : 0x6D80);
        turf1 = maybeFlash(0x8F20);
        turf2 = maybeFlash(0xBFE0);
        FLOWER_COLS = FLOWER_SPRING;
    } else if (isAutumn) {
        GRASS_BASE = PAL_AUTUMN_BASE;
        GRASS_TIP  = PAL_AUTUMN_TIP;
        turf0 = maybeFlash(wetGrass ? 0x4920 : 0x6A40);
        turf1 = maybeFlash(0x8AC0);
        turf2 = maybeFlash(0xB340);
        FLOWER_COLS = FLOWER_AUTUMN;
    } else if (isWinter) {
        GRASS_BASE = PAL_WINTER_BASE;
        GRASS_TIP  = PAL_WINTER_TIP;
        // Frosted blue-gray turf (stable pattern — no flicker)
        turf0 = maybeFlash(0x420C);
        turf1 = maybeFlash(0x5D14);
        turf2 = maybeFlash(0x9CF3);
        FLOWER_COLS = FLOWER_WINTER;
    }

    // === BACK LAYER ONLY: soil + turf carpet ===
    if (!frontLayer) {
        canvas.fillRect(0, 104, 240, 1, dirtMid);
        canvas.fillRect(0, 105, 240, 2, dirtDark);
        for (int x = 0; x < 240; x += 5) {
            // Static dirt speckles (no time term → no flicker)
            uint32_t h = (uint32_t)x * 2654435761u;
            if ((h & 3) == 0) canvas.drawPixel(x, 104, dirtLite);
        }
        // Classic dense band for all seasons (winter just colder colors)
        canvas.fillRect(0, 99, 240, 5, turf0);
        for (int x = 0; x < 240; x += 2) {
            uint32_t h = (uint32_t)x * 1103515245u + 12345u;  // static, not now
            if ((h & 3) != 0) canvas.drawPixel(x, 98, turf1);
            if ((h & 5) == 0) canvas.drawPixel(x + 1, 97, turf2);
        }
        // Darker turf patch under the pig (ground compression / shade)
        {
            int feet = currentX + 14 * PX;
            int pl = feet - 16 * PX / 2;
            int pr = feet + 18 * PX / 2;
            uint16_t darkTurf = maybeFlash(isWinter ? 0x3186 : (wetGrass ? 0x2140 : 0x2A40));
            uint16_t midDark  = maybeFlash(isWinter ? 0x4A49 : 0x3A60);
            for (int x = pl; x <= pr; x++) {
                if (x < 0 || x >= 240) continue;
                int dist = x - feet;
                if (dist < 0) dist = -dist;
                int maxd = (pr - pl) / 2;
                if (maxd < 1) maxd = 1;
                if (dist * 2 > maxd * 3) continue;
                canvas.drawPixel(x, 104, darkTurf);
                canvas.drawPixel(x, 105, darkTurf);
                if (dist * 3 < maxd * 2) {
                    canvas.drawPixel(x, 103, midDark);
                    canvas.drawPixel(x, 99, midDark);
                }
            }
        }
        if (isWinter) {
            // Stable frost dust (no time-based flicker)
            for (int x = 0; x < 240; x += 4) {
                uint32_t h = (uint32_t)x * 2654435761u;
                if ((h & 7) < 3) canvas.drawPixel(x, 97, maybeFlash(0xDEFB));
                if ((h & 15) == 0) canvas.fillRect(x, 98, 2, 1, maybeFlash(0xBDF7));
            }
        } else if (isSpring) {
            // Flower carpet on turf — little blooms (static hash, no flicker)
            for (int x = 4; x < 236; x += 7) {
                uint32_t h = (uint32_t)x * 2654435761u + 0x9E37u;
                if ((h & 7) > 4) continue;  // sparse
                uint16_t fc = maybeFlash(FLOWER_COLS[(h >> 3) & 3]);
                int16_t fy = 96 + (int)(h & 3);
                canvas.drawPixel(x, fy, fc);
                canvas.drawPixel(x + 1, fy, fc);
                // yellow daisy center on white blooms
                if (((h >> 3) & 3) == 0)
                    canvas.drawPixel(x, fy, maybeFlash(0xFFE0));
            }
        }
    }

    // Crouch phase of jump still counts as "on ground" for grass bend / shade
    bool jumpInAir = jumpActive && (millis() - jumpStartTime >= JUMP_CROUCH_MS);
    bool pigOnGround = !jumpInAir && !attackHopActive;
    int feet = currentX + 14 * PX;
    int pigLeft  = feet - 14 * PX;
    int pigRight = feet + 16 * PX;
    int pigCenter = (pigLeft + pigRight) / 2;
    int pigHalf  = (pigRight - pigLeft) / 2;
    if (pigHalf < 1) pigHalf = 1;

    int16_t treeScreenX = Trees::getFruitTreeScreenX();  // -1 if no fruit tree

    // Winter: frosty blades a bit shorter; spring: tender young shoots (+flowers)
    const int16_t heightBoost = isWinter ? 2 : (isSpring ? 3 : 4);

    for (int i = 0; i < GRASS_BLADE_COUNT; i++) {
        int16_t cx = i * GRASS_STRIDE + grassOffset;
        if (cx < -GRASS_STRIDE) cx += 240 + GRASS_STRIDE;
        if (cx >= 240) continue;

        // Depth split — pig sits BETWEEN layers:
        // near pig: ~half blades front (ankles only), half stay behind body
        // far: occasional tall front pops for parallax
        bool nearPig = (cx >= pigLeft - 6 && cx <= pigRight + 6);
        bool frontBlade;
        if (nearPig) {
            // NOT all blades front (was hiding the whole body/legs)
            frontBlade = ((i % 3) != 0);  // 2/3 front, 1/3 back
        } else {
            frontBlade = ((i % 5) == 0 && grassBlades[i].height >= 12);
        }
        if (frontLayer && !frontBlade) continue;
        if (!frontLayer && frontBlade) continue;

        // Winter: only lightly thinned (keep density for "иней" look)
        if (isWinter && ((i & 3) == 0)) continue;

        int16_t xJit = (int16_t)(((i * 17) ^ (i >> 2)) & 1);
        const GrassBlade& b = grassBlades[i];
        // Spring: force more flower stems visually (without rewriting blade table)
        uint8_t kind = b.kind;
        if (isSpring && kind == 0 && ((i % 3) == 0)) kind = 2;       // extra blooms
        if (isSpring && kind == 4 && ((i % 2) == 0)) kind = 2;       // stubble → sprouts
        int16_t drawHeight = (int16_t)(b.height + heightBoost);
        if (isSpring && kind == 2) drawHeight = (int16_t)(b.height + 5);  // flower stems taller
        // Front near pig: ankle-high only so body/legs stay readable
        if (frontLayer && nearPig) {
            if (drawHeight > 11) drawHeight = 11;
        } else if (frontLayer) {
            drawHeight = (int16_t)(drawHeight + 1);
        }
        if (drawHeight > 24) drawHeight = 24;
        int8_t drawLean = b.lean;

        // Wind sway — stronger so grass visibly waves
        {
            uint32_t phase = now + (uint32_t)i * 197;
            int period = wetGrass ? 1400 : (isWinter ? 1800 : 2200);
            int wave = (int)(phase % period);
            int half = period / 2;
            int sway = (wave < half) ? (wave - half / 2) : (period * 3 / 4 - wave);
            // Scale to ~±2..4 px lean (was weak / half-zero)
            sway = (sway * 4) / (half > 0 ? half : 1);
            if (wetGrass) sway = (sway * 3) / 2;
            if (isWinter) sway = (sway * 5) / 4;  // cold wind
            if (sway > 4) sway = 4;
            if (sway < -4) sway = -4;
            drawLean += (int8_t)sway;
        }

        // Bend under pig
        if (pigOnGround && cx >= pigLeft && cx <= pigRight) {
            int distFromCenter = cx - pigCenter;
            if (distFromCenter < 0) distFromCenter = -distFromCenter;
            float bend = 1.0f - (float)distFromCenter / (float)pigHalf;
            if (bend < 0) bend = 0;
            // Front blades bend less so they still poke past the pig
            float bendAmt = frontLayer ? 0.45f : 0.70f;
            drawHeight = drawHeight - (int16_t)((float)drawHeight * bendAmt * bend);
            if (drawHeight < 4) drawHeight = 4;
            int8_t leanPush = (int8_t)((frontLayer ? 3.0f : 5.0f) * bend);
            drawLean = (cx < pigCenter) ? (int8_t)(b.lean - leanPush)
                                        : (int8_t)(b.lean + leanPush);
        }

        if (shakeActive && shakeDecay > 0.05f) {
            float edgeDist = 1.0f - (float)(cx > 120 ? cx - 120 : 120 - cx) / 120.0f;
            if (edgeDist < 0.0f) edgeDist = 0.0f;
            float impact = edgeDist * shakeDecay * ((float)shakeIntensity / 5.0f);
            if (impact > 0.15f) {
                int8_t jitter = ((now / 33) % 2 == 0) ? 1 : -1;
                drawLean += jitter;
            }
        }

        if (treeColliding && treeScreenX >= 0) {
            int16_t dist = cx > treeScreenX ? cx - treeScreenX : treeScreenX - cx;
            int16_t radius = 60;  // fruit crown influence on grass
            if (dist < radius) {
                float falloff = 1.0f - (float)dist / (float)radius;
                uint32_t phase = now + (uint32_t)(dist * 7);
                int8_t jitter = ((phase / 33) % 2 == 0) ? 1 : -1;
                drawLean += (int8_t)((float)jitter * falloff);
            }
        }

        uint8_t sh = b.shade & 3;
        uint16_t colBase = maybeFlash(GRASS_BASE[sh]);
        uint16_t colTip  = maybeFlash(GRASS_TIP[sh]);
        // Darker blades under/around the pig (ground shade)
        if (pigOnGround && cx >= pigLeft && cx <= pigRight) {
            colBase = maybeFlash(GRASS_BASE[0]);
            colTip  = maybeFlash(GRASS_TIP[0]);
            if (frontLayer) {
                // slightly less dark on front so ankles still read
                colBase = maybeFlash(GRASS_BASE[sh > 0 ? sh - 1 : 0]);
            }
        }
        if (wetGrass && !isWinter) {
            colBase = maybeFlash(GRASS_BASE[0]);
            colTip  = maybeFlash(GRASS_TIP[1]);
        }

        int16_t bx = cx + xJit;
        int16_t by = baseY;

        if (kind == 3) {
            // Pebbles only on back layer (spring: tiny flower buds instead)
            if (frontLayer) continue;
            if (bx >= 0 && bx < 239) {
                if (isSpring) {
                    uint16_t fc = maybeFlash(FLOWER_COLS[b.shade % 4]);
                    canvas.drawPixel(bx, by - 2, fc);
                    canvas.drawPixel(bx + 1, by - 2, fc);
                    canvas.drawPixel(bx, by - 3, maybeFlash(0xFFE0));
                } else {
                    canvas.drawPixel(bx, by - 1, maybeFlash(isWinter ? 0xBDF7 : 0x9CD3));
                    canvas.drawPixel(bx + 1, by - 1, maybeFlash(0x8410));
                    canvas.drawPixel(bx, by - 2, maybeFlash(isWinter ? 0xFFFF : 0xBDF7));
                }
            }
            continue;
        }

        // === Classic fat double-stroke blades (all seasons) ===
        int16_t tipX = snapPx(cx + drawLean);
        int16_t tipY = snapPx(by - drawHeight);

        if (kind == 4) {
            // Short stubble — fat single stroke
            fatLine(canvas, bx, by, tipX, tipY, colBase);
            canvas.drawPixel(tipX, tipY, colTip);
            continue;
        }

        if (kind == 2) {
            // Flower / winter frost tip
            int16_t hy = tipY;
            int16_t hx = snapPx(bx + drawLean / 2);
            fatLine(canvas, bx, by, hx, hy, colBase);
            fatLine(canvas, bx + 1, by, hx + 1, hy, colBase);
            uint16_t fc = maybeFlash(FLOWER_COLS[b.shade % 4]);
            if (isWinter) {
                canvas.fillRect(hx, hy - 2, 2, 2, maybeFlash(0xDEFB));
                canvas.drawPixel(hx + 1, hy - 3, maybeFlash(0xFFFF));
            } else if (isSpring) {
                // Daisy / dandelion: petals + yellow center
                canvas.fillRect(hx - 1, hy - PX - 1, PX + 2, PX + 1, fc);
                canvas.drawPixel(hx - 2, hy - PX + 1, fc);
                canvas.drawPixel(hx + PX + 1, hy - PX + 1, fc);
                canvas.drawPixel(hx + 1, hy - PX - 2, fc);
                canvas.drawPixel(hx, hy - PX, maybeFlash(0xFFE0));
            } else {
                canvas.fillRect(hx, hy - PX, PX, PX, fc);
                canvas.drawPixel(hx - 1, hy - PX + 1, fc);
                canvas.drawPixel(hx + PX, hy - PX + 1, fc);
            }
            continue;
        }

        if (kind == 1) {
            // Thick tuft — full 5 blades; spring = clover cluster on top
            for (int t = -2; t <= 2; t++) {
                int16_t tx = snapPx(cx + drawLean + t * 2);
                int16_t ty = snapPx(by - drawHeight + abs(t));
                fatLine(canvas, bx, by, tx, ty, colBase);
                fatLine(canvas, bx + 1, by, tx + 1, ty, colBase);
                canvas.drawPixel(tx, ty, colTip);
                if (isWinter) {
                    canvas.fillRect(tx, ty - 2, 2, 2, maybeFlash(0xDEFB));
                } else if (isAutumn && (t & 1) && ty > 0) {
                    canvas.drawPixel(tx, ty - 1, maybeFlash(0xFD20));
                }
            }
            if (isSpring) {
                int16_t cxC = snapPx(cx + drawLean);
                int16_t cyC = snapPx(by - drawHeight - 1);
                uint16_t leaf = maybeFlash(0x07E0);
                canvas.fillRect(cxC - 2, cyC, 2, 2, leaf);
                canvas.fillRect(cxC + 1, cyC, 2, 2, leaf);
                canvas.fillRect(cxC - 1, cyC - 2, 2, 2, leaf);
                canvas.drawPixel(cxC, cyC + 1, maybeFlash(0x04A0));
            }
            continue;
        }

        // Normal blade: double fat stroke base → tip
        int16_t midY = (by + tipY) / 2;
        fatLine(canvas, bx, by, tipX, midY, colBase);
        fatLine(canvas, bx + 1, by, tipX + 1, midY, colBase);
        fatLine(canvas, tipX, midY, tipX, tipY, colTip);
        fatLine(canvas, tipX + 1, midY, tipX + 1, tipY, colTip);
        // Winter: icy tip (blue-white frost)
        if (isWinter) {
            canvas.fillRect(tipX, tipY - 2, 2, 2, maybeFlash(0xDEFB));
            canvas.drawPixel(tipX + 1, tipY - 3, maybeFlash(0xFFFF));
        } else if (isSpring && drawHeight >= 12) {
            canvas.drawPixel(tipX, tipY - 1, maybeFlash(0xEFF8));
            if ((b.shade & 1) && !frontLayer)
                canvas.drawPixel(tipX + 1, tipY, maybeFlash(0xFFFF));
        } else if (isAutumn && drawHeight >= 14 && (b.shade & 1)) {
            canvas.drawPixel(tipX, tipY - 1, maybeFlash(0xFD20));
        }
    }

    // Trail particles only once (front pass — after pig, so dust is on top)
    if (!frontLayer) return;

    // === Trail particles (dust from running pig) ===
    bool isRunning = transitioning || grassMoving;

    // Spawn one particle per ~70ms while pig is moving on the ground
    if (isRunning && pigOnGround && now - lastTrailSpawn > 70) {
        lastTrailSpawn = now;
        TrailParticle& p = trailParticles[trailSpawnIdx];
        trailSpawnIdx = (trailSpawnIdx + 1) % TRAIL_COUNT;

        // Spawn at pig trailing edge near the feet
        if (facingRight) {
            p.x = (float)(currentX + random(0, 3 * PX));
            p.vx = -(1.0f + (float)random(0, 20) / 10.0f);
        } else {
            p.x = (float)(currentX + (PIG_LAYOUT_W - 3) * PX + random(0, 3 * PX));
            p.vx = 1.0f + (float)random(0, 20) / 10.0f;
        }
        p.y = (float)(96 + random(0, 10));
        p.vy = -(0.2f + (float)random(0, 10) / 20.0f);  // slight upward drift
        p.startX = p.x;
        p.maxDist = 30.0f + (float)random(0, 31);  // 30-60px
        p.baseSize = random(1, 3);  // 1-2 px radius
        p.active = true;
    }

    // Update trail particles
    if (now - lastTrailUpdate > 50) {
        lastTrailUpdate = now;
        for (int i = 0; i < TRAIL_COUNT; i++) {
            if (!trailParticles[i].active) continue;
            trailParticles[i].x += trailParticles[i].vx;
            trailParticles[i].y += trailParticles[i].vy;
            float dx = trailParticles[i].x - trailParticles[i].startX;
            if (dx < 0) dx = -dx;
            if (dx >= trailParticles[i].maxDist) {
                trailParticles[i].active = false;
            }
        }
    }

    // Draw trail particles (fat-pixel dust)
    for (int i = 0; i < TRAIL_COUNT; i++) {
        if (!trailParticles[i].active) continue;
        int tpx = snapPx((int16_t)trailParticles[i].x);
        int tpy = snapPx((int16_t)trailParticles[i].y);
        if (tpx < 0 || tpx >= 240) continue;

        float tdx = trailParticles[i].x - trailParticles[i].startX;
        if (tdx < 0) tdx = -tdx;
        if (tdx >= trailParticles[i].maxDist) continue;

        canvas.fillRect(tpx, tpy, PX, PX, maybeFlash(C_DUST));
    }

    // === Fruit splash particles (burst on ground impact) ===
    static uint32_t lastSplashUpdate = 0;
    if (now - lastSplashUpdate > 50) {
        lastSplashUpdate = now;
        for (uint8_t i = 0; i < FRUIT_SPLASH_COUNT; i++) {
            if (!fruitSplashes[i].active) continue;
            fruitSplashes[i].x += fruitSplashes[i].vx;
            fruitSplashes[i].y += fruitSplashes[i].vy;
            fruitSplashes[i].vy += 0.15f;  // slight gravity pull-back
            if (now - fruitSplashes[i].spawnTime > 500) {
                fruitSplashes[i].active = false;
            }
        }
    }

    // Draw cheese splash particles
    for (uint8_t i = 0; i < FRUIT_SPLASH_COUNT; i++) {
        if (!fruitSplashes[i].active) continue;
        int spx = snapPx((int16_t)fruitSplashes[i].x);
        int spy = snapPx((int16_t)fruitSplashes[i].y);
        if (spx < 0 || spx >= 240) continue;

        float progress = (float)(now - fruitSplashes[i].spawnTime) / 500.0f;
        if (progress >= 1.0f) continue;

        canvas.fillRect(spx, spy, PX, PX, maybeFlash(C_FRUIT));
    }
}

// --- Fruit tree system (delegates to Trees module) ---
// Kinds: (1) FRUIT pushes pig + drops, (2) DECOR no fruit no push, (3) BERRY bush static berries no push

void Avatar::generateTree(uint8_t /*fruitCount*/) {
    // Legacy stub — generation lives in Trees::showFruit
}

void Avatar::showTree(uint8_t fruitCount) {
    Trees::setPigHint(currentX, onRightSide);
    Trees::showFruit(fruitCount);
}

void Avatar::hideTree() {
    Trees::hideFruit();
}

bool Avatar::isTreeVisible() {
    return Trees::isFruitVisible();
}

void Avatar::dropFruit() {
    Trees::dropFruit();
}

int16_t Avatar::getTreeScreenX() {
    return Trees::getFruitTreeScreenX();
}

bool Avatar::tryStompTree() {
    bool airborne = attackHopActive ||
                    (jumpActive && getJumpLiftPx() > 3);
    int feet = currentX + 14 * PX;
    bool hit = Trees::tryStompFruitTree(feet, airborne);
    if (hit) triggerSparkles(4);
    return hit;
}

bool Avatar::tryCollectNearbyFruit(int pigCenterX, int pigFeetY, int radius) {
    return Trees::tryCollectNearbyFruit(pigCenterX, pigFeetY, radius);
}

void Avatar::nudgeX(int dx) {
    if (dx == 0) return;
    // Can't scoot while playing dead (or mid-dead blend)
    if (s_playDead || s_deadBlend > 64) return;
    // Standing up from sit when you walk
    if (s_sitting) s_sitting = false;
    // Cancel walk transition so free-move is snappy (currentX already mid-path — keep it)
    if (transitioning) {
        // Stay at the interpolated position already written each frame
        transitioning = false;
        pendingGrassStart = false;
    }
    // Don't interrupt tree bonk mid-recoil (would snap)
    if (treeBonkPhase != 0) return;

    currentX += dx;
    if (currentX < 4) currentX = 4;
    if (currentX > 155) currentX = 155;
    onRightSide = (currentX > 60);
    if (dx > 0) facingRight = true;
    else facingRight = false;
    notifyPlayerControl();
}

void Avatar::setManualWalk(bool walking) {
    s_manualWalk = walking;
    if (walking) s_manualWalkUntil = millis() + 160;
}

void Avatar::setPlayerWalkScroll(bool walking, bool faceRight) {
    // Like Piggy Blues B-mode treadmill: world scrolls while we steer, but pig
    // stays free (no slide to edge park / coast-back).
    if (walking) {
        s_playerWalkScroll = true;
        s_manualWalk = true;
        s_manualWalkUntil = millis() + 220;
        grassMoving = true;
        pendingGrassStart = false;
        lastGrassStopTime = 0;
        grassSpeed = scrollGrassSpeedMs();  // SCENE → SCROLL SPD (world only)
        // World scrolls opposite walk direction
        grassDirection = !faceRight;
        facingRight = faceRight;
        if (transitioning) transitioning = false;
        notifyPlayerControl();
    } else if (s_playerWalkScroll) {
        s_playerWalkScroll = false;
        grassMoving = false;
        pendingGrassStart = false;
        grassSpeed = 80;  // restore mode treadmill default
        // Do NOT coast pig home — free roam stays put
        lastGrassStopTime = millis();
        s_manualWalk = false;
    }
}

void Avatar::playerWalkHold(int dir) {
    // dir: -1 left, +1 right, 0 stop
    // Classic side-scroller camera:
    //   middle ~34% — pig walks freely, world still
    //   hit left/right scroll line (~33% from each edge) — pig STAYS there,
    //   world scrolls so you see further ahead (no need to hug screen edge)
    const int STEP = kWalkStepPx;

    // Allow hold-walk during attack hop: still scroll if at rail
    if (dir == 0 || s_playDead || s_deadBlend > 64) {
        if (!attackHopActive) {
            setPlayerWalkScroll(false, facingRight);
            setManualWalk(false);
        }
        return;
    }

    if (s_sitting) s_sitting = false;
    if (transitioning) {
        transitioning = false;
        pendingGrassStart = false;
    }
    if (treeBonkPhase != 0) return;

    bool faceR = (dir > 0);
    // During attack hop, hop owns X motion — only assist with world scroll at rails
    if (attackHopActive) {
        facingRight = faceR ? faceR : facingRight;
        if (faceR && currentX >= kCamRightLine) {
            currentX = kCamRightLine;
            setPlayerWalkScroll(true, true);
        } else if (!faceR && currentX <= kCamLeftLine) {
            currentX = kCamLeftLine;
            setPlayerWalkScroll(true, false);
        }
        s_manualWalk = true;
        s_manualWalkUntil = millis() + 220;
        return;
    }

    facingRight = faceR;
    s_manualWalk = true;
    s_manualWalkUntil = millis() + 220;
    notifyPlayerControl();

    if (dir < 0) {
        if (currentX > kCamLeftLine) {
            setPlayerWalkScroll(false, false);
            currentX -= STEP;
            if (currentX < kCamLeftLine) currentX = kCamLeftLine;
        } else {
            setPlayerWalkScroll(true, false);
            currentX = kCamLeftLine;
        }
    } else {
        if (currentX < kCamRightLine) {
            setPlayerWalkScroll(false, true);
            currentX += STEP;
            if (currentX > kCamRightLine) currentX = kCamRightLine;
        } else {
            setPlayerWalkScroll(true, true);
            currentX = kCamRightLine;
        }
    }
    onRightSide = (currentX > 60);
}

void Avatar::notifyPlayerControl() {
    // Delay AI auto-roam so player owns the pig for a while
    lastFlipTime = millis();
    flipInterval = random(18000, 40000);
    pendingGrassStart = false;
    if (transitioning) {
        transitioning = false;
    }
    // Brief leg anim even without grass treadmill
    s_manualWalk = true;
    s_manualWalkUntil = millis() + 160;
}

void Avatar::setSitting(bool on) {
    s_sitting = on;
    if (on) {
        s_playDead = false;
        jumpActive = false;
        notifyPlayerControl();
    }
}

bool Avatar::isSitting() { return s_sitting; }

void Avatar::setPlayDead(bool on) {
    // Cannot stand up while wolf-bite stun is active
    if (!on && isControlLocked()) return;
    s_playDead = on;
    if (on) {
        s_sitting = false;
        jumpActive = false;
        attackHopActive = false;
        s_manualWalk = false;
        s_playerWalkScroll = false;
        grassMoving = false;
        notifyPlayerControl();
        setState(AvatarState::SLEEPY);
    }
}

bool Avatar::isPlayDead() { return s_playDead; }

void Avatar::onWolfBitten() {
    // Play-dead + control lock. Shorter in Fruit Run (lives handle death).
    uint32_t lockMs = kWolfBiteLockMs;
    if (FruitRunMode::isRunning()) lockMs = 1800;  // brief stun, not 10s
    s_controlLockUntil = millis() + lockMs;
    attackHopActive = false;
    jumpActive = false;
    setPlayerWalkScroll(false, facingRight);
    setManualWalk(false);
    setPlayDead(true);
    setState(AvatarState::SAD);
    SFX::play(SFX::OINK_GRUNT);
}

bool Avatar::isControlLocked() {
    if (s_controlLockUntil == 0) return false;
    return (int32_t)(millis() - s_controlLockUntil) < 0;
}

void Avatar::updateTree() {
    // Phases updated inside Trees::draw
}

void Avatar::drawTree(M5Canvas& canvas, int16_t yOffset, bool /*doUpdate*/) {
    Trees::setPigHint(currentX, onRightSide);
    Trees::draw(canvas, yOffset);
}

// --- Night sky star system ---
// skyMode: 0=AUTO (RTC/synthetic with dusk/dawn), 1=always DAY, 2=always NIGHT
bool Avatar::isNightTime() {
    uint32_t now = millis();
    uint16_t amt = (s_lastNightBlendMs != 0) ? s_nightBlend : computeNightTarget(now);
    return (amt >= 128);
}

void Avatar::drawTreeBarOverflow(M5Canvas& bar) {
    Trees::drawBarOverflow(bar);
}
void Avatar::initStarPositions() {
    // Pre-gen star positions, hide until spawn
    for (uint8_t i = 0; i < MAX_STARS; i++) {
        // y 20-100 sky/backdrop, bubble still wins
        // x 5-235 near full width
        stars[i].x = random(5, 235);
        // Match rain clip: keep stars above grass (rain clips at y < 88)
        stars[i].y = random(35, 103);
        stars[i].size = 1;
        stars[i].brightness = 0;
        stars[i].fadeInStart = 0;
        // About 20 percent twinkle
        stars[i].isBlinking = (random(0, 100) < 20);
    }
}

void Avatar::updateStars() {
    uint32_t now = millis();
    // Keep blend advancing even if sky draw order varies
    updateNightBlend(now);
    const uint16_t nb = s_nightBlend;

    // Never show stars while raining
    if (Weather::isRaining()) {
        if (starsActive) {
            starsActive = false;
            starCount = 0;
        }
        return;
    }

    // Stars appear mid-dusk; fade out toward dawn (no hard kill)
    if (nb >= 140 && !starsActive) {
        starsActive = true;
        starCount = 0;
        lastStarSpawn = now;
        nextSpawnDelay = random(600, 2500);
        initStarPositions();
    } else if (nb < 80 && starsActive) {
        starsActive = false;
        starCount = 0;
        return;
    }

    if (!starsActive) return;

    // Spawn new star when timer expires
    if (starCount < MAX_STARS && (now - lastStarSpawn >= nextSpawnDelay)) {
        stars[starCount].fadeInStart = now;
        stars[starCount].brightness = 0;
        starCount++;
        lastStarSpawn = now;
        nextSpawnDelay = random(800, 4001);
    }

    // Fade-in per star + global gate from night blend (smooth dusk/dawn)
    // nb 80..200 maps to gate 0..255
    uint16_t gate = 255;
    if (nb < 200) {
        if (nb <= 80) gate = 0;
        else gate = (uint16_t)(((nb - 80) * 255) / 120);
    }

    for (uint8_t i = 0; i < starCount; i++) {
        uint32_t age = now - stars[i].fadeInStart;
        uint16_t local = (age < 500) ? (uint16_t)((age * 255) / 500) : 255;
        stars[i].brightness = (uint8_t)((local * gate) / 255);
    }
}


void Avatar::drawStars(M5Canvas& canvas) {
    if (!starsActive || starCount == 0) return;

    uint32_t now = millis();

    for (uint8_t i = 0; i < starCount; i++) {
        if (stars[i].brightness < 40) continue;
        if (stars[i].y >= 103) continue;  // Match rain clip above grass

        int16_t sx = snapPx(stars[i].x);
        int16_t sy = snapPx(stars[i].y);
        // Dim stars: softer color until fully bright
        uint16_t col = maybeFlash(C_STAR);
        if (stars[i].brightness < 200) {
            col = lerp565(s_skyTop, C_STAR, (uint8_t)((stars[i].brightness * 16) / 255));
            col = maybeFlash(col);
        }
        canvas.fillRect(sx, sy, PX, PX, col);
        if (stars[i].isBlinking && stars[i].brightness > 180) {
            uint32_t phase = (now + i * 700) % 4000;
            if (phase >= 1700 && phase < 2300) {
                canvas.fillRect(sx - PX, sy, PX, PX, col);
                canvas.fillRect(sx + PX, sy, PX, PX, col);
                canvas.fillRect(sx, sy - PX, PX, PX, col);
                canvas.fillRect(sx, sy + PX, PX, PX, col);
            }
        }
    }
}

// --- Phase 8: Direction control helpers ---
void Avatar::setFacingLeft() {
    facingRight = false;
}

void Avatar::setFacingRight() {
    facingRight = true;
}

// --- Phase 2: Attack shake control ---
void Avatar::setAttackShake(bool active, bool strong) {
    attackShakeActive = active;
    attackShakeStrong = strong;
    attackShakeRefreshTime = active ? millis() : 0;
}

// --- Thunder flash control (weather effect) ---
void Avatar::setThunderFlash(bool active) {
    thunderFlashActive = active;
}

bool Avatar::isThunderFlashing() {
    return thunderFlashActive;
}

// --- Wave ripple animation (radio activity feedback) ---
// Burst-based: each call extends the burst deadline without resetting phase.
// waveBurstStart only sets on NONE→active transition (keeps rings phase-coherent).
// OUTGOING priority prevents INCOMING flicker.
void Avatar::setMicDance(float level) {
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    micDanceLevel = level;
}

void Avatar::waveRipple(WaveMode mode, uint8_t intensity) {
    if (mode == WaveMode::NONE) {
        waveMode = WaveMode::NONE;
        return;
    }
    uint32_t now = millis();
    // OUTGOING priority: don't let INCOMING override an active OUTGOING burst
    if (mode == WaveMode::INCOMING && waveMode == WaveMode::OUTGOING) {
        if (now < waveBurstEnd) return;  // OUTGOING still active
    }
    bool alreadyActive = (waveMode != WaveMode::NONE && now < waveBurstEnd);
    waveMode = mode;
    waveBurstEnd = now + 4000;  // extend deadline (longer for 1.5x slower cycle)
    if (!alreadyActive) {
        waveBurstStart = now;   // phase origin only on fresh start
    }
    waveIntensity = intensity;
}

// Invert RGB565 (for wave rings that must contrast with sky)
static inline uint16_t invert565(uint16_t c) {
    int r = 31 - ((c >> 11) & 0x1F);
    int g = 63 - ((c >> 5) & 0x3F);
    int b = 31 - (c & 0x1F);
    // keep a little punch so pure invert of mid-blue isn't muddy
    if (r < 4) r = 4; if (g < 8) g = 8; if (b < 4) b = 4;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// Midpoint circle on PX grid. thickness = number of concentric fat rings (1..3).
// reflect=true folds off-screen points back via reflectAxis; reflect=false clips.
static void drawCircleRing(M5Canvas& canvas, int16_t cx, int16_t cy,
                            int16_t r, uint16_t color, bool reflect,
                            int16_t maxPxX, int16_t maxPxY, uint8_t thickness = 1) {
    if (thickness < 1) thickness = 1;
    if (thickness > 3) thickness = 3;
    cx = snapPx(cx);
    cy = snapPx(cy);

    for (uint8_t t = 0; t < thickness; t++) {
        int16_t rr = r + (int16_t)t * PX;
        int16_t gr = rr / PX;
        if (gr < 1) continue;
        int16_t gx = gr, gy = 0, d = 1 - gr;

        while (gx >= gy) {
            const int16_t ox[8] = { gx, (int16_t)-gx,  gx, (int16_t)-gx,  gy, (int16_t)-gy,  gy, (int16_t)-gy };
            const int16_t oy[8] = { gy,  gy, (int16_t)-gy, (int16_t)-gy,  gx,  gx, (int16_t)-gx, (int16_t)-gx };
            for (uint8_t p = 0; p < 8; p++) {
                int16_t px = cx + ox[p] * PX;
                int16_t py = cy + oy[p] * PX;
                if (reflect) {
                    uint8_t bounces = 0;
                    px = reflectAxis(px, maxPxX, bounces);
                    py = reflectAxis(py, maxPxY, bounces);
                    if (bounces >= 4) continue;
                    if (bounces == 3 && ((px / PX + py / PX) & 1)) continue;
                    if (bounces == 2 && ((px / PX + py / PX) % 3 == 0)) continue;
                } else {
                    if (px < 0 || px > maxPxX || py < 0 || py > maxPxY) continue;
                }
                // Fat stamp: PX x PX plus 1px grow for "жирнее" look
                canvas.fillRect(px, py, PX + 1, PX + 1, color);
            }
            gy++;
            if (d < 0) { d += 2 * gy + 1; }
            else       { gx--; d += 2 * (gy - gx) + 1; }
        }
    }
}

void Avatar::drawWaveRipples(M5Canvas& canvas, bool faceRight, int startX, int startY) {
    if (waveMode == WaveMode::NONE) return;

    uint32_t now = millis();

    // Geiger-counter clicks while waves are actively bursting
    static uint32_t nextGeigerClick = 0;
    if (now < waveBurstEnd && now >= nextGeigerClick) {
        uint16_t freq = (uint16_t)random(800, 1600);
        SFX::tone(freq, random(3, 8));
        nextGeigerClick = now + random(80, 300);
    }

    // Gradual fade: after burst ends, suppress young rings over one cycle
    const uint16_t FADE_MS = 3600;
    float minProgress = 0.0f;
    if (now >= waveBurstEnd) {
        uint32_t fadeElapsed = now - waveBurstEnd;
        if (fadeElapsed >= FADE_MS) { waveMode = WaveMode::NONE; return; }
        minProgress = (float)fadeElapsed / (float)FADE_MS * 0.80f;
    }

    // Ring color = invert of current sky (always readable over day/night/rain)
    uint16_t color = maybeFlash(invert565(s_skyTop));
    // Soft second tone (slightly dimmer invert) for outer edge depth
    uint16_t colorOuter = maybeFlash(invert565(
        (uint16_t)(((s_skyTop >> 1) & 0x7BEF) | 0x1082)));  // slightly lifted sky

    const bool outgoing = (waveMode == WaveMode::OUTGOING);

    // startX/Y = feet center (procedural pig). Snout at local (+9,-12)
    int waveCX = faceRight ? (startX + 9 * PX) : (startX - 9 * PX);
    int waveCY = startY - 12 * PX;

    // Both modes: thick propagating circles that clip at screen edges
    const uint8_t  COUNT    = outgoing ? 5 : waveIntensity;
    const uint16_t CYCLE_MS = 3600;
    const int16_t  R_MIN    = 0;
    const int16_t  R_MAX    = 130;
    const int16_t  MAX_PX_X = DISPLAY_W - PX;
    const int16_t  MAX_PX_Y = MAIN_H - PX;
    const int16_t GRID_STEPS = (R_MAX - R_MIN) / PX;

    uint32_t elapsed = now - waveBurstStart;

    for (uint8_t i = 0; i < COUNT; i++) {
        uint32_t phaseOffset = i * (CYCLE_MS / COUNT);
        uint32_t phase = (elapsed + phaseOffset) % CYCLE_MS;
        float progress = (float)phase / (float)CYCLE_MS;

        if (progress < minProgress) continue;
        if (progress > 0.80f) continue;
        float t = progress / 0.80f;

        int16_t gridStep = (int16_t)(t * GRID_STEPS);
        int16_t rRaw = R_MIN + gridStep * PX;
        int16_t r = outgoing
            ? snapPx(rRaw)
            : snapPx(R_MIN + R_MAX - rRaw);

        // Thickness: fat rings always (2), extra fat when young (3)
        uint8_t thick = (t < 0.45f) ? 3 : 2;

        // Outer dim invert + inner bright invert = readable on any sky
        drawCircleRing(canvas, waveCX, waveCY, r + PX, colorOuter, false, MAX_PX_X, MAX_PX_Y, 1);
        drawCircleRing(canvas, waveCX, waveCY, r, color, false, MAX_PX_X, MAX_PX_Y, thick);

        if (outgoing) {
            // Fruit tree shake when OUTGOING ring hits crown
            int16_t tbx = Trees::getFruitTreeScreenX();
            if (tbx >= 0) {
                int32_t dx = tbx - waveCX;
                int32_t dy = 106 - waveCY;
                int32_t dist2 = dx * dx + dy * dy;
                const int32_t crownR = 22;
                int32_t rOuter = r + crownR;
                int32_t rInner = r - crownR;
                if (rInner < 0) rInner = 0;
                if (dist2 <= rOuter * rOuter && dist2 >= rInner * rInner) {
                    Trees::shakeFromWave();
                }
            }
        }
    }
}

// --- Bird-wave collision check (called by Weather bird system) ---
bool Avatar::checkBirdWaveCollision(int16_t bx, int16_t by) {
    if (waveMode != WaveMode::OUTGOING) return false;

    uint32_t now = millis();
    if (now >= waveBurstEnd) return false;  // burst already fading

    // Compute wave origin (mirrors drawWaveRipples logic)
    bool faceR = facingRight;
    // feet ≈ currentX + 14*PX, snout local (+9,-12)
    int feetX = currentX + 14 * PX;
    int waveCX = faceR ? (feetX + 9 * PX) : (feetX - 9 * PX);
    int waveCY = 106 - 12 * PX;

    // Burst-relative elapsed time
    uint32_t elapsed = now - waveBurstStart;
    const uint16_t CYCLE_MS = 3600;
    const int16_t  R_MIN = 0;
    const int16_t  R_MAX = 130;
    const uint8_t  COUNT = 5;

    int32_t dx = (int32_t)bx - waveCX;
    int32_t dy = (int32_t)by - waveCY;
    int32_t dist2 = dx * dx + dy * dy;

    for (uint8_t i = 0; i < COUNT; i++) {
        uint32_t phaseOffset = i * (CYCLE_MS / COUNT);
        uint32_t phase = (elapsed + phaseOffset) % CYCLE_MS;
        float progress = (float)phase / (float)CYCLE_MS;
        if (progress > 0.80f) continue;
        float t = progress / 0.80f;

        int16_t r = (int16_t)(R_MIN + t * (R_MAX - R_MIN));
        int32_t rOuter = r + 4;
        int32_t rInner = r - 4;
        if (rInner < 0) rInner = 0;
        if (dist2 <= rOuter * rOuter && dist2 >= rInner * rInner) {
            return true;
        }
    }
    return false;
}

// --- Phase 6: Windup slide for coast-back ---
void Avatar::startWindupSlide(int targetX, bool faceRight) {
    // Start a smooth transition to target position
    // Uses standard TRANSITION_DURATION_MS (300ms) from draw() logic
    if (currentX != targetX) {
        transitioning = true;
        transitionFromX = currentX;
        transitionToX = targetX;
        transitionStartTime = millis();
        transitionToFacingRight = faceRight;

        // Dust burst on walk start: spawn 4 trail particles at pig's feet
        for (int b = 0; b < 4; b++) {
            TrailParticle& p = trailParticles[trailSpawnIdx];
            trailSpawnIdx = (trailSpawnIdx + 1) % TRAIL_COUNT;
            p.x = (float)(currentX + 40 + random(-15, 16));
            p.y = (float)(96 + random(0, 8));
            p.vx = (float)(random(-20, 21)) / 10.0f;
            p.vy = -(0.5f + (float)random(0, 10) / 10.0f);
            p.startX = p.x;
            p.maxDist = 20.0f + (float)random(0, 21);
            p.baseSize = random(1, 3);
            p.active = true;
        }
    }
    // Set facing direction for when transition completes
    facingRight = faceRight;
}

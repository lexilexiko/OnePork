// Seasonal decorative animations (not core precip / clouds).
// Keep this file the home for "pretty season stuff".

#include "seasonal_fx.h"
#include "weather.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include <esp_random.h>
#include <math.h>

namespace SeasonalFx {

// ---------------------------------------------------------------------------
// Winter: snow banks (сугробы) — fat-pixel steps on ground line
// Can grow tall; pig trampling melts a path
// ---------------------------------------------------------------------------
struct SnowBank {
    int16_t x;
    uint8_t w;  // width px
    uint8_t h;  // fat rows 1..MAX
};
static constexpr int SNOW_BANK_COUNT = 12;
static constexpr uint8_t SNOW_BANK_MAX_H = 8;  // tall drifts
static SnowBank snowBanks[SNOW_BANK_COUNT] = {};
static uint32_t lastBankGrowMs = 0;
static bool snowBanksInited = false;
static uint32_t lastRainSfxMs = 0;

// ---------------------------------------------------------------------------
// Autumn: falling leaves (denser / more visible)
// ---------------------------------------------------------------------------
struct Leaf {
    float x, y;
    float vx, vy;
    uint8_t col;
    uint8_t size;
    bool active;
};
static constexpr int LEAF_COUNT = 22;
static Leaf leaves[LEAF_COUNT] = {};
static uint32_t lastLeafUpdate = 0;

// ---------------------------------------------------------------------------
// Autumn: tumbleweed (перекати-поле)
// ---------------------------------------------------------------------------
struct Tumbleweed {
    float x, y;
    float vx;
    uint8_t phase;
    bool active;
};
static Tumbleweed tumble = {};
static uint32_t lastTumbleSpawnMs = 0;

// ---------------------------------------------------------------------------
// Summer: butterflies (brighter, more of them)
// ---------------------------------------------------------------------------
struct Butterfly {
    float x, y;
    float vx, vy;
    uint8_t phase;
    uint8_t hue;  // color variant
    bool active;
};
static constexpr int BUTTERFLY_COUNT = 5;
static Butterfly butterflies[BUTTERFLY_COUNT] = {};
static uint32_t lastButterflySpawnMs = 0;

// ---------------------------------------------------------------------------
// Summer: pollen / heat motes (golden dust in air)
// ---------------------------------------------------------------------------
struct PollenMote {
    float x, y;
    float vx, vy;
    uint8_t phase;
    bool active;
};
static constexpr int POLLEN_COUNT = 16;
static PollenMote pollen[POLLEN_COUNT] = {};
static uint32_t lastPollenUpdate = 0;

// ---------------------------------------------------------------------------
// Spring: sky lightning bolts (behind tree/pig)
// ---------------------------------------------------------------------------
struct LightningBolt {
    int16_t x0;           // start X at top
    int8_t  segDx[7];     // horizontal jogs
    int8_t  segDy[7];     // vertical steps (always +)
    uint8_t segs;
    uint8_t life;         // frames remaining
    uint8_t bright;       // 0=dim fork, 1=main
    bool active;
};
static constexpr int BOLT_COUNT = 2;
static LightningBolt bolts[BOLT_COUNT] = {};
static uint32_t lastBoltSpawnMs = 0;
static bool wasThunderFlash = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool isSpring() { return Weather::getActiveSeason() == Season::SPRING; }
static bool isSummer() { return Weather::getActiveSeason() == Season::SUMMER; }
static bool isAutumn() { return Weather::getActiveSeason() == Season::AUTUMN; }
static bool isWinter() { return Weather::getActiveSeason() == Season::WINTER; }

// ---------------------------------------------------------------------------
// Winter banks
// ---------------------------------------------------------------------------
static void initSnowBanks() {
    for (int i = 0; i < SNOW_BANK_COUNT; i++) {
        snowBanks[i].x = (int16_t)(i * 20 + random(0, 10));
        snowBanks[i].w = (uint8_t)random(18, 34);
        // Some already tall — winter should feel deep right away
        snowBanks[i].h = (uint8_t)random(3, 7);
    }
    snowBanksInited = true;
    lastBankGrowMs = millis();
}

static void updateSnowBanks(uint32_t now) {
    if (!isWinter()) {
        snowBanksInited = false;
        for (int i = 0; i < SNOW_BANK_COUNT; i++) snowBanks[i].h = 0;
        return;
    }
    if (!snowBanksInited) initSnowBanks();

    int alive = 0;
    for (int i = 0; i < SNOW_BANK_COUNT; i++)
        if (snowBanks[i].h > 0) alive++;
    if (alive == 0) {
        initSnowBanks();
        return;
    }

    if (now - lastBankGrowMs < 400) return;
    lastBankGrowMs = now;

    if (Weather::isSnowing()) {
        // Grow tall drifts (up to SNOW_BANK_MAX_H) — can become walls of snow
        for (int n = 0; n < 4; n++) {
            int i = random(0, SNOW_BANK_COUNT);
            if (snowBanks[i].h == 0) {
                // Rebuild a trampled gap
                snowBanks[i].x = (int16_t)random(0, DISPLAY_W - 24);
                snowBanks[i].w = (uint8_t)random(14, 28);
                snowBanks[i].h = (uint8_t)random(2, 4);
            } else if (snowBanks[i].h < SNOW_BANK_MAX_H) {
                snowBanks[i].h++;
            } else if (random(0, 100) < 15) {
                snowBanks[i].x = (int16_t)random(0, DISPLAY_W - 24);
                snowBanks[i].w = (uint8_t)random(18, 34);
                snowBanks[i].h = (uint8_t)random(4, 7);
            }
            if (snowBanks[i].h > 0 && snowBanks[i].w < 36 && random(0, 100) < 50)
                snowBanks[i].w += 2;
        }
    } else {
        // Very slow natural melt, never wipe banks in winter
        int i = random(0, SNOW_BANK_COUNT);
        if (snowBanks[i].h > 2) snowBanks[i].h--;
    }
}

static void drawSnowBanks(M5Canvas& canvas, bool flash) {
    if (!isWinter() || !snowBanksInited) return;
    const int S = 3;
    for (int i = 0; i < SNOW_BANK_COUNT; i++) {
        int rows = (int)snowBanks[i].h;
        if (rows < 1) continue;
        if (rows > (int)SNOW_BANK_MAX_H) rows = SNOW_BANK_MAX_H;
        int16_t bx = snowBanks[i].x;
        while (bx > 260) bx -= 300;
        while (bx < -60) bx += 300;
        int wBlocks = (int)snowBanks[i].w / S;
        if (wBlocks < 2) wBlocks = 2;
        if (wBlocks > 11) wBlocks = 11;

        for (int r = 0; r < rows; r++) {
            int inset = r / 2;  // steeper tall banks
            int bw = wBlocks - inset * 2;
            if (bw < 1) break;
            int16_t top = (int16_t)(107 - S * (r + 1));
            if (top < 78) break;  // allow taller drifts
            int16_t left = (int16_t)(bx + inset * S);
            uint16_t col = flash ? 0xFFFF
                                 : ((r == rows - 1) ? 0xFFFF : ((r & 1) ? 0xDEFB : 0xEF7D));
            canvas.fillRect(left, top, bw * S, S, col);
            if (r == 0)
                canvas.fillRect(left, top + S - 1, bw * S, 1, flash ? 0xFFFF : 0xC618);
        }
    }
}

void scroll(int dir) {
    if (!snowBanksInited) return;
    const int16_t d = (dir > 0) ? 1 : -1;
    for (int i = 0; i < SNOW_BANK_COUNT; i++) {
        if (snowBanks[i].h < 1) continue;
        snowBanks[i].x = (int16_t)(snowBanks[i].x + d);
        if (snowBanks[i].x > 260) snowBanks[i].x -= 300;
        if (snowBanks[i].x < -60) snowBanks[i].x += 300;
    }
}

void trampleSnow(int pigFeetX) {
    if (!isWinter() || !snowBanksInited) return;
    // Throttle so tall drifts don't vanish in one frame burst
    static uint32_t lastTrampleMs = 0;
    uint32_t now = millis();
    if (now - lastTrampleMs < 120) return;
    lastTrampleMs = now;

    // Melt path under pig (trampled road / протоптанная тропинка)
    const int R = 18;
    for (int i = 0; i < SNOW_BANK_COUNT; i++) {
        if (snowBanks[i].h < 1) continue;
        int16_t bx = snowBanks[i].x;
        while (bx > 260) bx -= 300;
        while (bx < -60) bx += 300;
        int16_t left = bx;
        int16_t right = bx + (int16_t)snowBanks[i].w;
        // Feet near bank body?
        if (pigFeetX < left - R || pigFeetX > right + R) continue;
        if (snowBanks[i].h > 0) snowBanks[i].h--;
        // Carve a notch: shrink width from the side the pig is on
        if (snowBanks[i].w > 8 && (esp_random() & 1)) {
            snowBanks[i].w -= 2;
            if (pigFeetX < (left + right) / 2)
                snowBanks[i].x = (int16_t)(snowBanks[i].x + 1);  // eat from left
        }
        // Fully flattened → leave a gap (path) until snow rebuilds
        if (snowBanks[i].h == 0) {
            snowBanks[i].w = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// Autumn leaves
// ---------------------------------------------------------------------------
static void updateLeaves(uint32_t now) {
    if (!isAutumn()) {
        for (int i = 0; i < LEAF_COUNT; i++) leaves[i].active = false;
        return;
    }
    if (now - lastLeafUpdate < 35) return;
    lastLeafUpdate = now;

    bool wet = Weather::isRaining();
    // Dense cascade — autumn signature (more on wet wind)
    int want = wet ? LEAF_COUNT : 14;
    int active = 0;
    for (int i = 0; i < LEAF_COUNT; i++) if (leaves[i].active) active++;

    // Spawn up to 2 per tick so sky fills faster
    int spawns = 0;
    while (active < want && spawns < 2 && random(0, 100) < 55) {
        for (int i = 0; i < LEAF_COUNT; i++) {
            if (leaves[i].active) continue;
            leaves[i].active = true;
            leaves[i].x = (float)random(0, DISPLAY_W);
            leaves[i].y = (float)random(-12, 16);
            leaves[i].vx = (float)(random(0, 24) - 10) / 10.0f;
            leaves[i].vy = 0.55f + (float)random(0, 28) / 20.0f;
            leaves[i].col = (uint8_t)(esp_random() % 5);
            leaves[i].size = (uint8_t)random(3, 6);  // chunkier = more visible
            active++;
            spawns++;
            break;
        }
        if (spawns == 0) break;
    }

    for (int i = 0; i < LEAF_COUNT; i++) {
        if (!leaves[i].active) continue;
        leaves[i].x += leaves[i].vx;
        leaves[i].y += leaves[i].vy;
        // Flutter wiggle
        leaves[i].vx += (float)(random(0, 3) - 1) * 0.08f;
        leaves[i].x += sinf(leaves[i].y * 0.12f) * 0.35f;
        if (leaves[i].vx > 1.5f) leaves[i].vx = 1.5f;
        if (leaves[i].vx < -1.5f) leaves[i].vx = -1.5f;
        if (leaves[i].y > 104.0f || leaves[i].x < -12.0f ||
            leaves[i].x > (float)DISPLAY_W + 12.0f) {
            leaves[i].active = false;
        }
    }
}

static void drawLeaves(M5Canvas& canvas, bool flash) {
    if (!isAutumn()) return;
    // Crimson / gold / orange / rust / brown — bold autumn palette
    static const uint16_t LEAF_COLS[5] = {
        0xE2C0, 0xFD20, 0xFB40, 0xC300, 0x9A40
    };
    for (int i = 0; i < LEAF_COUNT; i++) {
        if (!leaves[i].active) continue;
        int16_t lx = (int16_t)leaves[i].x;
        int16_t ly = (int16_t)leaves[i].y;
        if (lx < -6 || lx >= DISPLAY_W + 6 || ly < -6 || ly >= 106) continue;
        uint16_t c = flash ? 0xFFFF : LEAF_COLS[leaves[i].col % 5];
        int s = leaves[i].size;
        // Fat diamond leaf shape
        canvas.fillRect(lx, ly, s, s - 1, c);
        canvas.fillRect(lx + 1, ly - 1, s - 2, 1, c);
        canvas.drawPixel(lx + s / 2, ly + s - 1, c);  // tip
    }
}

// ---------------------------------------------------------------------------
// Autumn tumbleweed
// ---------------------------------------------------------------------------
static void updateTumbleweed(uint32_t now) {
    if (!isAutumn()) {
        tumble.active = false;
        return;
    }

    if (!tumble.active) {
        if (lastTumbleSpawnMs == 0) lastTumbleSpawnMs = now;
        // More often — autumn signature critter
        if (now - lastTumbleSpawnMs < 5500) return;
        if (now - lastTumbleSpawnMs < 9000 && random(0, 100) > 18) return;

        tumble.active = true;
        tumble.y = 99.0f;
        tumble.phase = 0;
        bool goRight = (random(0, 2) == 0);
        tumble.vx = goRight ? (1.8f + (float)random(0, 12) / 10.0f)
                            : -(1.8f + (float)random(0, 12) / 10.0f);
        tumble.x = goRight ? -20.0f : (float)(DISPLAY_W + 20);
        lastTumbleSpawnMs = now;
        return;
    }

    tumble.x += tumble.vx;
    tumble.phase++;
    float bounce = (float)((tumble.phase / 2) % 5);
    if (bounce > 2.5f) bounce = 5.0f - bounce;
    tumble.y = 99.0f - bounce * 1.1f;

    if (tumble.x < -32.0f || tumble.x > (float)DISPLAY_W + 32.0f) {
        tumble.active = false;
        lastTumbleSpawnMs = now;
    }
}

static void drawTumbleweed(M5Canvas& canvas, bool flash) {
    if (!tumble.active || !isAutumn()) return;
    int16_t tx = (int16_t)tumble.x;
    int16_t ty = (int16_t)tumble.y;
    int spin = (tumble.phase / 2) % 4;
    // Avoid BR name — ESP32 specreg.h #define BR 4
    const uint16_t twDry  = flash ? 0xFFFF : 0x9A40;
    const uint16_t twHi   = flash ? 0xFFFF : 0xC300;
    const uint16_t twDark = flash ? 0xFFFF : 0x6A20;
    // Larger rolling bush (4×4 blocks)
    static const int8_t bush[12][2] = {
        {-1,-2},{0,-2},{1,-2},
        {-2,-1},{-1,-1},{0,-1},{1,-1},{2,-1},
        {-1, 0},{0, 0},{1, 0},
        {0, 1}
    };
    for (int b = 0; b < 12; b++) {
        int idx = (b + spin) % 12;
        int16_t px = tx + bush[idx][0] * 3;
        int16_t py = ty + bush[idx][1] * 3 - 4;
        if (px < -4 || px >= 240 || py < 86 || py >= 108) continue;
        uint16_t c = (b == 5) ? twHi : ((b & 1) ? twDry : twDark);
        canvas.fillRect(px, py, 3, 3, c);
    }
    canvas.fillRect(tx - 5, 105, 12, 1, 0x4208);
}

// ---------------------------------------------------------------------------
// Summer butterflies + pollen (summer signature)
// ---------------------------------------------------------------------------
static void updateButterflies(uint32_t now) {
    if (!isSummer()) {
        for (int i = 0; i < BUTTERFLY_COUNT; i++) butterflies[i].active = false;
        return;
    }

    // Keep 3–4 fluttering — noticeable summer sky life
    int active = 0;
    for (int i = 0; i < BUTTERFLY_COUNT; i++) if (butterflies[i].active) active++;

    if (active < 4 && now - lastButterflySpawnMs > 1800) {
        lastButterflySpawnMs = now;
        for (int i = 0; i < BUTTERFLY_COUNT; i++) {
            if (butterflies[i].active) continue;
            butterflies[i].active = true;
            butterflies[i].x = (float)random(8, DISPLAY_W - 8);
            butterflies[i].y = (float)random(18, 75);
            butterflies[i].vx = ((float)random(0, 24) - 12) / 10.0f;
            butterflies[i].vy = ((float)random(0, 12) - 6) / 10.0f;
            butterflies[i].phase = 0;
            butterflies[i].hue = (uint8_t)(esp_random() % 4);
            break;
        }
    }

    for (int i = 0; i < BUTTERFLY_COUNT; i++) {
        if (!butterflies[i].active) continue;
        butterflies[i].phase++;
        butterflies[i].x += butterflies[i].vx;
        butterflies[i].y += butterflies[i].vy + sinf((float)butterflies[i].phase * 0.22f) * 0.55f;
        if (butterflies[i].x < -10 || butterflies[i].x > DISPLAY_W + 10 ||
            butterflies[i].y < 6 || butterflies[i].y > 98) {
            butterflies[i].active = false;
            lastButterflySpawnMs = now;
        }
        if ((butterflies[i].phase % 36) == 0) {
            butterflies[i].vx = ((float)random(0, 24) - 12) / 10.0f;
            butterflies[i].vy = ((float)random(0, 12) - 6) / 12.0f;
        }
    }
}

static void drawButterflies(M5Canvas& canvas, bool flash) {
    if (!isSummer()) return;
    // Magenta / gold / cyan / orange — bright summer palette
    static const uint16_t HUES[4] = { 0xF81F, 0xFFE0, 0x07FF, 0xFD20 };
    for (int i = 0; i < BUTTERFLY_COUNT; i++) {
        if (!butterflies[i].active) continue;
        int16_t x = (int16_t)butterflies[i].x;
        int16_t y = (int16_t)butterflies[i].y;
        uint16_t c = flash ? 0xFFFF : HUES[butterflies[i].hue % 4];
        // Bigger fat wings so they read on 240×135
        int flap = ((butterflies[i].phase / 3) & 1) ? 3 : 2;
        canvas.fillRect(x - flap - 1, y, flap + 1, 3, c);
        canvas.fillRect(x + 1, y, flap + 1, 3, c);
        canvas.fillRect(x, y, 2, 3, 0x2104);  // body
        // Tiny antenna tips
        canvas.drawPixel(x, y - 1, c);
        canvas.drawPixel(x + 1, y - 1, c);
    }
}

static void updatePollen(uint32_t now) {
    if (!isSummer()) {
        for (int i = 0; i < POLLEN_COUNT; i++) pollen[i].active = false;
        return;
    }
    if (now - lastPollenUpdate < 45) return;
    lastPollenUpdate = now;

    int active = 0;
    for (int i = 0; i < POLLEN_COUNT; i++) if (pollen[i].active) active++;

    // Golden dust always in the air
    if (active < 12 && random(0, 100) < 40) {
        for (int i = 0; i < POLLEN_COUNT; i++) {
            if (pollen[i].active) continue;
            pollen[i].active = true;
            pollen[i].x = (float)random(0, DISPLAY_W);
            pollen[i].y = (float)random(15, 90);
            pollen[i].vx = ((float)random(0, 10) - 3) / 20.0f;
            pollen[i].vy = -0.15f - (float)random(0, 10) / 40.0f;  // slow rise
            pollen[i].phase = (uint8_t)(esp_random() & 0xFF);
            break;
        }
    }

    for (int i = 0; i < POLLEN_COUNT; i++) {
        if (!pollen[i].active) continue;
        pollen[i].phase++;
        pollen[i].x += pollen[i].vx + sinf((float)pollen[i].phase * 0.08f) * 0.2f;
        pollen[i].y += pollen[i].vy;
        if (pollen[i].y < 8 || pollen[i].y > 100 ||
            pollen[i].x < -4 || pollen[i].x > DISPLAY_W + 4) {
            pollen[i].active = false;
        }
    }
}

static void drawPollen(M5Canvas& canvas, bool flash) {
    if (!isSummer()) return;
    // Warm gold / cream motes — summer heat shimmer
    const uint16_t cBright = flash ? 0xFFFF : 0xFFE0;
    const uint16_t cSoft   = flash ? 0xFFFF : 0xF7B0;
    for (int i = 0; i < POLLEN_COUNT; i++) {
        if (!pollen[i].active) continue;
        int16_t x = (int16_t)pollen[i].x;
        int16_t y = (int16_t)pollen[i].y;
        if (x < 0 || x >= DISPLAY_W || y < 0 || y >= 105) continue;
        uint16_t c = (pollen[i].phase & 2) ? cBright : cSoft;
        // 2×2 fat pixel so motes catch the eye
        canvas.fillRect(x, y, 2, 2, c);
    }
}

// Quiet rain drip SFX — sparse so it never nags
static void updateRainSfx(uint32_t now) {
    if (!Weather::isRaining()) {
        lastRainSfxMs = 0;
        return;
    }
    // First drip a bit after rain starts, then every ~2.8–4.5s
    if (lastRainSfxMs == 0) {
        lastRainSfxMs = now;
        return;
    }
    uint32_t gap = 2800 + (esp_random() % 1700);
    if (now - lastRainSfxMs < gap) return;
    lastRainSfxMs = now;
    // Only queue if speaker is idle — never fight thunder/wolf/jump
    if (!SFX::isPlaying())
        SFX::play(SFX::RAIN_TICK);
}

// ---------------------------------------------------------------------------
// Spring lightning bolts (sky layer)
// ---------------------------------------------------------------------------
static void spawnBolt(bool mainBolt) {
    for (int i = 0; i < BOLT_COUNT; i++) {
        if (bolts[i].active) continue;
        bolts[i].active = true;
        bolts[i].x0 = (int16_t)random(20, DISPLAY_W - 20);
        bolts[i].segs = (uint8_t)random(4, 7);
        bolts[i].life = mainBolt ? 5 : 3;
        bolts[i].bright = mainBolt ? 1 : 0;
        int16_t yLeft = 55 + (int)random(0, 25);  // total drop budget
        for (int s = 0; s < bolts[i].segs; s++) {
            bolts[i].segDx[s] = (int8_t)random(-10, 11);
            int step = yLeft / (bolts[i].segs - s);
            if (step < 6) step = 6;
            if (step > 18) step = 18;
            bolts[i].segDy[s] = (int8_t)step;
            yLeft -= step;
            if (yLeft < 0) yLeft = 0;
        }
        return;
    }
}

static void updateLightning(uint32_t now) {
    if (!isSpring()) {
        for (int i = 0; i < BOLT_COUNT; i++) bolts[i].active = false;
        wasThunderFlash = false;
        return;
    }

    // Tick life
    for (int i = 0; i < BOLT_COUNT; i++) {
        if (!bolts[i].active) continue;
        if (bolts[i].life > 0) bolts[i].life--;
        if (bolts[i].life == 0) bolts[i].active = false;
    }

    // Sync with weather thunder flash — bolt appears with the flash
    bool flash = Weather::isThunderFlashing();
    if (flash && !wasThunderFlash) {
        spawnBolt(true);
        if (random(0, 100) < 40) spawnBolt(false);  // secondary fork
        lastBoltSpawnMs = now;
    }
    wasThunderFlash = flash;

    // Distant rumbles during spring storms (no full-screen flash yet)
    if (Weather::isStorming() && !flash && (now - lastBoltSpawnMs > 3500)) {
        if (random(0, 100) < 12) {
            spawnBolt(false);
            lastBoltSpawnMs = now;
        }
    }
}

static void drawLightningBolts(M5Canvas& canvas) {
    if (!isSpring()) return;
    for (int i = 0; i < BOLT_COUNT; i++) {
        if (!bolts[i].active) continue;
        const bool hot = bolts[i].life >= 3 || bolts[i].bright;
        // Core white-yellow, rim pale cyan-blue for comic bolt look
        const uint16_t core = hot ? 0xFFFF : 0xFFE0;   // white / yellow
        const uint16_t glow = hot ? 0xA7FF : 0x5D5F;   // light cyan / dim blue
        int16_t x = bolts[i].x0;
        int16_t y = 0;  // from top of main canvas (under bar)
        for (int s = 0; s < bolts[i].segs; s++) {
            int16_t nx = x + bolts[i].segDx[s];
            int16_t ny = y + bolts[i].segDy[s];
            // Glow stroke (wider)
            canvas.drawLine(x - 1, y, nx - 1, ny, glow);
            canvas.drawLine(x + 1, y, nx + 1, ny, glow);
            // Fat core
            canvas.drawLine(x, y, nx, ny, core);
            canvas.drawLine(x, y + 1, nx, ny + 1, core);
            // Branch tip spark
            if (s == bolts[i].segs - 1 || (s > 0 && (s & 1))) {
                canvas.fillRect(nx - 1, ny - 1, 3, 3, core);
            }
            // Small side fork
            if (s == 2 && bolts[i].bright) {
                int16_t fx = nx + ((i & 1) ? 8 : -8);
                int16_t fy = ny + 10;
                canvas.drawLine(nx, ny, fx, fy, glow);
                canvas.drawLine(nx, ny, fx, fy - 1, core);
            }
            x = nx;
            y = ny;
            if (y > 90) break;
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void init() {
    reset();
}

void reset() {
    snowBanksInited = false;
    for (int i = 0; i < SNOW_BANK_COUNT; i++) snowBanks[i].h = 0;
    for (int i = 0; i < LEAF_COUNT; i++) leaves[i].active = false;
    tumble = {};
    lastTumbleSpawnMs = 0;
    lastLeafUpdate = 0;
    lastBankGrowMs = 0;
    lastRainSfxMs = 0;
    for (int i = 0; i < BUTTERFLY_COUNT; i++) butterflies[i].active = false;
    lastButterflySpawnMs = 0;
    for (int i = 0; i < POLLEN_COUNT; i++) pollen[i].active = false;
    lastPollenUpdate = 0;
    for (int i = 0; i < BOLT_COUNT; i++) bolts[i].active = false;
    lastBoltSpawnMs = 0;
    wasThunderFlash = false;
}

void update() {
    // RETRO film world: only pixel rain (weather module) — no color FX
    if (Weather::getActiveSeason() == Season::RETRO) return;
    uint32_t now = millis();
    updateSnowBanks(now);
    updateLeaves(now);
    updateTumbleweed(now);
    updateButterflies(now);
    updatePollen(now);
    updateLightning(now);
    updateRainSfx(now);
}

void drawBackdrop(M5Canvas& canvas) {
    if (Weather::getActiveSeason() == Season::RETRO) return;
    // Sky-layer only (behind tree/pig)
    drawLightningBolts(canvas);
}

void draw(M5Canvas& canvas) {
    if (Weather::getActiveSeason() == Season::RETRO) return;
    const bool flash = Weather::isThunderFlashing();
    // Order: ground decor first, then air
    drawSnowBanks(canvas, flash);
    drawLeaves(canvas, flash);
    drawTumbleweed(canvas, flash);
    drawPollen(canvas, flash);
    drawButterflies(canvas, flash);
}

}  // namespace SeasonalFx

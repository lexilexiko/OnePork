// Seasonal decorative animations (not core precip / clouds).
// Keep this file the home for "pretty season stuff".

#include "seasonal_fx.h"
#include "weather.h"
#include "../ui/display.h"
#include <esp_random.h>
#include <math.h>

namespace SeasonalFx {

// ---------------------------------------------------------------------------
// Winter: snow banks (сугробы) — fat-pixel steps on ground line
// ---------------------------------------------------------------------------
struct SnowBank {
    int16_t x;
    uint8_t w;  // width px
    uint8_t h;  // fat rows 1-4
};
static constexpr int SNOW_BANK_COUNT = 10;
static SnowBank snowBanks[SNOW_BANK_COUNT] = {};
static uint32_t lastBankGrowMs = 0;
static bool snowBanksInited = false;

// ---------------------------------------------------------------------------
// Autumn: falling leaves
// ---------------------------------------------------------------------------
struct Leaf {
    float x, y;
    float vx, vy;
    uint8_t col;
    uint8_t size;
    bool active;
};
static constexpr int LEAF_COUNT = 14;
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
// Summer: butterflies (simple fat-pixel, ready for expansion)
// ---------------------------------------------------------------------------
struct Butterfly {
    float x, y;
    float vx, vy;
    uint8_t phase;
    uint8_t hue;  // color variant
    bool active;
};
static constexpr int BUTTERFLY_COUNT = 3;
static Butterfly butterflies[BUTTERFLY_COUNT] = {};
static uint32_t lastButterflySpawnMs = 0;

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
        snowBanks[i].x = (int16_t)(i * 24 + random(0, 8));
        snowBanks[i].w = (uint8_t)random(12, 24);
        snowBanks[i].h = (uint8_t)random(1, 3);
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

    if (now - lastBankGrowMs < 450) return;
    lastBankGrowMs = now;

    if (Weather::isSnowing()) {
        for (int n = 0; n < 2; n++) {
            int i = random(0, SNOW_BANK_COUNT);
            if (snowBanks[i].h < 4) {
                snowBanks[i].h++;
            } else if (random(0, 100) < 25) {
                snowBanks[i].x = (int16_t)random(0, DISPLAY_W - 24);
                snowBanks[i].w = (uint8_t)random(12, 26);
                snowBanks[i].h = 1;
            }
            if (snowBanks[i].w < 30 && random(0, 100) < 40)
                snowBanks[i].w += 2;
        }
    } else {
        int i = random(0, SNOW_BANK_COUNT);
        if (snowBanks[i].h > 0) snowBanks[i].h--;
    }
}

static void drawSnowBanks(M5Canvas& canvas, bool flash) {
    if (!isWinter() || !snowBanksInited) return;
    const int S = 3;
    for (int i = 0; i < SNOW_BANK_COUNT; i++) {
        int rows = (int)snowBanks[i].h;
        if (rows < 1) continue;
        if (rows > 4) rows = 4;
        int16_t bx = snowBanks[i].x;
        int wBlocks = (int)snowBanks[i].w / S;
        if (wBlocks < 2) wBlocks = 2;
        if (wBlocks > 9) wBlocks = 9;

        for (int r = 0; r < rows; r++) {
            int inset = r;
            int bw = wBlocks - inset * 2;
            if (bw < 1) break;
            int16_t top = (int16_t)(107 - S * (r + 1));  // bottom row 104..106
            if (top < 90) break;
            int16_t left = (int16_t)(bx + inset * S);
            uint16_t col = flash ? 0xFFFF
                                 : ((r == rows - 1) ? 0xFFFF : ((r & 1) ? 0xDEFB : 0xEF7D));
            canvas.fillRect(left, top, bw * S, S, col);
            if (r == 0)
                canvas.fillRect(left, top + S - 1, bw * S, 1, flash ? 0xFFFF : 0xC618);
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
    if (now - lastLeafUpdate < 40) return;
    lastLeafUpdate = now;

    bool wet = Weather::isRaining();
    int want = wet ? LEAF_COUNT : 7;
    int active = 0;
    for (int i = 0; i < LEAF_COUNT; i++) if (leaves[i].active) active++;

    if (active < want && random(0, 100) < 35) {
        for (int i = 0; i < LEAF_COUNT; i++) {
            if (leaves[i].active) continue;
            leaves[i].active = true;
            leaves[i].x = (float)random(0, DISPLAY_W);
            leaves[i].y = (float)random(-8, 20);
            leaves[i].vx = (float)(random(0, 20) - 8) / 10.0f;
            leaves[i].vy = 0.4f + (float)random(0, 20) / 20.0f;
            leaves[i].col = (uint8_t)(esp_random() % 4);
            leaves[i].size = (uint8_t)random(2, 4);
            break;
        }
    }

    for (int i = 0; i < LEAF_COUNT; i++) {
        if (!leaves[i].active) continue;
        leaves[i].x += leaves[i].vx;
        leaves[i].y += leaves[i].vy;
        leaves[i].vx += (float)(random(0, 3) - 1) * 0.05f;
        if (leaves[i].vx > 1.2f) leaves[i].vx = 1.2f;
        if (leaves[i].vx < -1.2f) leaves[i].vx = -1.2f;
        if (leaves[i].y > 104.0f || leaves[i].x < -10.0f ||
            leaves[i].x > (float)DISPLAY_W + 10.0f) {
            leaves[i].active = false;
        }
    }
}

static void drawLeaves(M5Canvas& canvas, bool flash) {
    if (!isAutumn()) return;
    static const uint16_t LEAF_COLS[4] = { 0xE2C0, 0xFD20, 0xC300, 0x9A40 };
    for (int i = 0; i < LEAF_COUNT; i++) {
        if (!leaves[i].active) continue;
        int16_t lx = (int16_t)leaves[i].x;
        int16_t ly = (int16_t)leaves[i].y;
        if (lx < -4 || lx >= DISPLAY_W + 4 || ly < -4 || ly >= 106) continue;
        uint16_t c = flash ? 0xFFFF : LEAF_COLS[leaves[i].col % 4];
        int s = leaves[i].size;
        canvas.fillRect(lx, ly, s, s - 1, c);
        canvas.drawPixel(lx + s / 2, ly - 1, c);
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
        if (now - lastTumbleSpawnMs < 10000) return;
        if (now - lastTumbleSpawnMs < 18000 && random(0, 100) > 8) return;

        tumble.active = true;
        tumble.y = 100.0f;
        tumble.phase = 0;
        bool goRight = (random(0, 2) == 0);
        tumble.vx = goRight ? (1.5f + (float)random(0, 10) / 10.0f)
                            : -(1.5f + (float)random(0, 10) / 10.0f);
        tumble.x = goRight ? -18.0f : (float)(DISPLAY_W + 18);
        lastTumbleSpawnMs = now;
        return;
    }

    tumble.x += tumble.vx;
    tumble.phase++;
    float bounce = (float)((tumble.phase / 3) % 4);
    if (bounce > 2.0f) bounce = 4.0f - bounce;
    tumble.y = 100.0f - bounce * 0.7f;

    if (tumble.x < -28.0f || tumble.x > (float)DISPLAY_W + 28.0f) {
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
    static const int8_t bush[9][2] = {
        {-1,-1},{0,-1},{1,-1},
        {-1, 0},{0, 0},{1, 0},
        {-1, 1},{0, 1},{1, 1}
    };
    for (int b = 0; b < 9; b++) {
        int idx = (b + spin) % 9;
        int16_t px = tx + bush[idx][0] * 3;
        int16_t py = ty + bush[idx][1] * 3 - 3;
        if (px < -3 || px >= 240 || py < 90 || py >= 107) continue;
        uint16_t c = (b == 4) ? twHi : ((b & 1) ? twDry : twDark);
        canvas.fillRect(px, py, 3, 3, c);
    }
    canvas.fillRect(tx - 4, 105, 10, 1, 0x4208);
}

// ---------------------------------------------------------------------------
// Summer butterflies (simple, expand later)
// ---------------------------------------------------------------------------
static void updateButterflies(uint32_t now) {
    if (!isSummer()) {
        for (int i = 0; i < BUTTERFLY_COUNT; i++) butterflies[i].active = false;
        return;
    }

    // Keep 1–2 fluttering
    int active = 0;
    for (int i = 0; i < BUTTERFLY_COUNT; i++) if (butterflies[i].active) active++;

    if (active < 2 && now - lastButterflySpawnMs > 4000) {
        lastButterflySpawnMs = now;
        for (int i = 0; i < BUTTERFLY_COUNT; i++) {
            if (butterflies[i].active) continue;
            butterflies[i].active = true;
            butterflies[i].x = (float)random(10, DISPLAY_W - 10);
            butterflies[i].y = (float)random(20, 70);
            butterflies[i].vx = ((float)random(0, 20) - 10) / 10.0f;
            butterflies[i].vy = ((float)random(0, 10) - 5) / 10.0f;
            butterflies[i].phase = 0;
            butterflies[i].hue = (uint8_t)(esp_random() % 3);
            break;
        }
    }

    for (int i = 0; i < BUTTERFLY_COUNT; i++) {
        if (!butterflies[i].active) continue;
        butterflies[i].phase++;
        butterflies[i].x += butterflies[i].vx;
        butterflies[i].y += butterflies[i].vy + sinf((float)butterflies[i].phase * 0.2f) * 0.4f;
        if (butterflies[i].x < -8 || butterflies[i].x > DISPLAY_W + 8 ||
            butterflies[i].y < 8 || butterflies[i].y > 95) {
            butterflies[i].active = false;
            lastButterflySpawnMs = now;
        }
        // occasional direction flip
        if ((butterflies[i].phase % 40) == 0) {
            butterflies[i].vx = ((float)random(0, 20) - 10) / 10.0f;
            butterflies[i].vy = ((float)random(0, 10) - 5) / 12.0f;
        }
    }
}

static void drawButterflies(M5Canvas& canvas, bool flash) {
    if (!isSummer()) return;
    static const uint16_t HUES[3] = { 0xFD1F, 0xFFE0, 0x07FF };  // pink / yellow / cyan
    for (int i = 0; i < BUTTERFLY_COUNT; i++) {
        if (!butterflies[i].active) continue;
        int16_t x = (int16_t)butterflies[i].x;
        int16_t y = (int16_t)butterflies[i].y;
        uint16_t c = flash ? 0xFFFF : HUES[butterflies[i].hue % 3];
        // Tiny fat wings (flap)
        int flap = ((butterflies[i].phase / 3) & 1) ? 2 : 1;
        canvas.fillRect(x - flap - 1, y, flap + 1, 2, c);
        canvas.fillRect(x + 1, y, flap + 1, 2, c);
        canvas.drawPixel(x, y + 1, 0x2104);  // body
    }
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
    for (int i = 0; i < BUTTERFLY_COUNT; i++) butterflies[i].active = false;
    lastButterflySpawnMs = 0;
    for (int i = 0; i < BOLT_COUNT; i++) bolts[i].active = false;
    lastBoltSpawnMs = 0;
    wasThunderFlash = false;
}

void update() {
    uint32_t now = millis();
    updateSnowBanks(now);
    updateLeaves(now);
    updateTumbleweed(now);
    updateButterflies(now);
    updateLightning(now);
}

void drawBackdrop(M5Canvas& canvas) {
    // Sky-layer only (behind tree/pig)
    drawLightningBolts(canvas);
}

void draw(M5Canvas& canvas) {
    const bool flash = Weather::isThunderFlashing();
    // Order: ground decor first, then air
    drawSnowBanks(canvas, flash);
    drawLeaves(canvas, flash);
    drawTumbleweed(canvas, flash);
    drawButterflies(canvas, flash);
}

}  // namespace SeasonalFx

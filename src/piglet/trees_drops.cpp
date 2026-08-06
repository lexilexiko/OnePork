// Falling fruit / berries — world-scrolled, foreground draw + collect.

#include "trees_drops.h"
#include "trees.h"
#include "avatar.h"
#include "../ui/display.h"
#include <esp_random.h>

namespace Trees {

static constexpr int16_t PX = 3;
static inline int16_t snapPx(int16_t v) {
    return (v >= 0) ? (v / PX) * PX : ((v - (PX - 1)) / PX) * PX;
}

// Match tree screenX wrap band so drops stay in the same world loop
static constexpr int16_t WRAP_HI = 260;
static constexpr int16_t WRAP_LO = -60;
static constexpr int16_t WRAP_SPAN = 300;

static bool thunder() { return Avatar::isThunderFlashing(); }
static uint16_t fl(uint16_t c) {
    if (!thunder()) return c;
    uint16_t r = ((c >> 11) + 31) >> 1;
    uint16_t g = (((c >> 5) & 0x3F) + 63) >> 1;
    uint16_t b = ((c & 0x1F) + 31) >> 1;
    return (r << 11) | (g << 5) | b;
}

// Produce palette — bright fruit, soft rim (never near-black)
static constexpr uint16_t C_APPLE_RED    = 0xF800;  // pure red body
static constexpr uint16_t C_APPLE_RED2   = 0xC800;  // warm mid shade
static constexpr uint16_t C_APPLE_RED3   = 0xE000;  // deep red (not black)
static constexpr uint16_t C_APPLE_RED_H  = 0xFED6;  // soft pink highlight
static constexpr uint16_t C_APPLE_YEL    = 0xFFE0;
static constexpr uint16_t C_APPLE_YEL2   = 0xD5C0;
static constexpr uint16_t C_APPLE_YEL3   = 0xC540;
static constexpr uint16_t C_APPLE_YEL_H  = 0xFFFB;
static constexpr uint16_t C_STEM   = 0x5A20;  // brown stem
static constexpr uint16_t C_LEAF   = 0x2C00;  // tiny leaf
static constexpr uint16_t C_CONE   = 0x9A40;
static constexpr uint16_t C_CONE2  = 0x7A00;
static constexpr uint16_t C_CONE_H = 0xDE20;
static constexpr uint16_t C_BIRCH_CAT  = 0xC840;
static constexpr uint16_t C_BIRCH_CAT2 = 0x8C40;
static constexpr uint16_t C_BIRCH_CAT_H= 0xE6A0;
static constexpr uint16_t C_BERRY  = 0xD81F;
static constexpr uint16_t C_BERRY2 = 0xF81F;
static constexpr uint16_t C_BERRY_H= 0xFCDF;

// Stamp one fat pixel cell at grid (lx, ly) relative to center gx,gy
static void cell(M5Canvas& canvas, int16_t gx, int16_t gy, int lx, int ly, uint16_t col) {
    canvas.fillRect(gx + lx * PX, gy + ly * PX, PX, PX, col);
}

struct Drop {
    int16_t x, y;           // world/screen X; spawn Y (fall from here)
    uint8_t r;
    uint32_t dropStart;
    uint32_t groundSince;   // 0 = still falling
    bool active;
    Produce produce;
};

struct Splash {
    float x, y, vx, vy;
    uint32_t t;
    bool active;
    Produce produce;
};

static constexpr uint8_t MAX_DROP = 8;
static constexpr uint32_t GROUND_MS = 9000;
static Drop drops[MAX_DROP];
static Splash splashes[8];
static uint8_t splashIdx = 0;

static void wrapX(int16_t& x) {
    while (x > WRAP_HI) x -= WRAP_SPAN;
    while (x < WRAP_LO) x += WRAP_SPAN;
}

static int16_t fallY(const Drop& d, uint32_t now) {
    if (d.groundSince != 0) return DROP_GROUND_Y;
    float tt = (float)(now - d.dropStart) / 1000.0f;
    int16_t cy = d.y + (int16_t)(0.5f * 800.0f * tt * tt);
    if (cy >= DROP_GROUND_Y) return DROP_GROUND_Y;
    return cy;
}

void dropsReset() {
    for (int i = 0; i < MAX_DROP; i++) drops[i].active = false;
    for (int i = 0; i < 8; i++) splashes[i].active = false;
    splashIdx = 0;
}

void dropsScroll(int dir) {
    // Same sign as tree Slot::scroll: +1 moves flora right on screen
    const int16_t d = (dir > 0) ? 1 : -1;
    for (uint8_t i = 0; i < MAX_DROP; i++) {
        if (!drops[i].active) continue;
        drops[i].x = (int16_t)(drops[i].x + d);
        wrapX(drops[i].x);
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (!splashes[i].active) continue;
        splashes[i].x += (float)d;
    }
}

bool dropsSpawn(int16_t x, int16_t y, uint8_t r, Produce p) {
    // Do NOT clamp X to the visible band — fruit lives in world space.
    // Soft wrap only so values stay near the tree loop.
    wrapX(x);
    if (y < 0) y = 0;
    if (y > DROP_GROUND_Y - 4) y = DROP_GROUND_Y - 4;
    if (r < 2) r = 2;

    for (uint8_t i = 0; i < MAX_DROP; i++) {
        if (drops[i].active) continue;
        drops[i].x = x;
        drops[i].y = y;
        drops[i].r = r;
        drops[i].dropStart = millis();
        drops[i].groundSince = 0;
        drops[i].active = true;
        drops[i].produce = p;
        return true;
    }
    return false;  // pool full — caller still may remove from tree
}

// Size tiers: apples always compact (tier 0); rare "plump" = tier 1 still 3-wide
static int produceSizeTier(int r) {
    if (r <= 3) return 0;
    return 1;
}

// Rounded apple — compact only (red was reading as watermelon).
// Both tiers stay ~3 cells wide; plump adds stem leaf + one shade pixel.
static void drawApple(M5Canvas& canvas, int16_t gx, int16_t gy, int tier, bool yellow) {
    uint16_t body = fl(yellow ? C_APPLE_YEL  : C_APPLE_RED);
    uint16_t mid  = fl(yellow ? C_APPLE_YEL2 : C_APPLE_RED2);
    uint16_t deep = fl(yellow ? C_APPLE_YEL3 : C_APPLE_RED3);
    uint16_t hi   = fl(yellow ? C_APPLE_YEL_H : C_APPLE_RED_H);
    uint16_t stem = fl(C_STEM);
    uint16_t leaf = fl(C_LEAF);

    // Core round apple (always)
    //   .
    //  ###
    //   #
    cell(canvas, gx, gy,  0, -2, stem);
    if (tier >= 1) cell(canvas, gx, gy,  1, -2, leaf);  // plump: tiny leaf
    cell(canvas, gx, gy,  0, -1, hi);
    cell(canvas, gx, gy, -1,  0, body);
    cell(canvas, gx, gy,  0,  0, body);
    cell(canvas, gx, gy,  1,  0, mid);
    cell(canvas, gx, gy,  0,  1, deep);
    if (tier >= 1) {
        // Slightly fuller but still apple-sized (not 5-wide)
        cell(canvas, gx, gy, -1, -1, body);
        cell(canvas, gx, gy,  1, -1, mid);
        cell(canvas, gx, gy, -1,  1, deep);
    }
}

static void drawBerry(M5Canvas& canvas, int16_t gx, int16_t gy, int tier) {
    uint16_t a = fl(C_BERRY);
    uint16_t b = fl(C_BERRY2);
    uint16_t h = fl(C_BERRY_H);
    if (tier <= 0) {
        // tiny round berry
        cell(canvas, gx, gy,  0, -1, h);
        cell(canvas, gx, gy, -1,  0, a);
        cell(canvas, gx, gy,  0,  0, b);
        cell(canvas, gx, gy,  1,  0, a);
        cell(canvas, gx, gy,  0,  1, a);
        return;
    }
    // slightly larger plump berry
    cell(canvas, gx, gy,  0, -2, h);
    cell(canvas, gx, gy, -1, -1, a);
    cell(canvas, gx, gy,  0, -1, b);
    cell(canvas, gx, gy,  1, -1, a);
    cell(canvas, gx, gy, -1,  0, a);
    cell(canvas, gx, gy,  0,  0, b);
    cell(canvas, gx, gy,  1,  0, a);
    cell(canvas, gx, gy,  0,  1, a);
}

static void drawAcorn(M5Canvas& canvas, int16_t gx, int16_t gy, int tier) {
    uint16_t cap = fl(C_CONE2);
    uint16_t nut = fl(C_CONE);
    uint16_t hi  = fl(C_CONE_H);
    // cap on top, round nut below (not a square)
    cell(canvas, gx, gy, -1, -1, cap);
    cell(canvas, gx, gy,  0, -1, cap);
    cell(canvas, gx, gy,  1, -1, cap);
    cell(canvas, gx, gy,  0, -2, hi);  // stem nub
    cell(canvas, gx, gy,  0,  0, nut);
    cell(canvas, gx, gy, -1,  0, nut);
    cell(canvas, gx, gy,  1,  0, fl(C_CONE2));
    if (tier >= 1) {
        cell(canvas, gx, gy,  0,  1, nut);
        cell(canvas, gx, gy, -1,  1, fl(C_CONE2));
    }
}

static void drawCone(M5Canvas& canvas, int16_t gx, int16_t gy, int tier) {
    uint16_t a = fl(C_CONE);
    uint16_t b = fl(C_CONE2);
    uint16_t h = fl(C_CONE_H);
    // tapered cone (point up)
    cell(canvas, gx, gy,  0, -2, h);
    cell(canvas, gx, gy, -1, -1, b);
    cell(canvas, gx, gy,  0, -1, a);
    cell(canvas, gx, gy,  1, -1, b);
    cell(canvas, gx, gy, -1,  0, a);
    cell(canvas, gx, gy,  0,  0, a);
    cell(canvas, gx, gy,  1,  0, b);
    if (tier >= 1) {
        cell(canvas, gx, gy, -1,  1, b);
        cell(canvas, gx, gy,  0,  1, a);
        cell(canvas, gx, gy,  1,  1, b);
    }
}

static void drawCatkin(M5Canvas& canvas, int16_t gx, int16_t gy, int tier) {
    uint16_t a = fl(C_BIRCH_CAT);
    uint16_t b = fl(C_BIRCH_CAT2);
    uint16_t h = fl(C_BIRCH_CAT_H);
    // hanging oval droop
    cell(canvas, gx, gy,  0, -1, h);
    cell(canvas, gx, gy,  0,  0, a);
    cell(canvas, gx, gy, -1,  0, b);
    cell(canvas, gx, gy,  0,  1, a);
    cell(canvas, gx, gy,  0,  2, b);
    if (tier >= 1) {
        cell(canvas, gx, gy,  1,  0, a);
        cell(canvas, gx, gy,  0,  3, b);
    }
}

void drawProduce(M5Canvas& canvas, int16_t cx, int16_t cy, int r, Produce p) {
    int16_t gx = snapPx(cx), gy = snapPx(cy);
    int tier = produceSizeTier(r);

    switch (p) {
        case Produce::RED_APPLE:
            drawApple(canvas, gx, gy, tier, false);
            break;
        case Produce::YELLOW_APPLE:
            drawApple(canvas, gx, gy, tier, true);
            break;
        case Produce::BERRY:
            drawBerry(canvas, gx, gy, tier);
            break;
        case Produce::ACORN:
            drawAcorn(canvas, gx, gy, tier);
            break;
        case Produce::CONE:
            drawCone(canvas, gx, gy, tier);
            break;
        case Produce::BIRCH_CATKIN:
            drawCatkin(canvas, gx, gy, tier);
            break;
        default:
            drawBerry(canvas, gx, gy, tier);
            break;
    }
}

bool tryCollectNearbyFruit(int pigCenterX, int pigFeetY, int radius) {
    // Generous oval: ground fruit sits at DROP_GROUND_Y, pig feet ~106
    const int radY = radius + 18;
    uint32_t now = millis();

    for (uint8_t i = 0; i < MAX_DROP; i++) {
        if (!drops[i].active) continue;
        int16_t fy = fallY(drops[i], now);
        int dx = (int)drops[i].x - pigCenterX;
        int dy = (int)fy - pigFeetY;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        // Prefer ground pickups: slightly larger X once landed
        const int radX = (drops[i].groundSince != 0) ? (radius + 14) : (radius + 10);
        if (dx <= radX && dy <= radY) {
            drops[i].active = false;
            return true;
        }
    }
    return false;
}

void drawDropsForeground(M5Canvas& canvas) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < MAX_DROP; i++) {
        if (!drops[i].active) continue;
        int16_t cy;
        if (drops[i].groundSince != 0) {
            cy = DROP_GROUND_Y;
            if (now - drops[i].groundSince > GROUND_MS) {
                drops[i].active = false;
                continue;
            }
        } else {
            float tt = (float)(now - drops[i].dropStart) / 1000.0f;
            cy = drops[i].y + (int16_t)(0.5f * 800.0f * tt * tt);
            if (cy >= DROP_GROUND_Y) {
                cy = DROP_GROUND_Y;
                drops[i].groundSince = now;
                Splash& sp = splashes[splashIdx];
                splashIdx = (splashIdx + 1) % 8;
                sp.x = (float)drops[i].x;
                sp.y = (float)DROP_GROUND_Y + 3.f;
                sp.vx = ((float)(esp_random() % 400) - 200.f) / 100.f;
                sp.vy = -1.5f;
                sp.t = now;
                sp.active = true;
                sp.produce = drops[i].produce;
            }
        }
        // Skip draw if fully off-screen (still collectable after wrap)
        if (drops[i].x < -20 || drops[i].x > DISPLAY_W + 20) continue;
        drawProduce(canvas, drops[i].x, cy, drops[i].r, drops[i].produce);
    }

    for (uint8_t i = 0; i < 8; i++) {
        if (!splashes[i].active) continue;
        float pr = (float)(now - splashes[i].t) / 500.f;
        if (pr >= 1.f) { splashes[i].active = false; continue; }
        int16_t sx = (int16_t)(splashes[i].x + splashes[i].vx * pr * 20);
        int16_t sy = (int16_t)(splashes[i].y + splashes[i].vy * pr * 20 + 40 * pr * pr);
        if (sx < 0 || sx >= DISPLAY_W) continue;
        // Splash tint matches produce (not always red apple)
        uint16_t col = fl(C_APPLE_RED);
        switch (splashes[i].produce) {
            case Produce::YELLOW_APPLE: col = fl(C_APPLE_YEL); break;
            case Produce::BIRCH_CATKIN: col = fl(C_BIRCH_CAT); break;
            case Produce::ACORN:
            case Produce::CONE:         col = fl(C_CONE); break;
            case Produce::BERRY:        col = fl(C_BERRY); break;
            default: break;
        }
        canvas.fillRect(sx, sy, PX, PX, col);
    }
}

}  // namespace Trees

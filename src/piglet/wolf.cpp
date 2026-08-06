// Fat-pixel wolf that visits the avatar scene and chases the pig.

#include "wolf.h"
#include "avatar.h"
#include "weather.h"
#include "../ui/display.h"
#include "../audio/sfx.h"
#include "../core/config.h"
#include <esp_random.h>
#include <math.h>
// SFX::WOLF / OINK_GRUNT on enter + scare
// Fur palette follows Weather::getActiveSeason()

namespace Wolf {

enum class Phase : uint8_t {
    HIDDEN = 0,
    ENTER,
    CHASE,
    SCARE,   // close to pig — brief snarl / pig flinch
    LEAVE
};

static Phase phase = Phase::HIDDEN;
static float x = -40.0f;
static float y = 106.0f;      // feet line (same as pig)
static float vx = 0.0f;
static bool faceRight = true;
static uint8_t phaseTick = 0;
static uint32_t phaseStartMs = 0;
static uint32_t nextVisitMs = 0;
static uint32_t lastStepMs = 0;
static uint8_t legPhase = 0;
static bool howled = false;

static constexpr int16_t PX = 3;
static constexpr float CHASE_SPEED = 3.2f;  // faster, more threatening
static constexpr float LEAVE_SPEED = 4.2f;
static constexpr int SCARE_DIST = 36;  // larger proximity to scare pig
static constexpr uint32_t MIN_GAP_MS = 45000;   // 45s–2.5min between visits
static constexpr uint32_t MAX_GAP_MS = 150000;
static constexpr uint32_t SCARE_MS = 900;
static constexpr uint32_t ENTER_MS = 400;

static void scheduleNext(uint32_t now) {
    nextVisitMs = now + MIN_GAP_MS + (esp_random() % (MAX_GAP_MS - MIN_GAP_MS + 1));
}

void init() {
    reset();
    scheduleNext(millis() + 8000);  // first possible visit after boot settle
}

void reset() {
    phase = Phase::HIDDEN;
    x = -40.0f;
    y = 106.0f;
    vx = 0.0f;
    faceRight = true;
    phaseTick = 0;
    phaseStartMs = 0;
    lastStepMs = 0;
    legPhase = 0;
    howled = false;
}

bool isActive() { return phase != Phase::HIDDEN; }
int16_t getX() { return (int16_t)x; }

static void beginEnter(uint32_t now) {
    phase = Phase::ENTER;
    phaseStartMs = now;
    howled = false;
    legPhase = 0;
    // Enter from the side opposite the pig
    int pigX = Avatar::getCurrentX() + 14 * PX;
    bool fromLeft = (pigX > 120) || ((esp_random() & 1) == 0 && pigX > 60);
    if (fromLeft) {
        x = -28.0f;
        faceRight = true;
        vx = CHASE_SPEED;
    } else {
        x = (float)(DISPLAY_W + 28);
        faceRight = false;
        vx = -CHASE_SPEED;
    }
    y = 106.0f;
    phase = Phase::CHASE;  // skip long enter — run in immediately
    phaseStartMs = now;
    SFX::play(SFX::WOLF);
}

void spawnNow() {
    if (!Config::personality().wolfEnabled) return;
    beginEnter(millis());
}

void scareAway() {
    if (phase == Phase::HIDDEN || phase == Phase::LEAVE) return;
    uint32_t now = millis();
    phase = Phase::LEAVE;
    phaseStartMs = now;
    // Flee away from pig
    int pigFeet = Avatar::getCurrentX() + 14 * PX;
    if (x < (float)pigFeet) {
        faceRight = false;
        vx = -LEAVE_SPEED * 1.35f;
    } else {
        faceRight = true;
        vx = LEAVE_SPEED * 1.35f;
    }
    howled = true;
    SFX::play(SFX::WOLF);
}

int16_t getY() { return (int16_t)y; }

void update() {
    uint32_t now = millis();

    // Settings → WOLF OFF: never spawn; clear if somehow active
    if (!Config::personality().wolfEnabled) {
        if (phase != Phase::HIDDEN) reset();
        return;
    }

    if (phase == Phase::HIDDEN) {
        if (nextVisitMs == 0) scheduleNext(now);
        if (now >= nextVisitMs) {
            beginEnter(now);
        }
        return;
    }

    // Step timer for leg anim + movement
    if (now - lastStepMs < 33) return;
    lastStepMs = now;
    legPhase++;

    int pigFeet = Avatar::getCurrentX() + 14 * PX;
    float target = (float)pigFeet;

    switch (phase) {
        case Phase::ENTER:
        case Phase::CHASE: {
            // Sitting pig = peaceful: wolf strolls past and leaves (no bite)
            const bool pigSitting = Avatar::isSitting();

            float dx = target - x;
            if (pigSitting) {
                // Keep walking in current direction (or toward pig once, then past)
                if (vx == 0.0f) {
                    if (dx > 0) { faceRight = true;  vx = CHASE_SPEED * 0.9f; }
                    else        { faceRight = false; vx = -CHASE_SPEED * 0.9f; }
                }
            } else {
                // Home in on pig
                if (dx > 1.0f) {
                    faceRight = true;
                    vx = CHASE_SPEED;
                } else if (dx < -1.0f) {
                    faceRight = false;
                    vx = -CHASE_SPEED;
                } else {
                    vx = 0.0f;
                }
            }
            // Slight vertical bob while running
            y = 106.0f - (float)((legPhase / 2) & 1);

            x += vx;

            float adx = dx < 0 ? -dx : dx;
            if (adx < (float)SCARE_DIST) {
                if (pigSitting) {
                    // Pass by — no snarl, no bite, keep going off-screen
                    phase = Phase::LEAVE;
                    phaseStartMs = now;
                    // Continue same direction (don't reverse into the pig)
                    if (faceRight) vx = LEAVE_SPEED * 0.95f;
                    else           vx = -LEAVE_SPEED * 0.95f;
                } else {
                    // Catch — bite: pig play-dead + 10s control lock
                    phase = Phase::SCARE;
                    phaseStartMs = now;
                    phaseTick = 0;
                    vx = 0.0f;
                    if (!howled) {
                        howled = true;
                        SFX::play(SFX::WOLF);
                        Avatar::onWolfBitten();
                    }
                }
            }

            // If pig jumps away far, keep chasing a bit then leave if too long
            if (now - phaseStartMs > 18000) {
                phase = Phase::LEAVE;
                phaseStartMs = now;
                // run off the nearer edge
                if (x < 120) {
                    faceRight = false;
                    vx = -LEAVE_SPEED;
                } else {
                    faceRight = true;
                    vx = LEAVE_SPEED;
                }
            }
            break;
        }
        case Phase::SCARE: {
            phaseTick++;
            // Snarl bob
            y = 106.0f - ((phaseTick / 3) & 1) * 2.0f;
            // Face pig
            faceRight = (target >= x);
            if (now - phaseStartMs > SCARE_MS) {
                phase = Phase::LEAVE;
                phaseStartMs = now;
                // Leave opposite to entry direction (away from pig)
                if (x < target) {
                    faceRight = false;
                    vx = -LEAVE_SPEED;
                } else {
                    faceRight = true;
                    vx = LEAVE_SPEED;
                }
            }
            break;
        }
        case Phase::LEAVE: {
            x += vx;
            y = 106.0f - (float)((legPhase / 2) & 1);
            if (x < -40.0f || x > (float)(DISPLAY_W + 40)) {
                phase = Phase::HIDDEN;
                scheduleNext(now);
            }
            break;
        }
        default:
            break;
    }
}

// Fat-pixel side wolf facing right (mirrored if !faceRight)
// Local cells around feet origin (0,0) at ground; body goes up.
static void stamp(M5Canvas& canvas, int16_t ox, int16_t oy, int lx, int ly,
                  bool right, uint16_t col) {
    int16_t px = right ? (ox + lx * PX) : (ox - (lx + 1) * PX);
    int16_t py = oy + ly * PX;
    if (px < -PX || px > DISPLAY_W || py < -PX || py > 120) return;
    canvas.fillRect(px, py, PX, PX, col);
}

static void stampBlock(M5Canvas& canvas, int16_t ox, int16_t oy,
                       int lx, int ly, int w, int h, bool right, uint16_t col) {
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++)
            stamp(canvas, ox, oy, lx + xx, ly + yy, right, col);
}

void draw(M5Canvas& canvas) {
    if (phase == Phase::HIDDEN) return;

    // Silhouette from pixel-wolf ref; colors follow season
    const bool flash = Avatar::isThunderFlashing();
    Season season = Weather::getActiveSeason();

    // OUT, FUR, FUR2 (shade), FURH (hi), BELLY, NOSE, EYE, EAR
    uint16_t OUT, FUR, FUR2, FURH, BELLY, NOSE, EYE, EAR;
    switch (season) {
        case Season::WINTER:
            // Arctic wolf — white / blue-gray
            OUT  = 0x6B6D; FUR = 0xEF5D; FUR2 = 0xC618; FURH = 0xFFFF;
            BELLY = 0xFFFF; NOSE = 0x3186; EYE = 0x5D7F; EAR = 0xF6B0;
            break;
        case Season::SPRING:
            // Fresh tawny / honey
            OUT  = 0x6200; FUR = 0xD4A0; FUR2 = 0x9A40; FURH = 0xF6E0;
            BELLY = 0xFFF0; NOSE = 0x28A2; EYE = 0x07E0; EAR = 0xFD2F;
            break;
        case Season::AUTUMN:
            // Deep russet / fox-brown
            OUT  = 0x4000; FUR = 0xC2C0; FUR2 = 0x8000; FURH = 0xE3E0;
            BELLY = 0xFDE0; NOSE = 0x2100; EYE = 0xFD20; EAR = 0xFBE0;
            break;
        case Season::SUMMER:
        default:
            // Classic brown / cinnamon
            OUT  = 0x41E0; FUR = 0x9A60; FUR2 = 0x7200; FURH = 0xD4A0;
            BELLY = 0xF6D0; NOSE = 0x28A2; EYE = 0xFE60; EAR = 0xC2C8;
            break;
    }
    if (flash) {
        OUT = FUR = FUR2 = FURH = BELLY = NOSE = EYE = EAR = 0xFFFF;
    }
    const uint16_t PUP  = flash ? 0x0000 : 0x2100;
    const uint16_t TONG = flash ? 0xFFFF : 0xF98C;

    int16_t ox = (int16_t)x;
    int16_t oy = (int16_t)y;
    bool right = faceRight;
    int leg = (legPhase / 2) & 1;

    // Soft shadow under wolf for depth
    for (int dx = -12; dx <= 14; dx++) {
        int adx = dx < 0 ? -dx : dx;
        int16_t sx = right ? (ox + dx * PX) : (ox - (dx + 1) * PX);
        if (sx < 0 || sx >= DISPLAY_W) continue;
        if (adx <= 10) {
            canvas.drawPixel(sx, oy, 0x4208);
            if ((dx & 1) == 0) canvas.drawPixel(sx, oy - 1, 0x5ACB);
        }
    }

    // ========== LEGS — slimmer, more articulated ==========
    // Hind legs (slightly staggered for motion)
    stampBlock(canvas, ox, oy, -8, -6 + leg, 2, 6, right, FUR);
    stampBlock(canvas, ox, oy, -7, -5 + leg, 2, 4, right, OUT);
    stampBlock(canvas, ox, oy, -5, -6 + (1 - leg), 2, 6, right, FUR2);
    stampBlock(canvas, ox, oy, -4, -5 + (1 - leg), 2, 4, right, OUT);
    // Front legs
    stampBlock(canvas, ox, oy, 2, -6 + (1 - leg), 2, 6, right, FUR);
    stampBlock(canvas, ox, oy, 3, -5 + (1 - leg), 2, 4, right, OUT);
    stampBlock(canvas, ox, oy, 6, -6 + leg, 2, 6, right, FUR2);
    stampBlock(canvas, ox, oy, 7, -5 + leg, 2, 4, right, OUT);
    // Paws (small highlights)
    stampBlock(canvas, ox, oy, -8, -1 + leg, 2, 1, right, OUT);
    stampBlock(canvas, ox, oy, -5, -1 + (1 - leg), 2, 1, right, OUT);
    stampBlock(canvas, ox, oy, 2, -1 + (1 - leg), 2, 1, right, OUT);
    stampBlock(canvas, ox, oy, 6, -1 + leg, 2, 1, right, OUT);

    // ========== BODY — sleeker, chest-forward silhouette ==========
    // Top rim and shoulder (broader chest, tapered belly)
    stampBlock(canvas, ox, oy, -7, -12, 14, 2, right, OUT);
    stampBlock(canvas, ox, oy, -8, -10, 16, 2, right, OUT);
    stampBlock(canvas, ox, oy, -8, -8, 16, 2, right, OUT);

    // Chest panel (emphasize forward mass)
    stampBlock(canvas, ox, oy, -5, -11, 10, 3, right, FUR);
    stampBlock(canvas, ox, oy, -4, -9, 8, 2, right, FURH);

    // Tapered midsection — narrower rows to avoid 'pregnant' belly
    stampBlock(canvas, ox, oy, -6, -7, 12, 2, right, FUR);
    stampBlock(canvas, ox, oy, -5, -5, 10, 1, right, FUR);

    // Minimal belly strip (small, recessed)
    stampBlock(canvas, ox, oy, -3, -4, 6, 1, right, BELLY);

    // Light fur texture to avoid flat slabs (small accents)
    stamp(canvas, ox, oy, -6, -9, right, FUR2);
    stamp(canvas, ox, oy, -2, -10, right, FUR2);
    stamp(canvas, ox, oy, -1, -8, right, FURH);

    // Subtle back/shoulder tuft
    stampBlock(canvas, ox, oy, -1, -13, 3, 2, right, FUR2);
    stamp(canvas, ox, oy, -3, -13, right, FUR);

    // ========== TAIL — long and tapered, expressive ==========
    int tw = ((legPhase / 2) & 3) - 1; // -1..2 for more motion
    stampBlock(canvas, ox, oy, -13, -11 + tw, 6, 5, right, OUT);
    stampBlock(canvas, ox, oy, -12, -10 + tw, 5, 4, right, FUR);
    stampBlock(canvas, ox, oy, -14, -10 + tw, 4, 3, right, FURH);
    stamp(canvas, ox, oy, -15, -9 + tw, right, OUT);

    // ========== HEAD — elongated, elegant snout ==========
    // Cranium + ear tufts
    stampBlock(canvas, ox, oy, 6, -14, 5, 6, right, OUT);
    stamp(canvas, ox, oy, 7, -15, right, EAR);
    stamp(canvas, ox, oy, 9, -15, right, EAR);
    stampBlock(canvas, ox, oy, 8, -13, 3, 2, right, FUR);

    // Snout (tapered)
    stampBlock(canvas, ox, oy, 11, -13, 6, 4, right, OUT);
    stampBlock(canvas, ox, oy, 13, -11, 4, 3, right, FURH);
    stampBlock(canvas, ox, oy, 15, -10, 2, 2, right, NOSE);

    // Muzzle shading and cheek
    stampBlock(canvas, ox, oy, 10, -12, 3, 3, right, FUR2);
    stampBlock(canvas, ox, oy, 12, -12, 2, 2, right, FUR);
    stampBlock(canvas, ox, oy, 9, -11, 3, 2, right, BELLY);

    // Eyes — stylish, in-style small slits
    uint16_t eyeCol = EYE;
    if (phase == Phase::SCARE || howled) eyeCol = 0xF800; // red glow on aggressive
    stamp(canvas, ox, oy, 10, -12, right, eyeCol);
    stamp(canvas, ox, oy, 11, -12, right, PUP);

    // Teeth / mouth for SCARE
    if (phase == Phase::SCARE) {
        // open snarl with fangs
        stamp(canvas, ox, oy, 14, -9, right, 0xFFFF);
        stamp(canvas, ox, oy, 14, -11, right, 0xFFFF);
        stampBlock(canvas, ox, oy, 12, -10, 3, 1, right, TONG);
    } else {
        // closed mouth line
        stampBlock(canvas, ox, oy, 13, -9, 3, 1, right, NOSE);
    }

    // Small accent fur near chest
    stampBlock(canvas, ox, oy, 2, -13, 2, 3, right, FURH);
    stamp(canvas, ox, oy, 0, -14, right, FUR2);

    // Optional howled glow (one-time) — brief eye flare
    if (howled) {
        stamp(canvas, ox, oy, 10, -13, right, 0xF800);
    }
}

}  // namespace Wolf

// Falling produce (apples / cherries / acorns / cones / berries).
// Separate from tree geometry so ground fruit can scroll with the world
// and draw in the foreground above grass.
#pragma once

#include <M5Unified.h>
#include <stdint.h>

namespace Trees {

// Seasonal produce kinds (on-tree + falling)
enum class Produce : uint8_t {
    BERRY = 0,
    RED_APPLE,
    YELLOW_APPLE,
    GREEN_APPLE,   // autumn old apple tree — soft green apples
    CHERRY,        // spring cherry tree — paired red cherries
    ACORN,
    CONE
};

// Ground line for landed fruit (pig feet sit at ~106)
static constexpr int16_t DROP_GROUND_Y = 103;

void dropsReset();
// Move active drops with the grass treadmill (+1 / -1 per step, same as tree scroll)
void dropsScroll(int dir);
// Spawn one falling item at screen/world X,Y (Y = spawn height, not ground)
bool dropsSpawn(int16_t x, int16_t y, uint8_t r, Produce p);
// Shared fat-pixel produce stamp (hanging fruit + falling)
void drawProduce(M5Canvas& canvas, int16_t cx, int16_t cy, int r, Produce p);

}  // namespace Trees

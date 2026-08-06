// Wolf visitor — separate scene actor (not weather / not seasonal FX).
// Sometimes enters from off-screen, chases the pig, then leaves.
// Keep all wolf logic HERE so avatar/weather stay clean.
//
// Call Wolf::update() with SeasonalFx; draw AFTER Avatar (on top of pig/grass).
#pragma once

#include <M5Unified.h>

namespace Wolf {

void init();
void update();
void draw(M5Canvas& canvas);
void reset();

// Force a visit (ANIM TEST / debug later)
void spawnNow();

// Pig jumped on / attacked wolf — flee off screen
void scareAway();

bool isActive();
// Feet X of wolf body center (for debug / future collisions)
int16_t getX();
// Feet Y (ground line ~106)
int16_t getY();

}  // namespace Wolf

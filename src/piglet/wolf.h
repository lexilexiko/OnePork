// Wolf visitor — separate scene actor (not weather / not seasonal FX).
// Ambient: 1 wolf auto-spawns. Fruit Run can disable auto and spawn 1–2 wolves.
//
// Call Wolf::update() with SeasonalFx; draw AFTER Avatar (on top of pig/grass).
#pragma once

#include <M5Unified.h>

namespace Wolf {

void init();
void update();
void draw(M5Canvas& canvas);
void reset();

// Force a visit (ANIM TEST / fruit-run spawns)
void spawnNow();
// Spawn a second wolf if slot free (hard mode)
bool spawnSecond();

// Pig jumped on / attacked wolf — nearest active flees
void scareAway();
// Scare any wolf near screen X (for multi-wolf stomp)
void scareNear(int feetX, int radius = 36);

// Ambient auto-spawn on/off (Fruit Run turns off and spawns itself)
void setAutoSpawn(bool enabled);
bool getAutoSpawn();
// How many wolves may be on screen at once (1 ambient, 2 hard mode)
void setMaxActive(uint8_t n);

bool isActive();
// Count of wolves currently on screen
uint8_t getActiveCount();
// Feet X of first active wolf (compat)
int16_t getX();
int16_t getY();

// Fruit Run: true once per bite event (clears flag)
bool consumeBiteEvent();

}  // namespace Wolf

// Seasonal decorative FX — separate from core weather (rain/snow/clouds/thunder).
// Put snow banks, tumbleweeds, butterflies, pollen, etc. HERE so weather.cpp stays stable.
//
// Seasons from Weather::getActiveSeason() / Config SEASON setting.
// Call after Weather::update() and draw after Weather::draw() (on top of precip).
#pragma once

#include <M5Unified.h>

namespace SeasonalFx {

// === Lifecycle ===
void init();
void update();
// Foreground season decor (banks, leaves, tumbleweed, butterflies)
void draw(M5Canvas& canvas);
// Backdrop season decor — call after clouds, BEFORE tree/pig
// (spring lightning bolts live here so they sit in the sky)
void drawBackdrop(M5Canvas& canvas);

// Optional: clear all FX (mode changes, tests)
void reset();

// Spring  — lightning bolts (backdrop), flower-heavy grass is in Avatar
// Summer  — butterflies
// Autumn  — leaves, tumbleweed
// Winter  — snow banks

}  // namespace SeasonalFx

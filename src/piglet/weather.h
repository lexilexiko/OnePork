// Weather effects module - clouds, rain/snow, thunder, wind, season clock
// Living weather cycle + seasons (AUTO 15 min or manual from SETTINGS)
// Seasonal decor (banks, leaves, tumbleweed, butterflies) → seasonal_fx.h
#pragma once

#include <M5Unified.h>
#include "../core/config.h"

namespace Weather {

// Ambient weather phases (independent of mood; mood only biases transitions)
enum class Phase : uint8_t {
    CLEAR = 0,
    CLOUDY,
    RAIN,
    STORM
};

// === INITIALIZATION ===
void init();

// === WEATHER STATE CONTROL ===
// Call from Mood system — biases rain/storm odds, does not force clear forever
void setMoodLevel(int momentum);  // -100 to 100

// Manual overrides (for testing or special events)
void setRaining(bool active);
void triggerThunderStorm();

// === ANIMATION UPDATES ===
void update();

// === DRAWING ===
void draw(M5Canvas& canvas, uint16_t colorFG, uint16_t colorBG);
// yOffset: shift cloud Y for compositing (TOP_BAR_H when drawing into top bar so
// cloud tops continue above the main canvas; text is drawn after on the bar).
void drawClouds(M5Canvas& canvas, uint16_t colorFG, int16_t yOffset = 0);
void drawBirds(M5Canvas& canvas, uint16_t colorFG);

// === QUERIES ===
bool isThunderFlashing();
bool isRaining();   // precip active (rain or snow)
bool isSnowing();   // winter precip (white flakes)
bool isStorming();
bool isCloudy();
Phase getPhase();

// Active season (never AUTO) — grass palette + precip type
Season getActiveSeason();
const char* getSeasonName();

}  // namespace Weather

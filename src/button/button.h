// button/button.h
// BOOT button handler (GPIO0) for toggle START/STOP capture
// M5Stamp C3 has a built-in BOOT button (pull-up, active LOW)

#pragma once

#include <Arduino.h>

namespace Button {

// Initialize BOOT button (GPIO0)
void begin();

// Call from loop() to service button debounce
void loop();

// Get current button state (pressed = true)
bool isPressed();

} // namespace Button

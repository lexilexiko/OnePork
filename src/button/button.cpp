// button/button.cpp
// M5Stamp C3 user button is GPIO3 (pulled up, active LOW).
// GPIO0 is only a header pin, not the onboard button.

#include "button.h"
#include <string.h>

namespace Button {

static const uint8_t BOOT_PIN = 3;
static const uint32_t DEBOUNCE_MS = 40;

static struct {
    bool pressed;
    bool lastRaw;
    unsigned long lastChange;
} s_state;

void begin() {
    memset(&s_state, 0, sizeof(s_state));
    pinMode(BOOT_PIN, INPUT_PULLUP);
    s_state.lastRaw = (digitalRead(BOOT_PIN) == LOW);
    s_state.pressed = s_state.lastRaw;
    s_state.lastChange = millis();
    Serial.println("[BUTTON] user button GPIO3 initialized");
}

void loop() {
    bool raw = (digitalRead(BOOT_PIN) == LOW);
    unsigned long now = millis();
    if (raw != s_state.lastRaw) {
        s_state.lastChange = now;
        s_state.lastRaw = raw;
    }
    if (now - s_state.lastChange >= DEBOUNCE_MS) {
        s_state.pressed = raw;
    }
}

bool isPressed() {
    return s_state.pressed;
}

} // namespace Button

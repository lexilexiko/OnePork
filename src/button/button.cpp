// button/button.cpp
#include "button.h"
#include "../board/board.h"
#include <nvs.h>
#include <string.h>

namespace Button {

static const char* NVS_NS = "pork";
static const char* KEY_BTN = "btngpio";
static const uint32_t DEBOUNCE_MS = 40;

static uint8_t s_pin = BUTTON_GPIO_DEFAULT;

static struct {
    bool pressed;
    bool lastRaw;
    unsigned long lastChange;
} s_state;

static uint8_t loadPin() {
    nvs_handle_t h;
    uint8_t pin = Board::defaultButtonGpio();
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        uint8_t stored = 0;
        if (nvs_get_u8(h, KEY_BTN, &stored) == ESP_OK && Board::gpioAllowed((int)stored)) {
            pin = stored;
        }
        nvs_close(h);
    }
    return pin;
}

static bool savePin(uint8_t pin) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_set_u8(h, KEY_BTN, pin);
    nvs_commit(h);
    nvs_close(h);
    return true;
}

static void armPin(uint8_t pin) {
    s_pin = pin;
    memset(&s_state, 0, sizeof(s_state));
    pinMode(s_pin, INPUT_PULLUP);
    s_state.lastRaw = (digitalRead(s_pin) == LOW);
    s_state.pressed = s_state.lastRaw;
    s_state.lastChange = millis();
}

void begin() {
    armPin(loadPin());
    Serial.printf("[BUTTON] GPIO%u (default GPIO%u) pull-up, active LOW\n",
                  (unsigned)s_pin, (unsigned)Board::defaultButtonGpio());
}

void loop() {
    bool raw = (digitalRead(s_pin) == LOW);
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

uint8_t pin() {
    return s_pin;
}

uint8_t defaultPin() {
    return Board::defaultButtonGpio();
}

bool setPin(int gpio) {
    if (!Board::gpioAllowed(gpio)) return false;
    uint8_t next = (uint8_t)gpio;
    if (!savePin(next)) return false;
    armPin(next);
    Serial.printf("[BUTTON] now GPIO%u\n", (unsigned)s_pin);
    return true;
}

} // namespace Button

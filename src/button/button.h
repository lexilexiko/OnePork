// button/button.h
// Aggressive-capture button. GPIO comes from NVS (web UI) or board default.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Button {

void begin();
void loop();
bool isPressed();

uint8_t pin();
uint8_t defaultPin();
bool setPin(int gpio);

} // namespace Button

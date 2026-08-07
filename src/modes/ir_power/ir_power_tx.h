// IR power-blast engine — compressed NA/EU packs + discrete NEC/Samsung/Sony
#pragma once

#include <stdint.h>
#include "ir_code_types.h"

namespace IrPower {

void setRegion(uint8_t region);   // IR_REGION_NA or IR_REGION_EU
uint8_t getRegion();
uint8_t getCodeCount();

// Cardputer IR LED (GPIO 44, active-low)
static constexpr int IR_TX_PIN = 44;

// Transmit one builtin power code by index (blocks briefly)
bool sendCode(uint8_t index);

void sendNEC(uint16_t address, uint8_t command, uint8_t repeats = 1);
void sendSamsung(uint16_t address, uint16_t command, uint8_t repeats = 1);
void sendSony(uint16_t data, uint8_t nbits, uint8_t repeats = 2);

}  // namespace IrPower

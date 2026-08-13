// cap/hc22000.h - build hashcat 22000 / hc22000 from 802.11 frames
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Hc22000 {

void reset();
void feed(const uint8_t* frame, uint16_t len);
bool shouldPauseDeauth();
uint16_t convertPcap(const char* pcapPath);
uint16_t convertAllPcaps();

} // namespace Hc22000

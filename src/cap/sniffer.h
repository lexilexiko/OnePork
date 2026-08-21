// cap/sniffer.h
// Light: this channel, UI stays, PMKID probe, no deauth/hop.
// Aggressive (board button): hop, kick, lock on EAPOL so M2 is not missed.

#pragma once

#include <Arduino.h>
#include <stdint.h>

namespace Cap {

enum class RunMode : uint8_t {
    Off = 0,
    Light,
    Aggressive,
};

void begin();
void startLight();
void startAggressive();
void stop();
bool isRunning();
RunMode runMode();
bool isLocked();

void loop();

struct Counters {
    uint32_t framesSeen;
    uint32_t framesEapol;
    uint32_t framesQueued;
    uint32_t framesDropped;
    uint32_t framesWritten;
    uint32_t framesDeauth;
    uint32_t filesOpened;
    uint8_t  currentChannel;
    char     currentBssid[18];
    char     lastHsSsid[33];
};
const Counters& counters();

} // namespace Cap

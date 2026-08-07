// IR transmitter — builtin compressed power packs + discrete protocols
// Cardputer IR LED on GPIO44 (active-low).

#include "ir_power_tx.h"
#include <Arduino.h>
#include <M5Unified.h>
#include "../../audio/sfx.h"

#include "WORLD_IR_CODES.h"

namespace IrPower {

static uint8_t s_region = IR_REGION_NA;
static uint8_t s_bitsleft = 0;
static uint8_t s_bits = 0;
static uint8_t s_codePtr = 0;
static const IrCode* s_powerCode = nullptr;

void setRegion(uint8_t region) {
    s_region = (region == IR_REGION_EU) ? IR_REGION_EU : IR_REGION_NA;
}

uint8_t getRegion() { return s_region; }

uint8_t getCodeCount() {
    return (s_region == IR_REGION_EU) ? num_EUcodes : num_NAcodes;
}

// Active-low IR LED
static inline void irOn()  { digitalWrite(IR_TX_PIN, LOW); }
static inline void irOff() { digitalWrite(IR_TX_PIN, HIGH); }

static void delay_ten_us(uint16_t us) {
    if (us == 0) return;
    delayMicroseconds((uint32_t)us * 10u);
}

static void carrierMark(uint32_t us, uint8_t khz) {
    if (khz < 20) khz = 38;
    if (khz > 60) khz = 38;
    const uint32_t period = 1000u / (uint32_t)khz;
    uint32_t onUs = period / 3;
    if (onUs < 3) onUs = 3;
    uint32_t offUs = period - onUs;
    if (offUs < 3) offUs = 3;

    uint32_t end = micros() + us;
    while ((int32_t)(micros() - end) < 0) {
        irOn();
        delayMicroseconds(onUs);
        irOff();
        delayMicroseconds(offUs);
    }
    irOff();
}

static void spaceUs(uint32_t us) {
    irOff();
    if (us > 0) delayMicroseconds(us);
}

static uint8_t read_bits(uint8_t count) {
    uint8_t tmp = 0;
    for (uint8_t i = 0; i < count; i++) {
        if (s_bitsleft == 0) {
            s_bits = s_powerCode->codes[s_codePtr++];
            s_bitsleft = 8;
        }
        s_bitsleft--;
        tmp |= (uint8_t)(((s_bits >> s_bitsleft) & 1) << (count - 1 - i));
    }
    return tmp;
}

bool sendCode(uint8_t index) {
    uint8_t n = getCodeCount();
    if (index >= n) return false;

    // Silence piezo — carrier loop starves audio and causes glitches
    SFX::stop();
    M5.Speaker.stop();

    pinMode(IR_TX_PIN, OUTPUT);
    irOff();

    s_powerCode = (s_region == IR_REGION_EU) ? EUpowerCodes[index] : NApowerCodes[index];
    if (!s_powerCode) return false;

    const uint8_t freq = s_powerCode->timer_val;
    const uint8_t numpairs = s_powerCode->numpairs;
    const uint8_t bitcompression = s_powerCode->bitcompression;
    s_codePtr = 0;
    s_bitsleft = 0;
    s_bits = 0;

    // Cardputer active-low: first table word → mark, second → space
    for (uint8_t k = 0; k < numpairs; k++) {
        uint16_t ti = (uint16_t)read_bits(bitcompression) * 2u;
        uint16_t t0 = s_powerCode->times[ti];
        uint16_t t1 = s_powerCode->times[ti + 1];
        uint32_t markUs  = (uint32_t)t0 * 10u;
        uint32_t spaceU = (uint32_t)t1 * 10u;

        if (freq == 0) {
            if (markUs) { irOn(); delayMicroseconds(markUs); }
            irOff();
            if (spaceU) delayMicroseconds(spaceU);
        } else {
            if (markUs) carrierMark(markUs, freq);
            spaceUs(spaceU);
        }
    }

    irOff();
    s_bitsleft = 0;
    delay_ten_us(20500);  // inter-code gap
    return true;
}

static void necBit(bool one, uint8_t khz) {
    carrierMark(560, khz);
    spaceUs(one ? 1690 : 560);
}

void sendNEC(uint16_t address, uint8_t command, uint8_t repeats) {
    SFX::stop();
    M5.Speaker.stop();
    pinMode(IR_TX_PIN, OUTPUT);
    irOff();
    auto frame = [&]() {
        carrierMark(9000, 38);
        spaceUs(4500);
        uint8_t a = (uint8_t)(address & 0xFF);
        uint8_t a2 = (uint8_t)((address >> 8) & 0xFF);
        if (a2 == 0) a2 = (uint8_t)~a;
        uint8_t c = command;
        uint8_t c2 = (uint8_t)~command;
        auto sb = [&](uint8_t b) {
            for (int i = 0; i < 8; i++) { necBit(b & 1, 38); b >>= 1; }
        };
        sb(a); sb(a2); sb(c); sb(c2);
        carrierMark(560, 38);
        spaceUs(40000);
    };
    frame();
    for (uint8_t r = 0; r < repeats; r++) {
        carrierMark(9000, 38);
        spaceUs(2250);
        carrierMark(560, 38);
        spaceUs(96000);
    }
    irOff();
}

void sendSamsung(uint16_t address, uint16_t command, uint8_t repeats) {
    SFX::stop();
    M5.Speaker.stop();
    pinMode(IR_TX_PIN, OUTPUT);
    irOff();
    uint32_t data = ((uint32_t)command << 16) | (uint32_t)address;
    auto frame = [&]() {
        carrierMark(4500, 38);
        spaceUs(4500);
        uint32_t d = data;
        for (int i = 0; i < 32; i++) {
            carrierMark(560, 38);
            spaceUs((d & 1u) ? 1690 : 560);
            d >>= 1;
        }
        carrierMark(560, 38);
        spaceUs(46000);
    };
    frame();
    for (uint8_t r = 0; r < repeats; r++) { delay(40); frame(); }
    irOff();
}

void sendSony(uint16_t data, uint8_t nbits, uint8_t repeats) {
    SFX::stop();
    M5.Speaker.stop();
    pinMode(IR_TX_PIN, OUTPUT);
    irOff();
    if (nbits < 12) nbits = 12;
    if (nbits > 20) nbits = 20;
    for (uint8_t r = 0; r <= repeats; r++) {
        carrierMark(2400, 40);
        spaceUs(600);
        uint16_t d = data;
        for (uint8_t i = 0; i < nbits; i++) {
            carrierMark((d & 1) ? 1200 : 600, 40);
            spaceUs(600);
            d >>= 1;
        }
        spaceUs(20000);
    }
    irOff();
}

}  // namespace IrPower

// Compressed IR power-code table format (TV-B-Gone style packs)
#pragma once

#include <stdint.h>

#define IR_REGION_NA 0
#define IR_REGION_EU 1

// Names used by the power-code table
#ifndef NA
#define NA IR_REGION_NA
#endif
#ifndef EU
#define EU IR_REGION_EU
#endif

#define NUM_ELEM(x) (sizeof(x) / sizeof(*(x)))

// Carrier kHz stored as freq/1000 (e.g. 38400 → 38)
#define freq_to_timerval(x) ((x) / 1000)

struct IrCode {
    uint8_t timer_val;       // carrier kHz
    uint8_t numpairs;        // mark/space pairs
    uint8_t bitcompression;  // bits per times[] index
    const uint16_t* times;   // timing table (units of 10 us)
    const uint8_t* codes;    // packed pair indices
};

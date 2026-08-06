// PigPass crypto — software SHA1/PBKDF2 hot path
// Technique adapted from BruceDevices/firmware wifi_recover.cpp:
//  - software SHA1 (not mbedtls HW — HW mutex kills dual-core)
//  - precomputed HMAC pads + mid-state
//  - specialized 20-byte HMAC for PBKDF2 U2..U4096
//  - IRAM + O3 on transforms
// Measured by Bruce: ~13-14 pwd/s dual-core ESP32-S3 @ 240 MHz
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct PigpassSha1Ctx {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buf[64];
};

struct PigpassHmacPre {
    PigpassSha1Ctx inner; // after ipad block
    PigpassSha1Ctx outer; // after opad block
};

// Prepare HMAC key schedule (once per password / once per PMK)
void pigpass_hmac_precompute(const uint8_t* key, size_t klen, PigpassHmacPre& out);

// General HMAC with precomputed pads
void pigpass_hmac_with_pre(const PigpassHmacPre& pre, const uint8_t* data, size_t dlen, uint8_t out20[20]);

// WPA2 PMK: PBKDF2-HMAC-SHA1(password, ssid, 4096, 32)
void pigpass_pbkdf2_pmk(const PigpassHmacPre& pwPre,
                        const uint8_t* ssid, size_t ssidLen,
                        uint8_t pmk[32]);

// PRF-512 Pairwise key expansion → 64-byte PTK
bool pigpass_derive_ptk(const uint8_t pmk[32],
                        const uint8_t* ptkData76, // prebuilt min/max MAC||nonce
                        uint8_t ptk[64]);

// WPA2 MIC: HMAC-SHA1(KCK, eapol_zeroed_mic) first 16 bytes
bool pigpass_verify_mic_wpa2(const uint8_t kck[16],
                             const uint8_t* eapol, uint16_t eapolLen,
                             const uint8_t expectedMic[16]);

// WPA (MD5) MIC fallback — rare; uses mbedtls
bool pigpass_verify_mic_wpa(const uint8_t kck[16],
                            const uint8_t* eapol, uint16_t eapolLen,
                            const uint8_t expectedMic[16]);

// PMKID = HMAC-SHA1-128(PMK, "PMK Name"||AA||SPA)
bool pigpass_verify_pmkid(const uint8_t pmk[32],
                          const uint8_t apMac[6], const uint8_t staMac[6],
                          const uint8_t expected[16]);

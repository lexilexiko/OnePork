// Software SHA1 / PBKDF2 for PigPass (see pigpass_crypto.h)
// Hot-path layout follows Bruce wifi_recover (dual-core friendly, no HW SHA mutex).

#include "pigpass_crypto.h"
#include <mbedtls/md.h>
#include <esp_attr.h>

#define SHA1_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define SHA1_BLK(i)                                                                                          \
    (blk[i & 15] = SHA1_ROL(blk[(i + 13) & 15] ^ blk[(i + 8) & 15] ^ blk[(i + 2) & 15] ^ blk[i & 15], 1))

#define R0(v, w, x, y, z, i)                                                                                 \
    z += ((w & (x ^ y)) ^ y) + blk[i] + 0x5A827999u + SHA1_ROL(v, 5);                                        \
    w = SHA1_ROL(w, 30)
#define R1(v, w, x, y, z, i)                                                                                 \
    z += ((w & (x ^ y)) ^ y) + SHA1_BLK(i) + 0x5A827999u + SHA1_ROL(v, 5);                                   \
    w = SHA1_ROL(w, 30)
#define R2(v, w, x, y, z, i)                                                                                 \
    z += (w ^ x ^ y) + SHA1_BLK(i) + 0x6ED9EBA1u + SHA1_ROL(v, 5);                                           \
    w = SHA1_ROL(w, 30)
#define R3(v, w, x, y, z, i)                                                                                 \
    z += (((w | x) & y) | (w & x)) + SHA1_BLK(i) + 0x8F1BBCDCu + SHA1_ROL(v, 5);                             \
    w = SHA1_ROL(w, 30)
#define R4(v, w, x, y, z, i)                                                                                 \
    z += (w ^ x ^ y) + SHA1_BLK(i) + 0xCA62C1D6u + SHA1_ROL(v, 5);                                           \
    w = SHA1_ROL(w, 30)

#define SHA1_80_ROUNDS()                                                                                     \
    R0(a, b, c, d, e, 0);                                                                                    \
    R0(e, a, b, c, d, 1);                                                                                    \
    R0(d, e, a, b, c, 2);                                                                                    \
    R0(c, d, e, a, b, 3);                                                                                    \
    R0(b, c, d, e, a, 4);                                                                                    \
    R0(a, b, c, d, e, 5);                                                                                    \
    R0(e, a, b, c, d, 6);                                                                                    \
    R0(d, e, a, b, c, 7);                                                                                    \
    R0(c, d, e, a, b, 8);                                                                                    \
    R0(b, c, d, e, a, 9);                                                                                    \
    R0(a, b, c, d, e, 10);                                                                                   \
    R0(e, a, b, c, d, 11);                                                                                   \
    R0(d, e, a, b, c, 12);                                                                                   \
    R0(c, d, e, a, b, 13);                                                                                   \
    R0(b, c, d, e, a, 14);                                                                                   \
    R0(a, b, c, d, e, 15);                                                                                   \
    R1(e, a, b, c, d, 16);                                                                                   \
    R1(d, e, a, b, c, 17);                                                                                   \
    R1(c, d, e, a, b, 18);                                                                                   \
    R1(b, c, d, e, a, 19);                                                                                   \
    R2(a, b, c, d, e, 20);                                                                                   \
    R2(e, a, b, c, d, 21);                                                                                   \
    R2(d, e, a, b, c, 22);                                                                                   \
    R2(c, d, e, a, b, 23);                                                                                   \
    R2(b, c, d, e, a, 24);                                                                                   \
    R2(a, b, c, d, e, 25);                                                                                   \
    R2(e, a, b, c, d, 26);                                                                                   \
    R2(d, e, a, b, c, 27);                                                                                   \
    R2(c, d, e, a, b, 28);                                                                                   \
    R2(b, c, d, e, a, 29);                                                                                   \
    R2(a, b, c, d, e, 30);                                                                                   \
    R2(e, a, b, c, d, 31);                                                                                   \
    R2(d, e, a, b, c, 32);                                                                                   \
    R2(c, d, e, a, b, 33);                                                                                   \
    R2(b, c, d, e, a, 34);                                                                                   \
    R2(a, b, c, d, e, 35);                                                                                   \
    R2(e, a, b, c, d, 36);                                                                                   \
    R2(d, e, a, b, c, 37);                                                                                   \
    R2(c, d, e, a, b, 38);                                                                                   \
    R2(b, c, d, e, a, 39);                                                                                   \
    R3(a, b, c, d, e, 40);                                                                                   \
    R3(e, a, b, c, d, 41);                                                                                   \
    R3(d, e, a, b, c, 42);                                                                                   \
    R3(c, d, e, a, b, 43);                                                                                   \
    R3(b, c, d, e, a, 44);                                                                                   \
    R3(a, b, c, d, e, 45);                                                                                   \
    R3(e, a, b, c, d, 46);                                                                                   \
    R3(d, e, a, b, c, 47);                                                                                   \
    R3(c, d, e, a, b, 48);                                                                                   \
    R3(b, c, d, e, a, 49);                                                                                   \
    R3(a, b, c, d, e, 50);                                                                                   \
    R3(e, a, b, c, d, 51);                                                                                   \
    R3(d, e, a, b, c, 52);                                                                                   \
    R3(c, d, e, a, b, 53);                                                                                   \
    R3(b, c, d, e, a, 54);                                                                                   \
    R3(a, b, c, d, e, 55);                                                                                   \
    R3(e, a, b, c, d, 56);                                                                                   \
    R3(d, e, a, b, c, 57);                                                                                   \
    R3(c, d, e, a, b, 58);                                                                                   \
    R3(b, c, d, e, a, 59);                                                                                   \
    R4(a, b, c, d, e, 60);                                                                                   \
    R4(e, a, b, c, d, 61);                                                                                   \
    R4(d, e, a, b, c, 62);                                                                                   \
    R4(c, d, e, a, b, 63);                                                                                   \
    R4(b, c, d, e, a, 64);                                                                                   \
    R4(a, b, c, d, e, 65);                                                                                   \
    R4(e, a, b, c, d, 66);                                                                                   \
    R4(d, e, a, b, c, 67);                                                                                   \
    R4(c, d, e, a, b, 68);                                                                                   \
    R4(b, c, d, e, a, 69);                                                                                   \
    R4(a, b, c, d, e, 70);                                                                                   \
    R4(e, a, b, c, d, 71);                                                                                   \
    R4(d, e, a, b, c, 72);                                                                                   \
    R4(c, d, e, a, b, 73);                                                                                   \
    R4(b, c, d, e, a, 74);                                                                                   \
    R4(a, b, c, d, e, 75);                                                                                   \
    R4(e, a, b, c, d, 76);                                                                                   \
    R4(d, e, a, b, c, 77);                                                                                   \
    R4(c, d, e, a, b, 78);                                                                                   \
    R4(b, c, d, e, a, 79)

static IRAM_ATTR __attribute__((optimize("O3"), hot)) void
sha1_transform(uint32_t state[5], const uint8_t buf[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t blk[16], tmp;
    for (int i = 0; i < 16; i++) {
        memcpy(&tmp, buf + i * 4, 4);
        blk[i] = __builtin_bswap32(tmp);
    }
    SHA1_80_ROUNDS();
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

// 20-byte message + fixed SHA1 padding in one transform (PBKDF2 U path)
// bitlen = (64+20)*8 = 672 = 0x2A0
static IRAM_ATTR __attribute__((optimize("O3"), hot)) void
sha1_transform_20b(uint32_t state[5], const uint8_t data[20]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t blk[16], tmp;
    memcpy(&tmp, data + 0, 4);  blk[0] = __builtin_bswap32(tmp);
    memcpy(&tmp, data + 4, 4);  blk[1] = __builtin_bswap32(tmp);
    memcpy(&tmp, data + 8, 4);  blk[2] = __builtin_bswap32(tmp);
    memcpy(&tmp, data + 12, 4); blk[3] = __builtin_bswap32(tmp);
    memcpy(&tmp, data + 16, 4); blk[4] = __builtin_bswap32(tmp);
    blk[5] = 0x80000000u;
    blk[6] = blk[7] = blk[8] = blk[9] = blk[10] = blk[11] = blk[12] = blk[13] = blk[14] = 0;
    blk[15] = 0x000002A0u;
    SHA1_80_ROUNDS();
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static IRAM_ATTR __attribute__((optimize("O3"), hot)) void
sha1_transform_20w(uint32_t state[5], const uint32_t words[5]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t blk[16];
    blk[0] = words[0];
    blk[1] = words[1];
    blk[2] = words[2];
    blk[3] = words[3];
    blk[4] = words[4];
    blk[5] = 0x80000000u;
    blk[6] = blk[7] = blk[8] = blk[9] = blk[10] = blk[11] = blk[12] = blk[13] = blk[14] = 0;
    blk[15] = 0x000002A0u;
    SHA1_80_ROUNDS();
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static inline void sha1_extract(const uint32_t state[5], uint8_t digest[20]) {
    for (int i = 0; i < 5; i++) {
        uint32_t s = __builtin_bswap32(state[i]);
        memcpy(digest + i * 4, &s, 4);
    }
}

static void sha1_init(PigpassSha1Ctx& ctx) {
    ctx.state[0] = 0x67452301u;
    ctx.state[1] = 0xEFCDAB89u;
    ctx.state[2] = 0x98BADCFEu;
    ctx.state[3] = 0x10325476u;
    ctx.state[4] = 0xC3D2E1F0u;
    ctx.count[0] = ctx.count[1] = 0;
}

static void sha1_update(PigpassSha1Ctx& ctx, const uint8_t* data, size_t len) {
    uint32_t j = (ctx.count[0] >> 3) & 63;
    if ((ctx.count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3)) ctx.count[1]++;
    ctx.count[1] += (uint32_t)(len >> 29);
    if (j + len > 63) {
        size_t i = 64 - j;
        memcpy(ctx.buf + j, data, i);
        sha1_transform(ctx.state, ctx.buf);
        for (; i + 63 < len; i += 64) sha1_transform(ctx.state, data + i);
        j = 0;
        memcpy(ctx.buf, data + i, len - i);
    } else {
        memcpy(ctx.buf + j, data, len);
    }
}

static void sha1_final(PigpassSha1Ctx& ctx, uint8_t digest[20]) {
    uint64_t total_bits = ((uint64_t)ctx.count[1] << 32) | ctx.count[0];
    uint32_t j = (ctx.count[0] >> 3) & 63;
    ctx.buf[j++] = 0x80;
    if (j > 56) {
        memset(ctx.buf + j, 0, 64 - j);
        sha1_transform(ctx.state, ctx.buf);
        j = 0;
    }
    memset(ctx.buf + j, 0, 56 - j);
    ctx.buf[56] = (uint8_t)(total_bits >> 56);
    ctx.buf[57] = (uint8_t)(total_bits >> 48);
    ctx.buf[58] = (uint8_t)(total_bits >> 40);
    ctx.buf[59] = (uint8_t)(total_bits >> 32);
    ctx.buf[60] = (uint8_t)(total_bits >> 24);
    ctx.buf[61] = (uint8_t)(total_bits >> 16);
    ctx.buf[62] = (uint8_t)(total_bits >> 8);
    ctx.buf[63] = (uint8_t)(total_bits);
    sha1_transform(ctx.state, ctx.buf);
    sha1_extract(ctx.state, digest);
}

void pigpass_hmac_precompute(const uint8_t* key, size_t klen, PigpassHmacPre& out) {
    uint8_t k_ipad[64], k_opad[64];
    memset(k_ipad, 0x36, 64);
    memset(k_opad, 0x5C, 64);
    uint8_t hk[20];
    if (klen > 64) {
        PigpassSha1Ctx t;
        sha1_init(t);
        sha1_update(t, key, klen);
        sha1_final(t, hk);
        key = hk;
        klen = 20;
    }
    for (size_t i = 0; i < klen; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }
    sha1_init(out.inner);
    sha1_update(out.inner, k_ipad, 64);
    sha1_init(out.outer);
    sha1_update(out.outer, k_opad, 64);
}

// Fast path: HMAC of exactly 20 bytes (8190 of 8192 PBKDF2 HMACs)
static IRAM_ATTR __attribute__((optimize("O3"), hot)) void
hmac_sha1_20w(const PigpassHmacPre& pre, const uint32_t data[5], uint32_t out[5]) {
    uint32_t state[5];
    state[0] = pre.inner.state[0];
    state[1] = pre.inner.state[1];
    state[2] = pre.inner.state[2];
    state[3] = pre.inner.state[3];
    state[4] = pre.inner.state[4];
    sha1_transform_20w(state, data);
    uint32_t ih[5] = {state[0], state[1], state[2], state[3], state[4]};
    state[0] = pre.outer.state[0];
    state[1] = pre.outer.state[1];
    state[2] = pre.outer.state[2];
    state[3] = pre.outer.state[3];
    state[4] = pre.outer.state[4];
    sha1_transform_20w(state, ih);
    out[0] = state[0];
    out[1] = state[1];
    out[2] = state[2];
    out[3] = state[3];
    out[4] = state[4];
}

void pigpass_hmac_with_pre(const PigpassHmacPre& pre, const uint8_t* data, size_t dlen, uint8_t out20[20]) {
    uint8_t inner_hash[20];
    PigpassSha1Ctx ctx = pre.inner;
    sha1_update(ctx, data, dlen);
    sha1_final(ctx, inner_hash);
    ctx = pre.outer;
    sha1_update(ctx, inner_hash, 20);
    sha1_final(ctx, out20);
}

void pigpass_pbkdf2_pmk(const PigpassHmacPre& pre,
                        const uint8_t* salt, size_t slen,
                        uint8_t pmk[32]) {
    uint8_t salt_int[40];
    if (slen > 32) slen = 32;
    memcpy(salt_int, salt, slen);

    uint8_t U8[20];
    uint32_t U[5];
    uint32_t T[5];

    auto loadU = [&](const uint8_t* src) {
        uint32_t t;
        memcpy(&t, src + 0, 4);  U[0] = __builtin_bswap32(t);
        memcpy(&t, src + 4, 4);  U[1] = __builtin_bswap32(t);
        memcpy(&t, src + 8, 4);  U[2] = __builtin_bswap32(t);
        memcpy(&t, src + 12, 4); U[3] = __builtin_bswap32(t);
        memcpy(&t, src + 16, 4); U[4] = __builtin_bswap32(t);
    };

    // Block 1 (count = 1)
    salt_int[slen] = 0;
    salt_int[slen + 1] = 0;
    salt_int[slen + 2] = 0;
    salt_int[slen + 3] = 1;
    pigpass_hmac_with_pre(pre, salt_int, slen + 4, U8);
    loadU(U8);
    T[0] = U[0]; T[1] = U[1]; T[2] = U[2]; T[3] = U[3]; T[4] = U[4];
    for (unsigned i = 1; i < 4096; i++) {
        hmac_sha1_20w(pre, U, U);
        T[0] ^= U[0]; T[1] ^= U[1]; T[2] ^= U[2]; T[3] ^= U[3]; T[4] ^= U[4];
    }
    sha1_extract(T, pmk);

    // Block 2 (count = 2) — only first 12 bytes of T needed, keep 20 for simplicity
    salt_int[slen + 3] = 2;
    pigpass_hmac_with_pre(pre, salt_int, slen + 4, U8);
    loadU(U8);
    T[0] = U[0]; T[1] = U[1]; T[2] = U[2]; T[3] = U[3]; T[4] = U[4];
    for (unsigned i = 1; i < 4096; i++) {
        hmac_sha1_20w(pre, U, U);
        T[0] ^= U[0]; T[1] ^= U[1]; T[2] ^= U[2]; T[3] ^= U[3]; T[4] ^= U[4];
    }
    uint8_t t2[20];
    sha1_extract(T, t2);
    memcpy(pmk + 20, t2, 12); // PMK is 32 bytes total
}

bool pigpass_derive_ptk(const uint8_t pmk[32], const uint8_t* ptkData76, uint8_t ptk[64]) {
    if (!pmk || !ptkData76 || !ptk) return false;
    const char* label = "Pairwise key expansion";
    const size_t label_len = 22;
    PigpassHmacPre pmk_pre;
    pigpass_hmac_precompute(pmk, 32, pmk_pre);

    uint8_t msg[22 + 1 + 76 + 1];
    memcpy(msg, label, label_len);
    msg[label_len] = 0x00;
    memcpy(msg + label_len + 1, ptkData76, 76);

    for (int i = 0; i < 4; i++) {
        msg[label_len + 1 + 76] = (uint8_t)i;
        uint8_t hash[20];
        pigpass_hmac_with_pre(pmk_pre, msg, label_len + 1 + 76 + 1, hash);
        size_t cp = (i == 3) ? 4 : 20;
        memcpy(ptk + (i * 20), hash, cp);
    }
    return true;
}

bool pigpass_verify_mic_wpa2(const uint8_t kck[16],
                             const uint8_t* eapol, uint16_t eapolLen,
                             const uint8_t expectedMic[16]) {
    if (!kck || !eapol || !expectedMic || eapolLen < 99) return false;
    PigpassHmacPre kck_pre;
    pigpass_hmac_precompute(kck, 16, kck_pre);
    uint8_t mic[20];
    pigpass_hmac_with_pre(kck_pre, eapol, eapolLen, mic);
    return memcmp(mic, expectedMic, 16) == 0;
}

bool pigpass_verify_mic_wpa(const uint8_t kck[16],
                            const uint8_t* eapol, uint16_t eapolLen,
                            const uint8_t expectedMic[16]) {
    if (!kck || !eapol || !expectedMic || eapolLen < 99) return false;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    if (!info) return false;
    uint8_t mic[16];
    if (mbedtls_md_hmac(info, kck, 16, eapol, eapolLen, mic) != 0) return false;
    return memcmp(mic, expectedMic, 16) == 0;
}

bool pigpass_verify_pmkid(const uint8_t pmk[32],
                          const uint8_t apMac[6], const uint8_t staMac[6],
                          const uint8_t expected[16]) {
    uint8_t msg[8 + 6 + 6];
    memcpy(msg, "PMK Name", 8);
    memcpy(msg + 8, apMac, 6);
    memcpy(msg + 14, staMac, 6);
    PigpassHmacPre pre;
    pigpass_hmac_precompute(pmk, 32, pre);
    uint8_t dig[20];
    pigpass_hmac_with_pre(pre, msg, sizeof(msg), dig);
    return memcmp(dig, expected, 16) == 0;
}

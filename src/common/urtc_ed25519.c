// =============================================================================
// URTC-COMMON - firmware_base/urtc_ed25519.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed. Ed25519 signature verification and the SHA-512 it needs; see urtc_ed25519.h. The group arithmetic
// follows the public-domain TweetNaCl design (16 limbs of 16 bits, no data-dependent table lookups); the constants below were computed from the curve
// definition (tools/make_ed25519_vectors.py checks them against the Python reference), the SHA-512 constants from the cube and square roots of the first
// primes, as FIPS 180-4 defines them.
#include "urtc_ed25519.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// SHA-512 (FIPS 180-4)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
static const uint64_t K512[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
    0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
    0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
    0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
    0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
    0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
    0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
    0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
    0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
    0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
    0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
    0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
    0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
    0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
    0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
    0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

static const uint64_t H512[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))

static void sha512_block(urtc_sha512_t *ctx, const uint8_t *p)
{
    uint64_t w[80];
    for (int i = 0; i < 16; i++) {
        uint64_t v = 0;
        for (int j = 0; j < 8; j++) {
            v = (v << 8) | p[i * 8 + j];
        }
        w[i] = v;
    }
    for (int i = 16; i < 80; i++) {
        const uint64_t s0 = ROR64(w[i - 15], 1) ^ ROR64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const uint64_t s1 = ROR64(w[i - 2], 19) ^ ROR64(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint64_t a = ctx->h[0], b = ctx->h[1], c = ctx->h[2], d = ctx->h[3], e = ctx->h[4], f = ctx->h[5], g = ctx->h[6], h = ctx->h[7];
    for (int i = 0; i < 80; i++) {
        const uint64_t big1 = ROR64(e, 14) ^ ROR64(e, 18) ^ ROR64(e, 41);
        const uint64_t ch = (e & f) ^ (~e & g);
        const uint64_t t1 = h + big1 + ch + K512[i] + w[i];
        const uint64_t big0 = ROR64(a, 28) ^ ROR64(a, 34) ^ ROR64(a, 39);
        const uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint64_t t2 = big0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->h[0] += a; ctx->h[1] += b; ctx->h[2] += c; ctx->h[3] += d;
    ctx->h[4] += e; ctx->h[5] += f; ctx->h[6] += g; ctx->h[7] += h;
}

void urtc_sha512_init(urtc_sha512_t *ctx)
{
    memcpy(ctx->h, H512, sizeof H512);
    ctx->used = 0;
    ctx->total = 0;
}

void urtc_sha512_update(urtc_sha512_t *ctx, const uint8_t *data, size_t length)
{
    ctx->total += length;
    while (length > 0) {
        size_t room = 128u - ctx->used;
        size_t take = length < room ? length : room;
        memcpy(ctx->block + ctx->used, data, take);
        ctx->used += (uint32_t)take;
        data += take;
        length -= take;
        if (ctx->used == 128u) {
            sha512_block(ctx, ctx->block);
            ctx->used = 0;
        }
    }
}

void urtc_sha512_final(urtc_sha512_t *ctx, uint8_t out[URTC_SHA512_DIGEST_SIZE])
{
    const uint64_t bits = ctx->total << 3;   // messages here are far below 2^61 bytes
    ctx->block[ctx->used++] = 0x80u;
    if (ctx->used > 112u) {
        memset(ctx->block + ctx->used, 0, 128u - ctx->used);
        sha512_block(ctx, ctx->block);
        ctx->used = 0;
    }
    memset(ctx->block + ctx->used, 0, 120u - ctx->used);   // the upper 64 bits of the 128-bit length stay zero
    for (int i = 0; i < 8; i++) {
        ctx->block[120 + i] = (uint8_t)(bits >> (56 - 8 * i));
    }
    sha512_block(ctx, ctx->block);
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            out[i * 8 + j] = (uint8_t)(ctx->h[i] >> (56 - 8 * j));
        }
    }
}

void urtc_sha512(uint8_t out[URTC_SHA512_DIGEST_SIZE], const uint8_t *data, size_t length)
{
    urtc_sha512_t ctx;
    urtc_sha512_init(&ctx);
    urtc_sha512_update(&ctx, data, length);
    urtc_sha512_final(&ctx, out);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Field arithmetic modulo 2^255 - 19, 16 limbs of 16 bits
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
typedef int64_t gf[16];

static const gf GF0 = {0};
static const gf GF1 = {1};
static const gf CURVE_D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const gf CURVE_D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
static const gf BASE_X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const gf BASE_Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
static const gf SQRT_M1 = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

// The order of the group, little-endian.
static const uint8_t GROUP_ORDER[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
};

static void gf_set(gf r, const gf a)
{
    for (int i = 0; i < 16; i++) {
        r[i] = a[i];
    }
}

static void gf_carry(gf o)
{
    for (int i = 0; i < 16; i++) {
        o[i] += (int64_t)1 << 16;
        const int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * ((int64_t)1 << 16);
    }
}

static void gf_select(gf p, gf q, int b)
{
    const int64_t c = ~((int64_t)b - 1);
    for (int i = 0; i < 16; i++) {
        const int64_t t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void gf_pack(uint8_t *o, const gf n)
{
    gf m, t;
    gf_set(t, n);
    gf_carry(t);
    gf_carry(t);
    gf_carry(t);
    for (int j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        gf_select(t, m, 1 - b);
    }
    for (int i = 0; i < 16; i++) {
        o[2 * i] = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static int ct_differs(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) {
        d |= (uint8_t)(a[i] ^ b[i]);
    }
    return d != 0;
}

static int gf_differs(const gf a, const gf b)
{
    uint8_t c[32], d[32];
    gf_pack(c, a);
    gf_pack(d, b);
    return ct_differs(c, d, 32);
}

static uint8_t gf_parity(const gf a)
{
    uint8_t d[32];
    gf_pack(d, a);
    return d[0] & 1u;
}

static void gf_unpack(gf o, const uint8_t *n)
{
    for (int i = 0; i < 16; i++) {
        o[i] = n[2 * i] + ((int64_t)n[2 * i + 1] << 8);
    }
    o[15] &= 0x7fff;
}

static void gf_add(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++) {
        o[i] = a[i] + b[i];
    }
}

static void gf_sub(gf o, const gf a, const gf b)
{
    for (int i = 0; i < 16; i++) {
        o[i] = a[i] - b[i];
    }
}

static void gf_mul(gf o, const gf a, const gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++) {
        t[i] = 0;
    }
    for (int i = 0; i < 16; i++) {
        for (int j = 0; j < 16; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (int i = 0; i < 15; i++) {
        t[i] += 38 * t[i + 16];
    }
    for (int i = 0; i < 16; i++) {
        o[i] = t[i];
    }
    gf_carry(o);
    gf_carry(o);
}

static void gf_sqr(gf o, const gf a)
{
    gf_mul(o, a, a);
}

static void gf_invert(gf o, const gf in)
{
    gf c;
    gf_set(c, in);
    for (int a = 253; a >= 0; a--) {
        gf_sqr(c, c);
        if (a != 2 && a != 4) {
            gf_mul(c, c, in);
        }
    }
    gf_set(o, c);
}

static void gf_pow2523(gf o, const gf in)
{
    gf c;
    gf_set(c, in);
    for (int a = 250; a >= 0; a--) {
        gf_sqr(c, c);
        if (a != 1) {
            gf_mul(c, c, in);
        }
    }
    gf_set(o, c);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Group arithmetic on the twisted Edwards curve, extended coordinates (X, Y, Z, T)
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
static void point_add(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;
    gf_sub(a, p[1], p[0]);
    gf_sub(t, q[1], q[0]);
    gf_mul(a, a, t);
    gf_add(b, p[0], p[1]);
    gf_add(t, q[0], q[1]);
    gf_mul(b, b, t);
    gf_mul(c, p[3], q[3]);
    gf_mul(c, c, CURVE_D2);
    gf_mul(d, p[2], q[2]);
    gf_add(d, d, d);
    gf_sub(e, b, a);
    gf_sub(f, d, c);
    gf_add(g, d, c);
    gf_add(h, b, a);
    gf_mul(p[0], e, f);
    gf_mul(p[1], h, g);
    gf_mul(p[2], g, f);
    gf_mul(p[3], e, h);
}

static void point_swap(gf p[4], gf q[4], int b)
{
    for (int i = 0; i < 4; i++) {
        gf_select(p[i], q[i], b);
    }
}

static void point_pack(uint8_t *r, gf p[4])
{
    gf tx, ty, zi;
    gf_invert(zi, p[2]);
    gf_mul(tx, p[0], zi);
    gf_mul(ty, p[1], zi);
    gf_pack(r, ty);
    r[31] ^= (uint8_t)(gf_parity(tx) << 7);
}

static void scalar_mult(gf p[4], gf q[4], const uint8_t *s)
{
    gf_set(p[0], GF0);
    gf_set(p[1], GF1);
    gf_set(p[2], GF1);
    gf_set(p[3], GF0);
    for (int i = 255; i >= 0; i--) {
        const int b = (s[i / 8] >> (i & 7)) & 1;
        point_swap(p, q, b);
        point_add(q, p);
        point_add(p, p);
        point_swap(p, q, b);
        if ((i & 7) == 0) {
            URTC_ED25519_YIELD();
        }
    }
}

static void scalar_base(gf p[4], const uint8_t *s)
{
    gf q[4];
    gf_set(q[0], BASE_X);
    gf_set(q[1], BASE_Y);
    gf_set(q[2], GF1);
    gf_mul(q[3], BASE_X, BASE_Y);
    scalar_mult(p, q, s);
}

// Decodes a public key into the NEGATED point -A (so the verification adds instead of subtracts). Returns non-zero for a y that is not on the curve.
static int point_unpack_neg(gf r[4], const uint8_t p[32])
{
    gf t, chk, num, den, den2, den4, den6;
    gf_set(r[2], GF1);
    gf_unpack(r[1], p);
    gf_sqr(num, r[1]);
    gf_mul(den, num, CURVE_D);
    gf_sub(num, num, r[2]);
    gf_add(den, r[2], den);
    gf_sqr(den2, den);
    gf_sqr(den4, den2);
    gf_mul(den6, den4, den2);
    gf_mul(t, den6, num);
    gf_mul(t, t, den);
    gf_pow2523(t, t);
    gf_mul(t, t, num);
    gf_mul(t, t, den);
    gf_mul(t, t, den);
    gf_mul(r[0], t, den);
    gf_sqr(chk, r[0]);
    gf_mul(chk, chk, den);
    if (gf_differs(chk, num)) {
        gf_mul(r[0], r[0], SQRT_M1);
    }
    gf_sqr(chk, r[0]);
    gf_mul(chk, chk, den);
    if (gf_differs(chk, num)) {
        return -1;
    }
    if (gf_parity(r[0]) == (p[31] >> 7)) {
        gf_sub(r[0], GF0, r[0]);
    }
    gf_mul(r[3], r[0], r[1]);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Reduction of a 64-byte hash modulo the group order
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
static void mod_l(uint8_t *r, int64_t x[64])
{
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * GROUP_ORDER[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * GROUP_ORDER[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) {
        x[j] -= carry * GROUP_ORDER[j];
    }
    for (i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void reduce64(uint8_t r[64])
{
    int64_t x[64];
    for (int i = 0; i < 64; i++) {
        x[i] = r[i];
    }
    for (int i = 0; i < 64; i++) {
        r[i] = 0;
    }
    mod_l(r, x);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------------------------------------------------------------------------------------
static int scalar_is_reduced(const uint8_t s[32])
{
    for (int i = 31; i >= 0; i--) {
        if (s[i] < GROUP_ORDER[i]) {
            return 1;
        }
        if (s[i] > GROUP_ORDER[i]) {
            return 0;
        }
    }
    return 0;   // equal to the order
}

static int y_is_reduced(const uint8_t k[32])
{
    // y (the low 255 bits) must be below 2^255 - 19: 7f ff ... ff ed is the first value that is not.
    if ((k[31] & 0x7f) != 0x7f) {
        return 1;
    }
    for (int i = 30; i >= 1; i--) {
        if (k[i] != 0xff) {
            return 1;
        }
    }
    return k[0] < 0xed;
}

int urtc_ed25519_verify(const uint8_t signature[URTC_ED25519_SIGNATURE_SIZE], const uint8_t *message, size_t message_length,
                        const uint8_t public_key[URTC_ED25519_PUBLIC_KEY_SIZE])
{
    if (signature == NULL || public_key == NULL || (message == NULL && message_length != 0)) {
        return -1;
    }
    if (!scalar_is_reduced(signature + 32)) {
        return -2;
    }
    if (!y_is_reduced(public_key)) {
        return -3;
    }
    gf p[4], q[4];
    if (point_unpack_neg(q, public_key) != 0) {
        return -4;
    }
    uint8_t h[64];
    urtc_sha512_t ctx;
    urtc_sha512_init(&ctx);
    urtc_sha512_update(&ctx, signature, 32);
    urtc_sha512_update(&ctx, public_key, 32);
    urtc_sha512_update(&ctx, message, message_length);
    urtc_sha512_final(&ctx, h);
    reduce64(h);
    scalar_mult(p, q, h);                 // h * (-A)
    scalar_base(q, signature + 32);       // S * B
    point_add(p, q);                      // S*B - h*A, which must be R
    uint8_t t[32];
    point_pack(t, p);
    return ct_differs(signature, t, 32) ? -5 : 0;
}

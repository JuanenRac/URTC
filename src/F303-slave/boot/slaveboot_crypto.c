// =============================================================================
// URTC Bootloader - SHA-256 / HMAC-SHA256
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// A from-scratch SHA-256/HMAC implementation (not a library) - verified
// against the official RFC 4231 HMAC-SHA256 test case 2 before this was
// ever wired into the update flow below.
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "slaveboot_common.h"
#include "slaveboot_crypto.h"

// Single definition matching slaveboot_common.h's extern declaration.
// Real CSPRNG-generated key (Node's crypto.randomBytes(32)),
// deliberately DIFFERENT from bootloader_crypto.c's own HMAC_KEY (master)
// - see slaveboot_common.h's own comment on this field for why the two
// must never match: a master-signed image must never verify against
// this chip's key, or vice versa. Same "committed to a public repo,
// therefore not a real confidentiality boundary as shipped" caveat as
// the master key - see bootloader_crypto.c's own comment for the full
// reasoning. The host tools' own SLAVE_HMAC_KEY (flasher_config.py,
// URTC-WEB-STUDIO's src/lib/flasher.ts) must match this exact value.
const uint8_t HMAC_KEY[32] = {
    0xF6, 0xFC, 0x5E, 0xC3, 0xC7, 0xC0, 0xB8, 0x32,
    0x5F, 0x08, 0x83, 0x85, 0xCD, 0x10, 0x9E, 0x63,
    0x7D, 0x45, 0x58, 0xD4, 0x53, 0x5F, 0x61, 0x9C,
    0xD0, 0x6F, 0x3D, 0xF2, 0xAF, 0xDC, 0x38, 0x1D
};

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} SHA256_CTX;

static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define SHA_ROTR(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define SHA_CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define SHA_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA_EP0(x) (SHA_ROTR(x,2) ^ SHA_ROTR(x,13) ^ SHA_ROTR(x,22))
#define SHA_EP1(x) (SHA_ROTR(x,6) ^ SHA_ROTR(x,11) ^ SHA_ROTR(x,25))
#define SHA_SIG0(x) (SHA_ROTR(x,7) ^ SHA_ROTR(x,18) ^ ((x) >> 3))
#define SHA_SIG1(x) (SHA_ROTR(x,17) ^ SHA_ROTR(x,19) ^ ((x) >> 10))

static void sha256_transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a,b,c,d,e,f,g,h,i,j,t1,t2,m[64];
    for (i=0,j=0; i<16; ++i,j+=4)
        m[i] = ((uint32_t)data[j]<<24)|((uint32_t)data[j+1]<<16)|((uint32_t)data[j+2]<<8)|(data[j+3]);
    for ( ; i<64; ++i)
        m[i] = SHA_SIG1(m[i-2]) + m[i-7] + SHA_SIG0(m[i-15]) + m[i-16];
    a=ctx->state[0]; b=ctx->state[1]; c=ctx->state[2]; d=ctx->state[3];
    e=ctx->state[4]; f=ctx->state[5]; g=ctx->state[6]; h=ctx->state[7];
    for (i=0; i<64; ++i) {
        t1 = h + SHA_EP1(e) + SHA_CH(e,f,g) + sha256_k[i] + m[i];
        t2 = SHA_EP0(a) + SHA_MAJ(a,b,c);
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

static void sha256_init(SHA256_CTX *ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85; ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c; ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t data[], uint32_t len) {
    for (uint32_t i=0; i<len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
        // A single call here can span the full 54KB backup slot (via
        // hmac_sha256_flash_region) - comfortably inside the IWDG's ~800ms
        // window under nominal timing, but LSI's own accuracy is
        // notoriously loose across temperature, and this costs nothing to
        // guard regardless. Every 4096 bytes keeps the refresh frequent
        // without adding meaningful overhead to the hot loop.
        if ((i & 0xFFF) == 0) {
            HAL_IWDG_Refresh(&hiwdg);
        }
    }
}

static void sha256_final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        memset(ctx->data, 0, 64);
    }
    ctx->bitlen += (uint64_t)ctx->datalen * 8;
    ctx->data[63] = (uint8_t)(ctx->bitlen);       ctx->data[62] = (uint8_t)(ctx->bitlen>>8);
    ctx->data[61] = (uint8_t)(ctx->bitlen>>16);   ctx->data[60] = (uint8_t)(ctx->bitlen>>24);
    ctx->data[59] = (uint8_t)(ctx->bitlen>>32);   ctx->data[58] = (uint8_t)(ctx->bitlen>>40);
    ctx->data[57] = (uint8_t)(ctx->bitlen>>48);   ctx->data[56] = (uint8_t)(ctx->bitlen>>56);
    sha256_transform(ctx, ctx->data);
    for (i=0; i<4; ++i) {
        hash[i]    = (uint8_t)(ctx->state[0] >> (24-i*8));
        hash[i+4]  = (uint8_t)(ctx->state[1] >> (24-i*8));
        hash[i+8]  = (uint8_t)(ctx->state[2] >> (24-i*8));
        hash[i+12] = (uint8_t)(ctx->state[3] >> (24-i*8));
        hash[i+16] = (uint8_t)(ctx->state[4] >> (24-i*8));
        hash[i+20] = (uint8_t)(ctx->state[5] >> (24-i*8));
        hash[i+24] = (uint8_t)(ctx->state[6] >> (24-i*8));
        hash[i+28] = (uint8_t)(ctx->state[7] >> (24-i*8));
    }
}

void hmac_sha256_flash_region(const uint8_t *key, uint32_t key_len,
                              uint32_t flash_addr, uint32_t len,
                              uint8_t out[32]) {
    uint8_t k_ipad[64], k_opad[64];
    uint8_t key_block[64] = {0};
    if (key_len > 64) {
        SHA256_CTX kctx;
        sha256_init(&kctx);
        sha256_update(&kctx, key, key_len);
        sha256_final(&kctx, key_block);
    } else {
        memcpy(key_block, key, key_len);
    }
    for (int i = 0; i < 64; i++) {
        k_ipad[i] = key_block[i] ^ 0x36;
        k_opad[i] = key_block[i] ^ 0x5c;
    }
    uint8_t inner_hash[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, k_ipad, 64);
    sha256_update(&ctx, (const uint8_t*)flash_addr, len);
    sha256_final(&ctx, inner_hash);

    sha256_init(&ctx);
    sha256_update(&ctx, k_opad, 64);
    sha256_update(&ctx, inner_hash, 32);
    sha256_final(&ctx, out);
}

uint8_t hmac_constant_time_compare(const uint8_t *a, const uint8_t *b, uint32_t len) {
    // XOR-accumulate rather than an early-exit compare - an early exit
    // leaks how many leading bytes matched through timing, which matters
    // for a signature check even on a small embedded target listening on a
    // shared bus. Costs nothing extra here since len is always 32.
    uint8_t diff = 0;
    for (uint32_t i = 0; i < len; i++) diff |= a[i] ^ b[i];
    return diff == 0;
}

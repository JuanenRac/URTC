// =============================================================================
// URTC Firmware - the master's verification of the expansion slave's image, and its confirmation to the slave bootloader (see firmware_slave_attest.h)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>

#include "firmware_slave_attest.h"
#include "urtc_auth.h"
#include "urtc_can_ids.h"
#include "urtc_slave_confirm.h"
#include "urtc_version_wire.h"

// ---- SHA-256 (FIPS 180-4), streaming; the application has no other ------------------------------------------------------------------------------
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    uint32_t datalen;
} AttestSha256;

static const uint32_t attest_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void attest_sha256_transform(AttestSha256 *ctx, const uint8_t *block)
{
    uint32_t w[64];
    for (uint32_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    for (uint32_t i = 16; i < 64; i++) {
        uint32_t s0 = ROTR32(w[i - 15], 7) ^ ROTR32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR32(w[i - 2], 17) ^ ROTR32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (uint32_t i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROTR32(e, 6) ^ ROTR32(e, 11) ^ ROTR32(e, 25)) + ((e & f) ^ (~e & g)) + attest_k[i] + w[i];
        uint32_t t2 = (ROTR32(a, 2) ^ ROTR32(a, 13) ^ ROTR32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void attest_sha256_init(AttestSha256 *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void attest_sha256_update(AttestSha256 *ctx, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64u) {
            attest_sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512u;
            ctx->datalen = 0;
        }
    }
}

static void attest_sha256_final(AttestSha256 *ctx, uint8_t out[32])
{
    uint32_t i = ctx->datalen;
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;
    ctx->data[i++] = 0x80;
    if (i > 56u) {
        while (i < 64u) ctx->data[i++] = 0;
        attest_sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56u) ctx->data[i++] = 0;
    for (uint32_t b = 0; b < 8u; b++) ctx->data[63u - b] = (uint8_t)(ctx->bitlen >> (8u * b));
    attest_sha256_transform(ctx, ctx->data);
    for (uint32_t s = 0; s < 8u; s++) {
        out[s * 4] = (uint8_t)(ctx->state[s] >> 24); out[s * 4 + 1] = (uint8_t)(ctx->state[s] >> 16);
        out[s * 4 + 2] = (uint8_t)(ctx->state[s] >> 8); out[s * 4 + 3] = (uint8_t)ctx->state[s];
    }
}

void SlaveAttest_Sha256(const uint8_t *data, uint32_t length, uint8_t out[32])
{
    AttestSha256 ctx;
    attest_sha256_init(&ctx);
    attest_sha256_update(&ctx, data, length);
    attest_sha256_final(&ctx, out);
}

// ---- the trusted keys -------------------------------------------------------------------------------------------------------------------------
static uint8_t trusted_keys[SLAVE_ATTEST_MAX_KEYS][URTC_ED25519_PUBLIC_KEY_SIZE];
static uint8_t trusted_count;

static uint32_t table_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; bit++) crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

// Same rules as Trust_Parse in the bootloader (boot/bootloader_trust.c): any doubt closes the table.
uint8_t SlaveAttest_SetKeysFromTable(const uint8_t *table, uint32_t available)
{
    trusted_count = 0;
    memset(trusted_keys, 0, sizeof trusted_keys);
    if (table == NULL || available < 12u) return 0;
    if (memcmp(table, "TKEY", 4) != 0 || table[4] != 1u || table[6] != 0u || table[7] != 0u || table[5] > SLAVE_ATTEST_MAX_KEYS) return 0;
    uint32_t count = table[5];
    uint32_t size = 8u + count * 44u + 4u;
    if (available < size) return 0;
    uint32_t stored = (uint32_t)table[size - 4u] | ((uint32_t)table[size - 3u] << 8) | ((uint32_t)table[size - 2u] << 16) | ((uint32_t)table[size - 1u] << 24);
    if (stored != table_crc32(table, size - 4u)) return 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *entry = table + 8u + i * 44u;
        uint8_t flags = entry[40];
        if (flags == 0u || (flags & (uint8_t)~7u) != 0u) return 0;
        if (((flags & 4u) != 0u && flags != 4u) || ((flags & 1u) != 0u && (flags & 2u) != 0u)) return 0;
        if ((entry[41] | entry[42] | entry[43]) != 0u) return 0;
        uint8_t id[URTC_AUTH_KEY_ID_SIZE];
        urtc_auth_key_id(entry + 8, id);
        if (memcmp(id, entry, URTC_AUTH_KEY_ID_SIZE) != 0) return 0;
    }
    uint8_t kept = 0;
    for (uint8_t pass = 0; pass < 2u; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *entry = table + 8u + i * 44u;
            uint8_t flags = entry[40];
            uint8_t is_current = (flags & 1u) != 0u ? 1u : 0u;
            if ((flags & 4u) != 0u || is_current == pass || kept >= SLAVE_ATTEST_MAX_KEYS) continue;
            memcpy(trusted_keys[kept], entry + 8, URTC_ED25519_PUBLIC_KEY_SIZE);
            kept++;
        }
    }
    trusted_count = kept;
    return kept;
}

void SlaveAttest_SetKeys(const uint8_t keys[][URTC_ED25519_PUBLIC_KEY_SIZE], uint8_t count)
{
    memset(trusted_keys, 0, sizeof trusted_keys);
    trusted_count = count > SLAVE_ATTEST_MAX_KEYS ? SLAVE_ATTEST_MAX_KEYS : count;
    for (uint8_t i = 0; i < trusted_count; i++) memcpy(trusted_keys[i], keys[i], URTC_ED25519_PUBLIC_KEY_SIZE);
}

uint8_t SlaveAttest_KeyCount(void)
{
    return trusted_count;
}

// ---- one update ---------------------------------------------------------------------------------------------------------------------------------
static AttestSha256 image_hash;
static uint8_t image_digest[32];
static uint8_t update_active;        // a 0x211 has been seen
static uint8_t relay_ok;             // every data piece reached the slave
static uint8_t verified;             // Verify said yes
static uint32_t declared_size;
static uint32_t declared_hw;
static uint32_t data_length;
static uint16_t confirmed_major;
static uint16_t confirmed_minor_field;
static uint8_t record[SLAVE_ATTEST_CHUNK_COUNT * SLAVE_ATTEST_CHUNK_BYTES];
static uint16_t record_chunks;

void SlaveAttest_Start(uint32_t size, uint32_t hardware_id)
{
    attest_sha256_init(&image_hash);
    memset(image_digest, 0, sizeof image_digest);
    memset(record, 0, sizeof record);
    record_chunks = 0;
    declared_size = size;
    declared_hw = hardware_id;
    data_length = 0;
    relay_ok = 1;
    verified = 0;
    update_active = 1;
}

void SlaveAttest_Data(const uint8_t *data, uint8_t length, uint8_t relayed)
{
    if (!update_active) return;
    if (!relayed) relay_ok = 0;
    // Bytes beyond the declared size are not part of the image (the slave drops them too).
    uint32_t room = declared_size > data_length ? declared_size - data_length : 0;
    uint32_t take = length < room ? length : room;
    attest_sha256_update(&image_hash, data, take);
    data_length += take;
}

void SlaveAttest_RecordChunk(uint8_t index, const uint8_t bytes[SLAVE_ATTEST_CHUNK_BYTES])
{
    if (!update_active || index >= SLAVE_ATTEST_CHUNK_COUNT) return;
    memcpy(&record[index * SLAVE_ATTEST_CHUNK_BYTES], bytes, SLAVE_ATTEST_CHUNK_BYTES);
    record_chunks = (uint16_t)(record_chunks | (1u << index));
}

// The 28 bytes of image header that the signature covers, the way the bootloader of the master rebuilds them (src/urtc_common/image_header.py is the reference).
static void build_header(uint8_t out[URTC_AUTH_HEADER_BYTES], uint16_t major, uint16_t minor_field, uint32_t crc)
{
    uint8_t patch = urtc_minor_field_patch(minor_field);
    memset(out, 0, URTC_AUTH_HEADER_BYTES);
    out[0] = 'U'; out[1] = 'R'; out[2] = 'I'; out[3] = 'H';
    out[4] = 1;
    out[5] = 32;
    out[8] = (uint8_t)(declared_hw >> 24); out[9] = (uint8_t)(declared_hw >> 16); out[10] = (uint8_t)(declared_hw >> 8); out[11] = (uint8_t)declared_hw;
    out[12] = (uint8_t)(major >> 8); out[13] = (uint8_t)major;
    out[14] = urtc_minor_field_minor(minor_field);
    out[15] = (uint8_t)((patch == URTC_PATCH_UNKNOWN) ? 0u : patch);
    out[20] = (uint8_t)(declared_size >> 24); out[21] = (uint8_t)(declared_size >> 16); out[22] = (uint8_t)(declared_size >> 8); out[23] = (uint8_t)declared_size;
    out[24] = (uint8_t)(crc >> 24); out[25] = (uint8_t)(crc >> 16); out[26] = (uint8_t)(crc >> 8); out[27] = (uint8_t)crc;
}

uint8_t SlaveAttest_Verify(uint32_t crc, uint16_t version_major, uint16_t version_minor_field)
{
    verified = 0;
    if (!update_active) return 0;
    update_active = 0;                       // one verdict per update: a second END cannot reuse the stream
    if (!relay_ok || data_length != declared_size || declared_size == 0u) return 0;
    if (record_chunks != 0x7FFu || trusted_count == 0u) return 0;
    if (declared_hw != (uint32_t)URTC_HARDWARE_ID_SLAVE) return 0;
    attest_sha256_final(&image_hash, image_digest);

    uint8_t header28[URTC_AUTH_HEADER_BYTES];
    build_header(header28, version_major, version_minor_field, crc);
    urtc_auth_context_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.policy = URTC_AUTH_ED25519_ONLY;
    ctx.keys = (const uint8_t (*)[URTC_ED25519_PUBLIC_KEY_SIZE])trusted_keys;
    ctx.key_count = trusted_count;
    ctx.hardware_id = (uint32_t)URTC_HARDWARE_ID_SLAVE;
    ctx.installed_counter = 0;               // the slave keeps no counter: see docs/SLAVE_BOOT_CONFIRMATION.md
    ctx.installed_version_known = 0;         // the slave applies its own version rule
    urtc_auth_image_t candidate;
    candidate.header28 = header28;
    candidate.payload_sha256 = image_digest;
    candidate.record = record;
    candidate.hmac_ok = 0;
    uint32_t new_counter = 0;
    if (urtc_auth_decide(&ctx, &candidate, &new_counter) != URTC_AUTH_OK) return 0;
    confirmed_major = version_major;
    confirmed_minor_field = version_minor_field;
    verified = 1;
    return 1;
}

uint8_t SlaveAttest_Tag(const uint8_t nonce[16], uint8_t tag[32])
{
    if (!verified) return 0;
    uint8_t message[URTC_SLAVE_CONFIRM_MESSAGE_SIZE];
    urtc_slave_confirm_message(message, nonce, image_digest, declared_hw, confirmed_major, confirmed_minor_field, declared_size);
    SlaveAttest_Sha256(message, sizeof message, tag);
    return 1;
}

// =============================================================================
// URTC Bootloader - the trust table (see bootloader_trust.h)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include <string.h>

#include "bootloader_trust.h"
#include "urtc_auth.h"

#ifdef URTC_TRUST_DEV
#include "urtc_trust_dev.h"
#else
#include "urtc_trust_release.h"
#endif

// The linker script puts this section at a fixed offset of the binary (STM32F303CCTx_BOOTLOADER.ld); `used` keeps it when unused sections are dropped.
__attribute__((section(".trust_keys"), used)) const uint8_t URTC_TRUST_TABLE[URTC_TRUST_TABLE_SIZE] = { URTC_TRUST_TABLE_BYTES };

static TrustKeys_t active_keys;
static uint8_t active_loaded;

static uint32_t trust_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8u; bit++) {
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t trust_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// One entry is acceptable when its flags are a legal state, its padding is zero and its key id is the hash of its key.
static uint8_t trust_entry_ok(const uint8_t *entry)
{
    uint8_t flags = entry[40];
    if (flags == 0u || (flags & (uint8_t)~(TRUST_FLAG_CURRENT | TRUST_FLAG_NEXT | TRUST_FLAG_REVOKED)) != 0u) {
        return 0;
    }
    if (((flags & TRUST_FLAG_REVOKED) != 0u && flags != TRUST_FLAG_REVOKED) || ((flags & TRUST_FLAG_CURRENT) != 0u && (flags & TRUST_FLAG_NEXT) != 0u)) {
        return 0;
    }
    if ((entry[41] | entry[42] | entry[43]) != 0u) {
        return 0;
    }
    uint8_t id[URTC_AUTH_KEY_ID_SIZE];
    urtc_auth_key_id(entry + 8, id);
    return memcmp(id, entry, TRUST_KEY_ID_SIZE) == 0 ? 1u : 0u;
}

uint8_t Trust_Parse(const uint8_t *table, uint32_t available, TrustKeys_t *out)
{
    memset(out, 0, sizeof *out);
    if (table == NULL || available < 12u) {
        return 0;
    }
    if (memcmp(table, "TKEY", 4) != 0 || table[4] != 1u || table[6] != 0u || table[7] != 0u || table[5] > TRUST_MAX_KEYS) {
        return 0;
    }
    uint32_t count = table[5];
    uint32_t size = 8u + count * TRUST_KEY_ENTRY_SIZE + 4u;
    if (available < size || trust_le32(table + size - 4u) != trust_crc32(table, size - 4u)) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        if (!trust_entry_ok(table + 8u + i * TRUST_KEY_ENTRY_SIZE)) {
            return 0;
        }
    }
    // the current key first, then the next one; a revoked key is skipped
    uint8_t kept = 0;
    for (uint8_t pass = 0; pass < 2u; pass++) {
        for (uint32_t i = 0; i < count; i++) {
            const uint8_t *entry = table + 8u + i * TRUST_KEY_ENTRY_SIZE;
            uint8_t flags = entry[40];
            uint8_t is_current = (flags & TRUST_FLAG_CURRENT) != 0u ? 1u : 0u;
            if ((flags & TRUST_FLAG_REVOKED) != 0u || is_current == pass || kept >= TRUST_MAX_KEYS) {
                continue;
            }
            memcpy(out->key_id[kept], entry, TRUST_KEY_ID_SIZE);
            memcpy(out->keys[kept], entry + 8, URTC_ED25519_PUBLIC_KEY_SIZE);
            out->flags[kept] = flags;
            kept++;
        }
    }
    out->count = kept;
    return kept;
}

const TrustKeys_t *Trust_Active(void)
{
    if (!active_loaded) {
        (void)Trust_Parse(URTC_TRUST_TABLE, URTC_TRUST_TABLE_SIZE, &active_keys);
        active_loaded = 1;
    }
    return &active_keys;
}

uint32_t Trust_MinCounter(void)
{
    return (uint32_t)URTC_TRUST_MIN_COUNTER;
}

#ifdef URTC_HOST_TEST
void Trust_UseTableForTest(const uint8_t *table, uint32_t length)
{
    if (table == NULL) {
        active_loaded = 0;
        return;
    }
    (void)Trust_Parse(table, length, &active_keys);
    active_loaded = 1;
}
#endif

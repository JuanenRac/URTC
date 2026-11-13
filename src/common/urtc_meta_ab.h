// =============================================================================
// URTC - metadata in two alternating flash pages (A/B)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Plain C99 with no STM32 HAL dependency: the flash is reached through three functions, so the same code runs in the bootloader and against the model of
// the flash in tests/test_meta_ab.c, which cuts the power at every step of a write.
//
// Each page holds ONE record: magic, generation, payload and the CRC-32 of the generation and the payload. A write goes to the page that does NOT hold the
// newest valid record: that page is erased, the generation, payload and CRC are programmed and the magic goes last, so a record that was cut has no magic or
// a wrong CRC and is ignored. The page with the newest record is never touched while the new one is written, so at every instant at least one valid
// record exists: a power loss costs the write in progress, never the state that was there.
//
// This is the layout behind URTC_BL_META_AB (off by default: it needs a second page, URTC_MASTER_METADATA_B_ADDR).
#ifndef URTC_META_AB_H
#define URTC_META_AB_H

#include <stdint.h>
#include <string.h>

#define URTC_META_AB_MAGIC       0x4D414231UL // "MAB1"
#define URTC_META_AB_MAX_PAYLOAD 128u
#define URTC_META_AB_HEADER      8u           // magic and generation
#define URTC_META_AB_BLANK       0xFFFFFFFFUL

typedef struct {
    const uint8_t *(*page_ptr)(void *ctx, uint8_t page);                                                    // where the page is read from
    uint8_t (*erase)(void *ctx, uint8_t page);                                                              // 1 on success
    uint8_t (*program)(void *ctx, uint8_t page, uint32_t offset, const uint8_t *data, uint32_t len);       // 1 on success
    void *ctx;
} UrtcMetaAbOps;

static inline uint32_t urtc_meta_ab_crc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320UL & (0u - (crc & 1u)));
    }
    return ~crc;
}

static inline uint32_t urtc_meta_ab_load32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t urtc_meta_ab_record_size(uint32_t payload_len)
{
    return URTC_META_AB_HEADER + payload_len + 4u;
}

// 1 when the page holds a complete record; its generation is returned in *generation.
static inline uint8_t urtc_meta_ab_page_valid(const uint8_t *page, uint32_t payload_len, uint32_t *generation)
{
    if (payload_len == 0 || payload_len > URTC_META_AB_MAX_PAYLOAD) return 0;
    if (urtc_meta_ab_load32(page) != URTC_META_AB_MAGIC) return 0;
    const uint32_t generation_read = urtc_meta_ab_load32(page + 4);
    if (generation_read == URTC_META_AB_BLANK || generation_read == 0) return 0;
    const uint32_t body = 4u + payload_len; // generation and payload
    if (urtc_meta_ab_crc(page + 4, body) != urtc_meta_ab_load32(page + URTC_META_AB_HEADER + payload_len)) return 0;
    *generation = generation_read;
    return 1;
}

// The page of the newest valid record (the generation compares with wrap-around), or -1.
static inline int urtc_meta_ab_newest(const UrtcMetaAbOps *ops, uint32_t payload_len, uint32_t *generation)
{
    int best = -1;
    uint32_t best_generation = 0;
    for (uint8_t page = 0; page < 2; page++) {
        uint32_t g;
        if (!urtc_meta_ab_page_valid(ops->page_ptr(ops->ctx, page), payload_len, &g)) continue;
        if (best < 0 || (int32_t)(g - best_generation) > 0) {
            best = page;
            best_generation = g;
        }
    }
    if (best >= 0 && generation) *generation = best_generation;
    return best;
}

// Copies the newest payload to out; 0 when no page holds a valid record.
static inline uint8_t urtc_meta_ab_read(const UrtcMetaAbOps *ops, uint32_t payload_len, uint8_t *out)
{
    uint32_t generation;
    const int page = urtc_meta_ab_newest(ops, payload_len, &generation);
    if (page < 0) return 0;
    memcpy(out, ops->page_ptr(ops->ctx, (uint8_t)page) + URTC_META_AB_HEADER, payload_len);
    return 1;
}

// Writes the payload as a new record in the page that does not hold the newest one. 1 when the new record reads back valid; 0 on a flash failure (the
// previous record is still in force). A payload equal to the newest one is not written again.
static inline uint8_t urtc_meta_ab_write(const UrtcMetaAbOps *ops, uint32_t payload_len, const uint8_t *payload)
{
    if (payload_len == 0 || payload_len > URTC_META_AB_MAX_PAYLOAD) return 0;
    uint32_t generation = 0;
    const int newest = urtc_meta_ab_newest(ops, payload_len, &generation);
    if (newest >= 0 && memcmp(ops->page_ptr(ops->ctx, (uint8_t)newest) + URTC_META_AB_HEADER, payload, payload_len) == 0) return 1;
    const uint8_t target = (newest == 0) ? 1u : 0u;
    uint32_t next = generation + 1u;
    if (next == 0 || next == URTC_META_AB_BLANK) next = 1; // the generation wraps over the two values that mean no record

    uint8_t body[4 + URTC_META_AB_MAX_PAYLOAD + 4];
    body[0] = (uint8_t)next; body[1] = (uint8_t)(next >> 8); body[2] = (uint8_t)(next >> 16); body[3] = (uint8_t)(next >> 24);
    memcpy(body + 4, payload, payload_len);
    const uint32_t crc = urtc_meta_ab_crc(body, 4u + payload_len);
    body[4 + payload_len] = (uint8_t)crc; body[5 + payload_len] = (uint8_t)(crc >> 8); body[6 + payload_len] = (uint8_t)(crc >> 16); body[7 + payload_len] = (uint8_t)(crc >> 24);

    if (!ops->erase(ops->ctx, target)) return 0;
    if (!ops->program(ops->ctx, target, 4u, body, 8u + payload_len)) return 0; // generation, payload and CRC
    const uint8_t magic[4] = { (uint8_t)URTC_META_AB_MAGIC, (uint8_t)(URTC_META_AB_MAGIC >> 8), (uint8_t)(URTC_META_AB_MAGIC >> 16), (uint8_t)(URTC_META_AB_MAGIC >> 24) };
    if (!ops->program(ops->ctx, target, 0u, magic, 4u)) return 0;               // the magic goes last
    uint32_t check;
    return urtc_meta_ab_page_valid(ops->page_ptr(ops->ctx, target), payload_len, &check) && check == next;
}

#endif // URTC_META_AB_H

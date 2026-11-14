// =============================================================================
// URTC - move from the metadata of one page to the two alternating pages without losing anything
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Plain C99 with no STM32 HAL dependency, like urtc_meta_ab.h, so the bootloader and the host tests share it.
//
// A board that already runs a bootloader of the single-page layout keeps its state in page 0: an append-only log of 28 records of 72 bytes (the generation, the
// 64 bytes of the metadata with the rollback counter, a CRC-32) followed by the two slots of the programmed serial number (16 bytes each). A bootloader built with
// URTC_BL_META_AB reads that state when no A/B record exists yet, so the serial and the rollback counter of an installed board are never lost, and the first
// A/B record goes to page 1: page 0 keeps the old log, untouched, until page 1 holds a complete record. A power cut at any step of that first write leaves the
// old log readable; the write after it erases page 0 only because page 1 is already valid.
#ifndef URTC_META_MIGRATE_H
#define URTC_META_MIGRATE_H

#include <stdint.h>
#include <string.h>

#include "urtc_bl_serial.h"
#include "urtc_meta_ab.h"

#define URTC_META_LEGACY_RECORD_SIZE 72u
#define URTC_META_LEGACY_SLOTS       28u
#define URTC_META_LEGACY_MAGIC       0x55415041UL // the magic of the metadata ("APAU"), at the start of the 64 bytes
#define URTC_META_STATE_BYTES        80u          // the 64 bytes of the metadata and the four words of the serial record
#define URTC_META_LEGACY_META_BYTES  64u
#define URTC_META_LEGACY_SERIAL_A    (URTC_META_LEGACY_SLOTS * URTC_META_LEGACY_RECORD_SIZE)
#define URTC_META_LEGACY_SERIAL_B    (URTC_META_LEGACY_SERIAL_A + URTC_SERIAL_SLOT_BYTES)

static inline void urtc_meta_state_blank(uint8_t state[URTC_META_STATE_BYTES])
{
    memset(state, 0, URTC_META_LEGACY_META_BYTES);
    memset(state + URTC_META_LEGACY_META_BYTES, 0xFF, URTC_SERIAL_SLOT_BYTES); // no serial yet: the blank slot
}

static inline void urtc_meta_state_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// 1 when the page holds the state of the single-page layout: the metadata of its newest valid record and the newest valid serial record. The state is laid out as
// the A/B payload (metadata, then the serial words). Nothing is written to the page.
static inline uint8_t urtc_meta_legacy_state(const uint8_t *page, uint8_t state[URTC_META_STATE_BYTES])
{
    urtc_meta_state_blank(state);
    uint8_t found = 0;
    int best = -1;
    uint32_t best_generation = 0;
    for (uint32_t i = 0; i < URTC_META_LEGACY_SLOTS; i++) {
        const uint8_t *record = page + i * URTC_META_LEGACY_RECORD_SIZE;
        const uint32_t generation = urtc_meta_ab_load32(record);
        if (urtc_meta_ab_load32(record + 4) != URTC_META_LEGACY_MAGIC || generation == 0xFFFFFFFFUL) continue;
        if (urtc_meta_ab_crc(record, URTC_META_LEGACY_RECORD_SIZE - 4u) != urtc_meta_ab_load32(record + URTC_META_LEGACY_RECORD_SIZE - 4u)) continue;
        if (best < 0 || generation > best_generation) {
            best = (int)i;
            best_generation = generation;
        }
    }
    if (best >= 0) {
        memcpy(state, page + (uint32_t)best * URTC_META_LEGACY_RECORD_SIZE + 4u, URTC_META_LEGACY_META_BYTES);
        found = 1;
    }
    uint32_t a[4], b[4], serial = 0, counter = 0;
    for (uint32_t w = 0; w < 4; w++) {
        a[w] = urtc_meta_ab_load32(page + URTC_META_LEGACY_SERIAL_A + 4u * w);
        b[w] = urtc_meta_ab_load32(page + URTC_META_LEGACY_SERIAL_B + 4u * w);
    }
    if (urtc_serial_pick(a, b, &serial, &counter)) {
        uint32_t words[4];
        urtc_serial_slot_pack(serial, counter, words);
        for (uint32_t w = 0; w < 4; w++) urtc_meta_state_put32(state + URTC_META_LEGACY_META_BYTES + 4u * w, words[w]);
        found = 1;
    }
    return found;
}

// The state in force: the newest A/B record; when there is none, the state of the single-page layout in page 0 (*from_legacy is 1); when there is none either,
// the blank state and 0. `state` is always filled.
static inline uint8_t urtc_meta_ab_load_state(const UrtcMetaAbOps *ops, uint8_t state[URTC_META_STATE_BYTES], uint8_t *from_legacy)
{
    if (from_legacy) *from_legacy = 0;
    if (urtc_meta_ab_read(ops, URTC_META_STATE_BYTES, state)) return 1;
    if (urtc_meta_legacy_state(ops->page_ptr(ops->ctx, 0u), state)) {
        if (from_legacy) *from_legacy = 1;
        return 1;
    }
    urtc_meta_state_blank(state);
    return 0;
}

// Stores the state as a new A/B record. While no A/B record exists and page 0 still holds a single-page state, the record goes to page 1, so the old log is
// intact until the new record is complete.
static inline uint8_t urtc_meta_ab_store_state(const UrtcMetaAbOps *ops, const uint8_t state[URTC_META_STATE_BYTES])
{
    uint8_t scratch[URTC_META_STATE_BYTES];
    uint32_t generation;
    const int newest = urtc_meta_ab_newest(ops, URTC_META_STATE_BYTES, &generation);
    const uint8_t first_page = (newest < 0 && urtc_meta_legacy_state(ops->page_ptr(ops->ctx, 0u), scratch)) ? 1u : 0u;
    return urtc_meta_ab_write_first(ops, URTC_META_STATE_BYTES, state, first_page);
}

#endif // URTC_META_MIGRATE_H

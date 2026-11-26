// =============================================================================
// URTC - where the master keeps the version rule: an annex of the metadata pages A/B
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Used only by a build with URTC_BL_VERSION_COMPAT_COMMAND (the bootloader, which writes it) or URTC_APP_VERSION_COMPAT_RELAY (the application, which only
// reads it). Both are off by default.
//
// The layout of the pages does not change: the record of urtc_meta_ab.h (magic, generation, state, CRC-32) is the one every bootloader reads. The 16 bytes of
// urtc_version_compat.h sit further into the same page, at URTC_VC_ANNEX_OFFSET. A bootloader that does not know the annex reads and writes the record as before;
// when it rewrites a page the annex is gone, which reads as "nothing stored", that is the strict rule of the build.
//
// The annex of a page is programmed before the magic of that page, so a page that has its magic has its annex whole: a power cut leaves the old record with the
// old setting or the new record with the new one.
#ifndef URTC_VC_ANNEX_H
#define URTC_VC_ANNEX_H

#include <stdint.h>

#include "urtc_meta_ab.h"
#include "urtc_version_compat.h"

#define URTC_VC_ANNEX_PAYLOAD 80u    // URTC_META_STATE_BYTES: the metadata and the serial words
#define URTC_VC_ANNEX_OFFSET  128u   // past the record (8 + 80 + 4 bytes), on a half-word boundary

// The mode and counter of the newest valid record of the pages; nothing stored (both 0) when there is no record or no usable annex. Reads only.
static inline void urtc_vc_annex_load(const UrtcMetaAbOps *ops, uint32_t *mode, uint32_t *counter)
{
    uint32_t generation;
    const int page = urtc_meta_ab_newest(ops, URTC_VC_ANNEX_PAYLOAD, &generation);
    *mode = URTC_VC_MODE_NONE;
    *counter = 0u;
    if (page >= 0) {
        (void)urtc_vc_record_read(ops->page_ptr(ops->ctx, (uint8_t)page) + URTC_VC_ANNEX_OFFSET, mode, counter);
    }
}

// Writes a new record even when the state is the one in force (urtc_meta_ab_write skips that case), which is how a change of the annex alone is stored. The
// order is the one of urtc_meta_ab_write_first: the page without the newest record is erased, the body is programmed and the magic goes last. `first_page` is
// used only while there is no record yet.
static inline uint8_t urtc_vc_annex_commit(const UrtcMetaAbOps *ops, const uint8_t *payload, uint8_t first_page)
{
    uint32_t generation = 0;
    const int newest = urtc_meta_ab_newest(ops, URTC_VC_ANNEX_PAYLOAD, &generation);
    const uint8_t target = (newest < 0) ? (uint8_t)(first_page ? 1u : 0u) : (newest == 0 ? 1u : 0u);
    uint32_t next = generation + 1u;
    if (next == 0u || next == URTC_META_AB_BLANK) next = 1u;

    uint8_t body[4u + URTC_VC_ANNEX_PAYLOAD + 4u];
    urtc_vc_put_le32(body, next);
    memcpy(body + 4, payload, URTC_VC_ANNEX_PAYLOAD);
    urtc_vc_put_le32(body + 4u + URTC_VC_ANNEX_PAYLOAD, urtc_meta_ab_crc(body, 4u + URTC_VC_ANNEX_PAYLOAD));

    if (!ops->erase(ops->ctx, target)) return 0;
    if (!ops->program(ops->ctx, target, 4u, body, (uint32_t)sizeof body)) return 0;
    uint8_t magic[4];
    urtc_vc_put_le32(magic, URTC_META_AB_MAGIC);
    if (!ops->program(ops->ctx, target, 0u, magic, 4u)) return 0;
    uint32_t check;
    return urtc_meta_ab_page_valid(ops->page_ptr(ops->ctx, target), URTC_VC_ANNEX_PAYLOAD, &check) && check == next;
}

#endif // URTC_VC_ANNEX_H

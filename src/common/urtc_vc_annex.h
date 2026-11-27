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

#endif // URTC_VC_ANNEX_H

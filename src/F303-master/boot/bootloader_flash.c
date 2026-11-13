// =============================================================================
// URTC Bootloader - CRC32, flash program/erase, and metadata persistence
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "bootloader_common.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "urtc_bl_serial.h"

// URTC_BL_META_AB=1 keeps the metadata (and the programmed serial) in two alternating pages instead of the append-only log of one page (src/common/urtc_meta_ab.h).
// Off by default: it needs a second 2 KB page, URTC_MASTER_METADATA_B_ADDR, that the layout of the bootloader does not give today.
#ifndef URTC_BL_META_AB
#define URTC_BL_META_AB 0
#endif
#if URTC_BL_META_AB
#include "urtc_meta_ab.h"
#ifndef URTC_MASTER_METADATA_B_ADDR
#error "URTC_BL_META_AB=1 needs URTC_MASTER_METADATA_B_ADDR: the address of a free 2 KB page (check that the bootloader image ends before it)"
#endif
#endif

static uint32_t crc32_table[256];
static uint8_t crc32_table_built = 0;

// Software CRC32 (standard polynomial 0xEDB88320, reflected, final XOR
// 0xFFFFFFFF) - the same variant used by zlib/PNG/Ethernet and available as
// a one-line call in most languages (Python's zlib.crc32, for instance),
// deliberately NOT using this chip's hardware CRC unit, which implements a
// different variant (no reflection, no final XOR) by default - using the
// hardware unit here would mean whatever tool ends up generating update
// images needs to replicate that specific variant exactly, where a
// standard library call would otherwise just work.
static void CRC32_BuildTable(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (uint8_t k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_built = 1;
}

uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len) {
    if (!crc32_table_built) CRC32_BuildTable();
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

uint32_t CRC32_Finalize(uint32_t crc) {
    return crc ^ 0xFFFFFFFFUL;
}

// -----------------------------------------------------------------------
// Flash routines
// -----------------------------------------------------------------------
uint8_t Flash_ErasePages(uint32_t start_addr, uint32_t num_pages) {
    // Erase one page at a time with an IWDG refresh between each, rather
    // than one HAL_FLASHEx_Erase call for every page at once - that single
    // call loops internally with nothing able to refresh the watchdog
    // until it fully returns, and for 112K (56 pages), the whole erase
    // could plausibly run long enough on its own to trip the IWDG
    // inherited from the application.
    for (uint32_t i = 0; i < num_pages; i++) {
        FLASH_EraseInitTypeDef eraseInit;
        uint32_t pageError;
        eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
        eraseInit.PageAddress = start_addr + (i * FLASH_PAGE_SIZE);
        eraseInit.NbPages = 1;

        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
        HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
        HAL_FLASH_Lock();
        if (res != HAL_OK || pageError != 0xFFFFFFFF) return 0;
        HAL_IWDG_Refresh(&hiwdg);
    }
    return 1;
}

// Writes len bytes (rounded up to a half-word) starting at addr, then reads
// every half-word back and compares it against the source buffer before
// reporting success. Flash cells that write but don't actually hold their
// value ("stuck" from wear or degradation) are a real, if uncommon, failure
// mode - this catches it at write time instead of finding out only when
// the CRC/SHA-256 check (or worse, execution) fails later.
uint8_t Flash_WriteVerified(uint32_t addr, const uint8_t *buf, uint32_t len) {
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t half_word = buf[i];
        if (i + 1 < len) half_word |= (buf[i+1] << 8);
        else half_word |= (0xFF << 8); // pad the odd trailing byte with the erased-flash value
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, half_word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
        // Not needed under typical timing (a full page's programming and
        // verification loops together are well within the ~800ms IWDG
        // window that Flash_CopyRegion's own once-per-page refresh
        // already covers), but cheap insurance against clock scaling,
        // flash wait-state, or bus-contention delays stretching a single
        // page's processing time unexpectedly.
        if ((i & 0xFF) == 0) HAL_IWDG_Refresh(&hiwdg);
    }
    HAL_FLASH_Lock();
    __DSB(); // ensures the last flash write is fully complete before the read-back below trusts it

    // Read-back verification: compare what's actually in flash now against
    // what was meant to go there.
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t expected = buf[i];
        if (i + 1 < len) expected |= (buf[i+1] << 8);
        else expected |= (0xFF << 8);
        uint16_t actual = *(volatile uint16_t*)(addr + i);
        if (actual != expected) return 0;
        if ((i & 0xFF) == 0) HAL_IWDG_Refresh(&hiwdg);
    }
    return 1;
}

// Copies one flash region into another, page by page: erase destination
// page, write+verify from source, refresh IWDG, repeat. Used for the
// backup-slot -> main-slot copy once backup has fully passed verification.
// Safe to call again from scratch after an interruption - source is never
// modified by this function, only destination.
uint8_t Flash_CopyRegion(uint32_t dest_addr, uint32_t src_addr, uint32_t total_len) {
    uint32_t num_pages = (total_len + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE;
    for (uint32_t p = 0; p < num_pages; p++) {
        uint32_t page_dest = dest_addr + (p * FLASH_PAGE_SIZE);
        uint32_t page_src  = src_addr  + (p * FLASH_PAGE_SIZE);
        uint32_t this_page_len = FLASH_PAGE_SIZE;
        if ((p + 1) * FLASH_PAGE_SIZE > total_len) {
            this_page_len = total_len - (p * FLASH_PAGE_SIZE);
        }

        FLASH_EraseInitTypeDef eraseInit;
        uint32_t pageError;
        eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
        eraseInit.PageAddress = page_dest;
        eraseInit.NbPages = 1;
        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
        HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
        HAL_FLASH_Lock();
        if (res != HAL_OK || pageError != 0xFFFFFFFF) return 0;

        if (!Flash_WriteVerified(page_dest, (const uint8_t*)page_src, this_page_len)) return 0;
        HAL_IWDG_Refresh(&hiwdg);

        // The main loop is blocked inside this whole function - without
        // this, no heartbeat could fire and the OLED bar would sit frozen
        // for however long the full copy takes (several seconds for a
        // large image), even though this is the phase most worth showing
        // progress for.
        uint8_t pct = (uint8_t)(((uint64_t)(p + 1) * 100) / num_pages);
        OLED_DrawProgressBar_Boot(pct);
        CAN_SendHeartbeat(STATUS_COPYING, pct);
    }
    return 1;
}

// ---- metadata: an append-only log (see FirmwareMetadata_t in bootloader_common.h) ------------------------------------------------------------
_Static_assert(sizeof(MetaRecord_t) == META_RECORD_SIZE, "a metadata record is 72 bytes");

static const MetaRecord_t *meta_slot(uint32_t index) {
    return (const MetaRecord_t*)(METADATA_ADDR + index * META_RECORD_SIZE);
}

static uint32_t meta_record_crc(const MetaRecord_t *record) {
    return CRC32_Finalize(CRC32_Update(0xFFFFFFFFUL, (const uint8_t*)record, META_RECORD_SIZE - 4u));
}

static uint8_t meta_slot_blank(const MetaRecord_t *record) {
    const uint8_t *bytes = (const uint8_t*)record;
    for (uint32_t i = 0; i < META_RECORD_SIZE; i++) {
        if (bytes[i] != 0xFF) return 0;
    }
    return 1;
}

// The slot of the valid record with the highest generation, or -1. A record cut by a power loss fails its CRC and is not a candidate.
static int32_t meta_find_newest(void) {
    int32_t best = -1;
    uint32_t best_generation = 0;
    for (uint32_t i = 0; i < META_RECORD_SLOTS; i++) {
        const MetaRecord_t *record = meta_slot(i);
        if (record->meta.magic != METADATA_MAGIC_VALID || record->generation == 0xFFFFFFFFUL) continue;
        if (meta_record_crc(record) != record->crc) continue;
        if (best < 0 || record->generation > best_generation) {
            best = (int32_t)i;
            best_generation = record->generation;
        }
    }
    return best;
}

#if !URTC_BL_META_AB
uint8_t Metadata_Read(FirmwareMetadata_t *out) {
    int32_t newest = meta_find_newest();
    if (newest < 0) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    memcpy(out, &meta_slot((uint32_t)newest)->meta, sizeof(*out));
    return 1;
}
#endif

// ---- the programmed serial number: two slots after the 28 records of the log ----------------------------------------------------------------------
#define SERIAL_SLOT_A (METADATA_ADDR + META_RECORD_SLOTS * META_RECORD_SIZE)
#define SERIAL_SLOT_B (SERIAL_SLOT_A + URTC_SERIAL_SLOT_BYTES)
_Static_assert(META_RECORD_SLOTS * META_RECORD_SIZE + URTC_SERIAL_SLOT_COUNT * URTC_SERIAL_SLOT_BYTES <= 2048u, "the two serial slots fit after the log");

static void serial_slot_words(uint32_t address, uint32_t words[4]) {
    memcpy(words, (const void*)(uintptr_t)address, URTC_SERIAL_SLOT_BYTES);
}

static uint8_t metadata_page_erase_at(uint32_t address) {
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = address;
    eraseInit.NbPages = 1;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
    HAL_FLASH_Lock();
    return (res == HAL_OK && pageError == 0xFFFFFFFF) ? 1u : 0u;
}

static uint8_t metadata_page_erase(void) {
    return metadata_page_erase_at(METADATA_ADDR);
}

// Programs a blank slot: the serial, its complement and the counter first, the magic last, so a write that is cut leaves no record at all.
static uint8_t serial_slot_program(uint32_t address, uint32_t serial, uint32_t counter) {
    uint32_t words[4];
    urtc_serial_slot_pack(serial, counter, words);
    if (!Flash_WriteVerified(address + 4u, (const uint8_t*)&words[1], 12u)) return 0;
    return Flash_WriteVerified(address, (const uint8_t*)&words[0], 4u);
}

#if !URTC_BL_META_AB
uint8_t Serial_ReadNewest(uint32_t *serial, uint32_t *counter) {
    uint32_t a[4], b[4];
    serial_slot_words(SERIAL_SLOT_A, a);
    serial_slot_words(SERIAL_SLOT_B, b);
    return urtc_serial_pick(a, b, serial, counter) ? 1u : 0u;
}
#endif

#if !URTC_BL_META_AB
uint8_t Serial_Store(uint32_t serial, uint32_t counter) {
    uint32_t a[4], b[4];
    serial_slot_words(SERIAL_SLOT_A, a);
    serial_slot_words(SERIAL_SLOT_B, b);
    if (urtc_serial_slot_blank(a)) return serial_slot_program(SERIAL_SLOT_A, serial, counter);
    if (urtc_serial_slot_blank(b)) return serial_slot_program(SERIAL_SLOT_B, serial, counter);
    // both slots are used: recycle the page. The new record is written first, then the newest metadata state again (a cut in between costs the
    // metadata state, which the bootloader rebuilds as a first start, never the serial).
    FirmwareMetadata_t meta;
    const uint8_t have_meta = Metadata_Read(&meta);
    if (!metadata_page_erase()) return 0;
    if (!serial_slot_program(SERIAL_SLOT_A, serial, counter)) return 0;
    return have_meta ? Metadata_EraseAndWrite(&meta) : 1u;
}
#endif

// Appends the state as a new record; nothing is erased unless the page is full (then it is erased and the new record is the first of a fresh log).
// The name is kept from the single-record page this replaced.
#if !URTC_BL_META_AB
uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta) {
    uint32_t generation = 1;
    int32_t newest = meta_find_newest();
    if (newest >= 0) {
        const MetaRecord_t *latest = meta_slot((uint32_t)newest);
        // Skip the write entirely if the log already ends with exactly this state - the page has the same endurance as any other.
        if (memcmp(&latest->meta, meta, sizeof(*meta)) == 0) {
            return 1;
        }
        generation = latest->generation + 1u;
    }
    uint32_t slot = META_RECORD_SLOTS;
    for (uint32_t i = 0; i < META_RECORD_SLOTS; i++) {
        if (meta_slot_blank(meta_slot(i))) {
            slot = i;
            break;
        }
    }
    if (slot == META_RECORD_SLOTS) {
        // the programmed serial is carried over the erase: read before, written first after (a cut in between leaves the fold of the unique ID, never a wrong serial)
        uint32_t serial = 0, counter = 0;
        const uint8_t have_serial = Serial_ReadNewest(&serial, &counter);
        if (!metadata_page_erase()) return 0;
        if (have_serial && !serial_slot_program(SERIAL_SLOT_A, serial, counter)) return 0;
        slot = 0;
    }
    MetaRecord_t record;
    memset(&record, 0, sizeof(record));
    record.generation = generation;
    record.meta = *meta;
    record.crc = meta_record_crc(&record);
    return Flash_WriteVerified((uint32_t)(METADATA_ADDR + slot * META_RECORD_SIZE), (const uint8_t*)&record, META_RECORD_SIZE);
}
#endif

#if URTC_BL_META_AB
// ---- metadata in two alternating pages (src/common/urtc_meta_ab.h) ------------------------------------------------------------------------------
// One record holds the metadata and the programmed serial (the four words of a serial slot), so erasing the page that is rewritten never loses the serial.
typedef struct {
    FirmwareMetadata_t meta;
    uint32_t serial_words[4];
} MetaAbState_t;
_Static_assert(sizeof(MetaAbState_t) % 2u == 0u, "the record is programmed by half-words");
_Static_assert(sizeof(MetaAbState_t) <= URTC_META_AB_MAX_PAYLOAD, "the state fits the record");

static const uint8_t *meta_ab_page_ptr(void *ctx, uint8_t page) {
    (void)ctx;
    return (const uint8_t*)(uintptr_t)(page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR);
}

static uint8_t meta_ab_erase(void *ctx, uint8_t page) {
    (void)ctx;
    return metadata_page_erase_at(page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR);
}

static uint8_t meta_ab_program(void *ctx, uint8_t page, uint32_t offset, const uint8_t *data, uint32_t len) {
    (void)ctx;
    return Flash_WriteVerified((page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR) + offset, data, len);
}

static const UrtcMetaAbOps meta_ab_ops = { meta_ab_page_ptr, meta_ab_erase, meta_ab_program, 0 };

static void meta_ab_load(MetaAbState_t *state) {
    if (!urtc_meta_ab_read(&meta_ab_ops, sizeof(*state), (uint8_t*)state)) {
        memset(state, 0, sizeof(*state));
        memset(state->serial_words, 0xFF, sizeof(state->serial_words)); // no serial yet: the blank slot
    }
}

uint8_t Metadata_Read(FirmwareMetadata_t *out) {
    MetaAbState_t state;
    meta_ab_load(&state);
    if (state.meta.magic != METADATA_MAGIC_VALID) {
        memset(out, 0, sizeof(*out));
        return 0;
    }
    *out = state.meta;
    return 1;
}

uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta) {
    MetaAbState_t state;
    meta_ab_load(&state);
    state.meta = *meta;
    return urtc_meta_ab_write(&meta_ab_ops, sizeof(state), (const uint8_t*)&state);
}

uint8_t Serial_ReadNewest(uint32_t *serial, uint32_t *counter) {
    MetaAbState_t state;
    uint32_t blank[4] = { 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL };
    meta_ab_load(&state);
    return urtc_serial_pick(state.serial_words, blank, serial, counter) ? 1u : 0u;
}

uint8_t Serial_Store(uint32_t serial, uint32_t counter) {
    MetaAbState_t state;
    meta_ab_load(&state);
    urtc_serial_slot_pack(serial, counter, state.serial_words);
    return urtc_meta_ab_write(&meta_ab_ops, sizeof(state), (const uint8_t*)&state);
}
#endif

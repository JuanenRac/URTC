// =============================================================================
// URTC Bootloader - CRC32, flash program/erase, and metadata persistence
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "bootloader_common.h"
#if URTC_BL_VERSION_COMPAT_COMMAND
// With the stored version rule built in, this file is optimised for size: the 28 KB of the bootloader have no room for it otherwise. The signature and hash
// files keep their optimisation, so the time of a verification does not change. As shipped (switch off) nothing here changes.
#pragma GCC optimize ("Os")
#endif
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "urtc_bl_serial.h"

// URTC_BL_META_AB=1 keeps the metadata (and the programmed serial) in two alternating pages instead of the append-only log of one page (src/common/urtc_meta_ab.h).
// The official build (URTC_RELEASE=1 of build_firmware.sh) turns it on; the bench build leaves it off unless URTC_BL_META_AB=1 is set. It uses the page
// URTC_MASTER_METADATA_B_ADDR (src/common/urtc_flash_layout.h), the last 2 KB page of the 30 KB region, which the linker script keeps free of code. A board that
// holds its state in the log of one page moves over without losing the serial or the rollback counter (src/common/urtc_meta_migrate.h).
#ifndef URTC_BL_META_AB
#define URTC_BL_META_AB 0
#endif
#if URTC_BL_VERSION_COMPAT_COMMAND
static unsigned char vc_force_write = 0;   // set while a change of the setting alone is written: the state is the same, the page is not
#define URTC_META_AB_SKIP_SAME() (!vc_force_write)
#endif
#if URTC_BL_META_AB
#include "urtc_meta_ab.h"
#include "urtc_meta_migrate.h"
#ifndef URTC_MASTER_METADATA_B_ADDR
#error "URTC_BL_META_AB=1 needs URTC_MASTER_METADATA_B_ADDR: the address of a free 2 KB page (check that the bootloader image ends before it)"
#endif
#endif
#if URTC_BL_VERSION_COMPAT_COMMAND
#if !URTC_BL_META_AB
#error "URTC_BL_VERSION_COMPAT_COMMAND=1 needs URTC_BL_META_AB=1: the setting is kept in the two metadata pages"
#endif
#include "urtc_vc_annex.h"
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

#if !URTC_BL_META_AB // the helpers of the log of one page; the two-page layout does not use them (it reads the old log through urtc_meta_migrate.h)
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

#if !URTC_BL_META_AB
static void serial_slot_words(uint32_t address, uint32_t words[4]) {
    memcpy(words, (const void*)(uintptr_t)address, URTC_SERIAL_SLOT_BYTES);
}
#endif

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

#if !URTC_BL_META_AB
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
#endif

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
_Static_assert(sizeof(MetaAbState_t) == URTC_META_STATE_BYTES && sizeof(FirmwareMetadata_t) == URTC_META_LEGACY_META_BYTES, "the state is laid out as the migration reads and writes it");
_Static_assert(URTC_MASTER_METADATA_B_ADDR + FLASH_PAGE_SIZE <= URTC_MASTER_METADATA_ADDR, "the second page is in front of the first and the two do not overlap");

// A marker that the linker keeps (the linker script has KEEP for the section): the build of the release candidate looks for it in the image, so a bootloader
// built without the two pages cannot reach a release by mistake.
__attribute__((used, section(".urtc_layout"))) const char urtc_bl_meta_layout[16] = "URTC-META-AB-V1";

static const uint8_t *meta_ab_page_ptr(void *ctx, uint8_t page) {
    (void)ctx;
    return (const uint8_t*)(uintptr_t)(page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR);
}

static uint8_t meta_ab_erase(void *ctx, uint8_t page) {
    (void)ctx;
    return metadata_page_erase_at(page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR);
}

#if URTC_BL_VERSION_COMPAT_COMMAND
_Static_assert(URTC_VC_ANNEX_PAYLOAD == URTC_META_STATE_BYTES, "the annex is read from the pages of this state");
_Static_assert(8u + URTC_VC_ANNEX_PAYLOAD + 4u <= URTC_VC_ANNEX_OFFSET, "the annex does not overlap the record");
// The annex of the next record: taken from the record in force before every write, so that no write of the state or the serial loses the setting.
static uint8_t vc_annex[URTC_VC_RECORD_SIZE];
static uint8_t vc_annex_present = 0;
#endif

static uint8_t meta_ab_program(void *ctx, uint8_t page, uint32_t offset, const uint8_t *data, uint32_t len) {
    (void)ctx;
#if URTC_BL_VERSION_COMPAT_COMMAND
    // offset 0 is the magic, programmed last: the annex goes in just before it, so a page that has a magic has its annex whole.
    if (offset == 0u && vc_annex_present &&
        !Flash_WriteVerified((page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR) + URTC_VC_ANNEX_OFFSET, vc_annex, URTC_VC_RECORD_SIZE)) {
        return 0;
    }
#endif
    return Flash_WriteVerified((page == 0u ? METADATA_ADDR : URTC_MASTER_METADATA_B_ADDR) + offset, data, len);
}

static const UrtcMetaAbOps meta_ab_ops = { meta_ab_page_ptr, meta_ab_erase, meta_ab_program, 0 };

// The newest A/B record; with none, the state of the single-page layout still in page 0 (a board that was installed before this layout); with none either, blank.
static void meta_ab_load(MetaAbState_t *state) {
    (void)urtc_meta_ab_load_state(&meta_ab_ops, (uint8_t*)state, 0);
}

// The first A/B record of a board that holds the single-page layout goes to page 1, so the old log stays readable until the new record is complete.
static uint8_t meta_ab_store(const MetaAbState_t *state) {
#if URTC_BL_VERSION_COMPAT_COMMAND
    uint32_t mode, counter;
    urtc_vc_annex_load(&meta_ab_ops, &mode, &counter);
    vc_annex_present = (mode != URTC_VC_MODE_NONE) ? 1u : 0u;
    if (vc_annex_present) urtc_vc_record_pack(mode, counter, vc_annex);
#endif
    return urtc_meta_ab_store_state(&meta_ab_ops, (const uint8_t*)state);
}

#if URTC_BL_VERSION_COMPAT_COMMAND
void VersionCompat_Read(uint32_t *mode, uint32_t *counter) {
    urtc_vc_annex_load(&meta_ab_ops, mode, counter);
}

uint8_t VersionCompat_Store(uint32_t mode, uint32_t counter) {
    MetaAbState_t state;
    meta_ab_load(&state);
    urtc_vc_record_pack(mode, counter, vc_annex);
    vc_annex_present = 1u;
    vc_force_write = 1u;
    const uint8_t done = urtc_meta_ab_store_state(&meta_ab_ops, (const uint8_t*)&state);   // the same path as every other write
    vc_force_write = 0u;
    return done;
}
#endif

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
    return meta_ab_store(&state);
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
    return meta_ab_store(&state);
}
#endif

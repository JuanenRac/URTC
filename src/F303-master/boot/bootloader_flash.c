// =============================================================================
// URTC Bootloader - CRC32, flash program/erase, and metadata persistence
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "bootloader_common.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"

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
// the CRC/HMAC check (or worse, execution) fails later.
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

uint8_t Metadata_Read(FirmwareMetadata_t *out) {
    memcpy(out, (const void*)METADATA_ADDR, sizeof(FirmwareMetadata_t));
    return out->magic == METADATA_MAGIC_VALID;
}

// Writes without erasing first - callers are responsible for that (STM32F3
// flash requires an erased page before programming; writing without one
// causes a PGERR fault). Static and single-caller today
// (Metadata_EraseAndWrite, right below, which does erase first), but
// worth flagging explicitly for any future caller added within this file.
static uint8_t Metadata_Write(const FirmwareMetadata_t *meta) {
    return Flash_WriteVerified(METADATA_ADDR, (const uint8_t*)meta, sizeof(FirmwareMetadata_t));
    // Note: caller is responsible for erasing the metadata page first - see
    // Metadata_ErraseAndWrite below, which every call site actually uses.
}

uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta) {
    // Skip the erase+write entirely if the page already holds exactly
    // this content - this page's flash has the same ~10,000-cycle
    // erase/write endurance as any other, and a boot sequence that ends
    // up calling this with unchanged data (nothing here currently does,
    // but future logic might) shouldn't spend a cycle on a no-op.
    FirmwareMetadata_t current;
    if (Metadata_Read(&current) && memcmp(&current, meta, sizeof(FirmwareMetadata_t)) == 0) {
        return 1;
    }
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = METADATA_ADDR;
    eraseInit.NbPages = 1;
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    HAL_StatusTypeDef res = HAL_FLASHEx_Erase(&eraseInit, &pageError);
    HAL_FLASH_Lock();
    if (res != HAL_OK || pageError != 0xFFFFFFFF) return 0;
    return Metadata_Write(meta);
}

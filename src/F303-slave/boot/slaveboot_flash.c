// =============================================================================
// URTC Expansion Slave Bootloader - CRC32, flash program/erase, and
// metadata persistence
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// CRC32/flash/metadata logic here is intentionally near-identical to the
// main board's own bootloader_flash.c - same algorithms, same endurance
// and watchdog reasoning apply regardless of which chip is running them.
// The one real difference: this chip has no OLED and can't push a CAN
// heartbeat as an I2C slave (it can only answer when the link-bus master
// reads a register), so progress is tracked in update_progress_percent
// instead and read back via REG_PROGRESS - see slaveboot_main.c.
// =============================================================================
#include "stm32f3xx_hal.h"
#include <string.h>
#include "slaveboot_common.h"
#include "slaveboot_flash.h"

// Defined here, declared extern in slaveboot_flash.h - read by the I2C
// register-read handler in slaveboot_main.c whenever the link-bus
// master polls REG_PROGRESS during a long copy.
volatile uint8_t update_progress_percent = 0xFF;

static uint32_t crc32_table[256];
static uint8_t crc32_table_built = 0;

// Software CRC32 (standard polynomial 0xEDB88320, reflected, final XOR
// 0xFFFFFFFF) - same variant as the main board's own bootloader, so the
// same signing tool logic applies to both without modification.
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
    // One page at a time with an IWDG refresh between each - the backup
    // slot here is 54KB (27 pages, see APP_MAX_SIZE in slaveboot_common.h),
    // smaller than the main board's own 112KB/56-page slot, so this margin
    // is even more comfortable here,
    // but the reasoning (a single erase-everything call can't refresh the
    // watchdog mid-loop) is identical.
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
// reporting success - identical reasoning and implementation to the main
// board's own Flash_WriteVerified.
uint8_t Flash_WriteVerified(uint32_t addr, const uint8_t *buf, uint32_t len) {
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t half_word = buf[i];
        if (i + 1 < len) half_word |= (buf[i+1] << 8);
        else half_word |= (0xFF << 8);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, half_word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
        if ((i & 0xFF) == 0) HAL_IWDG_Refresh(&hiwdg);
    }
    HAL_FLASH_Lock();
    __DSB();

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

// Copies backup slot into main slot, page by page. Same algorithm as the
// main board's own Flash_CopyRegion; the only change is what happens with
// progress - no OLED bar, no active CAN push, just updating the module-
// level percent that REG_PROGRESS reads back on request.
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

        update_progress_percent = (uint8_t)(((uint64_t)(p + 1) * 100) / num_pages);
    }
    return 1;
}

uint8_t Metadata_Read(FirmwareMetadata_t *out) {
    memcpy(out, (const void*)METADATA_ADDR, sizeof(FirmwareMetadata_t));
    return out->magic == METADATA_MAGIC_VALID;
}

static uint8_t Metadata_Write(const FirmwareMetadata_t *meta) {
    return Flash_WriteVerified(METADATA_ADDR, (const uint8_t*)meta, sizeof(FirmwareMetadata_t));
}

uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta) {
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

// =============================================================================
// URTC Bootloader - Flash erase/write/verify and metadata I/O declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_FLASH_H
#define BOOTLOADER_FLASH_H

#include <stdint.h>
#include "bootloader_common.h"

uint32_t CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t CRC32_Finalize(uint32_t crc);

uint8_t Flash_ErasePages(uint32_t start_addr, uint32_t num_pages);
uint8_t Flash_WriteVerified(uint32_t addr, const uint8_t *buf, uint32_t len);
uint8_t Flash_CopyRegion(uint32_t dest_addr, uint32_t src_addr, uint32_t total_len);

uint8_t Metadata_Read(FirmwareMetadata_t *out);
uint8_t Metadata_EraseAndWrite(const FirmwareMetadata_t *meta);

#endif // BOOTLOADER_FLASH_H

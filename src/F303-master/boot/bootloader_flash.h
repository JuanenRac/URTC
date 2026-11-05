// =============================================================================
// URTC Bootloader - Flash erase/write/verify and metadata I/O declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
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

// The programmed serial number (see urtc_bl_serial.h): two 16-byte slots in the 32 bytes that the metadata page keeps after its 28 records. A page that is
// erased to recycle the log carries the newest record over, so the serial survives it.
// Reads the newest valid record: 1 with the serial and the counter, or 0 when there is none.
uint8_t Serial_ReadNewest(uint32_t *serial, uint32_t *counter);
// Keeps a new record: in a blank slot when there is one, else the page is recycled (the newest metadata state and the new record are written again).
uint8_t Serial_Store(uint32_t serial, uint32_t counter);

#endif // BOOTLOADER_FLASH_H

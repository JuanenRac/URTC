// =============================================================================
// URTC Bootloader - Shared types, defines, and cross-module externs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_COMMON_H
#define BOOTLOADER_COMMON_H

#include "stm32f3xx_hal.h"

// -----------------------------------------------------------------------
// Shared peripheral handles - defined once in bootloader_main.c, declared
// extern here so every module that needs one can see it without each
// module guessing at ownership.
// -----------------------------------------------------------------------
extern CAN_HandleTypeDef hcan;
extern IWDG_HandleTypeDef hiwdg;
extern I2C_HandleTypeDef hi2c2;

// -----------------------------------------------------------------------
// Flash layout
// -----------------------------------------------------------------------
#define MAIN_APP_ADDR        0x08008000UL
#define BACKUP_APP_ADDR      0x08024000UL
#define APP_MAX_SIZE         (112UL * 1024UL)
#define METADATA_ADDR        0x08007800UL
#define METADATA_MAGIC_VALID 0x55415041UL // "APAU" as a magic marker, arbitrary but distinctive
// FLASH_PAGE_SIZE is already defined by the HAL library itself
// (stm32f3xx_hal_flash_ex.h) as 0x800 (2048) - the same value this project
// uses, so it's referenced directly rather than redefined here.

// -----------------------------------------------------------------------
// Firmware identity - rejects an image built for different hardware or
// declared incompatible, before ever trusting its CRC/HMAC.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID     0x0303CC01UL // STM32F303CCT6, URTC board revision 1
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0

// BOOTLOADER_VERSION_* describes THIS bootloader binary itself - separate
// from FIRMWARE_VERSION_MAJOR/MINOR above, which is the version of
// whatever APPLICATION image is currently installed in the main slot.
// Versioning convention for this project: PATCH increments on every
// change to this file (0-9), then MINOR increments and PATCH resets to 0
// (so 1.0.9's next change becomes 1.1.0). Bump this every time any part
// of the partitioned bootloader source changes.
#define BOOTLOADER_VERSION_MAJOR 1
#define BOOTLOADER_VERSION_MINOR 1
#define BOOTLOADER_VERSION_PATCH 2

// -----------------------------------------------------------------------
// HMAC-SHA256 signing key - shared between this bootloader and whatever
// build/signing tool prepares a firmware image for a CAN update. This is a
// placeholder default; change it before relying on this for anything, and
// keep the signing tool's copy in sync with whatever it's changed to.
// Declared here, defined once in bootloader_crypto.c - a static array in
// this header would otherwise get its own private copy compiled into
// every .c file that includes it.
// -----------------------------------------------------------------------
extern const uint8_t HMAC_KEY[32];

// -----------------------------------------------------------------------
// CAN protocol (0x7F0-0x7FA) - see CANBUS.TXT for the full wire-level reference
// -----------------------------------------------------------------------
#define CAN_ID_ENTER_BOOTLOADER  0x7F0 // sent to the RUNNING APPLICATION (it resets into this bootloader) - handled in the app's firmware, not here
#define CAN_ID_START_UPDATE      0x7F1 // sent to THIS bootloader: DLC=8, big-endian total firmware size (4 bytes) + big-endian HardwareID (4 bytes)
#define CAN_ID_DATA              0x7F2 // sent to THIS bootloader: up to 8 bytes of firmware data, sent sequentially - goes to the BACKUP slot, never main
#define CAN_ID_PAGE_ACK          0x7F3 // sent BY this bootloader after each backup-slot page write: DLC=4, big-endian page index
#define CAN_ID_END_UPDATE        0x7F4 // sent to THIS bootloader: DLC=8, big-endian CRC32 (4 bytes) + big-endian VersionMajor/Minor (2 bytes each)
#define CAN_ID_STATUS            0x7F5 // sent BY this bootloader: DLC=1, status codes below
#define CAN_ID_HEARTBEAT         0x7F6 // sent BY this bootloader every ~1s while listening or updating: DLC=2, status byte + progress percent (0-100, 0xFF if not applicable)
#define CAN_ID_HMAC_CHUNK        0x7F7 // sent to THIS bootloader: DLC=8 x4, sent sequentially right after CAN_ID_START_UPDATE and before the first CAN_ID_DATA frame
#define CAN_ID_QUERY_VERSION     0x7F8 // sent to either the application or this bootloader, whichever is currently running
#define CAN_ID_VERSION_RESPONSE  0x7F9 // sent BY whichever answered: DLC=8, byte0 (0=application, 1=bootloader) + HardwareID (4 bytes) + version major (2 bytes) + version minor (1 byte), all big-endian
#define CAN_ID_BOOTLOADER_VERSION_RESPONSE 0x7FA // sent ONLY by the bootloader, right alongside 0x7F9

#define STATUS_LISTENING     0x01 // waiting for CAN_ID_START_UPDATE
#define STATUS_ERASING       0x02 // erasing the backup slot
#define STATUS_RECEIVING     0x03 // writing firmware data into the backup slot
#define STATUS_VERIFYING     0x06 // checking size/CRC32/HMAC/HardwareID on the backup slot
#define STATUS_COPYING       0x07 // backup slot verified - erasing and copying it into the main slot
#define STATUS_VERIFY_OK     0x04 // copy to main slot complete and read-back verified; about to jump
#define STATUS_VERIFY_FAIL   0x05 // size, CRC32, HMAC, or HardwareID mismatch on the backup slot - main slot untouched
#define VERIFY_FAIL_REASON_INCOMPLETE  0x01
#define VERIFY_FAIL_REASON_CRC32       0x02
#define VERIFY_FAIL_REASON_HMAC        0x03
#define VERIFY_FAIL_REASON_HARDWARE_ID 0x04
#define VERIFY_FAIL_REASON_ROLLBACK    0x05
#define STATUS_ERROR         0xFF

// -----------------------------------------------------------------------
// Firmware metadata (single 2K page, one struct, one "state" field).
// -----------------------------------------------------------------------
#define META_STATE_APP_VALID     1
#define META_STATE_COPY_PENDING  2

typedef struct {
    uint32_t magic;
    uint32_t state;
    uint32_t hardware_id;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t size;
    uint32_t crc32;
    uint8_t  hmac[32];
} FirmwareMetadata_t;

// Defined in bootloader_protocol.c - declared here since Flash_CopyRegion
// (in bootloader_flash.c) calls this during a long copy to keep the
// master informed of progress.
void CAN_SendHeartbeat(uint8_t status, uint8_t progress_percent);

#endif // BOOTLOADER_COMMON_H

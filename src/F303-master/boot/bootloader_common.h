// =============================================================================
// URTC Bootloader - Shared types, defines, and cross-module externs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef BOOTLOADER_COMMON_H
#define BOOTLOADER_COMMON_H

#include "stm32f3xx_hal.h"
#include "urtc_flash_layout.h"
#include "urtc_can_ids.h" // the identifiers of the update exchange come from the shared specification

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
// The numbers are written once in src/common/urtc_flash_layout.h.
#define MAIN_APP_ADDR        URTC_MASTER_MAIN_APP_ADDR
#define BACKUP_APP_ADDR      URTC_MASTER_BACKUP_APP_ADDR
#define APP_MAX_SIZE         URTC_MASTER_APP_MAX_SIZE
#define METADATA_ADDR        URTC_MASTER_METADATA_ADDR
#define METADATA_MAGIC_VALID 0x55415041UL // "APAU" as a magic marker, arbitrary but distinctive
// FLASH_PAGE_SIZE is already defined by the HAL library itself
// (stm32f3xx_hal_flash_ex.h) as 0x800 (2048) - the same value this project
// uses, so it's referenced directly rather than redefined here.

// -----------------------------------------------------------------------
// Firmware identity - rejects an image built for different hardware or
// declared incompatible, before ever trusting its CRC/HMAC.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID     0x0303CC01UL // STM32F303CCT6, URTC board revision 1
// Kept in sync with firmware_common.h's own FIRMWARE_VERSION_* by
// bump_version.py's mirror-header argument (repo root) every time the
// application is built - never edit this by hand, it will just drift
// again (exactly what had happened here before that argument existed:
// this copy sat at MINOR 0 while the real application had already moved
// to MINOR 1, weakening the anti-rollback check in HandleEndUpdate() for
// any chip that adopts this as its baseline on first boot / after a
// non-OTA flash).
#define FIRMWARE_VERSION_MAJOR 0
#define FIRMWARE_VERSION_MINOR 4
#define FIRMWARE_VERSION_PATCH 6

// BOOTLOADER_VERSION_* describes THIS bootloader binary itself - separate
// from FIRMWARE_VERSION_MAJOR/MINOR above, which is the version of
// whatever APPLICATION image is currently installed in the main slot.
// Versioning convention for this project: PATCH increments on every
// change to this file (0-9), then MINOR increments and PATCH resets to 0
// (so 0.0.9's next change becomes 0.1.0). Bump this every time any part
// of the partitioned bootloader source changes.
#define BOOTLOADER_VERSION_MAJOR 0
#define BOOTLOADER_VERSION_MINOR 4
#define BOOTLOADER_VERSION_PATCH 8

// -----------------------------------------------------------------------
// Image authentication: the bootloader holds no secret. An application is
// installed only when an Ed25519 record (CAN_ID_IMG_SIG_CHUNK) verifies under
// a PUBLIC key of the trust table (bootloader_trust.h); with an empty table
// nothing installs. See docs/FIRMWARE_SIGNING.TXT.
// -----------------------------------------------------------------------

// -----------------------------------------------------------------------
// CAN protocol (0x7F0-0x7FA) - see CANBUS.TXT for the full wire-level reference
// -----------------------------------------------------------------------
#define CAN_ID_ENTER_BOOTLOADER  URTC_CAN_ID_ENTER_BOOTLOADER // sent to the RUNNING APPLICATION (it resets into this bootloader) - handled in the app's firmware, not here
#define CAN_ID_START_UPDATE      URTC_CAN_ID_START_UPDATE // sent to THIS bootloader: DLC=8, big-endian total firmware size (4 bytes) + big-endian HardwareID (4 bytes)
#define CAN_ID_DATA              URTC_CAN_ID_DATA // sent to THIS bootloader: up to 8 bytes of firmware data, sent sequentially - goes to the BACKUP slot, never main
#define CAN_ID_PAGE_ACK          URTC_CAN_ID_PAGE_ACK // sent BY this bootloader after each backup-slot page write: DLC=4, big-endian page index
#define CAN_ID_END_UPDATE        URTC_CAN_ID_END_UPDATE // sent to THIS bootloader: DLC=8, big-endian CRC32 (4 bytes) + big-endian VersionMajor/Minor (2 bytes each)
#define CAN_ID_STATUS            URTC_CAN_ID_STATUS // sent BY this bootloader: DLC=1, status codes below
#define CAN_ID_HEARTBEAT         URTC_CAN_ID_HEARTBEAT // sent BY this bootloader every ~1s while listening or updating: DLC=2, status byte + progress percent (0-100, 0xFF if not applicable)
#define CAN_ID_HMAC_CHUNK        URTC_CAN_ID_HMAC_CHUNK // HMAC chunk of the old bootloaders; this one does not read it (it asks for the Ed25519 record on CAN_ID_IMG_SIG_CHUNK)
#define CAN_ID_IMG_SIG_CHUNK     URTC_CAN_ID_BOOTLOADER_SIGNATURE_CHUNK // sent to THIS bootloader: DLC=8 x11, index (0-10) + 7 bytes of the 76-byte record (key id, counter, signature), after CAN_ID_START_UPDATE
#define CAN_ID_KEY_ID            URTC_CAN_ID_BOOTLOADER_KEY_ID // sent BY this bootloader right after its answer to CAN_ID_QUERY_VERSION: DLC=8, the id of the key it trusts
#define CAN_ID_QUERY_VERSION     URTC_CAN_ID_QUERY_VERSION // sent to either the application or this bootloader, whichever is currently running
#define CAN_ID_VERSION_RESPONSE  URTC_CAN_ID_VERSION_RESPONSE // sent BY whichever answered: DLC=8, byte0 (0=application, 1=bootloader) + HardwareID (4 bytes) + version major (2 bytes) + version minor (1 byte), all big-endian
#define CAN_ID_BOOTLOADER_VERSION_RESPONSE URTC_CAN_ID_BOOTLOADER_VERSION_RESPONSE // sent ONLY by the bootloader, right alongside 0x7F9
#define CAN_ID_QUERY_ERROR_COUNTERS URTC_CAN_ID_QUERY_ERROR_COUNTERS // sent to either the application or this bootloader, whichever is currently running - same dual-answerable convention as CAN_ID_QUERY_VERSION
#define CAN_ID_ERROR_COUNTERS_RESPONSE URTC_CAN_ID_ERROR_COUNTERS_RESPONSE // sent BY whichever answered: DLC=2, TEC (Transmit Error Counter) + REC (Receive Error Counter), read directly from the CAN peripheral's own ESR register
#define CAN_ID_AUTHORIZE_DOWNGRADE URTC_CAN_ID_AUTHORIZE_DOWNGRADE // sent to THIS bootloader: DLC=4, magic payload 0xD0,0x9E,0x12,0xAD - authorizes the CURRENT update attempt only to install an older version than what's running, bypassing HandleEndUpdate's own anti-rollback check just this once
// CAN_ID_READBACK is used bidirectionally, distinguished by DLC - same
// convention CAN_ID_STATUS already uses for its own DLC=1-vs-2 verify-fail
// reason. Master->bootloader with any DLC starts the transfer; the very
// first bootloader->master reply is DLC=4 (the total byte count, or 0 if
// there's nothing valid to back up); every reply after that is DLC=8, raw
// main-slot bytes, sequential from MAIN_APP_ADDR. This is the last 2 IDs
// in the entire 11-bit standard CAN ID space (0x7FE/0x7FF) - nothing more
// fits after these without extended (29-bit) IDs.
#define CAN_ID_READBACK URTC_CAN_ID_READBACK
#define CAN_ID_READBACK_PAGE_ACK URTC_CAN_ID_READBACK_PAGE_ACK // sent to THIS bootloader after each 2048-byte page is safely received: DLC=4, big-endian page index - same big-endian-page-index convention as CAN_ID_PAGE_ACK, just the opposite direction (host acking a read instead of the bootloader acking a write)

#define STATUS_LISTENING     0x01 // waiting for CAN_ID_START_UPDATE
#define STATUS_ERASING       0x02 // erasing the backup slot
#define STATUS_RECEIVING     0x03 // writing firmware data into the backup slot
#define STATUS_VERIFYING     0x06 // checking size/CRC32/HMAC/HardwareID on the backup slot
#define STATUS_COPYING       0x07 // backup slot verified - erasing and copying it into the main slot
#define STATUS_VERIFY_OK     0x04 // copy to main slot complete and read-back verified; about to jump
#define STATUS_VERIFY_FAIL   0x05 // size, CRC32, HMAC, or HardwareID mismatch on the backup slot - main slot untouched
#define VERIFY_FAIL_REASON_INCOMPLETE  0x01
#define VERIFY_FAIL_REASON_CRC32       0x02
#define VERIFY_FAIL_REASON_HMAC        0x03 // the wire value of "the signature did not verify" (an Ed25519 record here, an HMAC in the old bootloaders)
#define VERIFY_FAIL_REASON_SIGNATURE   VERIFY_FAIL_REASON_HMAC
#define VERIFY_FAIL_REASON_HARDWARE_ID 0x04
#define VERIFY_FAIL_REASON_ROLLBACK    0x05
#define STATUS_ERROR         0xFF

// -----------------------------------------------------------------------
// Firmware metadata: an append-only log of records in one 2K page.
//
// Every change appends a record (generation, state, CRC-32); nothing is erased
// to make one. The valid record with the highest generation is the state. A
// record cut by a power loss has a wrong CRC and is ignored, so the previous
// one stays in force. The page is erased only when it is full, and then the
// newest state is written first. A board that loses the page that way
// falls back to the compiled-in floor of the rollback counter
// (URTC_TRUST_MIN_COUNTER) and adopts the application as a first boot.
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
    uint32_t counter;     // the floor of the signed rollback counter: an image below it is refused
    uint8_t  sha256[32];  // SHA-256 of the installed application (no key: it detects a damaged flash, the signature was checked at the installation)
} FirmwareMetadata_t;

typedef struct {
    uint32_t generation;
    FirmwareMetadata_t meta;
    uint32_t crc;         // CRC-32 (zlib) of the generation and the metadata
} MetaRecord_t;

#define META_RECORD_SIZE  72u
#define META_RECORD_SLOTS (2048u / META_RECORD_SIZE)

// Defined in bootloader_protocol.c - declared here since Flash_CopyRegion
// (in bootloader_flash.c) calls this during a long copy to keep the
// master informed of progress.
void CAN_SendHeartbeat(uint8_t status, uint8_t progress_percent);

#endif // BOOTLOADER_COMMON_H

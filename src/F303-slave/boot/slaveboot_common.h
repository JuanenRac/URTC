// =============================================================================
// URTC Expansion Slave Bootloader - Shared types, defines, and cross-module
// externs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
//
// Runs on the STM32F303CBT6 populating the ADVANCED expansion board variants
// (the 2 basic variants carry only a stepper driver, no MCU at all - this
// bootloader/firmware pair only exists on hardware that actually has this
// chip). Its own update path has no direct CAN or USB connection to the
// outside world - a new image arrives over the I2C LINK bus shared with the
// main URTC board's own STM32F303CC, which bit-bangs that bus as MASTER
// (ExpansionI2C_* in the main firmware) while this chip is the I2C SLAVE on
// it, using its own real hardware I2C peripheral instead of bit-banging.
// The main board's own CAN-OTA flow (0x7F0-0x7FA, see CANBUS.TXT) still
// starts this whole thing off - the Flasher's update bytes for THIS chip
// arrive over CAN same as any other update, get relayed across that I2C
// link one register-write at a time, and this bootloader is what's on the
// other end of that link actually receiving them.
// =============================================================================
#ifndef BOOTLOADER_COMMON_H
#define BOOTLOADER_COMMON_H

#include "stm32f3xx_hal.h"
#include "urtc_flash_layout.h"
#include "urtc_can_ids.h" // the registers, status codes, reasons, address and hardware identity come from the shared specification

// -----------------------------------------------------------------------
// Shared peripheral handles - defined once in slaveboot_main.c, declared
// extern here so every module that needs one can see it without each
// module guessing at ownership.
// -----------------------------------------------------------------------
extern IWDG_HandleTypeDef hiwdg;
// I2C1: the LINK bus to the main board's own STM32F303CC, hardware I2C in
// SLAVE mode here (the main board is master on this same physical bus,
// same pins/pull-ups the main board's own EXP_I2C2_SCL/SDA already
// describes - this chip is just what's listening on the other end).
extern I2C_HandleTypeDef hi2c1;

// -----------------------------------------------------------------------
// Flash layout - 128KB total (STM32F303CBT6), scaled down from the main
// board's own 256KB A/B scheme, same 2KB page size (same xB/xC density
// line, confirmed against the official ST datasheet - shares its flash
// controller behavior with the main board's own F303CC, just less of it).
// 18KB bootloader (vs. the main board's own 30KB) - smaller since this
// one has no OLED to drive and a much smaller command set, but not as
// small as first estimated: the real compiled size (crypto + flash +
// protocol + I2C slave HAL) came in at ~13KB once actually built and
// linked, not assumed - this 18KB leaves roughly 5KB of headroom above
// that rather than being sized to fit with nothing to spare.
// -----------------------------------------------------------------------
// The numbers are written once in src/common/urtc_flash_layout.h.
#define BOOTLOADER_FLASH_SIZE URTC_SLAVE_BOOTLOADER_SIZE   // 18KB, 9 pages
#define METADATA_ADDR         URTC_SLAVE_METADATA_ADDR     // 2KB, 1 page
#define MAIN_APP_ADDR         URTC_SLAVE_MAIN_APP_ADDR     // 54KB, 27 pages
#define BACKUP_APP_ADDR       URTC_SLAVE_BACKUP_APP_ADDR   // 54KB, 27 pages
#define APP_MAX_SIZE          URTC_SLAVE_APP_MAX_SIZE
#define METADATA_MAGIC_VALID 0x55415041UL // "APAU" - same marker as the main board's own bootloader, no reason for these to differ

// -----------------------------------------------------------------------
// Firmware identity - rejects an image built for different hardware or
// declared incompatible, before ever trusting its CRC/HMAC. Distinct from
// THIS_HARDWARE_ID in the main board's own bootloader (0x0303CC01) - a
// firmware image meant for one board must never be mistakenly accepted by
// the other, and this ID is the check that catches that.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID     URTC_HARDWARE_ID_SLAVE // STM32F303CBT6, expansion slave revision 1
// Kept in sync with slave_common.h's own FIRMWARE_VERSION_* by
// bump_version.py's mirror-header argument (repo root) every time the
// application is built - never edit this by hand.
#define FIRMWARE_VERSION_MAJOR 0
#define FIRMWARE_VERSION_MINOR 2
#define FIRMWARE_VERSION_PATCH 8

// BOOTLOADER_VERSION_* describes THIS bootloader binary itself - separate
// from FIRMWARE_VERSION_MAJOR/MINOR above, which is the version of
// whatever slave APPLICATION image is currently installed in the main
// slot. Same versioning convention as the main board's own bootloader
// (PATCH increments on every change to this file, rolling over to MINOR
// at 9), but this is its own independent version number, starting fresh
// at 0.0.0 - this is a brand new bootloader, not a continuation of the
// main board's own version history.
#define BOOTLOADER_VERSION_MAJOR 0
#define BOOTLOADER_VERSION_MINOR 3
#define BOOTLOADER_VERSION_PATCH 0

// -----------------------------------------------------------------------
// No signing key. This bootloader holds no secret of any kind: the master
// verifies the Ed25519 record of the image and confirms it (urtc_slave_confirm.h);
// the slave only checks integrity (CRC-32, SHA-256), the version rule and the
// rollback counter below.
// -----------------------------------------------------------------------
// The lowest signed counter this build accepts, whatever the metadata says: a floor compiled in, so that erasing the metadata page does not reopen
// the counters already retired by an earlier release. The floor that is kept in flash can only be higher.
#define URTC_SLAVE_COUNTER_FLOOR 0UL

// -----------------------------------------------------------------------
// I2C LINK protocol (register-style, not CAN framed) - the main board's
// STM32F303CC is master, writes/reads these as ordinary I2C register
// accesses (write the register address, then either write data or repeat-
// start into a read) exactly like talking to any other I2C peripheral -
// this chip doesn't know or care that the ultimate source of an update is
// the Flasher over CAN-OTA; from here, it's just I2C register traffic.
// Numbered 0x00-0x0F deliberately low and out of the way of anything an
// application-mode register map might want later (see slaveboot_main.c's
// own note on where this address range sits relative to the running
// application's own I2C register map, once that exists).
// -----------------------------------------------------------------------
#define I2C_SLAVE_ADDRESS        URTC_SLAVE_I2C_ADDRESS // 7-bit address on the link bus - distinct from every other address already in use elsewhere in this project (F-RAM 0x50, MLX90640 0x33, ADS1115 0x48-0x4B - no collision risk sharing a bus with those, though this link bus is physically separate from that one regardless)

#define REG_STATUS               URTC_SLAVE_BOOT_REG_STATUS // read-only, 1 byte - status codes below
#define REG_START_UPDATE         URTC_SLAVE_BOOT_REG_START_UPDATE // write-only, 8 bytes: big-endian total size (4) + big-endian HardwareID (4)
#define REG_DIGEST_EXPECTED        URTC_SLAVE_BOOT_REG_DIGEST_EXPECTED // write-only, 32 bytes: the expected SHA-256 (no key) of the complete image, sent once, before REG_END_UPDATE. The slave checks no HMAC
#define REG_DATA                 URTC_SLAVE_BOOT_REG_DATA // write-only, up to 32 bytes per transaction, sent sequentially - goes to the BACKUP slot, never main; unlike the main board's own CAN-framed 8-byte-per-frame limit, I2C has no equivalent per-transaction ceiling here, so this moves in much larger pieces
#define REG_END_UPDATE           URTC_SLAVE_BOOT_REG_END_UPDATE // write-only, 8 bytes: big-endian CRC32 (4) + big-endian VersionMajor (2) + big-endian VersionMinor (2)
#define REG_PROGRESS             URTC_SLAVE_BOOT_REG_PROGRESS // read-only, 1 byte: 0-100, 0xFF if not currently updating
#define REG_QUERY_VERSION        URTC_SLAVE_BOOT_REG_QUERY_VERSION // read-only, 10 bytes: byte0 (0=application,1=bootloader) + HardwareID (4) + version major (2) + version minor (2, all big-endian) - mirrors the main board's own 0x7F9 response fields, minus the CAN framing they need and this doesn't
#define REG_VERIFY_FAIL_REASON   URTC_SLAVE_BOOT_REG_VERIFY_FAIL_REASON // read-only, 1 byte: one of the VERIFY_FAIL_REASON_* values below, valid to read once REG_STATUS reports STATUS_VERIFY_FAIL - same reason the main board's own CAN_ID_STATUS frame carries as its 2nd byte, exposed here as its own register instead since I2C reads don't share CAN's fixed-DLC framing
#define REG_QUERY_VERSION_EXT    URTC_SLAVE_BOOT_REG_QUERY_VERSION_EXT // read-only, 4 bytes: URTC_SLAVE_VERSION_EXT_MARKER (0xE1, never a status code) + installed application PATCH (0xFF = unknown) + this bootloader's MINOR + this bootloader's PATCH. A bootloader that predates it answers this register with its 1-byte status instead, which is how the master tells the two apart.
#define REG_CHALLENGE            URTC_SLAVE_BOOT_REG_CHALLENGE // read-only, 16 bytes: the nonce of the update session in progress (made at REG_START_UPDATE); all zero when there is none. The master binds its confirmation to it
#define REG_CONFIRM              URTC_SLAVE_BOOT_REG_CONFIRM // write-only, 32 bytes: the master's confirmation that it verified the signature record of this image (urtc_slave_confirm.h). Accepted only in STATUS_AWAIT_CONFIRM
#define REG_COUNTER_FLOOR        URTC_SLAVE_BOOT_REG_COUNTER_FLOOR // read-only, 4 bytes, big-endian: the rollback floor this slave keeps (the higher of the compiled floor and the counter of the installed image). The master reads it before it verifies the record, and refuses a record below it
#define REG_RECORD_COUNTER       URTC_SLAVE_BOOT_REG_RECORD_COUNTER // write-only, 4 bytes, big-endian: the signed counter of the record the master verified for this session. Sent before REG_END_UPDATE is relayed; the confirmation is bound to it and a counter below the floor is refused

#define STATUS_LISTENING     URTC_STATUS_LISTENING // waiting for REG_START_UPDATE
#define STATUS_ERASING       URTC_STATUS_ERASING // erasing the backup slot
#define STATUS_RECEIVING     URTC_STATUS_RECEIVING // writing firmware data into the backup slot
#define STATUS_VERIFYING     URTC_STATUS_VERIFYING // checking size/CRC32/HMAC/HardwareID on the backup slot
#define STATUS_COPYING       URTC_STATUS_COPYING // backup slot verified - erasing and copying it into the main slot
#define STATUS_AWAIT_CONFIRM URTC_SLAVE_STATUS_AWAIT_CONFIRM // size, CRC32, SHA-256 and rollback checks passed; waiting (ten seconds at most) for the master's REG_CONFIRM. Without it the update fails closed
#define STATUS_VERIFY_OK     URTC_STATUS_VERIFY_OK // copy to main slot complete and read-back verified; about to jump
#define STATUS_VERIFY_FAIL   URTC_STATUS_VERIFY_FAIL // size, CRC32, HMAC, or HardwareID mismatch on the backup slot - main slot untouched
#define VERIFY_FAIL_REASON_INCOMPLETE  URTC_VERIFY_FAIL_REASON_INCOMPLETE
#define VERIFY_FAIL_REASON_CRC32       URTC_VERIFY_FAIL_REASON_CRC32
#define VERIFY_FAIL_REASON_HMAC        URTC_VERIFY_FAIL_REASON_HMAC
#define VERIFY_FAIL_REASON_HARDWARE_ID URTC_VERIFY_FAIL_REASON_HARDWARE_ID
#define VERIFY_FAIL_REASON_ROLLBACK    URTC_VERIFY_FAIL_REASON_ROLLBACK
#define VERIFY_FAIL_REASON_NOT_CONFIRMED URTC_SLAVE_VERIFY_FAIL_REASON_NOT_CONFIRMED // the master did not confirm in time, or its confirmation does not match this session and this image
#define STATUS_ERROR         URTC_STATUS_ERROR

// -----------------------------------------------------------------------
// Firmware metadata: an APPEND-ONLY LOG in the metadata page (same layout as
// the main board's bootloader). A record is the generation, the metadata and a
// CRC-32; a new state is written after the last record and nothing is erased
// until the 28 slots are full, so the rollback counter survives every normal
// update and a power cut leaves the previous record intact. The newest valid
// record is the state.
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
    uint32_t counter;    // the rollback floor: the signed counter of the installed image. An image below it is refused
    uint8_t  sha256[32]; // the SHA-256 of the installed application (no key)
} FirmwareMetadata_t;

typedef struct {
    uint32_t generation;
    FirmwareMetadata_t meta;
    uint32_t crc;        // CRC-32 (zlib) of the generation and the metadata
} MetaRecord_t;

#define META_RECORD_SIZE  72u
#define META_RECORD_SLOTS (2048u / META_RECORD_SIZE)

#endif // BOOTLOADER_COMMON_H

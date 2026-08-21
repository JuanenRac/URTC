// =============================================================================
// URTC Expansion Slave Bootloader - Shared types, defines, and cross-module
// externs
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
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
#define BOOTLOADER_FLASH_SIZE 18432UL           // 18KB, 9 pages
#define METADATA_ADDR        0x08004800UL       // 2KB, 1 page
#define MAIN_APP_ADDR         0x08005000UL      // 54KB, 27 pages
#define BACKUP_APP_ADDR       0x08012800UL      // 54KB, 27 pages
#define APP_MAX_SIZE          (54UL * 1024UL)
#define METADATA_MAGIC_VALID 0x55415041UL // "APAU" - same marker as the main board's own bootloader, no reason for these to differ

// -----------------------------------------------------------------------
// Firmware identity - rejects an image built for different hardware or
// declared incompatible, before ever trusting its CRC/HMAC. Distinct from
// THIS_HARDWARE_ID in the main board's own bootloader (0x0303CC01) - a
// firmware image meant for one board must never be mistakenly accepted by
// the other, and this ID is the check that catches that.
// -----------------------------------------------------------------------
#define THIS_HARDWARE_ID     0x0303CB01UL // STM32F303CBT6, expansion slave revision 1
// Kept in sync with slave_common.h's own FIRMWARE_VERSION_* by
// bump_version.py's mirror-header argument (repo root) every time the
// application is built - never edit this by hand.
#define FIRMWARE_VERSION_MAJOR 1
#define FIRMWARE_VERSION_MINOR 0
#define FIRMWARE_VERSION_PATCH 2

// BOOTLOADER_VERSION_* describes THIS bootloader binary itself - separate
// from FIRMWARE_VERSION_MAJOR/MINOR above, which is the version of
// whatever slave APPLICATION image is currently installed in the main
// slot. Same versioning convention as the main board's own bootloader
// (PATCH increments on every change to this file, rolling over to MINOR
// at 9), but this is its own independent version number, starting fresh
// at 1.0.0 - this is a brand new bootloader, not a continuation of the
// main board's own version history.
#define BOOTLOADER_VERSION_MAJOR 1
#define BOOTLOADER_VERSION_MINOR 0
#define BOOTLOADER_VERSION_PATCH 5

// -----------------------------------------------------------------------
// HMAC-SHA256 signing key - deliberately different from the main board's
// own HMAC_KEY (bootloader_crypto.c's own comment on that one already
// says as much: change it before relying on it). A firmware image signed
// for the main board must never verify successfully against this chip's
// key, or vice versa - two different keys is what keeps a mismatched
// image from an all-too-easy accidental cross-flash from ever passing
// verification on the wrong board. Same placeholder-until-you-change-it
// status as the main board's own key.
// -----------------------------------------------------------------------
extern const uint8_t HMAC_KEY[32];

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
#define I2C_SLAVE_ADDRESS        0x42 // 7-bit address on the link bus - distinct from every other address already in use elsewhere in this project (F-RAM 0x50, MLX90640 0x33, ADS1115 0x48-0x4B - no collision risk sharing a bus with those, though this link bus is physically separate from that one regardless)

#define REG_STATUS               0x00 // read-only, 1 byte - status codes below
#define REG_START_UPDATE         0x01 // write-only, 8 bytes: big-endian total size (4) + big-endian HardwareID (4)
#define REG_HMAC_EXPECTED        0x02 // write-only, 32 bytes: the expected HMAC-SHA256 of the complete image, sent once, before any REG_DATA writes
#define REG_DATA                 0x03 // write-only, up to 32 bytes per transaction, sent sequentially - goes to the BACKUP slot, never main; unlike the main board's own CAN-framed 8-byte-per-frame limit, I2C has no equivalent per-transaction ceiling here, so this moves in much larger pieces
#define REG_END_UPDATE           0x04 // write-only, 8 bytes: big-endian CRC32 (4) + big-endian VersionMajor (2) + big-endian VersionMinor (2)
#define REG_PROGRESS             0x05 // read-only, 1 byte: 0-100, 0xFF if not currently updating
#define REG_QUERY_VERSION        0x06 // read-only, 10 bytes: byte0 (0=application,1=bootloader) + HardwareID (4) + version major (2) + version minor (2, all big-endian) - mirrors the main board's own 0x7F9 response fields, minus the CAN framing they need and this doesn't
#define REG_VERIFY_FAIL_REASON   0x07 // read-only, 1 byte: one of the VERIFY_FAIL_REASON_* values below, valid to read once REG_STATUS reports STATUS_VERIFY_FAIL - same reason the main board's own CAN_ID_STATUS frame carries as its 2nd byte, exposed here as its own register instead since I2C reads don't share CAN's fixed-DLC framing

#define STATUS_LISTENING     0x01 // waiting for REG_START_UPDATE
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
// Firmware metadata (single 2K page, one struct, one "state" field) -
// identical layout to the main board's own FirmwareMetadata_t (same field
// order, same sizes) so the same struct definition, the same
// SavedState_Checksum-style CRC-8 pattern, and the same signing-tool
// logic can be reused nearly verbatim, differing only in which
// THIS_HARDWARE_ID and HMAC_KEY end up embedded in a given signed image.
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

#endif // BOOTLOADER_COMMON_H

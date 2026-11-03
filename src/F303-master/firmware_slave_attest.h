// =============================================================================
// URTC Firmware - the master's verification of the expansion slave's image, and its confirmation to the slave bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The slave bootloader has no room for an Ed25519 verifier, so the master verifies for it (docs/SLAVE_BOOT_CONFIRMATION.md). While the host updates the slave
// through the CAN bridge, this module hashes the image bytes the bridge relays (SHA-256, streaming), collects the 76-byte signature record (0x21A) and, at the
// end of the update, checks the record under the keys of the trust table of the master's own bootloader. Only then does it build the tag that confirms the
// image to the slave (urtc_slave_confirm.h). Nothing here touches flash or the bus: the bridge does the I2C and passes the bytes in, which is what lets the
// host tests drive it.
#ifndef FIRMWARE_SLAVE_ATTEST_H
#define FIRMWARE_SLAVE_ATTEST_H

#include <stdint.h>

#include "urtc_ed25519.h"

#define SLAVE_ATTEST_RECORD_SIZE   76u
#define SLAVE_ATTEST_CHUNK_COUNT   11u
#define SLAVE_ATTEST_CHUNK_BYTES   7u
#define SLAVE_ATTEST_MAX_KEYS      2u
#define SLAVE_ATTEST_NONCE_SIZE    16u

// Where the trust table of the master's bootloader sits in flash (the linker script of the bootloader places it at 0x200 of the binary).
#define SLAVE_ATTEST_TRUST_TABLE_ADDR 0x08000200UL

// The trusted keys, taken from a trust table (same rules as the bootloader's Trust_Parse: "TKEY", schema 1, CRC-32, key ids that are the hash of their keys,
// revoked keys skipped). Returns how many keys it kept (0 for a closed table or for any doubt, in which case nothing is ever confirmed).
uint8_t SlaveAttest_SetKeysFromTable(const uint8_t *table, uint32_t available);
// Tests and tools that already hold the keys.
void SlaveAttest_SetKeys(const uint8_t keys[][URTC_ED25519_PUBLIC_KEY_SIZE], uint8_t count);
uint8_t SlaveAttest_KeyCount(void);

// A new update of the slave begins (0x211): forget the previous one.
void SlaveAttest_Start(uint32_t declared_size, uint32_t hardware_id);
// One piece of image data (0x213) and whether the bridge managed to relay it to the slave.
void SlaveAttest_Data(const uint8_t *data, uint8_t length, uint8_t relayed);
// One chunk of the record (0x21A): the index (0 to 10) and the 7 bytes after it.
void SlaveAttest_RecordChunk(uint8_t index, const uint8_t bytes[SLAVE_ATTEST_CHUNK_BYTES]);
// The end of the update (0x214): may the slave be told to install the image? 1 only when every byte arrived and was relayed, the record is complete and
// verifies under a trusted key over the header (hardware id of the slave, size, CRC, version) and the SHA-256 of what was relayed.
uint8_t SlaveAttest_Verify(uint32_t crc, uint16_t version_major, uint16_t version_minor_field);
// After a successful Verify: the 32-byte confirmation for the session whose nonce the slave published in REG_CHALLENGE. Returns 0 when Verify did not succeed.
uint8_t SlaveAttest_Tag(const uint8_t nonce[16], uint8_t tag[32]);

// Loads the keys from the trust table of the master's bootloader (firmware_slave_attest_flash.c); the bridge calls it at the start of every update.
void SlaveAttest_LoadTrustFromBootloader(void);

// Plain SHA-256 of a buffer, exported for the tests (the module has its own, the application has no other).
void SlaveAttest_Sha256(const uint8_t *data, uint32_t length, uint8_t out[32]);

#endif // FIRMWARE_SLAVE_ATTEST_H

// =============================================================================
// URTC Bootloader - the trust table: the public keys that may sign an application
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The only thing the bootloader holds to decide who may sign an image: one or two PUBLIC Ed25519 keys, in a table that the linker places at a fixed offset of
// the binary (section .trust_keys, 0x200 from the start of flash) so that a tool can read it without disassembling anything. The table is generated from the
// public file keys/image_trust.json (tools/image_trust.py headers); there are two flavours, chosen at build time:
//
//   -DURTC_TRUST_DEV   the bench table: its key's seed is public, a bootloader built with it is never an official one
//   (nothing)          the release table: closed (no key, nothing installs) until the owner puts the image-signing key in the public file
//
// Layout, little endian: "TKEY" | schema 1 | number of keys | flags 0 (u16) | per key: key id (8) | public key (32) | flags (1: current, 2: next, 4: revoked)
// | 3 zero bytes | CRC-32 (zlib) of all of it. Any doubt (a bad CRC, an unknown schema or flag, a key id that is not the hash of its key) closes the table: the
// bootloader then trusts nobody and installs nothing. A revoked key stays in the table but is never trusted.
#ifndef BOOTLOADER_TRUST_H
#define BOOTLOADER_TRUST_H

#include <stdint.h>

#include "urtc_ed25519.h"

#define TRUST_MAX_KEYS        2u
#define TRUST_KEY_ENTRY_SIZE  44u
#define TRUST_KEY_ID_SIZE     8u
#define TRUST_FLAG_CURRENT    1u
#define TRUST_FLAG_NEXT       2u
#define TRUST_FLAG_REVOKED    4u

typedef struct {
    uint8_t count;                                                  // trusted keys (revoked ones are not counted); 0 means the bootloader trusts nobody
    uint8_t flags[TRUST_MAX_KEYS];
    uint8_t key_id[TRUST_MAX_KEYS][TRUST_KEY_ID_SIZE];
    uint8_t keys[TRUST_MAX_KEYS][URTC_ED25519_PUBLIC_KEY_SIZE];     // the current key first
} TrustKeys_t;

// Decodes and validates a table of `available` bytes; returns the number of trusted keys (0 for a closed table or for any doubt). `out` is always filled.
uint8_t Trust_Parse(const uint8_t *table, uint32_t available, TrustKeys_t *out);

// The keys of the table compiled into this bootloader, decoded once.
const TrustKeys_t *Trust_Active(void);

// The floor of the rollback counter compiled into this bootloader: nothing signed with a lower counter installs, whatever the metadata says.
uint32_t Trust_MinCounter(void);

#ifdef URTC_HOST_TEST
// Host tests only: use `table` (length bytes) instead of the compiled-in one; NULL goes back to the compiled-in table.
void Trust_UseTableForTest(const uint8_t *table, uint32_t length);
#endif

#endif // BOOTLOADER_TRUST_H

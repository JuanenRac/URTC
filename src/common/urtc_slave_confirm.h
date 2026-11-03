// =============================================================================
// URTC - the confirmation that the master gives the expansion slave bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The slave bootloader has no room for an Ed25519 verifier. The master verifies the signature record of the slave image and then tells the slave so with a
// 32-byte tag over a message that both sides build with this header (each hashes it with its own SHA-256). The message binds the confirmation to ONE update
// session (a nonce the slave made when the update started and that the master read from REG_CHALLENGE) and to ONE image (its SHA-256, its hardware id, its
// version and its size). The slave rebuilds the message from its own values, hashes it and compares the result with the tag it received.
//
// What this proves and what it does not (see docs/SLAVE_BOOT_CONFIRMATION.md): the tag is a hash, not a MAC, because there is no secret to share. It
// catches a master that never confirmed (the slave fails closed), a confirmation for another image, another session or an image altered after the check,
// and a replay of an old confirmation. It does NOT stop a device on the link bus that reads the nonce and computes the tag itself: the slave trusts the
// master through the bus.
#ifndef URTC_SLAVE_CONFIRM_H
#define URTC_SLAVE_CONFIRM_H

#include <stdint.h>
#include <string.h>

#define URTC_SLAVE_CONFIRM_NONCE_SIZE   16u
#define URTC_SLAVE_CONFIRM_DIGEST_SIZE  32u
#define URTC_SLAVE_CONFIRM_TAG_SIZE     32u
#define URTC_SLAVE_CONFIRM_MESSAGE_SIZE 76u   // domain (16) | nonce (16) | image SHA-256 (32) | hardware id (4) | major (2) | minor field (2) | size (4)

static const uint8_t URTC_SLAVE_CONFIRM_DOMAIN[16] = {'U', 'R', 'T', 'C', '-', 'S', 'L', 'V', '-', 'C', 'O', 'N', 'F', '-', 'V', '1'};

static inline void urtc_slave_confirm_message(uint8_t out[URTC_SLAVE_CONFIRM_MESSAGE_SIZE],
                                              const uint8_t nonce[URTC_SLAVE_CONFIRM_NONCE_SIZE],
                                              const uint8_t digest[URTC_SLAVE_CONFIRM_DIGEST_SIZE],
                                              uint32_t hardware_id, uint16_t version_major, uint16_t version_minor_field, uint32_t size)
{
    memcpy(out, URTC_SLAVE_CONFIRM_DOMAIN, 16);
    memcpy(out + 16, nonce, URTC_SLAVE_CONFIRM_NONCE_SIZE);
    memcpy(out + 32, digest, URTC_SLAVE_CONFIRM_DIGEST_SIZE);
    out[64] = (uint8_t)(hardware_id >> 24); out[65] = (uint8_t)(hardware_id >> 16); out[66] = (uint8_t)(hardware_id >> 8); out[67] = (uint8_t)hardware_id;
    out[68] = (uint8_t)(version_major >> 8); out[69] = (uint8_t)version_major;
    out[70] = (uint8_t)(version_minor_field >> 8); out[71] = (uint8_t)version_minor_field;
    out[72] = (uint8_t)(size >> 24); out[73] = (uint8_t)(size >> 16); out[74] = (uint8_t)(size >> 8); out[75] = (uint8_t)size;
}

#endif // URTC_SLAVE_CONFIRM_H

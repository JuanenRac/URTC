// =============================================================================
// URTC-COMMON - firmware_base/urtc_bl_serial.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] portable C held to the shared vectors on the host (conformance/bl_serial_vectors.json). The programmed serial number of a bootloader: how the
// signed command that writes it is read, and how the record that keeps it is laid out and chosen.
//
// The command travels on 0x7EE in 12 chunks of 7 bytes (index, then 7 data bytes; 84 bytes in all):
//     target (4)  | key id (8) | serial (4) | counter (4) | signature (64)         all big-endian
// `target` is the serial the head answers with today (the fold of its unique ID, or the programmed one): a head that is not the target stays silent, so
// several heads on one bus do not answer each other. What the signature covers is a fixed 44-byte message that names the 96-bit unique ID of the head, never
// the target field: "URTC-SERIAL-V1\0\0" (16) | unique ID (12, three words, each big-endian) | serial (4) | counter (4) | key id (8). A command signed for another
// head fails; one signed for this head with a counter that is not higher than the stored one is a replay and is refused.
//
// The record that keeps the serial is 16 bytes in one of two slots (the 32 bytes that stay free in the page of the metadata): magic "URS1", serial, the
// complement of the serial, the counter (four little-endian words). The magic is programmed last, so a write cut by a power loss leaves no record. The
// newest valid slot (the highest counter) is the serial. A page that is erased to recycle its log carries the newest record over.
#ifndef URTC_BL_SERIAL_H
#define URTC_BL_SERIAL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "urtc_bl_select.h"
#include "urtc_ed25519.h"

#define URTC_SERIAL_CHUNKS        12u
#define URTC_SERIAL_CHUNK_BYTES   7u
#define URTC_SERIAL_COMMAND_SIZE  84u
#define URTC_SERIAL_MESSAGE_SIZE  44u
#define URTC_SERIAL_DOMAIN_SIZE   16u
#define URTC_SERIAL_KEY_ID_SIZE   8u
#define URTC_SERIAL_SLOT_BYTES    16u
#define URTC_SERIAL_SLOT_COUNT    2u
#define URTC_SERIAL_ALL_CHUNKS    0x0FFFu

typedef enum {
    URTC_SERIAL_OK = 0,
    URTC_SERIAL_BAD_SIGNATURE = 1,
    URTC_SERIAL_UNKNOWN_KEY = 2,
    URTC_SERIAL_REPLAY = 3,          // the counter is not higher than the one stored
    URTC_SERIAL_BAD_SERIAL = 4,      // 0 and 0xFFFFFFFF are not serials
    URTC_SERIAL_FLASH_FAILED = 5,
    URTC_SERIAL_DISABLED = 6,        // this bootloader was built without the programmable serial
    URTC_SERIAL_BUSY = 7,            // an update is in progress
    URTC_SERIAL_NOT_FOR_ME = 8       // the target is another head: no answer is sent
} urtc_serial_status_t;

static const uint8_t URTC_SERIAL_DOMAIN[URTC_SERIAL_DOMAIN_SIZE] = {'U', 'R', 'T', 'C', '-', 'S', 'E', 'R', 'I', 'A', 'L', '-', 'V', '1', 0, 0};

static inline void urtc_serial_put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline uint32_t urtc_serial_get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static inline void urtc_serial_message(uint8_t out[URTC_SERIAL_MESSAGE_SIZE], const uint32_t uid[3], uint32_t serial, uint32_t counter, const uint8_t key_id[URTC_SERIAL_KEY_ID_SIZE])
{
    memcpy(out, URTC_SERIAL_DOMAIN, URTC_SERIAL_DOMAIN_SIZE);
    urtc_serial_put32(out + 16, uid[0]);
    urtc_serial_put32(out + 20, uid[1]);
    urtc_serial_put32(out + 24, uid[2]);
    urtc_serial_put32(out + 28, serial);
    urtc_serial_put32(out + 32, counter);
    memcpy(out + 36, key_id, URTC_SERIAL_KEY_ID_SIZE);
}

// May this command write the serial? On URTC_SERIAL_OK, *serial and *counter are what to store. `enabled` is the build switch, `busy` an update in progress,
// `stored_counter` the counter of the record now kept (0 when there is none), so the first command needs a counter of 1 or more.
static inline urtc_serial_status_t urtc_serial_decide(const uint8_t command[URTC_SERIAL_COMMAND_SIZE], const uint32_t uid[3], uint32_t current_serial,
                                                     const uint8_t (*keys)[URTC_ED25519_PUBLIC_KEY_SIZE], uint8_t key_count, uint32_t stored_counter,
                                                     int enabled, int busy, uint32_t *serial, uint32_t *counter)
{
    if (urtc_serial_get32(command) != current_serial) {
        return URTC_SERIAL_NOT_FOR_ME;
    }
    if (!enabled) {
        return URTC_SERIAL_DISABLED;
    }
    if (busy) {
        return URTC_SERIAL_BUSY;
    }
    const uint32_t new_serial = urtc_serial_get32(command + 12);
    const uint32_t new_counter = urtc_serial_get32(command + 16);
    const uint8_t *key = NULL;
    for (uint8_t i = 0; keys != NULL && i < key_count && key == NULL; i++) {
        uint8_t digest[URTC_SHA512_DIGEST_SIZE];
        urtc_sha512(digest, keys[i], URTC_ED25519_PUBLIC_KEY_SIZE);
        if (memcmp(digest, command + 4, URTC_SERIAL_KEY_ID_SIZE) == 0) {
            key = keys[i];
        }
    }
    if (key == NULL) {
        return URTC_SERIAL_UNKNOWN_KEY;
    }
    uint8_t message[URTC_SERIAL_MESSAGE_SIZE];
    urtc_serial_message(message, uid, new_serial, new_counter, command + 4);
    if (urtc_ed25519_verify(command + 20, message, sizeof message, key) != 0) {
        return URTC_SERIAL_BAD_SIGNATURE;
    }
    if (!urtc_bl_serial_is_valid(new_serial)) {
        return URTC_SERIAL_BAD_SERIAL;
    }
    if (new_counter <= stored_counter) {
        return URTC_SERIAL_REPLAY;
    }
    *serial = new_serial;
    *counter = new_counter;
    return URTC_SERIAL_OK;
}

// ---- the record in flash ---------------------------------------------------------------------------------------------------------------------------------
static inline void urtc_serial_slot_pack(uint32_t serial, uint32_t counter, uint32_t words[4])
{
    words[0] = URTC_BL_SERIAL_MAGIC;
    words[1] = serial;
    words[2] = (uint32_t)~serial;
    words[3] = counter;
}

// 1 when the four words are a complete record (magic, a usable serial, its complement). The counter is trusted: it is programmed before the magic.
static inline int urtc_serial_slot_valid(const uint32_t words[4])
{
    return urtc_bl_serial_from_record(words[0], words[1], words[2]) != 0u;
}

static inline int urtc_serial_slot_blank(const uint32_t words[4])
{
    return words[0] == 0xFFFFFFFFu && words[1] == 0xFFFFFFFFu && words[2] == 0xFFFFFFFFu && words[3] == 0xFFFFFFFFu;
}

// The newest valid record of the two slots (the highest counter; the first slot on a tie): 1 and the serial and counter, or 0 when neither is valid.
static inline int urtc_serial_pick(const uint32_t a[4], const uint32_t b[4], uint32_t *serial, uint32_t *counter)
{
    const int va = urtc_serial_slot_valid(a);
    const int vb = urtc_serial_slot_valid(b);
    const uint32_t *best = NULL;
    if (va && vb) {
        best = b[3] > a[3] ? b : a;
    } else if (va) {
        best = a;
    } else if (vb) {
        best = b;
    }
    if (best == NULL) {
        return 0;
    }
    *serial = best[1];
    *counter = best[3];
    return 1;
}

#endif // URTC_BL_SERIAL_H

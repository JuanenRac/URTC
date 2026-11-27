// =============================================================================
// URTC-COMMON - firmware_base/urtc_version_compat.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] portable C held to the shared vectors on the host (conformance/version_compat_vectors.json); the twin of src/urtc_common/version_compat.py.
// How a bootloader reads an image that declares no PATCH number, kept in its metadata and changed only by a signed command. It is OFF in the builds: nothing
// below is used by a bootloader built without it.
//
// The rule itself is the one of the version helpers: STRICT counts an image without a patch as patch 0 against an installed version that has one,
// ACCEPT_PATCHLESS accepts it against the same major.minor. With nothing stored the build option of the bootloader decides, and its default is STRICT. The
// signed rollback counter is not touched by any of this.
//
// The command travels on 0x7EE, the channel of the other signed command of the workshop, with its own range of chunk indexes (0x40 to 0x4B), 7 bytes each,
// 84 bytes in all:
//     target (4) | key id (8) | mode (4) | counter (4) | signature (64)                 all big-endian
// `target` is the serial the head answers with: a head that is not the target stays silent. The signature covers a 44-byte message that names the 96-bit
// unique ID of the head: "URTC-VERCOMPAT-1" (16) | unique ID (12) | mode (4) | counter (4) | key id (8). The counter must be higher than the stored one, so an
// old command cannot be sent again. A frame on 0x7EE whose first byte is 0x60 asks for the state without changing it.
// The answer (0x7EF, 8 bytes): 0xC5 | status | mode in force | flags (bit 0: a mode is stored) | stored counter (4).
//
// The record that keeps the setting is 16 bytes (magic "VCV1", mode, counter, CRC-32 of the three; little-endian words). Any doubt reads as "nothing stored".
#ifndef URTC_VERSION_COMPAT_H
#define URTC_VERSION_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "urtc_ed25519.h"

#define URTC_VC_CHUNKS          12u
#define URTC_VC_CHUNK_BYTES     7u
#define URTC_VC_CHUNK_BASE      0x40u    // index of the first chunk on 0x7EE; 0x00 to 0x0B belong to the serial command
#define URTC_VC_QUERY_INDEX     0x60u    // first byte of the frame that only asks for the state
#define URTC_VC_COMMAND_SIZE    84u
#define URTC_VC_MESSAGE_SIZE    44u
#define URTC_VC_DOMAIN_SIZE     16u
#define URTC_VC_KEY_ID_SIZE     8u
#define URTC_VC_ALL_CHUNKS      0x0FFFu
#define URTC_VC_ANSWER_SIZE     8u
#define URTC_VC_ANSWER_MARKER   0xC5u
#define URTC_VC_FLAG_STORED     0x01u

#define URTC_VC_MODE_NONE             0u   // nothing stored: the build option decides
#define URTC_VC_MODE_STRICT           1u
#define URTC_VC_MODE_ACCEPT_PATCHLESS 2u

#define URTC_VC_RECORD_SIZE     16u
#define URTC_VC_RECORD_MAGIC    0x31564356UL // "VCV1" as a little-endian word

// The link between the master and the expansion slave: a bootloader register of the slave that relaxes the rule for ONE update session. It is reset by every
// start of an update, so nothing is stored on the slave.
#define URTC_VC_SLAVE_REG       0x0Du
#define URTC_VC_SLAVE_ACCEPT    0xA5u    // the only value that relaxes; anything else keeps the rule of the build

typedef enum {
    URTC_VC_OK = 0,
    URTC_VC_BAD_SIGNATURE = 1,
    URTC_VC_UNKNOWN_KEY = 2,
    URTC_VC_REPLAY = 3,          // the counter is not higher than the one stored
    URTC_VC_BAD_MODE = 4,
    URTC_VC_FLASH_FAILED = 5,
    URTC_VC_DISABLED = 6,
    URTC_VC_BUSY = 7,            // an update is in progress
    URTC_VC_NOT_FOR_ME = 8       // the target is another head: no answer is sent
} urtc_vc_status_t;

static const uint8_t URTC_VC_DOMAIN[URTC_VC_DOMAIN_SIZE] = {'U', 'R', 'T', 'C', '-', 'V', 'E', 'R', 'C', 'O', 'M', 'P', 'A', 'T', '-', '1'};

static inline void urtc_vc_put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline uint32_t urtc_vc_get_be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]; }
static inline void urtc_vc_put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static inline uint32_t urtc_vc_get_le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static inline int urtc_vc_mode_is_valid(uint32_t mode) { return mode == URTC_VC_MODE_STRICT || mode == URTC_VC_MODE_ACCEPT_PATCHLESS; }

// The mode in force: the stored one, or the one of the build when nothing (or nothing usable) is stored.
static inline uint32_t urtc_vc_effective_mode(uint32_t stored_mode, int build_unknown_is_zero)
{
    if (urtc_vc_mode_is_valid(stored_mode)) {
        return stored_mode;
    }
    return build_unknown_is_zero ? URTC_VC_MODE_STRICT : URTC_VC_MODE_ACCEPT_PATCHLESS;
}

// The `unknown_is_zero` argument of the version comparison for a mode in force.
static inline int urtc_vc_unknown_is_zero(uint32_t effective_mode) { return effective_mode != URTC_VC_MODE_ACCEPT_PATCHLESS; }

static inline void urtc_vc_message(uint8_t out[URTC_VC_MESSAGE_SIZE], const uint32_t uid[3], uint32_t mode, uint32_t counter, const uint8_t key_id[URTC_VC_KEY_ID_SIZE])
{
    memcpy(out, URTC_VC_DOMAIN, URTC_VC_DOMAIN_SIZE);
    urtc_vc_put_be32(out + 16, uid[0]);
    urtc_vc_put_be32(out + 20, uid[1]);
    urtc_vc_put_be32(out + 24, uid[2]);
    urtc_vc_put_be32(out + 28, mode);
    urtc_vc_put_be32(out + 32, counter);
    memcpy(out + 36, key_id, URTC_VC_KEY_ID_SIZE);
}

// May this command change the setting? On URTC_VC_OK, *mode and *counter are what to store; otherwise they are not written. `enabled` is the build switch,
// `busy` an update in progress, `stored_counter` the counter of the record now kept (0 when there is none), so the first command needs 1 or more.
static inline urtc_vc_status_t urtc_vc_decide(const uint8_t command[URTC_VC_COMMAND_SIZE], const uint32_t uid[3], uint32_t current_serial,
                                              const uint8_t (*keys)[URTC_ED25519_PUBLIC_KEY_SIZE], uint8_t key_count, uint32_t stored_counter,
                                              int enabled, int busy, uint32_t *mode, uint32_t *counter)
{
    if (urtc_vc_get_be32(command) != current_serial) {
        return URTC_VC_NOT_FOR_ME;
    }
    if (!enabled) {
        return URTC_VC_DISABLED;
    }
    if (busy) {
        return URTC_VC_BUSY;
    }
    const uint32_t new_mode = urtc_vc_get_be32(command + 12);
    const uint32_t new_counter = urtc_vc_get_be32(command + 16);
    const uint8_t *key = NULL;
    for (uint8_t i = 0; keys != NULL && i < key_count && key == NULL; i++) {
        uint8_t digest[URTC_SHA512_DIGEST_SIZE];
        urtc_sha512(digest, keys[i], URTC_ED25519_PUBLIC_KEY_SIZE);
        if (memcmp(digest, command + 4, URTC_VC_KEY_ID_SIZE) == 0) {
            key = keys[i];
        }
    }
    if (key == NULL) {
        return URTC_VC_UNKNOWN_KEY;
    }
    uint8_t message[URTC_VC_MESSAGE_SIZE];
    urtc_vc_message(message, uid, new_mode, new_counter, command + 4);
    if (urtc_ed25519_verify(command + 20, message, sizeof message, key) != 0) {
        return URTC_VC_BAD_SIGNATURE;
    }
    if (!urtc_vc_mode_is_valid(new_mode)) {
        return URTC_VC_BAD_MODE;
    }
    if (new_counter <= stored_counter) {
        return URTC_VC_REPLAY;
    }
    *mode = new_mode;
    *counter = new_counter;
    return URTC_VC_OK;
}

// ---- the record in flash ---------------------------------------------------------------------------------------------------------------------------------
static inline uint32_t urtc_vc_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (0xEDB88320UL & (0u - (crc & 1u)));
    }
    return ~crc;
}

static inline void urtc_vc_record_pack(uint32_t mode, uint32_t counter, uint8_t out[URTC_VC_RECORD_SIZE])
{
    urtc_vc_put_le32(out, URTC_VC_RECORD_MAGIC);
    urtc_vc_put_le32(out + 4, mode);
    urtc_vc_put_le32(out + 8, counter);
    urtc_vc_put_le32(out + 12, urtc_vc_crc32(out, 12u));
}

// 1 and the mode and counter of a complete record; 0 (both 0) for a blank, damaged or unknown one.
static inline int urtc_vc_record_read(const uint8_t record[URTC_VC_RECORD_SIZE], uint32_t *mode, uint32_t *counter)
{
    *mode = URTC_VC_MODE_NONE;
    *counter = 0u;
    if (urtc_vc_get_le32(record) != URTC_VC_RECORD_MAGIC || urtc_vc_get_le32(record + 12) != urtc_vc_crc32(record, 12u)) {
        return 0;
    }
    if (!urtc_vc_mode_is_valid(urtc_vc_get_le32(record + 4))) {
        return 0;
    }
    *mode = urtc_vc_get_le32(record + 4);
    *counter = urtc_vc_get_le32(record + 8);
    return 1;
}

static inline void urtc_vc_answer_build(uint8_t out[URTC_VC_ANSWER_SIZE], urtc_vc_status_t status, uint32_t effective_mode, int stored, uint32_t counter)
{
    out[0] = (uint8_t)URTC_VC_ANSWER_MARKER;
    out[1] = (uint8_t)status;
    out[2] = (uint8_t)effective_mode;
    out[3] = stored ? (uint8_t)URTC_VC_FLAG_STORED : 0u;
    urtc_vc_put_be32(out + 4, counter);
}

#endif // URTC_VERSION_COMPAT_H

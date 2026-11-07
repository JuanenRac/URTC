// =============================================================================
// URTC-COMMON - firmware_base/urtc_key_rotation.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] portable C held to the shared vectors on the host (conformance/key_rotation_vectors.json); the twin of src/urtc_common/key_rotation.py.
// Nothing here is wired into a bootloader and nothing is built for the board: it is the decision a bootloader will take once the metadata fields and the clock
// of the window exist.
//
// A bootloader trusts a CURRENT key and, while a rotation is in progress, also the PREVIOUS one. Keys are named by their fingerprint (the first 8 bytes of the
// SHA-512 of the public key). The rules, all of them in urtc_kr_active():
//   - the window is a PARAMETER (seconds, kept with the start of the rotation), never a constant of this file; 0 means no overlap;
//   - inside [started_at, started_at + window) both fingerprints are valid; the previous one expires when the window ends and nothing renews it;
//   - outside the window, with a window of zero, or when the clock reads earlier than the start (fails closed), only the current one is valid;
//   - a revoked key is never valid, window or not; a revoked current key leaves nothing trusted;
//   - a fingerprint listed twice counts once; never more than two are announced;
//   - the policy is untouched: a rotation only decides WHICH Ed25519 keys verify, it never opens another path.
// Times are monotonic seconds in one unit for both started_at and now.
//
// The announcement: up to two 8-byte frames on 0x7ED (current key first) and a 7-byte status on 0x7EB: 05, number of keys, flags, seconds left (big-endian,
// 0xFFFFFFFF when there is no window).
#ifndef URTC_KEY_ROTATION_H
#define URTC_KEY_ROTATION_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define URTC_KR_KEY_ID_SIZE     8u
#define URTC_KR_MAX_ANNOUNCED   2u
#define URTC_KR_STATUS_OP       0x05u
#define URTC_KR_STATUS_SIZE     7u
#define URTC_KR_NO_WINDOW       0xFFFFFFFFu
#define URTC_KR_FLAG_ROTATING   0x01u   /* a previous key is still valid */
#define URTC_KR_FLAG_CURRENT_REVOKED 0x02u

typedef struct {
    uint8_t current[URTC_KR_KEY_ID_SIZE];
    uint8_t previous[URTC_KR_KEY_ID_SIZE];
    uint8_t has_previous;                       /* 0: previous[] is not used */
    uint32_t window_s;                          /* seconds; 0 = no overlap */
    uint32_t started_at;                        /* same monotonic seconds as `now` */
    const uint8_t (*revoked)[URTC_KR_KEY_ID_SIZE];
    size_t revoked_count;
} urtc_rotation_t;

static inline int urtc_kr_same(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, URTC_KR_KEY_ID_SIZE) == 0;
}

static inline int urtc_kr_is_revoked(const urtc_rotation_t *rot, const uint8_t *kid)
{
    for (size_t i = 0; rot->revoked != NULL && i < rot->revoked_count; i++) {
        if (urtc_kr_same(rot->revoked[i], kid)) {
            return 1;
        }
    }
    return 0;
}

static inline int urtc_kr_in_window(uint32_t window_s, uint32_t started_at, uint32_t now)
{
    if (window_s == 0u || now < started_at) {
        return 0;
    }
    return (uint32_t)(now - started_at) < window_s;
}

/* Writes the fingerprints valid at `now` (current first) and returns how many (0 to 2). */
static inline size_t urtc_kr_active(const urtc_rotation_t *rot, uint32_t now, uint8_t out[URTC_KR_MAX_ANNOUNCED][URTC_KR_KEY_ID_SIZE])
{
    size_t n = 0;
    const int current_revoked = urtc_kr_is_revoked(rot, rot->current);
    if (!current_revoked) {
        memcpy(out[n++], rot->current, URTC_KR_KEY_ID_SIZE);
    }
    if (rot->has_previous && !current_revoked && !urtc_kr_is_revoked(rot, rot->previous) && !urtc_kr_same(rot->previous, rot->current) &&
        urtc_kr_in_window(rot->window_s, rot->started_at, now)) {
        memcpy(out[n++], rot->previous, URTC_KR_KEY_ID_SIZE);
    }
    return n;
}

static inline int urtc_kr_is_accepted(const urtc_rotation_t *rot, uint32_t now, const uint8_t *kid, size_t kid_len)
{
    uint8_t ids[URTC_KR_MAX_ANNOUNCED][URTC_KR_KEY_ID_SIZE];
    if (kid == NULL || kid_len != URTC_KR_KEY_ID_SIZE) {
        return 0;
    }
    const size_t n = urtc_kr_active(rot, now, ids);
    for (size_t i = 0; i < n; i++) {
        if (urtc_kr_same(ids[i], kid)) {
            return 1;
        }
    }
    return 0;
}

/* Seconds the previous key stays valid, or URTC_KR_NO_WINDOW when there is no overlap. */
static inline uint32_t urtc_kr_seconds_left(const urtc_rotation_t *rot, uint32_t now)
{
    uint8_t ids[URTC_KR_MAX_ANNOUNCED][URTC_KR_KEY_ID_SIZE];
    if (!rot->has_previous || urtc_kr_active(rot, now, ids) < 2u) {
        return URTC_KR_NO_WINDOW;
    }
    const uint64_t left = (uint64_t)rot->started_at + rot->window_s - now;   /* in 64 bits: started_at + window may pass 2^32 */
    return left > (uint64_t)(URTC_KR_NO_WINDOW - 1u) ? URTC_KR_NO_WINDOW - 1u : (uint32_t)left;
}

/* The answer for `now`: the 0x7ED frames (returned count) and the 7-byte payload for 0x7EB. No active key: zero frames (a host reads "no data"). */
static inline size_t urtc_kr_announce(const urtc_rotation_t *rot, uint32_t now, uint8_t frames[URTC_KR_MAX_ANNOUNCED][URTC_KR_KEY_ID_SIZE],
                                      uint8_t status[URTC_KR_STATUS_SIZE])
{
    const size_t n = urtc_kr_active(rot, now, frames);
    const uint32_t left = urtc_kr_seconds_left(rot, now);
    status[0] = (uint8_t)URTC_KR_STATUS_OP;
    status[1] = (uint8_t)n;
    status[2] = (uint8_t)((n == 2u ? URTC_KR_FLAG_ROTATING : 0u) | (urtc_kr_is_revoked(rot, rot->current) ? URTC_KR_FLAG_CURRENT_REVOKED : 0u));
    status[3] = (uint8_t)(left >> 24);
    status[4] = (uint8_t)(left >> 16);
    status[5] = (uint8_t)(left >> 8);
    status[6] = (uint8_t)left;
    return n;
}

/* Reads a status payload; returns 1 and fills the outputs, or 0 for anything that is not a well-formed status. */
static inline int urtc_kr_parse_status(const uint8_t *payload, size_t len, uint8_t *count, uint8_t *flags, uint32_t *seconds_left)
{
    if (payload == NULL || len != URTC_KR_STATUS_SIZE || payload[0] != URTC_KR_STATUS_OP || payload[1] > URTC_KR_MAX_ANNOUNCED ||
        (payload[2] & (uint8_t)~(URTC_KR_FLAG_ROTATING | URTC_KR_FLAG_CURRENT_REVOKED)) != 0u) {
        return 0;
    }
    *count = payload[1];
    *flags = payload[2];
    *seconds_left = ((uint32_t)payload[3] << 24) | ((uint32_t)payload[4] << 16) | ((uint32_t)payload[5] << 8) | (uint32_t)payload[6];
    return 1;
}

#endif /* URTC_KEY_ROTATION_H */

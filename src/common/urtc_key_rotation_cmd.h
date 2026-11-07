// =============================================================================
// URTC-COMMON - firmware_base/urtc_key_rotation_cmd.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] portable C held to the shared vectors on the host (conformance/key_rotation_command_vectors.json); the twin of
// src/urtc_common/key_rotation_command.py. Nothing here is wired into a bootloader and nothing is built for the board.
//
// The signed command that starts a rotation of the signing key, with the time of the HOST: the host signs, the bootloader only verifies. The record (big-endian,
// 100 bytes): signer id (8) | new current id (8) | previous id (8, zeros: none) | window seconds (4) | host time (4) | counter (4) | Ed25519 signature (64). The
// signature covers URTC_KRC_DOMAIN and the first 36 bytes. The first rule that fails decides the reason (urtc_krc_decide documents the order).
#ifndef URTC_KEY_ROTATION_CMD_H
#define URTC_KEY_ROTATION_CMD_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "urtc_ed25519.h"
#include "urtc_key_rotation.h"

#define URTC_KRC_RECORD_SIZE 100u
#define URTC_KRC_BODY_SIZE   36u
#define URTC_KRC_MAX_KEYS    2u
#define URTC_KRC_DOMAIN_SIZE 19u

typedef enum {
    URTC_KRC_OK = 0,
    URTC_KRC_DISABLED,
    URTC_KRC_MALFORMED,
    URTC_KRC_UNKNOWN_SIGNER,
    URTC_KRC_SIGNER_NOT_CURRENT,
    URTC_KRC_BAD_SIGNATURE,
    URTC_KRC_REPLAYED_COUNTER,
    URTC_KRC_TIME_BACKWARDS,
    URTC_KRC_UNKNOWN_NEW_KEY,
    URTC_KRC_REVOKED_NEW_KEY,
    URTC_KRC_BAD_PREVIOUS,
    URTC_KRC_WINDOW_TOO_LONG
} urtc_krc_result_t;

typedef struct {
    urtc_rotation_t rotation;
    uint8_t key_id[URTC_KRC_MAX_KEYS][URTC_KR_KEY_ID_SIZE];
    uint8_t key_public[URTC_KRC_MAX_KEYS][URTC_ED25519_PUBLIC_KEY_SIZE];
    size_t key_count;
    uint32_t last_counter;
    uint32_t last_host_time;
    uint32_t clock_offset;     /* modulo 2^32: now = uptime + clock_offset */
    uint8_t allow;             /* 0: the option is off (the default) */
    uint32_t max_window_s;
} urtc_krc_state_t;

static inline uint32_t urtc_krc_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline int urtc_krc_find(const urtc_krc_state_t *st, const uint8_t *kid)
{
    for (size_t i = 0; i < st->key_count && i < URTC_KRC_MAX_KEYS; i++) {
        if (urtc_kr_same(st->key_id[i], kid)) {
            return (int)i;
        }
    }
    return -1;
}

/* Order of the checks: disabled, malformed, unknown signer, signer not the current key (or revoked), bad signature, replayed counter, time backwards (or zero),
 * unknown new key, revoked new key, bad previous, window too long. On URTC_KRC_OK `out` is the new state; otherwise `out` is a copy of `st`. */
static inline urtc_krc_result_t urtc_krc_decide(const urtc_krc_state_t *st, const uint8_t *record, size_t length, uint32_t uptime_s, urtc_krc_state_t *out)
{
    static const uint8_t domain[URTC_KRC_DOMAIN_SIZE] = {'U', 'R', 'T', 'C', '-', 'K', 'E', 'Y', '-', 'R', 'O', 'T', 'A', 'T', 'E', '-', 'V', '1', 0};
    static const uint8_t none[URTC_KR_KEY_ID_SIZE] = {0};
    uint8_t message[URTC_KRC_DOMAIN_SIZE + URTC_KRC_BODY_SIZE];
    *out = *st;
    if (!st->allow) {
        return URTC_KRC_DISABLED;
    }
    if (record == NULL || length != URTC_KRC_RECORD_SIZE) {
        return URTC_KRC_MALFORMED;
    }
    const uint8_t *signer = record;
    const uint8_t *new_current = record + 8;
    const uint8_t *previous = record + 16;
    const uint32_t window_s = urtc_krc_be32(record + 24);
    const uint32_t host_time = urtc_krc_be32(record + 28);
    const uint32_t counter = urtc_krc_be32(record + 32);
    const int signer_at = urtc_krc_find(st, signer);
    if (signer_at < 0) {
        return URTC_KRC_UNKNOWN_SIGNER;
    }
    if (!urtc_kr_same(signer, st->rotation.current) || urtc_kr_is_revoked(&st->rotation, st->rotation.current)) {
        return URTC_KRC_SIGNER_NOT_CURRENT;
    }
    memcpy(message, domain, URTC_KRC_DOMAIN_SIZE);
    memcpy(message + URTC_KRC_DOMAIN_SIZE, record, URTC_KRC_BODY_SIZE);
    if (urtc_ed25519_verify(record + URTC_KRC_BODY_SIZE, message, sizeof message, st->key_public[signer_at]) != 0) {
        return URTC_KRC_BAD_SIGNATURE;
    }
    if (counter <= st->last_counter) {
        return URTC_KRC_REPLAYED_COUNTER;
    }
    if (host_time < st->last_host_time || host_time == 0u) {
        return URTC_KRC_TIME_BACKWARDS;
    }
    if (urtc_krc_find(st, new_current) < 0) {
        return URTC_KRC_UNKNOWN_NEW_KEY;
    }
    if (urtc_kr_is_revoked(&st->rotation, new_current)) {
        return URTC_KRC_REVOKED_NEW_KEY;
    }
    const int no_previous = urtc_kr_same(previous, none);
    if (urtc_kr_same(new_current, previous) || (!no_previous && !urtc_kr_same(previous, st->rotation.current))) {
        return URTC_KRC_BAD_PREVIOUS;
    }
    if (window_s > st->max_window_s) {
        return URTC_KRC_WINDOW_TOO_LONG;
    }
    const int same_key = urtc_kr_same(new_current, st->rotation.current);
    memcpy(out->rotation.current, new_current, URTC_KR_KEY_ID_SIZE);
    out->rotation.has_previous = (uint8_t)((no_previous || same_key) ? 0u : 1u);
    if (out->rotation.has_previous) {
        memcpy(out->rotation.previous, previous, URTC_KR_KEY_ID_SIZE);
    }
    out->rotation.window_s = out->rotation.has_previous ? window_s : 0u;
    out->rotation.started_at = host_time;
    out->last_counter = counter;
    out->last_host_time = host_time;
    out->clock_offset = (uint32_t)(host_time - uptime_s);
    return URTC_KRC_OK;
}

#endif /* URTC_KEY_ROTATION_CMD_H */

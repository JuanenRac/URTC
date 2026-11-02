// =============================================================================
// URTC-COMMON - firmware_base/urtc_auth.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed: portable C held to the shared vectors on the host (conformance/signed_image_vectors.json). Nothing in a
// bootloader calls it yet. The decision a bootloader takes at the end of an update, after it has the image in the backup slot: may it be installed?
//
// What is signed is a fixed 88-byte record, never the image: "URTC-IMG-SIG-V1\0" (16), bytes 0..27 of the image header (28: hardware id, the four version
// components, payload length and CRC-32), the SHA-256 of the payload (32), the rollback counter (4, big-endian) and the key id (8: the first 8 bytes of the
// SHA-512 of the public key). The record that travels with the image is key id (8) | counter (4) | signature (64) = 76 bytes. The bootloader computes the
// payload SHA-256 by streaming over the flash (it already has a SHA-256), keeps one or two trusted public keys and a floor for the counter, and calls
// urtc_auth_decide(). The rules (the same as src/urtc_common/image_auth.py): the hardware id first, then the authentication by policy, then the rollback
// rules, so an image nobody vouched for reveals nothing about the installed version. The policy value 0 is the strict one (only Ed25519), so a zeroed
// structure fails closed.
#ifndef URTC_AUTH_H
#define URTC_AUTH_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "urtc_ed25519.h"

#define URTC_AUTH_DOMAIN_SIZE    16u
#define URTC_AUTH_HEADER_BYTES   28u
#define URTC_AUTH_HASH_SIZE      32u
#define URTC_AUTH_KEY_ID_SIZE    8u
#define URTC_AUTH_RECORD_SIZE    76u    // key id | counter | signature
#define URTC_AUTH_MESSAGE_SIZE   88u

typedef enum {
    URTC_AUTH_ED25519_ONLY = 0,   // only an Ed25519 signature installs an image (the default)
    URTC_AUTH_EITHER = 1,         // transition: an Ed25519 signature or the HMAC-SHA256 the bootloader already checks
    URTC_AUTH_HMAC_ONLY = 2       // today's behaviour
} urtc_auth_policy_t;

typedef enum {
    URTC_AUTH_OK = 0,
    URTC_AUTH_BAD_ARGS,
    URTC_AUTH_NOT_SIGNED,          // the policy needs a signature and the image has none
    URTC_AUTH_UNKNOWN_KEY,         // signed by a key this bootloader does not trust
    URTC_AUTH_BAD_SIGNATURE,
    URTC_AUTH_BAD_HMAC,
    URTC_AUTH_HARDWARE_MISMATCH,
    URTC_AUTH_ROLLBACK_COUNTER,    // the counter is below the floor: never overridable
    URTC_AUTH_ROLLBACK_VERSION     // an older version, and nobody authorised a downgrade
} urtc_auth_status_t;

typedef struct {
    urtc_auth_policy_t policy;
    const uint8_t (*keys)[URTC_ED25519_PUBLIC_KEY_SIZE];   // the trusted public keys (rotation: the current one and the next)
    uint8_t key_count;
    uint32_t hardware_id;                                   // this board
    uint32_t installed_counter;                             // the floor
    uint8_t installed_version_known;
    uint16_t installed_major, installed_build;              // the installed version, compared as the tuple (major, minor, patch, build) when known
    uint8_t installed_minor, installed_patch;
    uint8_t downgrade_authorized;
} urtc_auth_context_t;

typedef struct {
    const uint8_t *header28;       // URTC_AUTH_HEADER_BYTES
    const uint8_t *payload_sha256; // URTC_AUTH_HASH_SIZE
    const uint8_t *record;         // URTC_AUTH_RECORD_SIZE, or NULL when the image carries none
    uint8_t hmac_ok;               // the result of the HMAC-SHA256 check the bootloader already does
} urtc_auth_image_t;

static const uint8_t URTC_AUTH_DOMAIN[URTC_AUTH_DOMAIN_SIZE] = {'U', 'R', 'T', 'C', '-', 'I', 'M', 'G', '-', 'S', 'I', 'G', '-', 'V', '1', 0};

static inline uint32_t urtc_auth_get32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static inline void urtc_auth_key_id(const uint8_t public_key[URTC_ED25519_PUBLIC_KEY_SIZE], uint8_t out[URTC_AUTH_KEY_ID_SIZE])
{
    uint8_t digest[URTC_SHA512_DIGEST_SIZE];
    urtc_sha512(digest, public_key, URTC_ED25519_PUBLIC_KEY_SIZE);
    memcpy(out, digest, URTC_AUTH_KEY_ID_SIZE);
}

static inline void urtc_auth_message(uint8_t out[URTC_AUTH_MESSAGE_SIZE], const uint8_t *header28, const uint8_t *payload_sha256, const uint8_t *counter_and_key_id_record)
{
    // `record` is key id (8) | counter (4) | ...: the message holds the counter first, then the key id
    memcpy(out, URTC_AUTH_DOMAIN, URTC_AUTH_DOMAIN_SIZE);
    memcpy(out + 16, header28, URTC_AUTH_HEADER_BYTES);
    memcpy(out + 44, payload_sha256, URTC_AUTH_HASH_SIZE);
    memcpy(out + 76, counter_and_key_id_record + 8, 4);
    memcpy(out + 80, counter_and_key_id_record, URTC_AUTH_KEY_ID_SIZE);
}

// Version tuple of the header (bytes 12..17: major u16, minor, patch, build u16) against the installed one: 1 when the image is older.
static inline int urtc_auth_header_is_older(const uint8_t *header28, const urtc_auth_context_t *ctx)
{
    const uint32_t major = ((uint32_t)header28[12] << 8) | header28[13];
    const uint32_t build = ((uint32_t)header28[16] << 8) | header28[17];
    const uint32_t a[4] = {major, header28[14], header28[15], build};
    const uint32_t b[4] = {ctx->installed_major, ctx->installed_minor, ctx->installed_patch, ctx->installed_build};
    for (int i = 0; i < 4; i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i];
        }
    }
    return 0;
}

// May this image be installed? On URTC_AUTH_OK, *new_counter is the value to store as the new floor.
static inline urtc_auth_status_t urtc_auth_decide(const urtc_auth_context_t *ctx, const urtc_auth_image_t *image, uint32_t *new_counter)
{
    if (ctx == NULL || image == NULL || new_counter == NULL || image->header28 == NULL || image->payload_sha256 == NULL ||
        (ctx->key_count > 0u && ctx->keys == NULL) || (unsigned)ctx->policy > (unsigned)URTC_AUTH_HMAC_ONLY) {
        return URTC_AUTH_BAD_ARGS;
    }
    *new_counter = ctx->installed_counter;
    if (urtc_auth_get32(image->header28 + 8) != ctx->hardware_id) {
        return URTC_AUTH_HARDWARE_MISMATCH;
    }
    urtc_auth_status_t ed = URTC_AUTH_NOT_SIGNED;
    uint32_t counter = ctx->installed_counter;
    if (image->record != NULL) {
        counter = urtc_auth_get32(image->record + 8);
        const uint8_t *key = NULL;
        for (uint8_t i = 0; i < ctx->key_count && key == NULL; i++) {
            uint8_t kid[URTC_AUTH_KEY_ID_SIZE];
            urtc_auth_key_id(ctx->keys[i], kid);
            if (memcmp(kid, image->record, URTC_AUTH_KEY_ID_SIZE) == 0) {
                key = ctx->keys[i];
            }
        }
        if (key == NULL) {
            ed = URTC_AUTH_UNKNOWN_KEY;
        } else {
            uint8_t msg[URTC_AUTH_MESSAGE_SIZE];
            urtc_auth_message(msg, image->header28, image->payload_sha256, image->record);
            ed = urtc_ed25519_verify(image->record + 12, msg, sizeof msg, key) == 0 ? URTC_AUTH_OK : URTC_AUTH_BAD_SIGNATURE;
        }
    }
    urtc_auth_status_t status;
    int via_ed = 0;
    if (ctx->policy == URTC_AUTH_HMAC_ONLY) {
        status = image->hmac_ok ? URTC_AUTH_OK : URTC_AUTH_BAD_HMAC;
    } else if (ctx->policy == URTC_AUTH_ED25519_ONLY) {
        status = ed;
        via_ed = ed == URTC_AUTH_OK;
    } else if (ed == URTC_AUTH_OK) {
        status = URTC_AUTH_OK;
        via_ed = 1;
    } else {
        status = image->hmac_ok ? URTC_AUTH_OK : ed;
    }
    if (status != URTC_AUTH_OK) {
        return status;
    }
    if (via_ed && counter < ctx->installed_counter) {
        return URTC_AUTH_ROLLBACK_COUNTER;
    }
    if (ctx->installed_version_known && urtc_auth_header_is_older(image->header28, ctx) && !ctx->downgrade_authorized) {
        return URTC_AUTH_ROLLBACK_VERSION;
    }
    *new_counter = via_ed ? counter : ctx->installed_counter;
    return URTC_AUTH_OK;
}

#endif // URTC_AUTH_H

// =============================================================================
// URTC-COMMON - tests/c/test_bootloader_auth.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] sin compilar placa ni flashear: host tests of the portable C of the bootloader authentication. The Ed25519 verifier is held to RFC 8032 and to the
// shared vectors, the decision to install an image (urtc_auth.h) to the shared cases, and the addressing by serial (urtc_bl_select.h) to the shared scenarios.
// Built by tests/test_bootloader_auth.py with the host compiler and, where there is one, the address and undefined-behaviour sanitizers.
#include <stdio.h>
#include <string.h>

#include "bootloader_select_vectors.h"
#include "ed25519_vectors.h"
#include "signed_image_vectors.h"
#include "urtc_auth.h"
#include "urtc_bl_select.h"
#include "urtc_ed25519.h"
#include "urtc_test.h"

static void test_sha512(int *failures)
{
    uint8_t d[64];
    urtc_sha512(d, (const uint8_t *)"abc", 3);
    static const uint8_t abc_head[8] = {0xDD, 0xAF, 0x35, 0xA1, 0x93, 0x61, 0x7A, 0xBA};
    static const uint8_t abc_tail[8] = {0x2A, 0x9A, 0xC9, 0x4F, 0xA5, 0x4C, 0xA4, 0x9F};
    TEST_ASSERT(memcmp(d, abc_head, 8) == 0 && memcmp(d + 56, abc_tail, 8) == 0, "SHA-512 of abc (FIPS 180-4)");
    // incremental feeding gives the same digest as one call, around the block boundary
    uint8_t data[300];
    for (size_t i = 0; i < sizeof data; i++) {
        data[i] = (uint8_t)(i * 31u + 7u);
    }
    for (size_t split = 0; split <= 260; split += 37) {
        uint8_t one[64], two[64];
        urtc_sha512(one, data, 260);
        urtc_sha512_t ctx;
        urtc_sha512_init(&ctx);
        urtc_sha512_update(&ctx, data, split);
        urtc_sha512_update(&ctx, data + split, 260 - split);
        urtc_sha512_final(&ctx, two);
        TEST_ASSERT(memcmp(one, two, 64) == 0, "SHA-512 is incremental");
    }
}

static void test_ed25519(int *failures)
{
    for (size_t i = 0; i < ED25519_VECTOR_COUNT; i++) {
        const ed25519_vec_t *v = &ED25519_VECTORS[i];
        const int ok = urtc_ed25519_verify(v->signature, v->message, v->message_length, v->public_key) == 0;
        if (ok != v->valid) {
            printf("  FAIL: ed25519 %s: got %s\n", v->name, ok ? "accepted" : "refused");
            (*failures)++;
        }
    }
    TEST_ASSERT(urtc_ed25519_verify(NULL, NULL, 0, NULL) != 0, "missing arguments are refused");
    TEST_ASSERT(urtc_ed25519_verify(ED25519_VECTORS[0].signature, NULL, 4, ED25519_VECTORS[0].public_key) != 0, "a missing message of non-zero length is refused");
}

static void test_auth(int *failures)
{
    for (size_t i = 0; i < SIGNED_IMAGE_VECTOR_COUNT; i++) {
        const signed_image_vec_t *v = &SIGNED_IMAGE_VECTORS[i];
        uint8_t keys[3][32];
        uint8_t count = 0;
        for (unsigned k = 0; k < 3; k++) {
            if (v->trusted_mask & (1u << k)) {
                memcpy(keys[count++], SIGNED_IMAGE_KEYS[k], 32);
            }
        }
        urtc_auth_context_t ctx;
        memset(&ctx, 0, sizeof ctx);
        ctx.policy = (urtc_auth_policy_t)v->policy;
        ctx.keys = (const uint8_t (*)[32])keys;
        ctx.key_count = count;
        ctx.hardware_id = v->hardware_id;
        ctx.installed_counter = v->installed_counter;
        ctx.installed_version_known = (uint8_t)v->version_known;
        ctx.installed_major = v->major;
        ctx.installed_minor = v->minor;
        ctx.installed_patch = v->patch;
        ctx.installed_build = v->build;
        ctx.downgrade_authorized = (uint8_t)v->downgrade_authorized;
        urtc_auth_image_t image = {v->header, v->payload_sha256, v->record, (uint8_t)v->hmac_ok};
        uint32_t counter = 0xDEADBEEFu;
        const urtc_auth_status_t status = urtc_auth_decide(&ctx, &image, &counter);
        if ((int)status != v->status || counter != v->new_counter) {
            printf("  FAIL: auth %s: status %d counter %u, expected %d and %u\n", v->name, (int)status, (unsigned)counter, v->status, (unsigned)v->new_counter);
            (*failures)++;
        }
    }
    uint32_t counter = 0;
    urtc_auth_context_t zero;
    memset(&zero, 0, sizeof zero);
    TEST_ASSERT(zero.policy == URTC_AUTH_ED25519_ONLY, "a zeroed context asks for Ed25519 only");
    TEST_ASSERT(urtc_auth_decide(&zero, NULL, &counter) == URTC_AUTH_BAD_ARGS, "a missing image is refused");
    TEST_ASSERT(urtc_auth_decide(NULL, NULL, &counter) == URTC_AUTH_BAD_ARGS, "a missing context is refused");
    zero.policy = (urtc_auth_policy_t)9;
    urtc_auth_image_t any = {SIGNED_IMAGE_VECTORS[0].header, SIGNED_IMAGE_VECTORS[0].payload_sha256, NULL, 0};
    TEST_ASSERT(urtc_auth_decide(&zero, &any, &counter) == URTC_AUTH_BAD_ARGS, "an unknown policy is refused");
}

static void test_select(int *failures)
{
    for (size_t s = 0; s < SELECT_SCENARIO_COUNT; s++) {
        const select_scenario_t *sc = &SELECT_SCENARIOS[s];
        urtc_bl_node_t node;
        urtc_bl_node_init(&node, sc->uid[0], sc->uid[1], sc->uid[2]);
        if (node.serial != sc->serial) {
            printf("  FAIL: select %s: serial 0x%08X, expected 0x%08X\n", sc->name, (unsigned)node.serial, (unsigned)sc->serial);
            (*failures)++;
            continue;
        }
        for (size_t i = 0; i < sc->step_count; i++) {
            const select_step_t *st = &sc->steps[i];
            urtc_bl_reply_t reply;
            memset(&reply, 0, sizeof reply);
            int same = 1;
            if (st->is_gate) {
                same = urtc_bl_node_accepts(&node, st->t, st->id, st->busy) == st->accept;
            } else {
                urtc_bl_node_on_cmd(&node, st->t, st->data, st->dlc, st->busy, &reply);
                same = reply.dlc == st->reply_dlc && (reply.send != 0) == (st->reply_dlc != 0) && memcmp(reply.data, st->reply, 8) == 0 &&
                       (!reply.send || reply.id == URTC_BL_ID_RESP);
            }
            same = same && node.selected == st->selected && node.addressed == st->addressed;
            if (!same) {
                printf("  FAIL: select %s: step %u differs from the reference\n", sc->name, (unsigned)i);
                (*failures)++;
                break;
            }
        }
    }
    TEST_ASSERT(urtc_bl_serial_from_uid(1, 0, 0) == 1u && urtc_bl_serial_from_uid(0, 1, 0) == 0x800u && urtc_bl_serial_from_uid(0, 0, 1) == 0x400000u,
                "the serial folds the three words with rotations of 0, 11 and 22 bits");
}

int main(void)
{
    int count = 0;
    int *failures = &count;
    test_sha512(failures);
    test_ed25519(failures);
    test_auth(failures);
    test_select(failures);
    if (count == 0) {
        printf("all bootloader authentication tests pass\n");
        return 0;
    }
    printf("%d bootloader authentication test(s) failed\n", count);
    return 1;
}

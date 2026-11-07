// =============================================================================
// URTC-COMMON - tests/c/test_key_rotation.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed: host tests of the rotation of the signing key (firmware_base/urtc_key_rotation.h), held to the shared
// vectors (generated/key_rotation_vectors.h, written from the hand-made expectations of tools/make_key_rotation_vectors.py). Built by
// tests/test_key_rotation.py with the host compiler and, where there is one, the address and undefined-behaviour sanitizers.
#include <stdio.h>
#include <string.h>

#include "key_rotation_vectors.h"
#include "urtc_key_rotation.h"
#include "urtc_test.h"

static urtc_rotation_t rotation_of(const key_rotation_vec_t *v)
{
    urtc_rotation_t rot;
    memset(&rot, 0, sizeof rot);
    memcpy(rot.current, v->current, URTC_KR_KEY_ID_SIZE);
    memcpy(rot.previous, v->previous, URTC_KR_KEY_ID_SIZE);
    rot.has_previous = (uint8_t)v->has_previous;
    rot.window_s = v->window_s;
    rot.started_at = v->started_at;
    rot.revoked = v->revoked_count != 0u ? v->revoked : NULL;
    rot.revoked_count = v->revoked_count;
    return rot;
}

static void test_vectors(int *failures)
{
    for (size_t i = 0; i < KEY_ROTATION_VECTOR_COUNT; i++) {
        const key_rotation_vec_t *v = &KEY_ROTATION_VECTORS[i];
        const urtc_rotation_t rot = rotation_of(v);
        uint8_t frames[URTC_KR_MAX_ANNOUNCED][URTC_KR_KEY_ID_SIZE];
        uint8_t status[URTC_KR_STATUS_SIZE];
        const size_t n = urtc_kr_announce(&rot, v->now, frames, status);
        TEST_ASSERT(n == v->accepted_count, v->name);
        for (size_t k = 0; k < n && k < v->accepted_count; k++) {
            TEST_ASSERT(memcmp(frames[k], v->accepted[k], URTC_KR_KEY_ID_SIZE) == 0, "the announced fingerprints are the accepted ones, the current first");
        }
        uint8_t count = 0xEE, flags = 0xEE;
        uint32_t left = 0;
        TEST_ASSERT(urtc_kr_parse_status(status, sizeof status, &count, &flags, &left), "the status the bootloader writes reads back");
        TEST_ASSERT(count == v->accepted_count && flags == v->flags && left == v->seconds_left, "the status carries the count, the flags and the seconds left");
        TEST_ASSERT(status[2] == v->flags, "the flags byte is the one of the vector");
        for (size_t k = 0; k < v->accepted_count; k++) {
            TEST_ASSERT(urtc_kr_is_accepted(&rot, v->now, v->accepted[k], URTC_KR_KEY_ID_SIZE), "an accepted fingerprint verifies");
        }
        for (size_t k = 0; k < v->refused_count; k++) {
            TEST_ASSERT(!urtc_kr_is_accepted(&rot, v->now, v->refused[k], URTC_KR_KEY_ID_SIZE), "a refused fingerprint never verifies");
        }
    }
}

static void test_window_is_a_parameter(int *failures)
{
    urtc_rotation_t rot;
    memset(&rot, 0, sizeof rot);
    memset(rot.current, 'N', URTC_KR_KEY_ID_SIZE);
    memset(rot.previous, 'O', URTC_KR_KEY_ID_SIZE);
    rot.has_previous = 1;
    rot.started_at = 100;
    const uint32_t windows[] = {1u, 60u, 86400u};
    for (size_t i = 0; i < sizeof windows / sizeof windows[0]; i++) {
        rot.window_s = windows[i];
        TEST_ASSERT(urtc_kr_is_accepted(&rot, 100u + windows[i] - 1u, rot.previous, URTC_KR_KEY_ID_SIZE), "the previous key verifies on the last second of any window");
        TEST_ASSERT(!urtc_kr_is_accepted(&rot, 100u + windows[i], rot.previous, URTC_KR_KEY_ID_SIZE), "and stops on the second the window ends");
    }
    // a window longer than the field can tell never reports the value that means no window
    rot.started_at = 0u;
    rot.window_s = 0xFFFFFFFFu;
    TEST_ASSERT(urtc_kr_seconds_left(&rot, 0u) == URTC_KR_NO_WINDOW - 1u, "the seconds left never reach the value that means no window");
}

static void test_malformed_inputs(int *failures)
{
    urtc_rotation_t rot;
    memset(&rot, 0, sizeof rot);
    memset(rot.current, 'N', URTC_KR_KEY_ID_SIZE);
    memset(rot.previous, 'O', URTC_KR_KEY_ID_SIZE);
    rot.has_previous = 1;
    rot.window_s = 60;
    TEST_ASSERT(!urtc_kr_is_accepted(&rot, 1u, rot.current, URTC_KR_KEY_ID_SIZE - 1u), "a short fingerprint is never accepted");
    TEST_ASSERT(!urtc_kr_is_accepted(&rot, 1u, rot.current, 0u), "an empty one neither");
    TEST_ASSERT(!urtc_kr_is_accepted(&rot, 1u, NULL, URTC_KR_KEY_ID_SIZE), "a missing one neither");
    TEST_ASSERT(urtc_kr_is_accepted(&rot, 1u, rot.current, URTC_KR_KEY_ID_SIZE), "the current key verifies");

    uint8_t count, flags;
    uint32_t left;
    const uint8_t good[URTC_KR_STATUS_SIZE] = {0x05, 2, 1, 0, 0, 0x0E, 0x10};
    TEST_ASSERT(urtc_kr_parse_status(good, sizeof good, &count, &flags, &left) && count == 2 && flags == 1 && left == 3600u, "a well-formed status reads");
    uint8_t bad[URTC_KR_STATUS_SIZE];
    memcpy(bad, good, sizeof bad);
    bad[0] = 0x04;
    TEST_ASSERT(!urtc_kr_parse_status(bad, sizeof bad, &count, &flags, &left), "another operation is not a status");
    memcpy(bad, good, sizeof bad);
    bad[1] = 3;
    TEST_ASSERT(!urtc_kr_parse_status(bad, sizeof bad, &count, &flags, &left), "more than two keys is refused");
    memcpy(bad, good, sizeof bad);
    bad[2] = 0x04;
    TEST_ASSERT(!urtc_kr_parse_status(bad, sizeof bad, &count, &flags, &left), "a flag that does not exist is refused");
    TEST_ASSERT(!urtc_kr_parse_status(good, sizeof good - 1u, &count, &flags, &left), "a short status is refused");
    TEST_ASSERT(!urtc_kr_parse_status(NULL, sizeof good, &count, &flags, &left), "a missing status is refused");
}

int main(void)
{
    int count = 0;
    int *failures = &count;
    test_vectors(failures);
    test_window_is_a_parameter(failures);
    test_malformed_inputs(failures);
    if (count == 0) {
        printf("all key rotation tests pass\n");
        return 0;
    }
    printf("%d key rotation test(s) failed\n", count);
    return 1;
}

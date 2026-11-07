// =============================================================================
// URTC-COMMON - tests/c/test_key_rotation_cmd.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed: host tests of the signed rotation command (firmware_base/urtc_key_rotation_cmd.h), held to the shared
// vectors (generated/key_rotation_command_vectors.h, written by tools/make_key_rotation_command_vectors.py). Built by tests/test_key_rotation_command.py with
// the host compiler and firmware_base/urtc_ed25519.c.
#include <stdio.h>
#include <string.h>

#include "key_rotation_command_vectors.h"
#include "urtc_key_rotation_cmd.h"
#include "urtc_test.h"

static urtc_krc_state_t state_of(const key_rotation_command_vec_t *v)
{
    urtc_krc_state_t st;
    memset(&st, 0, sizeof st);
    memcpy(st.rotation.current, v->current, URTC_KR_KEY_ID_SIZE);
    memcpy(st.rotation.previous, v->previous, URTC_KR_KEY_ID_SIZE);
    st.rotation.has_previous = (uint8_t)v->has_previous;
    st.rotation.revoked = v->revoked_count != 0u ? v->revoked : NULL;
    st.rotation.revoked_count = v->revoked_count;
    st.key_count = v->trust_count;
    memcpy(st.key_id, v->trust_id, sizeof st.key_id);
    memcpy(st.key_public, v->trust_public, sizeof st.key_public);
    st.last_counter = v->last_counter;
    st.last_host_time = v->last_time;
    st.allow = (uint8_t)v->allow;
    st.max_window_s = v->max_window_s;
    return st;
}

static void test_vectors(int *failures)
{
    for (size_t i = 0; i < KEY_ROTATION_COMMAND_VECTOR_COUNT; i++) {
        const key_rotation_command_vec_t *v = &KEY_ROTATION_COMMAND_VECTORS[i];
        const urtc_krc_state_t before = state_of(v);
        urtc_krc_state_t after;
        const urtc_krc_result_t got = urtc_krc_decide(&before, v->record, v->record_length, v->uptime, &after);
        TEST_ASSERT((int)got == v->reason, v->name);
        if (!v->expect_ok) {
            TEST_ASSERT(memcmp(&after, &before, sizeof after) == 0, "a refused command changes nothing");
            continue;
        }
        TEST_ASSERT(memcmp(after.rotation.current, v->expect_current, URTC_KR_KEY_ID_SIZE) == 0, "the new current key is the one of the command");
        TEST_ASSERT(after.rotation.has_previous == (uint8_t)v->expect_has_previous, "the previous key is kept only for an overlap");
        if (v->expect_has_previous) {
            TEST_ASSERT(memcmp(after.rotation.previous, v->expect_previous, URTC_KR_KEY_ID_SIZE) == 0, "the previous key is the old current one");
        }
        TEST_ASSERT(after.rotation.window_s == v->expect_window_s, "the window is the one of the command (zero without overlap)");
        TEST_ASSERT(after.rotation.started_at == v->expect_started_at && after.last_host_time == v->expect_started_at, "the rotation starts at the time of the host");
        TEST_ASSERT(after.last_counter == v->expect_counter, "the counter moves up");
        TEST_ASSERT(after.clock_offset == v->expect_offset, "the offset between the host time and the uptime is kept");
        if (v->expect_has_previous && v->expect_window_s > 0u) {
            const uint32_t now = v->uptime + after.clock_offset;
            TEST_ASSERT(urtc_kr_is_accepted(&after.rotation, now, v->expect_previous, URTC_KR_KEY_ID_SIZE), "the old key still verifies inside the window");
            TEST_ASSERT(!urtc_kr_is_accepted(&after.rotation, now + v->expect_window_s, v->expect_previous, URTC_KR_KEY_ID_SIZE), "and stops when it ends");
        }
    }
}

static void test_null_record(int *failures)
{
    urtc_krc_state_t st;
    urtc_krc_state_t out;
    memset(&st, 0, sizeof st);
    st.allow = 1;
    TEST_ASSERT(urtc_krc_decide(&st, NULL, URTC_KRC_RECORD_SIZE, 0u, &out) == URTC_KRC_MALFORMED, "a missing record is malformed");
    st.allow = 0;
    TEST_ASSERT(urtc_krc_decide(&st, NULL, 0u, 0u, &out) == URTC_KRC_DISABLED, "the option off wins over everything else");
}

int main(void)
{
    int count = 0;
    int *failures = &count;
    test_vectors(failures);
    test_null_record(failures);
    if (count == 0) {
        printf("all key rotation command tests pass\n");
        return 0;
    }
    printf("%d key rotation command test(s) failed\n", count);
    return 1;
}

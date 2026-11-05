// =============================================================================
// URTC Firmware - Host tests of the signed command that programs the serial number of a bootloader
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board. Holds urtc_bl_serial.h (src/common, a copy of the shared one) to the shared vectors of URTC-COMMON: the decision on 17
// commands (valid, other head, other key, altered, replay, disabled, busy...) and the choice between the two slots of the record. The keys of the vectors are
// throwaway test keys (the first is the public development key); no key of a release is here.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bl_serial_vectors.h"
#include "urtc_bl_serial.h"
#include "vectors/urtc_test.h"

static void test_commands(int *failures)
{
    for (size_t i = 0; i < BL_SERIAL_COMMAND_VECTOR_COUNT; i++) {
        const bl_serial_command_vec_t *v = &BL_SERIAL_COMMAND_VECTORS[i];
        uint8_t keys[2][32];
        uint8_t count = 0;
        for (unsigned k = 0; k < 2; k++) {
            if (v->trusted_mask & (1u << k)) {
                memcpy(keys[count++], BL_SERIAL_KEYS[k], 32);
            }
        }
        uint32_t serial = 0, counter = 0;
        urtc_serial_status_t status = urtc_serial_decide(v->command, v->uid, v->current_serial, (const uint8_t (*)[32])keys, count, v->stored_counter,
                                                        v->enabled, v->busy, &serial, &counter);
        TEST_ASSERT((int)status == v->status, v->name);
        if (v->status == 0) {
            TEST_ASSERT(serial == v->serial && counter == v->counter, "a command that is accepted carries the serial and the counter that were signed");
        }
    }
}

static void test_slots(int *failures)
{
    for (size_t i = 0; i < BL_SERIAL_SLOT_VECTOR_COUNT; i++) {
        const bl_serial_slot_vec_t *v = &BL_SERIAL_SLOT_VECTORS[i];
        uint32_t serial = 0, counter = 0;
        int valid = urtc_serial_pick(v->slot_a, v->slot_b, &serial, &counter);
        TEST_ASSERT(valid == v->valid, v->name);
        if (v->valid) {
            TEST_ASSERT(serial == v->serial && counter == v->counter, "the newest valid slot gives its serial and counter");
        }
    }
    uint32_t words[4];
    urtc_serial_slot_pack(0x00C0FFEEu, 7u, words);
    TEST_ASSERT(words[0] == URTC_BL_SERIAL_MAGIC && words[1] == 0x00C0FFEEu && words[2] == ~0x00C0FFEEu && words[3] == 7u, "a slot is the magic, the serial, its complement and the counter");
    TEST_ASSERT(urtc_serial_slot_valid(words), "a packed slot is valid");
    words[2] ^= 1u;
    TEST_ASSERT(!urtc_serial_slot_valid(words), "a slot whose complement does not match is not");
    const uint32_t blank[4] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    TEST_ASSERT(urtc_serial_slot_blank(blank) && !urtc_serial_slot_valid(blank), "an erased slot is blank, not valid");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the signed command that programs the serial number\n");
    test_commands(&failures);
    test_slots(&failures);
    if (failures == 0) {
        printf("  OK   all serial programming host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

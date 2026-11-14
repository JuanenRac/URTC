// =============================================================================
// URTC Firmware - Host logic tests for urtc_can_recovery.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The mailboxes are aborted after a real bus-off and only then: an error of any other kind, whatever the counters say, leaves the answers that are waiting to go out.
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "urtc_can_recovery.h"

void run_can_recovery_tests(int *failures)
{
    TEST_ASSERT(URTC_CAN_ESR_BOFF == 4u, "the bus-off flag is bit 2 of the error status register");
    TEST_ASSERT(urtc_can_recovery_abort_tx(URTC_CAN_ESR_BOFF) == 1, "a bus-off aborts the mailboxes");
    TEST_ASSERT(urtc_can_recovery_abort_tx(0) == 0, "no flag at all does not");
    TEST_ASSERT(urtc_can_recovery_abort_tx(0x00000001u) == 0, "an error warning alone does not");
    TEST_ASSERT(urtc_can_recovery_abort_tx(0x00000002u) == 0, "an error passive alone does not");
    TEST_ASSERT(urtc_can_recovery_abort_tx(0x00000070u) == 0, "a last error code alone does not");
    TEST_ASSERT(urtc_can_recovery_abort_tx(0x00FF0000u) == 0 && urtc_can_recovery_abort_tx(0xFF000000u) == 0, "nor do counters that are high but below the bus-off");
    TEST_ASSERT(urtc_can_recovery_abort_tx(0xFFFFFFFFu) == 1, "every flag set includes the bus-off");
    for (uint32_t esr = 0; esr < 4096u; esr++) {
        TEST_ASSERT(urtc_can_recovery_abort_tx(esr) == (uint8_t)((esr >> 2) & 1u), "the decision is the bus-off bit and nothing else, over every combination of the low flags");
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - urtc_can_recovery.h\n");
    run_can_recovery_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN recovery host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

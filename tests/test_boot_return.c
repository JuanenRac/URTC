// =============================================================================
// URTC Firmware - Host logic tests for urtc_boot_return.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// When a listening bootloader goes back to a valid application: never with the switch off, never without a valid application, never while a transfer is going on, and
// exactly when the silence has lasted the configured seconds.
#include <stdint.h>
#include <stdio.h>

#include "test_runner.h"
#include "urtc_boot_return.h"

void run_boot_return_tests(int *failures)
{
    TEST_ASSERT(URTC_BL_RETURN_TO_APP_S == 0, "the default build does not go back to the application by itself");
    TEST_ASSERT(urtc_boot_return_to_app_due(0xFFFFFFFFu, 0, 1, 0) == 0, "with the switch off it never goes back, however long the silence");
    TEST_ASSERT(urtc_boot_return_to_app_due(29999u, 30, 1, 0) == 0, "one millisecond before the limit it stays");
    TEST_ASSERT(urtc_boot_return_to_app_due(30000u, 30, 1, 0) == 1, "exactly at the limit it goes back");
    TEST_ASSERT(urtc_boot_return_to_app_due(30001u, 30, 1, 0) == 1 && urtc_boot_return_to_app_due(0xFFFFFFFFu, 30, 1, 0) == 1, "and after it");
    TEST_ASSERT(urtc_boot_return_to_app_due(0xFFFFFFFFu, 30, 0, 0) == 0, "without a valid application there is nothing to go back to");
    TEST_ASSERT(urtc_boot_return_to_app_due(0xFFFFFFFFu, 30, 1, 1) == 0, "while an update is in progress it never goes back");
    TEST_ASSERT(urtc_boot_return_to_app_due(1000u, 1, 1, 0) == 1 && urtc_boot_return_to_app_due(999u, 1, 1, 0) == 0, "a limit of one second");
    TEST_ASSERT(urtc_boot_return_to_app_due(0xFFFFFFFFu, 0xFFFFFFFFu, 1, 0) == 0, "a limit that large does not overflow into an early return");
    for (uint32_t limit = 1; limit <= 600; limit += 37) {
        TEST_ASSERT(urtc_boot_return_to_app_due(limit * 1000u - 1u, limit, 1, 0) == 0 && urtc_boot_return_to_app_due(limit * 1000u, limit, 1, 0) == 1, "the edge is exactly limit seconds");
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - urtc_boot_return.h\n");
    run_boot_return_tests(&failures);
    if (failures == 0) {
        printf("  OK   all boot return host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

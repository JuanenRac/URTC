// =============================================================================
// URTC Firmware - the constants of the bootloader of the master against the ones written from the specification
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// boot/bootloader_common.h still spells out the identifiers of the update exchange, the status codes, the reasons of a failed verification and the hardware
// identity as numbers of its own. src/common/urtc_can_ids.h is written from the specification (spec/can_protocol.json) and is what the hosts use. This file
// compiles both side by side and fails the build when a number differs, so the bootloader cannot drift from the specification while it keeps its own
// spelling; each pair is checked at compile time (the build stops) and again at run time (the suite names it). Replacing the numbers of the bootloader by the
// macros of the header is a change of the bootloader source, and so of its version, to be made with the next build of the images.
#include <stdint.h>
#include <stdio.h>

#include "stm32f3xx_hal.h"
#include "bootloader_common.h"
#include "urtc_can_ids.h"
#include "test_runner.h"

#define SAME(ours, spec) \
    _Static_assert((unsigned long)(ours) == (unsigned long)(spec), #ours " differs from " #spec); \
    TEST_ASSERT((unsigned long)(ours) == (unsigned long)(spec), #ours " is " #spec)

void run_spec_macros_master_tests(int *failures)
{
    SAME(CAN_ID_ENTER_BOOTLOADER, URTC_CAN_ID_ENTER_BOOTLOADER);
    SAME(CAN_ID_START_UPDATE, URTC_CAN_ID_START_UPDATE);
    SAME(CAN_ID_DATA, URTC_CAN_ID_DATA);
    SAME(CAN_ID_PAGE_ACK, URTC_CAN_ID_PAGE_ACK);
    SAME(CAN_ID_END_UPDATE, URTC_CAN_ID_END_UPDATE);
    SAME(CAN_ID_STATUS, URTC_CAN_ID_STATUS);
    SAME(CAN_ID_HEARTBEAT, URTC_CAN_ID_HEARTBEAT);
    SAME(CAN_ID_HMAC_CHUNK, URTC_CAN_ID_HMAC_CHUNK);
    SAME(CAN_ID_QUERY_VERSION, URTC_CAN_ID_QUERY_VERSION);
    SAME(CAN_ID_VERSION_RESPONSE, URTC_CAN_ID_VERSION_RESPONSE);
    SAME(CAN_ID_BOOTLOADER_VERSION_RESPONSE, URTC_CAN_ID_BOOTLOADER_VERSION_RESPONSE);
    SAME(CAN_ID_QUERY_ERROR_COUNTERS, URTC_CAN_ID_QUERY_ERROR_COUNTERS);
    SAME(CAN_ID_ERROR_COUNTERS_RESPONSE, URTC_CAN_ID_ERROR_COUNTERS_RESPONSE);
    SAME(CAN_ID_AUTHORIZE_DOWNGRADE, URTC_CAN_ID_AUTHORIZE_DOWNGRADE);
    SAME(CAN_ID_READBACK, URTC_CAN_ID_READBACK);
    SAME(CAN_ID_READBACK_PAGE_ACK, URTC_CAN_ID_READBACK_PAGE_ACK);

    SAME(STATUS_LISTENING, URTC_STATUS_LISTENING);
    SAME(STATUS_ERASING, URTC_STATUS_ERASING);
    SAME(STATUS_RECEIVING, URTC_STATUS_RECEIVING);
    SAME(STATUS_VERIFYING, URTC_STATUS_VERIFYING);
    SAME(STATUS_COPYING, URTC_STATUS_COPYING);
    SAME(STATUS_VERIFY_OK, URTC_STATUS_VERIFY_OK);
    SAME(STATUS_VERIFY_FAIL, URTC_STATUS_VERIFY_FAIL);
    SAME(STATUS_ERROR, URTC_STATUS_ERROR);

    SAME(VERIFY_FAIL_REASON_INCOMPLETE, URTC_VERIFY_FAIL_REASON_INCOMPLETE);
    SAME(VERIFY_FAIL_REASON_CRC32, URTC_VERIFY_FAIL_REASON_CRC32);
    SAME(VERIFY_FAIL_REASON_HMAC, URTC_VERIFY_FAIL_REASON_HMAC);
    SAME(VERIFY_FAIL_REASON_HARDWARE_ID, URTC_VERIFY_FAIL_REASON_HARDWARE_ID);
    SAME(VERIFY_FAIL_REASON_ROLLBACK, URTC_VERIFY_FAIL_REASON_ROLLBACK);

    SAME(THIS_HARDWARE_ID, URTC_HARDWARE_ID_MAIN);
    SAME(FLASH_PAGE_SIZE, URTC_FLASH_PAGE_SIZE);
    SAME(sizeof(((FirmwareMetadata_t *)0)->sha256), URTC_HMAC_LENGTH);
    SAME(URTC_HMAC_LENGTH, URTC_HMAC_CHUNK_COUNT * 8u);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - bootloader constants against the specification (master)\n");
    run_spec_macros_master_tests(&failures);
    if (failures == 0) {
        printf("  OK   all constants of the master bootloader match the specification\n");
        return 0;
    }
    printf("  %d constant(s) differ\n", failures);
    return 1;
}

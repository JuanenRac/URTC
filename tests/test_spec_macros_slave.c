// =============================================================================
// URTC Firmware - the constants of the bootloader of the expansion slave against the ones written from the specification
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// boot/slaveboot_common.h spells out the registers of the update exchange on the link bus, the status codes, the reasons of a failed verification, its
// address and its hardware identity as numbers of its own. src/common/urtc_can_ids.h is written from the specification (spec/can_protocol.json). This file
// compiles both side by side and fails the build when a number differs; each pair is checked at compile time and again at run time. Replacing the numbers
// of the bootloader by the macros of the header is a change of the bootloader source, and so of its version, to be made with the next build of the images.
#include <stdint.h>
#include <stdio.h>

#include "stm32f3xx_hal.h"
#include "slaveboot_common.h"
#include "urtc_can_ids.h"
#include "urtc_slave_confirm.h"
#include "test_runner.h"

#define SAME(ours, spec) \
    _Static_assert((unsigned long)(ours) == (unsigned long)(spec), #ours " differs from " #spec); \
    TEST_ASSERT((unsigned long)(ours) == (unsigned long)(spec), #ours " is " #spec)

void run_spec_macros_slave_tests(int *failures)
{
    SAME(REG_STATUS, URTC_SLAVE_BOOT_REG_STATUS);
    SAME(REG_START_UPDATE, URTC_SLAVE_BOOT_REG_START_UPDATE);
    SAME(REG_HMAC_EXPECTED, URTC_SLAVE_BOOT_REG_HMAC_EXPECTED);
    SAME(REG_DATA, URTC_SLAVE_BOOT_REG_DATA);
    SAME(REG_END_UPDATE, URTC_SLAVE_BOOT_REG_END_UPDATE);
    SAME(REG_PROGRESS, URTC_SLAVE_BOOT_REG_PROGRESS);
    SAME(REG_QUERY_VERSION, URTC_SLAVE_BOOT_REG_QUERY_VERSION);
    SAME(REG_VERIFY_FAIL_REASON, URTC_SLAVE_BOOT_REG_VERIFY_FAIL_REASON);
    SAME(REG_QUERY_VERSION_EXT, URTC_SLAVE_BOOT_REG_QUERY_VERSION_EXT);

    SAME(REG_CHALLENGE, URTC_SLAVE_BOOT_REG_CHALLENGE);
    SAME(REG_CONFIRM, URTC_SLAVE_BOOT_REG_CONFIRM);
    SAME(STATUS_AWAIT_CONFIRM, URTC_SLAVE_STATUS_AWAIT_CONFIRM);
    SAME(VERIFY_FAIL_REASON_NOT_CONFIRMED, URTC_SLAVE_VERIFY_FAIL_REASON_NOT_CONFIRMED);
    SAME(URTC_SLAVE_CONFIRM_NONCE_SIZE, 16);
    SAME(URTC_SLAVE_CONFIRM_TAG_SIZE, 32);
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

    SAME(I2C_SLAVE_ADDRESS, URTC_SLAVE_I2C_ADDRESS);
    SAME(THIS_HARDWARE_ID, URTC_HARDWARE_ID_SLAVE);
    SAME(FLASH_PAGE_SIZE, URTC_FLASH_PAGE_SIZE);
    SAME(sizeof(((FirmwareMetadata_t *)0)->hmac), URTC_HMAC_LENGTH);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - bootloader constants against the specification (slave)\n");
    run_spec_macros_slave_tests(&failures);
    if (failures == 0) {
        printf("  OK   all constants of the slave bootloader match the specification\n");
        return 0;
    }
    printf("  %d constant(s) differ\n", failures);
    return 1;
}

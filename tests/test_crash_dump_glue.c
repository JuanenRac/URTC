// =============================================================================
// URTC Firmware - Host logic tests for the crash capture glue: the fault handler and the CAN readback (built with URTC_CRASH_DUMP=1)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// firmware_interrupts_fault.c, firmware_crash_dump.c and crash_dump.c are compiled against the fake HAL with the switch on. The part of the HardFault
// handler that can run on a host (the function the naked entry code jumps to) is called with a fake stacked frame and fake fault registers: the outputs are cut
// first, the record is written, and a reset that keeps it hands it to the host over CAN as six frames of 0x1BD, byte for byte, only to an operation 0 request.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_crash_dump.h"
#include "firmware_interrupts_fault.h"
#include "test_runner.h"
#include "urtc_can_ids.h"

#if !URTC_CRASH_DUMP
#error "this suite is built with -DURTC_CRASH_DUMP=1"
#endif

static const uint32_t FRAME[8] = {1, 2, 3, 4, 5, 0x08001235u, 0x08002468u, 0x61000000u};

static void collect(uint8_t out[48])
{
    memset(out, 0, 48);
    for (int i = 0; i < fake_tx_count && i < 6; i++) memcpy(out + i * 8, fake_tx[i].data, 8);
}

int main(void)
{
    int failure_count = 0;
    int *failures = &failure_count;
    printf("URTC host logic tests - crash capture glue\n");

    TEST_ASSERT(URTC_CAN_ID_CRASH_DUMP_QUERY == 0x1BC && URTC_CAN_ID_CRASH_DUMP_RESP == 0x1BD, "the identifiers come from the shared specification");

    // Power-on: nothing has crashed.
    CrashDump_AppBoot(CRASH_RESET_POWER_ON);
    fake_hal_reset_recording();
    CrashDump_HandleQuery(0);
    uint8_t wire[48];
    collect(wire);
    TEST_ASSERT(fake_tx_count == 6, "a query is answered with six frames");
    int shape_ok = 1;
    for (int i = 0; i < fake_tx_count; i++) shape_ok &= (fake_tx[i].id == 0x1BD && fake_tx[i].dlc == 8);
    TEST_ASSERT(shape_ok, "on 0x1BD, eight bytes each");
    TEST_ASSERT(memcmp(wire, "CRSH", 4) == 0 && wire[4] == 0 && wire[41] == CRASH_KIND_NONE && wire[40] == CRASH_RESET_POWER_ON, "after a power-on the record says no crash, with the cause of the power-on");

    // The fault: outputs first, then the registers.
    fake_gpio_a.MODER = 0;
    fake_gpio_b.MODER = 0;
    fake_scb.CFSR = 0x8200u;
    fake_scb.HFSR = 0x40000000u;
    fake_scb.MMFAR = 0xDEADBEEFu;
    fake_scb.BFAR = 0x40021000u;
    CrashDump_OnFault(FRAME, 0x20009F00u);
    TEST_ASSERT(fake_gpio_a.MODER == 0xEBFFFFFFu && fake_gpio_b.MODER == 0xFFFFFFFFu, "the outputs are cut exactly as the handler without the capture cuts them (SWD pins stay)");
    TEST_ASSERT(CrashDump_IsValid(&crash_dump_store) && crash_dump_store.seq == 1 && crash_dump_store.kind == CRASH_KIND_FAULT, "the record holds the crash");
    TEST_ASSERT(crash_dump_store.pc == 0x08002468u && crash_dump_store.lr == 0x08001235u && crash_dump_store.cfsr == 0x8200u && crash_dump_store.bfar == 0x40021000u, "with the registers the fake chip had");

    // The watchdog resets the chip; the application starts again and the host asks.
    CrashDump_AppBoot(0x01);
    fake_hal_reset_recording();
    CrashDump_HandleQuery(0);
    collect(wire);
    static const uint8_t EXPECTED[48] = {
        0x43,0x52,0x53,0x48,0x01,0x00,0x00,0x00,0x68,0x24,0x00,0x08,0x35,0x12,0x00,0x08,0x00,0x00,0x00,0x61,0x00,0x82,0x00,0x00,0x00,0x00,0x00,0x40,0xEF,0xBE,0xAD,0xDE,
        0x00,0x10,0x02,0x40,0x00,0x9F,0x00,0x20,0x01,0x01,0x01,0x00};
    TEST_ASSERT(fake_tx_count == 6 && memcmp(wire, EXPECTED, 44) == 0, "after the watchdog reset the six frames carry the crash, with the cause of that reset (watchdog) in byte 40");
    TEST_ASSERT(CrashDump_Crc32(wire, 44) == ((uint32_t)wire[44] | ((uint32_t)wire[45] << 8) | ((uint32_t)wire[46] << 16) | ((uint32_t)wire[47] << 24)), "and a CRC-32 that the host can check");

    // Only operation 0 is answered, and a full bus gets no half answer.
    fake_hal_reset_recording();
    CrashDump_HandleQuery(1);
    CrashDump_HandleQuery(0xFF);
    TEST_ASSERT(fake_tx_count == 0, "an operation other than 0 gets no answer");
    fake_tx_mailboxes_free = 0;
    CrashDump_HandleQuery(0);
    TEST_ASSERT(fake_tx_count == 0, "with no free mailbox nothing is sent, not a part of the record");

    // A record the RAM corrupted is never sent as it is: the host gets an empty one.
    crash_dump_store.pc ^= 1u;
    fake_hal_reset_recording();
    CrashDump_HandleQuery(0);
    collect(wire);
    TEST_ASSERT(fake_tx_count == 6 && wire[41] == CRASH_KIND_NONE && wire[8] == 0 && wire[4] == 0, "a record that fails its CRC is answered as an empty one");

    // A fault whose stack was unreadable.
    CrashDump_AppBoot(CRASH_RESET_POWER_ON);
    CrashDump_OnFault((const uint32_t *)0, 0x00000004u);
    CrashDump_AppBoot(0x01);
    fake_hal_reset_recording();
    CrashDump_HandleQuery(0);
    collect(wire);
    TEST_ASSERT(wire[41] == CRASH_KIND_FAULT_NO_FRAME && wire[8] == 0 && wire[12] == 0 && wire[36] == 0x04, "a crash without a readable stack says so and keeps the stack pointer it had");

    if (failure_count == 0) {
        printf("  OK   all crash capture glue host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failure_count);
    return 1;
}

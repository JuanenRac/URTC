// =============================================================================
// URTC Firmware - Host logic tests for the fault recorder (firmware_blackbox.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The recorder keeps the last ~10 s of samples in a ring and flushes them to the F-RAM on the rising edge of the error flag. The
// real firmware_blackbox.c is compiled against the fake HAL (the F-RAM is the fake one of tests/host_hal) and replays the scenarios
// that URTC-COMMON writes from its own model of the recorder (tests/vectors/blackbox_vectors.h): the ticks after which a record
// must have been written and the exact 678 bytes of the last one. The ring is static state of the module, so every scenario runs
// in a child process that starts from a clean one. The CAN readback (0x1A8 / 0x1A9) is checked against the same bytes.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>

#include "firmware_blackbox.h"
#include "firmware_common.h"
#include "test_runner.h"
#include "vectors/blackbox_vectors.h"

#define RECORD_BYTES ((int)sizeof(BlackBoxRecord_t))

static void readback(int *failures, const uint8_t *record, const char *name)
{
    (void)name;
    for (int chunk = 0; chunk < 22; chunk++) {
        fake_hal_reset_recording();
        BlackBox_HandleReadbackRequest((uint8_t)chunk);
        TEST_ASSERT(fake_tx_count == 4, "a chunk is answered with four frames");
        for (int frame = 0; frame < fake_tx_count && frame < 4; frame++) {
            uint8_t expected[8] = {0};
            for (int b = 0; b < 8; b++) {
                int at = chunk * 32 + frame * 8 + b;
                expected[b] = at < RECORD_BYTES ? record[at] : 0;  // the last chunk is short: its tail is zero, not whatever the F-RAM holds next
            }
            TEST_ASSERT(fake_tx[frame].id == 0x1A9 && fake_tx[frame].dlc == 8 && memcmp(fake_tx[frame].data, expected, 8) == 0, "the frames carry the record byte for byte");
        }
    }
    fake_hal_reset_recording();
    BlackBox_HandleReadbackRequest(22);
    BlackBox_HandleReadbackRequest(255);
    TEST_ASSERT(fake_tx_count == 0, "a chunk index past the record is not answered");
    fake_fram_fail_reads = 1;
    BlackBox_HandleReadbackRequest(0);
    fake_fram_fail_reads = 0;
    TEST_ASSERT(fake_tx_count == 0, "no answer when the F-RAM cannot be read");
    fake_tx_mailboxes_free = 0;
    BlackBox_HandleReadbackRequest(0);
    TEST_ASSERT(fake_tx_count == 0, "no answer when no transmit mailbox is free");
}

// Runs in a child process: returns the number of failed assertions.
static int scenario(const bb_vec_scenario_t *sc)
{
    int fails = 0;
    int *failures = &fails;
    memset(fake_fram, 0, sizeof fake_fram);
    fake_fram_write_calls = 0;
    fake_hal_reset_recording();
    fake_tick_step = 0;
    int flushed[16], flush_count = 0;
    for (int i = 0; i < sc->step_count; i++) {
        const bb_vec_step_t *st = &sc->steps[i];
        fake_tick = st->tick_ms;
        active_tool = (ToolMode_t)st->tool_id;
        current_temperature = st->current;
        target_temperature = st->target;
        endstop_triggered = st->endstop;
        system_error_flag = st->error;
        thermal_fault_code = st->thermal_code;
        int before = fake_fram_write_calls;
        BlackBox_Sample();
        if (fake_fram_write_calls != before && flush_count < 16) {
            flushed[flush_count++] = i;
        }
    }
    TEST_ASSERT(flush_count == sc->flush_count, sc->name);
    for (int k = 0; k < flush_count && k < sc->flush_count; k++) {
        TEST_ASSERT(flushed[k] == sc->flush_steps[k], "the record is written after the tick the model says");
    }
    if (sc->record != 0) {
        TEST_ASSERT(memcmp(fake_fram + BLACKBOX_FRAM_ADDR, sc->record, RECORD_BYTES) == 0, "the record in the F-RAM is the 678 bytes of the model");
        readback(failures, sc->record, sc->name);
    } else {
        uint8_t zeros[RECORD_BYTES];
        memset(zeros, 0, sizeof zeros);
        TEST_ASSERT(memcmp(fake_fram + BLACKBOX_FRAM_ADDR, zeros, RECORD_BYTES) == 0, "without a fault the F-RAM is untouched");
    }
    return fails;
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the fault recorder\n");
    fflush(stdout);
    for (int n = 0; n < BB_VEC_COUNT(BB_VEC_SCENARIOS); n++) {
        pid_t pid = fork();
        if (pid == 0) {
            int fails = scenario(&BB_VEC_SCENARIOS[n]);
            fflush(stdout);
            exit(fails > 100 ? 100 : fails);  // exit(), not _exit(): the coverage counters of the child are written by the exit handlers
        }
        int status = 0;
        waitpid(pid, &status, 0);
        int bad = WIFEXITED(status) ? WEXITSTATUS(status) : 101;
        printf("  %s  %s\n", bad == 0 ? "ok  " : "FAIL", BB_VEC_SCENARIOS[n].name);
        fflush(stdout);  // a child must not inherit (and print again) what is still buffered
        failures += bad;
    }
    if (failures == 0) {
        printf("  OK   all fault recorder host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

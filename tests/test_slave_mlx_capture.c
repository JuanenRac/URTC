// =============================================================================
// URTC Firmware - Host logic tests for the thermal camera capture state machine of the expansion slave (slave_mlx_capture.h)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// [FIRMWARE] not built for the board and not flashed. The four joints of a capture (start, ready?, read, calibrate) are replaced here by doubles that count
// their calls and the clock is given by the test, so what is asserted is the shape of the machine: one joint per step, the camera asked no faster than the
// poll interval, the wait bounded by the timeout, every failure ending in an error with the machine idle again. The real camera libraries and the bus are
// exercised by tests/test_slave.c.
#include <stdint.h>
#include <stdio.h>

#include "slave_mlx_capture.h"
#include "test_runner.h"

static int trigger_rc, ready_after, ready_rc, read_rc;
static int trigger_calls, ready_calls, read_calls, calibrate_calls;

static int d_trigger(void) { trigger_calls++; return trigger_rc; }
static int d_ready(void)
{
    ready_calls++;
    if (ready_rc < 0) return ready_rc;
    return ready_calls > ready_after ? 1 : 0;
}
static int d_read(void) { read_calls++; return read_rc; }
static void d_calibrate(void) { calibrate_calls++; }

static const MlxCaptureOps OPS = { d_trigger, d_ready, d_read, d_calibrate, NULL, NULL };

// The camera that needs more than one frame: the sub-pages it gives are a script, one per frame; more() says whether the other one is still missing.
static int script[8], script_len, frames_seen_mask, start_calls, more_rc_override, more_calls;
static void d_start(void) { start_calls++; frames_seen_mask = 0; }
static int d_read_pages(void)
{
    read_calls++;
    if (read_rc < 0) return read_rc;
    int page = read_calls <= script_len ? script[read_calls - 1] : 0;
    frames_seen_mask |= 1 << page;
    return page;
}
static int d_more(void)
{
    more_calls++;
    if (more_rc_override != 0) return more_rc_override;
    if (frames_seen_mask == 3) return 0;
    return read_calls < 4 ? 1 : -1;
}
static const MlxCaptureOps OPS_PAGES = { d_trigger, d_ready, d_read_pages, d_calibrate, d_start, d_more };

static void reset(void)
{
    trigger_rc = 0; ready_after = 0; ready_rc = 0; read_rc = 0;
    trigger_calls = ready_calls = read_calls = calibrate_calls = 0;
}

static int calls_total(void) { return trigger_calls + ready_calls + read_calls + calibrate_calls; }

// Runs the machine to the end with the clock moving `tick` ms per step; returns the last result and counts the steps.
static int run(MlxCapture *c, uint32_t start, uint32_t tick, int *steps)
{
    uint32_t now = start;
    int result = MLX_CAPTURE_STEP_RUNNING;
    *steps = 0;
    while (result == MLX_CAPTURE_STEP_RUNNING && *steps < 100000) {
        now += tick;
        result = mlx_capture_step(c, &OPS, now);
        (*steps)++;
    }
    return result;
}

static void test_happy_path(int *failures)
{
    MlxCapture c = {0};
    reset();
    ready_after = 3;
    TEST_ASSERT(!mlx_capture_active(&c), "a machine that was never started is idle");
    TEST_ASSERT(mlx_capture_begin(&c, &OPS, 1000) == MLX_CAPTURE_STEP_RUNNING && trigger_calls == 1 && mlx_capture_active(&c), "begin starts the measurement and leaves a capture in flight");
    TEST_ASSERT(ready_calls == 0 && read_calls == 0, "begin does nothing else");
    int steps;
    int result = run(&c, 1000, MLX_CAPTURE_POLL_MS, &steps);
    TEST_ASSERT(result == MLX_CAPTURE_STEP_DONE, "the capture finishes");
    TEST_ASSERT(ready_calls == 4 && read_calls == 1 && calibrate_calls == 1 && trigger_calls == 1, "the camera is asked until it is ready, then the frame is read once and calibrated once");
    TEST_ASSERT(steps == 4 + 1 + 1, "four questions, one read and one calibration, one joint per step");
    TEST_ASSERT(!mlx_capture_active(&c), "the machine is idle again after the end");
}

static void test_one_joint_per_step(int *failures)
{
    MlxCapture c = {0};
    reset();
    mlx_capture_begin(&c, &OPS, 0);
    int max_work = 0;
    uint32_t now = 0;
    int result = MLX_CAPTURE_STEP_RUNNING;
    for (int steps = 0; result == MLX_CAPTURE_STEP_RUNNING && steps < 50; steps++) {
        now += MLX_CAPTURE_POLL_MS;
        int before = calls_total();
        result = mlx_capture_step(&c, &OPS, now);
        int work = calls_total() - before;
        if (work > max_work) max_work = work;
    }
    TEST_ASSERT(result == MLX_CAPTURE_STEP_DONE && max_work == 1, "no step does more than one joint of the capture");
}

static void test_poll_interval(int *failures)
{
    MlxCapture c = {0};
    reset();
    ready_after = 1000;
    mlx_capture_begin(&c, &OPS, 500);
    for (uint32_t now = 500; now < 500 + MLX_CAPTURE_POLL_MS; now++) mlx_capture_step(&c, &OPS, now);
    TEST_ASSERT(ready_calls == 0, "the camera is not asked again before the poll interval has gone by");
    mlx_capture_step(&c, &OPS, 500 + MLX_CAPTURE_POLL_MS);
    TEST_ASSERT(ready_calls == 1, "it is asked once the interval has gone by");
    for (int i = 0; i < 100; i++) mlx_capture_step(&c, &OPS, 500 + MLX_CAPTURE_POLL_MS);
    TEST_ASSERT(ready_calls == 1, "and not again on the same millisecond, however many passes the loop makes");
}

static void test_timeout(int *failures)
{
    MlxCapture c = {0};
    reset();
    ready_after = 1 << 30;
    mlx_capture_begin(&c, &OPS, 0);
    int steps;
    int result = run(&c, 0, MLX_CAPTURE_POLL_MS, &steps);
    TEST_ASSERT(result == MLX_CAPTURE_STEP_ERROR && read_calls == 0 && calibrate_calls == 0, "a camera that never has a frame ready ends in an error and nothing is read");
    TEST_ASSERT(!mlx_capture_active(&c), "and the machine is idle again");
    TEST_ASSERT((uint32_t)steps * MLX_CAPTURE_POLL_MS > MLX_CAPTURE_READY_TIMEOUT_MS && (uint32_t)steps * MLX_CAPTURE_POLL_MS <= MLX_CAPTURE_READY_TIMEOUT_MS + 2 * MLX_CAPTURE_POLL_MS, "the wait ends right after the timeout");
}

static void test_clock_wrap(int *failures)
{
    MlxCapture c = {0};
    reset();
    ready_after = 2;
    uint32_t start = 0xFFFFFF00u;
    mlx_capture_begin(&c, &OPS, start);
    int steps;
    int result = run(&c, start, MLX_CAPTURE_POLL_MS, &steps);
    TEST_ASSERT(result == MLX_CAPTURE_STEP_DONE, "a clock that wraps around in the middle of the wait does not end it early or late");
}

static void test_failures(int *failures)
{
    MlxCapture c = {0};
    int steps;
    reset();
    trigger_rc = -1;
    TEST_ASSERT(mlx_capture_begin(&c, &OPS, 0) == MLX_CAPTURE_STEP_ERROR && !mlx_capture_active(&c), "a camera that does not take the start is an error at once and nothing is left in flight");
    reset();
    ready_rc = -3;
    mlx_capture_begin(&c, &OPS, 0);
    TEST_ASSERT(run(&c, 0, MLX_CAPTURE_POLL_MS, &steps) == MLX_CAPTURE_STEP_ERROR && read_calls == 0 && !mlx_capture_active(&c), "a camera that stops answering while it is asked is an error");
    reset();
    read_rc = -8;
    mlx_capture_begin(&c, &OPS, 0);
    TEST_ASSERT(run(&c, 0, MLX_CAPTURE_POLL_MS, &steps) == MLX_CAPTURE_STEP_ERROR && calibrate_calls == 0 && !mlx_capture_active(&c), "a frame that cannot be read is an error and is not calibrated");
    reset();
    TEST_ASSERT(mlx_capture_step(&c, &OPS, 0) == MLX_CAPTURE_STEP_ERROR && calls_total() == 0, "a step with nothing in flight does nothing");
}

static void test_abort_and_restart(int *failures)
{
    MlxCapture c = {0};
    reset();
    mlx_capture_begin(&c, &OPS, 0);
    mlx_capture_abort(&c);
    TEST_ASSERT(!mlx_capture_active(&c), "an aborted capture is gone");
    int before = calls_total();
    TEST_ASSERT(mlx_capture_step(&c, &OPS, 100) == MLX_CAPTURE_STEP_ERROR && calls_total() == before, "and no further step touches the camera");
    int steps;
    TEST_ASSERT(mlx_capture_begin(&c, &OPS, 200) == MLX_CAPTURE_STEP_RUNNING && run(&c, 200, MLX_CAPTURE_POLL_MS, &steps) == MLX_CAPTURE_STEP_DONE, "a new capture can be started after an abort");
    TEST_ASSERT(mlx_capture_begin(&c, &OPS, 300) == MLX_CAPTURE_STEP_RUNNING && run(&c, 300, MLX_CAPTURE_POLL_MS, &steps) == MLX_CAPTURE_STEP_DONE, "and one after a finished one");
}

static void test_more_frames(int *failures)
{
    MlxCapture c = {0};
    uint32_t now = 0;
    int result;
    // both sub-pages come in two frames: the camera is started twice, read twice, calibrated twice and the capture ends
    reset(); start_calls = more_calls = more_rc_override = 0; script[0] = 0; script[1] = 1; script_len = 2;
    mlx_capture_begin(&c, &OPS_PAGES, now);
    TEST_ASSERT(start_calls == 1 && trigger_calls == 1, "a new capture says it starts, once");
    for (int steps = 0, result0 = MLX_CAPTURE_STEP_RUNNING; result0 == MLX_CAPTURE_STEP_RUNNING && steps < 200; steps++) {
        now += MLX_CAPTURE_POLL_MS;
        result0 = mlx_capture_step(&c, &OPS_PAGES, now);
        if (result0 != MLX_CAPTURE_STEP_RUNNING) { result = result0; TEST_ASSERT(result == MLX_CAPTURE_STEP_DONE, "it ends well"); }
    }
    TEST_ASSERT(trigger_calls == 2 && read_calls == 2 && calibrate_calls == 2 && more_calls == 2 && start_calls == 1 && !mlx_capture_active(&c), "two frames were measured, read and calibrated, and the capture is over");
    // a camera that repeats one sub-page is an error after the limit, and nothing is left in flight
    reset(); start_calls = more_calls = more_rc_override = 0; script[0] = 1; script[1] = 1; script[2] = 1; script[3] = 1; script_len = 4;
    mlx_capture_begin(&c, &OPS_PAGES, now);
    result = MLX_CAPTURE_STEP_RUNNING;
    for (int steps = 0; result == MLX_CAPTURE_STEP_RUNNING && steps < 400; steps++) { now += MLX_CAPTURE_POLL_MS; result = mlx_capture_step(&c, &OPS_PAGES, now); }
    TEST_ASSERT(result == MLX_CAPTURE_STEP_ERROR && read_calls == 4 && !mlx_capture_active(&c), "a camera that keeps repeating one sub-page ends in an error after four frames");
    // the second measurement cannot be started: error at once
    reset(); start_calls = more_calls = more_rc_override = 0; script[0] = 0; script_len = 1;
    mlx_capture_begin(&c, &OPS_PAGES, now);
    result = MLX_CAPTURE_STEP_RUNNING;
    for (int steps = 0; result == MLX_CAPTURE_STEP_RUNNING && steps < 400; steps++) { now += MLX_CAPTURE_POLL_MS; if (read_calls == 1) trigger_rc = -2; result = mlx_capture_step(&c, &OPS_PAGES, now); }
    TEST_ASSERT(result == MLX_CAPTURE_STEP_ERROR && trigger_calls == 2 && !mlx_capture_active(&c), "a camera that does not take the second start is an error");
    // the timeout of the wait counts from the second start, not from the first
    reset(); start_calls = more_calls = more_rc_override = 0; script[0] = 0; script[1] = 1; script_len = 2; ready_after = 0;
    mlx_capture_begin(&c, &OPS_PAGES, 0);
    now = 0; result = MLX_CAPTURE_STEP_RUNNING;
    for (int steps = 0; result == MLX_CAPTURE_STEP_RUNNING && steps < 4000; steps++) { now += MLX_CAPTURE_READY_TIMEOUT_MS / 2u; result = mlx_capture_step(&c, &OPS_PAGES, now); }
    TEST_ASSERT(result == MLX_CAPTURE_STEP_DONE && read_calls == 2, "each frame has its own wait: two frames of 1.5 s each do not exceed the 3 s limit of one");
    // without the optional joints the machine behaves as before: one frame
    reset();
    mlx_capture_begin(&c, &OPS, 0);
    int steps;
    TEST_ASSERT(run(&c, 0, MLX_CAPTURE_POLL_MS, &steps) == MLX_CAPTURE_STEP_DONE && trigger_calls == 1 && read_calls == 1, "a camera without the optional joints takes one frame");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the thermal camera capture state machine\n");
    test_happy_path(&failures);
    test_one_joint_per_step(&failures);
    test_poll_interval(&failures);
    test_timeout(&failures);
    test_clock_wrap(&failures);
    test_failures(&failures);
    test_abort_and_restart(&failures);
    test_more_frames(&failures);
    if (failures == 0) {
        printf("  OK   all thermal camera capture state machine host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

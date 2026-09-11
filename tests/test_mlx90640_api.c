// =============================================================================
// URTC Firmware - Host logic tests for MLX90640_API.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
// Covers this project's own additions to the vendor sensor API (bounded
// data-ready polling instead of an unbounded while loop) plus that the
// parsing/validation entry points fail cleanly on garbage input rather
// than hanging or invoking undefined behaviour. Runs against the fake I2C
// driver - no F303 board, no thermal head.
#include <stdint.h>
#include <string.h>

#include "test_runner.h"
// MLX90640_API.h expects stdint's fixed-width types to already be in scope
// (in the firmware build MLX90640_I2C_Driver.h pulls them in first); do the
// same here explicitly so the header is usable from a host test.
#include "MLX90640_I2C_Driver.h"
#include "MLX90640_API.h"
#include "fake_mlx90640_i2c.h"

#define SLAVE 0x33

void run_mlx90640_api_tests(int *failures)
{
    int rc;
    uint16_t frame[834];
    uint16_t ee[MLX90640_EEPROM_DUMP_NUM];
    static paramsMLX90640 params;

    // 1. MLX90640_SynchFrame: a sensor whose data-ready bit never sets
    //    (stuck / marginal bus) must fail with the project's bounded-poll
    //    error, not spin forever.
    fake_i2c_reset();
    fake_i2c.status_ready_after = -1;
    rc = MLX90640_SynchFrame(SLAVE);
    TEST_ASSERT(rc == -MLX90640_DATA_READY_TIMEOUT_ERROR,
                "SynchFrame on a stuck bus returns DATA_READY_TIMEOUT_ERROR");
    TEST_ASSERT(fake_i2c.read_count <= (unsigned)MLX90640_DATA_READY_MAX_POLLS + 2,
                "SynchFrame polls a bounded number of times, never unbounded");
    TEST_ASSERT(fake_i2c.read_count >= 100,
                "SynchFrame actually polled the status register (sanity)");

    // 2. Data-ready set from the first poll: prompt success.
    fake_i2c_reset();
    fake_i2c.status_ready_after = 0;
    rc = MLX90640_SynchFrame(SLAVE);
    TEST_ASSERT(rc == MLX90640_NO_ERROR, "SynchFrame returns NO_ERROR once data is ready");
    TEST_ASSERT(fake_i2c.read_count <= 3, "SynchFrame returns promptly when data is ready");

    // 3. A real I2C error mid-poll is surfaced as itself, not masked by the
    //    timeout.
    fake_i2c_reset();
    fake_i2c.status_ready_after = -1;
    fake_i2c.fail_read_at = 5;
    rc = MLX90640_SynchFrame(SLAVE);
    TEST_ASSERT(rc == -MLX90640_I2C_NACK_ERROR,
                "SynchFrame surfaces a real I2C NACK instead of the timeout");
    TEST_ASSERT(fake_i2c.read_count <= 7, "SynchFrame stops at the I2C error");

    // 4. MLX90640_GetFrameData has the same bounded-poll guard.
    fake_i2c_reset();
    fake_i2c.status_ready_after = -1;
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == -MLX90640_DATA_READY_TIMEOUT_ERROR,
                "GetFrameData on a stuck bus returns DATA_READY_TIMEOUT_ERROR");
    TEST_ASSERT(fake_i2c.read_count <= (unsigned)MLX90640_DATA_READY_MAX_POLLS + 2,
                "GetFrameData polls a bounded number of times, never unbounded");

    // 5. Ready + a clean (all-zero, no 0x7FFF sentinels) frame: returns a
    //    real subpage number.
    fake_i2c_reset();
    fake_i2c.status_ready_after = 0;
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == 0 || rc == 1,
                "GetFrameData success returns a subpage number (0 or 1)");

    // 6. A 0x7FFF pixel sentinel on the active subpage is rejected as a
    //    frame-data error (not silently returned as valid pixels).
    fake_i2c_reset();
    fake_i2c.status_ready_after = 0;
    fake_i2c_set_word(MLX90640_PIXEL_DATA_START_ADDRESS, 0x7FFF); // line 0, subpage 0
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == -MLX90640_FRAME_DATA_ERROR,
                "GetFrameData rejects a corrupt (0x7FFF) frame word");

    // 7. MLX90640_DumpEE pulls the whole 832-word EEPROM window through the
    //    driver.
    fake_i2c_reset();
    fake_i2c_fill(MLX90640_EEPROM_START_ADDRESS, MLX90640_EEPROM_DUMP_NUM, 0xBEEF);
    rc = MLX90640_DumpEE(SLAVE, ee);
    TEST_ASSERT(rc == MLX90640_NO_ERROR, "DumpEE returns NO_ERROR");
    TEST_ASSERT(ee[0] == 0xBEEF && ee[MLX90640_EEPROM_DUMP_NUM - 1] == 0xBEEF,
                "DumpEE copied the full 832-word window");

    // 8. MLX90640_ExtractParameters() on an all-zero EEPROM: every pixel
    //    word reads as 0, so ExtractDeviatingPixels() (run last) returns a
    //    defined bad-pixel error. The point of this test is that it
    //    RETURNS - the Extract*Parameters scale-normalisation loops
    //    (`while (temp < K) { temp *= 2; }`) used to spin forever on
    //    temp == 0; MLX90640_SCALE_ITER_MAX now bounds them. If this test
    //    hangs, that cap regressed.
    {
        static uint16_t eeZero[MLX90640_EEPROM_DUMP_NUM];
        static paramsMLX90640 p0;
        memset(eeZero, 0, sizeof(eeZero));
        memset(&p0, 0, sizeof(p0));
        int erc = MLX90640_ExtractParameters(eeZero, &p0);
        TEST_ASSERT(erc < 0,
                    "ExtractParameters flags an all-zero EEPROM as an error, not success");
        TEST_ASSERT(erc == -MLX90640_BROKEN_PIXELS_NUM_ERROR
                 || erc == -MLX90640_OUTLIER_PIXELS_NUM_ERROR
                 || erc == -MLX90640_BAD_PIXELS_NUM_ERROR
                 || erc == -MLX90640_ADJACENT_BAD_PIXELS_ERROR,
                    "ExtractParameters returns a defined bad-pixel error code");
        TEST_ASSERT(p0.alphaScale <= MLX90640_SCALE_ITER_MAX,
                    "the alpha-scale normalisation loop is bounded, not infinite");
    }
    (void)params;

    // 9. MLX90640_GetSubPageNumber is a pure accessor for frameData[833].
    memset(frame, 0, sizeof(frame));
    frame[833] = 1;
    TEST_ASSERT(MLX90640_GetSubPageNumber(frame) == 1,
                "GetSubPageNumber reads frameData[833]");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - MLX90640_API.c\n");
    run_mlx90640_api_tests(&failures);
    if (failures == 0) {
        printf("  OK   all MLX90640_API host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

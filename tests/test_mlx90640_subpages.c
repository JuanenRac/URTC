// =============================================================================
// URTC Firmware - Host logic tests for the sub-pages of the MLX90640 capture (firmware_mlx90640_app.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The MLX90640 measures half of its pixels per frame (a chess pattern, two sub-pages) and the library calculates only the pixels of the sub-page that came. The
// sensor double alternates the sub-page each time the host takes a frame. By default a capture reads one frame, so half of the image is whatever the buffer held
// before (zeros after start-up); built with URTC_MLX90640_TWO_SUBPAGES=1 a capture reads both sub-pages and every pixel is calculated. Evidence: simulator; the
// real sensor's step mode, refresh rate and timing are still to be checked with a sensor on a board.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "MLX90640_API.h"
#include "fake_mlx90640_i2c.h"
#include "mlx_synthetic.h"
#include "test_runner.h"

void MLX90640_Direct_Init(void);
void MLX90640_Direct_TriggerCapture(void);
uint8_t MLX90640_Direct_GetCaptureStatus(void);
void MLX90640_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

#ifndef URTC_MLX90640_TWO_SUBPAGES
#define URTC_MLX90640_TWO_SUBPAGES 0
#endif

static void load_sensor(void)
{
    uint16_t ee[MLX90640_EEPROM_DUMP_NUM];
    uint16_t frame[834];
    synthetic_eeprom(ee);
    synthetic_frame(frame, 0x0100);
    fake_i2c_reset();
    fake_i2c.clear_trigger_on_reset = 1;
    fake_i2c.status_ready_after = 0;
    fake_i2c.alternate_subpage = 1;
    for (int i = 0; i < MLX90640_EEPROM_DUMP_NUM; i++) fake_i2c_set_word((uint16_t)(MLX90640_EEPROM_START_ADDRESS + i), ee[i]);
    for (int i = 0; i < 768; i++) fake_i2c_set_word((uint16_t)(MLX90640_PIXEL_DATA_START_ADDRESS + i), frame[i]);
    for (int i = 0; i < MLX90640_AUX_NUM; i++) fake_i2c_set_word((uint16_t)(MLX90640_AUX_DATA_START_ADDRESS + i), frame[768 + i]);
    fake_i2c_set_word(MLX90640_CTRL_REG, frame[832]);
}

static int zero_pixels(void)
{
    int zeros = 0;
    uint8_t chunk[32];
    for (uint8_t c = 0; c < 48; c++) {
        MLX90640_Direct_GetCalibratedChunk(c, chunk);
        for (int i = 0; i < 16; i++) if (chunk[i * 2] == 0 && chunk[i * 2 + 1] == 0) zeros++;
    }
    return zeros;
}

static void test_subpages(int *failures)
{
    load_sensor();
    MLX90640_Direct_Init();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_IDLE, "nothing has been captured yet");
    TEST_ASSERT(zero_pixels() == 768, "before the first capture the whole image is zero");
    fake_hal_reset_recording();
    MLX90640_Direct_TriggerCapture();
#if URTC_MLX90640_TWO_SUBPAGES
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_READY, "a capture of both sub-pages is ready");
    TEST_ASSERT(zero_pixels() == 0, "and every pixel of the image is calculated, none left at zero");
    TEST_ASSERT(fake_iwdg_refreshes == 1, "the watchdog is refreshed between the two frames");
    // a second capture keeps the image whole
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_READY && zero_pixels() == 0, "a later capture leaves the image whole too");
    // a sensor that gives the same sub-page every time cannot complete the image: an error after a bounded number of frames, never a loop
    load_sensor();
    fake_i2c.alternate_subpage = 0;
    MLX90640_Direct_Init();
    fake_hal_reset_recording();
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR && fake_iwdg_refreshes <= 3, "a sensor that repeats one sub-page is an error after at most four frames");
    // a frame that fails in the middle of the capture is an error
    load_sensor();
    MLX90640_Direct_Init();
    fake_i2c.fail_read_at = (int)fake_i2c.read_count + 7;   // a frame is five reads: this cuts the bus inside the second frame
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a bus that fails during the capture is an error, not a half image reported as ready");
#else
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_READY, "by default a capture reads one frame");
    int zeros = zero_pixels();
    TEST_ASSERT(zeros > 0 && zeros < 768, "and only the pixels of the sub-page that came are calculated: part of the image is still zero");
    TEST_ASSERT(fake_iwdg_refreshes == 0, "no extra watchdog refresh in the default capture");
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(zero_pixels() < zeros, "the next capture brings the other sub-page, so the image fills up one half at a time");
#endif
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the sub-pages of the MLX90640 capture\n");
    test_subpages(&failures);
    if (failures == 0) {
        printf("  OK   all MLX90640 sub-page host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

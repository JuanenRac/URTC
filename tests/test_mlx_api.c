// =============================================================================
// URTC Firmware - Host logic tests for the MLX90640 driver beyond its start-up, and for the applications of the MLX90640 and MLX90641 heads
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// tests/test_mlx90640_api.c holds the guards of MLX90640_API.c (a stuck sensor, an all-zero EEPROM, a bad frame). This file goes through the rest of the
// file against the register model of tests/fake_mlx90640_i2c.c: the control register (resolution, refresh rate, interleaved and chess mode) with its
// neighbouring bits left alone, the trigger of a measurement, the frame read and its validation, the correction of broken pixels in both modes, and the
// parameter extraction, the temperatures and the image on a synthetic EEPROM. Then the two thermal-camera applications (firmware_mlx90640_app.c and
// firmware_mlx90641_app.c): the status they report, the chunks they hand to the CAN side and the clamping of the values. The MLX90641 library is C++ and is
// not built here: its entry points are replaced by recorders, so what is asserted is what the application decides.
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "fake_mlx90640_i2c.h"
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"
#include "MLX90641_API.h"
#include "mlx_synthetic.h"
#include "test_runner.h"

#define SLAVE 0x33

void MLX90640_Direct_Init(void);
void MLX90640_Direct_TriggerCapture(void);
uint8_t MLX90640_Direct_GetCaptureStatus(void);
void MLX90640_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX90640_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX90641_Direct_Init(void);
void MLX90641_Direct_TriggerCapture(void);
uint8_t MLX90641_Direct_GetCaptureStatus(void);
void MLX90641_Direct_GetRawChunk(uint8_t chunk_index, uint8_t out[32]);
void MLX90641_Direct_GetCalibratedChunk(uint8_t chunk_index, uint8_t out[32]);

// ---- recorders for the MLX90641 library (C++ in the firmware) ----------------------------------------------------------------------------
static struct {
    int dump_rc, extract_calls, trigger_rc, frame_rc, ta_calls, to_calls;
    uint16_t frame_word0;
    float to_value;
    float last_tr, last_emissivity;
} m641;

int MLX90641_DumpEE(uint8_t slaveAddr, uint16_t *eeData) { (void)slaveAddr; (void)eeData; return m641.dump_rc; }
int MLX90641_ExtractParameters(uint16_t *eeData, paramsMLX90641 *p) { (void)eeData; (void)p; m641.extract_calls++; return 0; }
int MLX90641_TriggerMeasurement(uint8_t slaveAddr) { (void)slaveAddr; return m641.trigger_rc; }
int MLX90641_GetFrameData(uint8_t slaveAddr, uint16_t *frame)
{
    (void)slaveAddr;
    for (int i = 0; i < 242; i++) frame[i] = (uint16_t)(m641.frame_word0 + i);
    return m641.frame_rc;
}
float MLX90641_GetTa(uint16_t *frame, const paramsMLX90641 *p) { (void)frame; (void)p; m641.ta_calls++; return 31.0f; }
void MLX90641_CalculateTo(uint16_t *frame, const paramsMLX90641 *p, float emissivity, float tr, float *result)
{
    (void)frame; (void)p;
    m641.to_calls++;
    m641.last_tr = tr;
    m641.last_emissivity = emissivity;
    for (int i = 0; i < 192; i++) result[i] = m641.to_value + (float)i * 0.5f;
}
// the entry points of the library that the application does not call, so the header is satisfied
int MLX90641_SynchFrame(uint8_t s) { (void)s; return 0; }
float MLX90641_GetVdd(uint16_t *f, const paramsMLX90641 *p) { (void)f; (void)p; return 3.3f; }
void MLX90641_GetImage(uint16_t *f, const paramsMLX90641 *p, float *r) { (void)f; (void)p; (void)r; }
int MLX90641_SetResolution(uint8_t s, uint8_t r) { (void)s; (void)r; return 0; }
int MLX90641_GetCurResolution(uint8_t s) { (void)s; return 0; }
int MLX90641_SetRefreshRate(uint8_t s, uint8_t r) { (void)s; (void)r; return 0; }
int MLX90641_GetRefreshRate(uint8_t s) { (void)s; return 0; }
int MLX90641_GetSubPageNumber(uint16_t *f) { (void)f; return 0; }
float MLX90641_GetEmissivity(const paramsMLX90641 *p) { (void)p; return 1.0f; }
void MLX90641_BadPixelsCorrection(uint16_t pixel, float *to) { (void)pixel; (void)to; }
void MLX90641_I2CInit(void) {}

// ---- the register-level functions -------------------------------------------------------------------------------------------------------
static void test_control_register(int *failures)
{
    fake_i2c_reset();
    fake_i2c_set_word(MLX90640_CTRL_REG, 0x1901);
    int all_resolutions = 1;
    for (int r = 0; r < 4; r++) {
        if (MLX90640_SetResolution(SLAVE, (uint8_t)r) != MLX90640_NO_ERROR || MLX90640_GetCurResolution(SLAVE) != r) all_resolutions = 0;
    }
    TEST_ASSERT(all_resolutions, "each of the four resolutions is written and read back");
    uint16_t ctrl;
    MLX90640_I2CRead(SLAVE, MLX90640_CTRL_REG, 1, &ctrl);
    TEST_ASSERT((ctrl & 0x1901 & ~(3u << MLX90640_CTRL_RESOLUTION_SHIFT)) == (0x1901 & ~(3u << MLX90640_CTRL_RESOLUTION_SHIFT)) && (ctrl & ~(3u << MLX90640_CTRL_RESOLUTION_SHIFT)) == (0x1901 & ~(3u << MLX90640_CTRL_RESOLUTION_SHIFT)),
                "the bits around the resolution are left as they were");

    int all_rates = 1;
    for (int r = 0; r < 8; r++) {
        if (MLX90640_SetRefreshRate(SLAVE, (uint8_t)r) != MLX90640_NO_ERROR || MLX90640_GetRefreshRate(SLAVE) != r) all_rates = 0;
    }
    TEST_ASSERT(all_rates, "each of the eight refresh rates is written and read back");
    MLX90640_I2CRead(SLAVE, MLX90640_CTRL_REG, 1, &ctrl);
    TEST_ASSERT(MLX90640_GetCurResolution(SLAVE) == 3 && (ctrl & (1u << 0)) != 0, "changing the rate does not change the resolution or the other bits");

    TEST_ASSERT(MLX90640_SetChessMode(SLAVE) == MLX90640_NO_ERROR && MLX90640_GetCurMode(SLAVE) == 1, "chess mode is set and read as 1");
    TEST_ASSERT(MLX90640_SetInterleavedMode(SLAVE) == MLX90640_NO_ERROR && MLX90640_GetCurMode(SLAVE) == 0, "interleaved mode is set and read as 0");
    MLX90640_I2CRead(SLAVE, MLX90640_CTRL_REG, 1, &ctrl);
    TEST_ASSERT(MLX90640_GetRefreshRate(SLAVE) == 7 && MLX90640_GetCurResolution(SLAVE) == 3, "the measurement mode leaves rate and resolution alone");

    fake_i2c.fail_read_at = 0;
    TEST_ASSERT(MLX90640_SetResolution(SLAVE, 1) == -MLX90640_I2C_NACK_ERROR && MLX90640_SetRefreshRate(SLAVE, 1) == -MLX90640_I2C_NACK_ERROR, "a sensor that does not answer makes the setters fail with the NACK error");
    TEST_ASSERT(MLX90640_SetChessMode(SLAVE) == -MLX90640_I2C_NACK_ERROR && MLX90640_SetInterleavedMode(SLAVE) == -MLX90640_I2C_NACK_ERROR, "and the two mode setters");
    TEST_ASSERT(MLX90640_GetCurResolution(SLAVE) == -MLX90640_I2C_NACK_ERROR && MLX90640_GetRefreshRate(SLAVE) == -MLX90640_I2C_NACK_ERROR && MLX90640_GetCurMode(SLAVE) == -MLX90640_I2C_NACK_ERROR, "and the getters");
}

static void test_trigger_and_frame(int *failures)
{
    fake_i2c_reset();
    fake_i2c.clear_trigger_on_reset = 1;
    TEST_ASSERT(MLX90640_TriggerMeasurement(SLAVE) == MLX90640_NO_ERROR, "a sensor that starts the measurement clears the trigger bit and the call succeeds");
    fake_i2c_reset();
    TEST_ASSERT(MLX90640_TriggerMeasurement(SLAVE) == -MLX90640_MEAS_TRIGGER_ERROR, "a sensor that leaves the trigger bit set did not start: the measurement error");
    fake_i2c_reset();
    fake_i2c.fail_read_at = 0;
    TEST_ASSERT(MLX90640_TriggerMeasurement(SLAVE) == -MLX90640_I2C_NACK_ERROR, "no answer: the NACK error");

    static uint16_t frame[834];
    fake_i2c_reset();
    fake_i2c.status_ready_after = 0;
    for (int i = 0; i < 768; i++) fake_i2c_set_word((uint16_t)(MLX90640_PIXEL_DATA_START_ADDRESS + i), (uint16_t)(0x1000 + i));
    for (int i = 0; i < MLX90640_AUX_NUM; i++) fake_i2c_set_word((uint16_t)(MLX90640_AUX_DATA_START_ADDRESS + i), (uint16_t)(0x2000 + i));
    fake_i2c_set_word(MLX90640_CTRL_REG, 0x1901);
    fake_i2c_set_word(MLX90640_STATUS_REG, 0x0008);
    int rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == 0 && frame[0] == 0x1000 && frame[767] == 0x12FF, "a ready frame is read: the pixels first");
    TEST_ASSERT(frame[768] == 0x2000 && frame[831] == 0x203F, "then the 64 auxiliary words after them");
    TEST_ASSERT(frame[832] == 0x1901 && MLX90640_GetSubPageNumber(frame) == 0, "then the control register, and the sub-page the status says");
    fake_i2c_set_word(MLX90640_STATUS_REG, 0x0009);
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == 1 && MLX90640_GetSubPageNumber(frame) == 1, "sub-page 1 is returned as 1");

    fake_i2c_set_word(MLX90640_STATUS_REG, 0x0008);
    fake_i2c_set_word((uint16_t)(MLX90640_PIXEL_DATA_START_ADDRESS + 32), 0x7FFF);   // first pixel of line 1, which belongs to sub-page 1 (line % 2 == 1)
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == 0, "a sentinel in a line of the other sub-page is not an error");
    fake_i2c_set_word(MLX90640_STATUS_REG, 0x0009);
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == -MLX90640_FRAME_DATA_ERROR, "a sentinel in a line of this sub-page is the frame data error");

    fake_i2c_set_word(MLX90640_STATUS_REG, 0x0008);
    fake_i2c_set_word((uint16_t)(MLX90640_PIXEL_DATA_START_ADDRESS + 32), 0x1000);
    fake_i2c_set_word(MLX90640_AUX_DATA_START_ADDRESS, 0x7FFF);
    for (int i = 0; i < 64; i++) frame[768 + i] = 0xEEEE;
    rc = MLX90640_GetFrameData(SLAVE, frame);
    TEST_ASSERT(rc == 0 && frame[768] == 0xEEEE, "an auxiliary block with a sentinel is not copied into the frame");
    fake_i2c.fail_read_at = 2;
    TEST_ASSERT(MLX90640_GetFrameData(SLAVE, frame) < 0, "a bus failure in the middle of the frame is returned");
}

// ---- the correction of broken pixels ----------------------------------------------------------------------------------------------------
static void test_bad_pixels(int *failures)
{
    static float to[768];
    static paramsMLX90640 params;
    uint16_t list[2];
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    memset(&params, 0, sizeof params);
    for (int i = 0; i < 5; i++) { params.brokenPixels[i] = 0xFFFF; params.outlierPixels[i] = 0xFFFF; }

    list[0] = 100; list[1] = 0xFFFF;                       // line 3, column 4
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[100] == (67.0f + 69.0f + 131.0f + 133.0f) / 4.0f, "interleaved mode: a pixel inside the array is the median of its four diagonal neighbours");
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    list[0] = 0;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[0] == 33.0f, "interleaved: the top left corner takes the pixel diagonally below");
    list[0] = 31;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[31] == 62.0f, "the top right corner");
    list[0] = 5;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[5] == (36.0f + 38.0f) / 2.0f, "the top edge averages the two diagonals below");
    list[0] = 23 * 32;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[736] == 705.0f, "the bottom left corner");
    list[0] = 23 * 32 + 31;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[767] == 734.0f, "the bottom right corner");
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    list[0] = 23 * 32 + 7;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[743] == (710.0f + 712.0f) / 2.0f, "the bottom edge averages the two diagonals above");
    list[0] = 4 * 32;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[128] == (97.0f + 161.0f) / 2.0f, "the left edge averages the diagonal above right and below right");
    list[0] = 4 * 32 + 31;
    MLX90640_BadPixelsCorrection(list, to, 1, &params);
    TEST_ASSERT(to[159] == (126.0f + 190.0f) / 2.0f, "the right edge averages the diagonal above left and below left");

    for (int i = 0; i < 768; i++) to[i] = (float)i;
    list[0] = 100;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[100] == 100.0f, "chess mode: on a smooth gradient the rebuilt pixel is the value the gradient gives");
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    list[0] = 64;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[64] == 65.0f, "chess mode, first column: a copy of the pixel to its right");
    list[0] = 95;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[95] == 94.0f, "last column: a copy of the pixel to its left");
    list[0] = 97;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[97] == (96.0f + 98.0f) / 2.0f, "second column: the mean of its two neighbours");
    list[0] = 126;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[126] == (125.0f + 127.0f) / 2.0f, "second to last column: the same");
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    to[103] = 500.0f;                                      // a jump on one side: the rule follows the side that is smoother
    list[0] = 100;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[100] == to[99] + (to[99] - to[98]), "chess mode: with a jump on the right the pixel follows the smoother left side");
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    to[97] = 500.0f;
    list[0] = 100;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[100] == to[101] + (to[101] - to[102]), "and with a jump on the left it follows the right side");
    for (int i = 0; i < 768; i++) to[i] = (float)i;
    params.brokenPixels[0] = 102;                          // a second broken pixel two places away: no gradient, just the mean
    list[0] = 100;
    MLX90640_BadPixelsCorrection(list, to, 0, &params);
    TEST_ASSERT(to[100] == (99.0f + 101.0f) / 2.0f, "a neighbour that is broken too is not used for the gradient");
}

// ---- the parameter extraction and the temperatures on a synthetic sensor -------------------------------------------------------------------
// (the synthetic sensor is in tests/mlx_synthetic.h: the head of the expansion slave uses the same one)

static void test_synthetic_calibration(int *failures)
{
    static uint16_t ee[MLX90640_EEPROM_DUMP_NUM];
    static paramsMLX90640 params;
    static uint16_t frame[834];
    static float to_a[768], to_b[768], image_a[768], image_b[768];
    synthetic_eeprom(ee);
    int rc = MLX90640_ExtractParameters(ee, &params);
    TEST_ASSERT(rc == MLX90640_NO_ERROR, "a consistent EEPROM is accepted: no broken pixel, no outlier");
    TEST_ASSERT(params.resolutionEE == 2 && params.gainEE == 0x1900 && params.kVdd == -52 * 32 && params.vdd25 == -9216, "the supply and gain parameters are the ones written");
    TEST_ASSERT(params.alphaPTAT == 9.0f && params.ct[2] == 50 && params.ct[3] == 150 && params.ct[4] == 400, "the PTAT slope and the corner temperatures of the ranges");
    TEST_ASSERT(params.alphaScale > 0 && params.alphaScale <= MLX90640_SCALE_ITER_MAX && params.alpha[0] == params.alpha[767] && params.alpha[0] > 30000, "the alpha of every pixel is the same and fills the 16-bit field");
    TEST_ASSERT(params.brokenPixels[0] == 0xFFFF && params.outlierPixels[0] == 0xFFFF, "the lists of deviating pixels are empty");

    synthetic_frame(frame, 0x0100);
    float vdd = MLX90640_GetVdd(frame, &params);
    float ta = MLX90640_GetTa(frame, &params);
    TEST_ASSERT(fabsf(vdd - 3.3f) < 0.01f, "the supply voltage of the frame is 3.3 V");
    TEST_ASSERT(ta > 20.0f && ta < 60.0f, "the ambient temperature is a plausible one");
    frame[832] = 0x1D01;                  // resolution 3 in the frame, 2 in the EEPROM: the supply voltage is corrected for it
    TEST_ASSERT(fabsf(MLX90640_GetVdd(frame, &params) - 3.3f) > 0.05f, "a different resolution in the frame is corrected, not ignored");
    synthetic_frame(frame, 0x0100);

    MLX90640_CalculateTo(frame, &params, 0.95f, ta - 8.0f, to_a);
    int finite = 1;
    for (int i = 0; i < 768; i += 2) if (!isfinite(to_a[i])) finite = 0;     // sub-page 0 holds the even pixels of even rows and the odd of odd rows
    TEST_ASSERT(finite, "the temperatures of the pixels of a sub-page are numbers");
    MLX90640_CalculateTo(frame, &params, 0.95f, ta - 8.0f, to_b);
    TEST_ASSERT(memcmp(to_a, to_b, sizeof to_a) == 0, "the temperatures of a frame are reproducible");

    frame[0] = 0x0100; frame[2] = 0x0400;
    MLX90640_CalculateTo(frame, &params, 0.95f, ta - 8.0f, to_a);
    TEST_ASSERT(isfinite(to_a[0]) && isfinite(to_a[2]) && to_a[2] > to_a[0], "a pixel with a higher raw value is hotter");
    frame[2] = 0x0100; frame[0] = 0x0400;
    MLX90640_CalculateTo(frame, &params, 0.95f, ta - 8.0f, to_b);
    TEST_ASSERT(to_b[0] > to_b[2] && fabsf((to_b[0] - to_b[2]) - (to_a[2] - to_a[0])) < 0.5f, "and the difference follows the pixel, not its place");
    synthetic_frame(frame, 0x0100);
    MLX90640_CalculateTo(frame, &params, 1.0f, ta - 8.0f, to_a);
    MLX90640_CalculateTo(frame, &params, 0.5f, ta - 8.0f, to_b);
    TEST_ASSERT(to_a[0] != to_b[0], "a lower emissivity gives another temperature for the same radiation");
    frame[833] = 1;
    MLX90640_CalculateTo(frame, &params, 0.95f, ta - 8.0f, to_a);
    TEST_ASSERT(isfinite(to_a[1]), "the other sub-page fills the other pixels");

    synthetic_frame(frame, 0x0100);
    MLX90640_GetImage(frame, &params, image_a);
    MLX90640_GetImage(frame, &params, image_b);
    TEST_ASSERT(memcmp(image_a, image_b, sizeof image_a) == 0 && isfinite(image_a[0]), "the image (the normalised data) is a number and reproducible");
    int all_resolutions = 1;
    for (int i = 0; i < 4; i++) {
        frame[832] = (uint16_t)(0x1901 | (i << MLX90640_CTRL_RESOLUTION_SHIFT));
        MLX90640_CalculateTo(frame, &params, 0.95f, 20.0f, to_a);
        if (!isfinite(MLX90640_GetVdd(frame, &params))) all_resolutions = 0;
    }
    TEST_ASSERT(all_resolutions, "every resolution gives a number");
    int ranges = 1;
    for (int pixel = 0x0000; pixel <= 0x7FF0; pixel += 0x0800) {   // from far under to far over the calibrated range: no fault in any temperature range
        synthetic_frame(frame, (uint16_t)pixel);
        MLX90640_CalculateTo(frame, &params, 0.95f, ta - 8.0f, to_a);
        MLX90640_GetImage(frame, &params, image_a);
        if (!isfinite(to_a[0])) ranges = 0;
    }
    TEST_ASSERT(ranges, "from a cold to a very hot raw value every temperature range of the calculation runs and returns a number");
}

// ---- the application of the MLX90640 head ------------------------------------------------------------------------------------------------
static void test_app_90640(int *failures)
{
    uint8_t chunk[32];
    fake_i2c_reset();
    fake_i2c.clear_trigger_on_reset = 1;
    fake_i2c.status_ready_after = 0;
    MLX90640_Direct_Init();
    TEST_ASSERT(fake_i2c.read_count >= 1, "start-up reads the EEPROM of the sensor");
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_IDLE, "nothing has been captured yet");
    for (int i = 0; i < 768; i++) fake_i2c_set_word((uint16_t)(MLX90640_PIXEL_DATA_START_ADDRESS + i), (uint16_t)(0x0300 + i));
    for (int i = 0; i < 64; i++) fake_i2c_set_word((uint16_t)(MLX90640_AUX_DATA_START_ADDRESS + i), (uint16_t)(0x0100 + i));
    MLX90640_Direct_TriggerCapture();
    uint8_t status = MLX90640_Direct_GetCaptureStatus();
    TEST_ASSERT(status == MLX_CAPTURE_READY || status == MLX_CAPTURE_ERROR, "a capture ends in ready or in error, never in busy");
    MLX90640_Direct_GetRawChunk(0, chunk);
    TEST_ASSERT(chunk[0] == 0x03 && chunk[1] == 0x00 && chunk[2] == 0x03 && chunk[3] == 0x01, "raw chunk 0 holds the first sixteen pixel words of the last frame, high byte first");
    MLX90640_Direct_GetRawChunk(47, chunk);
    TEST_ASSERT(chunk[31] == 0xFF && chunk[30] == 0x05, "raw chunk 47 ends with pixel 767");
    MLX90640_Direct_GetRawChunk(48, chunk);
    int zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "a raw chunk beyond the last is zeros");
    MLX90640_Direct_GetCalibratedChunk(200, chunk);
    zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "so is a calibrated one");
    MLX90640_Direct_GetCalibratedChunk(0, chunk);
    TEST_ASSERT(1, "a calibrated chunk of a synthetic frame is produced without a fault");

    fake_i2c_reset();
    fake_i2c.status_ready_after = 0;
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a sensor that does not start the measurement is an error");
    fake_i2c_reset();
    fake_i2c.clear_trigger_on_reset = 1;
    fake_i2c.status_ready_after = -1;
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR && fake_i2c.read_count <= (unsigned)MLX90640_DATA_READY_MAX_POLLS + 20, "a sensor that never says ready is an error after a bounded number of polls");
    fake_i2c_reset();
    fake_i2c.fail_read_at = 0;
    MLX90640_Direct_Init();
    MLX90640_Direct_TriggerCapture();
    TEST_ASSERT(MLX90640_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "with no answer at all the head reports an error");
}

// ---- the application of the MLX90641 head -----------------------------------------------------------------------------------------------
static void test_app_90641(int *failures)
{
    uint8_t chunk[32];
    memset(&m641, 0, sizeof m641);
    m641.frame_word0 = 0x1000;
    m641.to_value = 20.0f;
    MLX90641_Direct_Init();
    TEST_ASSERT(m641.extract_calls == 1, "a readable EEPROM is turned into parameters");
    m641.dump_rc = -1;
    MLX90641_Direct_Init();
    TEST_ASSERT(m641.extract_calls == 1, "an unreadable one is not");
    TEST_ASSERT(MLX90641_Direct_GetCaptureStatus() == MLX_CAPTURE_IDLE, "idle before the first capture");
    MLX90641_Direct_TriggerCapture();
    TEST_ASSERT(MLX90641_Direct_GetCaptureStatus() == MLX_CAPTURE_READY && m641.ta_calls == 1 && m641.to_calls == 1, "a capture reads the frame, the ambient temperature and the temperatures");
    TEST_ASSERT(m641.last_emissivity == 0.95f && m641.last_tr == 23.0f, "with an emissivity of 0.95 and a reflected temperature 8 degrees under the ambient one");
    MLX90641_Direct_GetRawChunk(0, chunk);
    TEST_ASSERT(chunk[0] == 0x10 && chunk[1] == 0x00 && chunk[3] == 0x01, "raw chunk 0 is the first sixteen words of the frame");
    MLX90641_Direct_GetRawChunk(11, chunk);
    TEST_ASSERT(chunk[30] == 0x10 && chunk[31] == 0xBF, "raw chunk 11 ends with word 191");
    MLX90641_Direct_GetRawChunk(12, chunk);
    int zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "this camera has twelve chunks: the thirteenth is zeros");
    MLX90641_Direct_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == (int16_t)(20.0f * MLX_TEMP_SCALE) && (int16_t)((chunk[2] << 8) | chunk[3]) == (int16_t)(20.5f * MLX_TEMP_SCALE), "calibrated values are the temperatures in hundredths of a degree");
    m641.to_value = 400.0f;
    MLX90641_Direct_TriggerCapture();
    MLX90641_Direct_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == 32767, "a value over the 16-bit range is clamped");
    m641.to_value = -400.0f;
    MLX90641_Direct_TriggerCapture();
    MLX90641_Direct_GetCalibratedChunk(0, chunk);
    TEST_ASSERT((int16_t)((chunk[0] << 8) | chunk[1]) == -32768, "and so is a value under it");
    MLX90641_Direct_GetCalibratedChunk(12, chunk);
    zeros = 1;
    for (int i = 0; i < 32; i++) if (chunk[i]) zeros = 0;
    TEST_ASSERT(zeros, "a calibrated chunk beyond the last is zeros");
    m641.trigger_rc = -9;
    MLX90641_Direct_TriggerCapture();
    TEST_ASSERT(MLX90641_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a trigger that fails is an error");
    m641.trigger_rc = 0; m641.frame_rc = -8;
    MLX90641_Direct_TriggerCapture();
    TEST_ASSERT(MLX90641_Direct_GetCaptureStatus() == MLX_CAPTURE_ERROR, "a frame that cannot be read is an error");
}

void run_mlx_api_tests(int *failures)
{
    test_control_register(failures);
    test_trigger_and_frame(failures);
    test_bad_pixels(failures);
    test_synthetic_calibration(failures);
    test_app_90640(failures);
    test_app_90641(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - MLX90640_API.c beyond its start-up, and the MLX90640 and MLX90641 applications\n");
    run_mlx_api_tests(&failures);
    if (failures == 0) {
        printf("  OK   all MLX API host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

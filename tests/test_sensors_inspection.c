// =============================================================================
// URTC Firmware - Host logic tests for the sensor loops and the thermal inspection commands
// (firmware_control_sensors.c, firmware_can_thermalinspection.c, firmware_thermal_stats.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Three small modules around the sensors: the telemetry loops (analogue and digital readings for the vacuum pickup and the flying probe, the
// debounced end-stop), the thermal camera commands 0x250..0x255 (which sensor answers - the MLX90640, 90641 or 90642 wired to the head, or the
// one on the expansion board - and the 32-byte chunks sent as four frames) and the lifetime thermal statistics (heater time, faults, the
// query answered on 0x1AB). The real sources are compiled against the fake HAL; the three camera drivers and the expansion link are
// recorders that return recognisable bytes, so what is asserted is the routing and the framing, not the sensors.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_can_thermalinspection.h"
#include "firmware_common.h"
#include "firmware_control_sensors.h"
#include "firmware_expansion_i2c.h"
#include "firmware_thermal_stats.h"
#include "melexis_mlx90640/firmware_mlx90640_app.h"
#include "melexis_mlx90641/firmware_mlx90641_app.h"
#include "melexis_mlx90642/firmware_mlx90642_app.h"
#include "test_runner.h"

// ---- recorders -------------------------------------------------------------------------------------------------------------------------
static int calls[3][4];   // [sensor][trigger, status, raw chunk, calibrated chunk]
static uint8_t last_chunk_index[3][2];
static uint8_t driver_status[3] = { 0x02, 0x01, 0x00 };
#define RECORD_DRIVER(n, name)                                                                               \
    void name##_Direct_Init(void) {}                                                                         \
    void name##_Direct_TriggerCapture(void) { calls[n][0]++; }                                               \
    uint8_t name##_Direct_GetCaptureStatus(void) { calls[n][1]++; return driver_status[n]; }                 \
    void name##_Direct_GetRawChunk(uint8_t i, uint8_t out[32]) { calls[n][2]++; last_chunk_index[n][0] = i; for (int k = 0; k < 32; k++) out[k] = (uint8_t)(0x40 * (n + 1) + k); } \
    void name##_Direct_GetCalibratedChunk(uint8_t i, uint8_t out[32]) { calls[n][3]++; last_chunk_index[n][1] = i; for (int k = 0; k < 32; k++) out[k] = (uint8_t)(0x80 + 0x10 * n + k); }
RECORD_DRIVER(0, MLX90640)
RECORD_DRIVER(1, MLX90641)
RECORD_DRIVER(2, MLX90642)

static uint8_t i2c_register, i2c_param, i2c_write_calls, i2c_read_ok, i2c_status_value;
uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len) { (void)data; (void)len; i2c_register = reg; i2c_write_calls++; return 1; }
uint8_t ExpansionI2C_SlaveReadRegister(uint8_t reg, uint8_t *data, uint8_t len) { i2c_register = reg; if (!i2c_read_ok) return 0; memset(data, i2c_status_value, len); return 1; }
uint8_t ExpansionI2C_SlaveReadRegisterWithParam(uint8_t reg, uint8_t param, uint8_t *data, uint8_t len)
{
    i2c_register = reg;
    i2c_param = param;
    if (!i2c_read_ok) return 0;
    for (int k = 0; k < len; k++) data[k] = (uint8_t)(0xC0 + k);
    return 1;
}

static void world(void)
{
    fake_hal_reset_recording();
    memset(calls, 0, sizeof calls);
    memset(last_chunk_index, 0, sizeof last_chunk_index);
    i2c_write_calls = 0;
    i2c_read_ok = 1;
    i2c_status_value = 0x02;
    boot_sequence_active = 0;
    expansion_board_type = 0;
    mlx_sensor_variant = MLX_VARIANT_NONE;
    fake_tick = 0;
    fake_tick_step = 0;
    fake_adc_status = HAL_OK;
    fake_adc_value = 0;
}

static void rx(uint32_t id, uint32_t dlc, uint8_t first)
{
    memset(&rxHeader, 0, sizeof rxHeader);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
    memset(rxData, 0, sizeof rxData);
    rxData[0] = first;
}

// ---- the telemetry loops ---------------------------------------------------------------------------------------------------------------------
static void telemetry(int *failures)
{
    world();
    system_error_flag = 0;
    sensor_analog_reading = 0;
    active_tool = TOOL_DRILL;
    fake_adc_value = 1234;
    Control_SensorTelemetry();
    TEST_ASSERT(fake_adc_starts == 0 && sensor_analog_reading == 0, "only the vacuum pickup and the flying probe read the analogue sensor");

    for (int tool = 0; tool < 2; tool++) {
        world();
        active_tool = tool ? TOOL_FLYING_PROBE : TOOL_VACUUM_PICKUP;
        fake_adc_value = 1234;
        fake_pin_set(GPIOB, TCRT_D0_DIG_PIN, GPIO_PIN_SET);
        Control_SensorTelemetry();
        TEST_ASSERT(sensor_analog_reading == 1234 && sensor_digital_reading == 1 && fake_adc_starts == 1 && fake_adc_stops == 1 && system_error_flag == 0, "the analogue and the digital reading are taken and the conversion stopped");
    }
    world();
    active_tool = TOOL_VACUUM_PICKUP;
    sensor_analog_reading = 77;
    fake_adc_status = HAL_TIMEOUT;
    fake_pin_set(GPIOB, TCRT_D0_DIG_PIN, GPIO_PIN_RESET);
    Control_SensorTelemetry();
    TEST_ASSERT(system_error_flag == 1 && sensor_analog_reading == 77 && fake_adc_stops == 1 && sensor_digital_reading == 0, "a conversion that does not complete latches the error and keeps the old analogue value");
    system_error_flag = 0;
}

static void endstop(int *failures)
{
    world();
    active_tool = TOOL_SOLDERING_IRON;
    endstop_triggered = 0;
    fake_pin_set(GPIOB, TOUCH_IN_PIN, GPIO_PIN_RESET);
    Control_EndstopTelemetry();
    Control_EndstopTelemetry();
    TEST_ASSERT(endstop_triggered == 0, "only the drill, the laser and the inspection head read the end-stop");

    active_tool = TOOL_DRILL;
    fake_pin_set(GPIOB, TOUCH_IN_PIN, GPIO_PIN_RESET);   // pressed (active low)
    Control_EndstopTelemetry();
    TEST_ASSERT(endstop_triggered == 0, "one reading is not enough: the first call only remembers it");
    Control_EndstopTelemetry();
    TEST_ASSERT(endstop_triggered == 1, "two equal readings in a row are taken as pressed");
    fake_pin_set(GPIOB, TOUCH_IN_PIN, GPIO_PIN_SET);
    Control_EndstopTelemetry();
    TEST_ASSERT(endstop_triggered == 1, "a single different reading (a bounce) does not release it");
    Control_EndstopTelemetry();
    TEST_ASSERT(endstop_triggered == 0, "two in a row do");
    fake_pin_set(GPIOB, TOUCH_IN_PIN, GPIO_PIN_RESET);
    Control_EndstopTelemetry();
    fake_pin_set(GPIOB, TOUCH_IN_PIN, GPIO_PIN_SET);
    Control_EndstopTelemetry();
    TEST_ASSERT(endstop_triggered == 0, "a bounce while released does not press it");
}

// ---- the thermal camera -------------------------------------------------------------------------------------------------------------------------
static void inspection_trigger(int *failures)
{
    static const uint8_t variants[3] = { MLX_VARIANT_90640, MLX_VARIANT_90641, MLX_VARIANT_90642 };
    for (int s = 0; s < 3; s++) {
        world();
        expansion_board_type = 6;
        mlx_sensor_variant = variants[s];
        rx(0x250, 0, 0);
        Handle_CAN_ThermalInspection();
        int others = 0;
        for (int o = 0; o < 3; o++) others += o != s ? calls[o][0] : 0;
        TEST_ASSERT(calls[s][0] == 1 && others == 0 && i2c_write_calls == 0, "0x250 triggers the sensor wired to the head, and only that one");
    }
    world();
    expansion_board_type = 6;
    mlx_sensor_variant = MLX_VARIANT_NONE;
    rx(0x250, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(calls[0][0] + calls[1][0] + calls[2][0] == 0 && i2c_write_calls == 0, "with no sensor variant set nothing is triggered");
    world();
    expansion_board_type = 0;
    rx(0x250, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(i2c_write_calls == 1 && i2c_register == 0x10, "any other board relays the trigger to the expansion board (register 0x10)");
    world();
    boot_sequence_active = 1;
    expansion_board_type = 0;
    rx(0x250, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(i2c_write_calls == 0, "0x250 is refused during the boot sequence");
}

static void inspection_status(int *failures)
{
    static const uint8_t variants[3] = { MLX_VARIANT_90640, MLX_VARIANT_90641, MLX_VARIANT_90642 };
    for (int s = 0; s < 3; s++) {
        world();
        expansion_board_type = 6;
        mlx_sensor_variant = variants[s];
        rx(0x251, 0, 0);
        Handle_CAN_ThermalInspection();
        TEST_ASSERT(calls[s][1] == 1 && fake_tx_count == 1 && fake_tx[0].id == 0x251 && fake_tx[0].dlc == 1 && fake_tx[0].data[0] == driver_status[s], "0x251 answers the status of the sensor wired to the head");
    }
    world();
    expansion_board_type = 6;
    mlx_sensor_variant = MLX_VARIANT_NONE;
    rx(0x251, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].data[0] == MLX_CAPTURE_ERROR, "with no sensor variant the status is the error status, not silence");
    world();
    expansion_board_type = 0;
    i2c_status_value = 0x01;
    rx(0x251, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].data[0] == 0x01 && i2c_register == 0x11, "any other board: the status is read from the expansion board (register 0x11)");
    world();
    expansion_board_type = 0;
    i2c_read_ok = 0;
    rx(0x251, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0, "no answer when the expansion board did not answer");
    world();
    expansion_board_type = 6;
    mlx_sensor_variant = MLX_VARIANT_90640;
    fake_tx_mailboxes_free = 0;
    rx(0x251, 0, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0, "no answer when no transmit mailbox is free");
}

static void expect_chunk(int *failures, uint32_t answer_id, uint8_t first_byte, const char *what)
{
    int ok = fake_tx_count == 4;
    for (int f = 0; f < 4 && ok; f++) {
        ok = fake_tx[f].id == answer_id && fake_tx[f].dlc == 8;
        for (int b = 0; b < 8 && ok; b++) ok = fake_tx[f].data[b] == (uint8_t)(first_byte + f * 8 + b);
    }
    TEST_ASSERT(ok, what);
}

static void inspection_chunks(int *failures)
{
    static const uint8_t variants[3] = { MLX_VARIANT_90640, MLX_VARIANT_90641, MLX_VARIANT_90642 };
    for (int s = 0; s < 3; s++) {
        world();
        expansion_board_type = 6;
        mlx_sensor_variant = variants[s];
        rx(0x252, 1, 17);
        Handle_CAN_ThermalInspection();
        TEST_ASSERT(calls[s][2] == 1 && last_chunk_index[s][0] == 17, "0x252 asks the sensor for the raw chunk the host named");
        expect_chunk(failures, 0x253, (uint8_t)(0x40 * (s + 1)), "0x253 carries the 32 raw bytes as four frames of eight, in order");
        world();
        expansion_board_type = 6;
        mlx_sensor_variant = variants[s];
        rx(0x254, 1, 9);
        Handle_CAN_ThermalInspection();
        TEST_ASSERT(calls[s][3] == 1 && last_chunk_index[s][1] == 9, "0x254 asks for the calibrated chunk");
        expect_chunk(failures, 0x255, (uint8_t)(0x80 + 0x10 * s), "0x255 carries the 32 calibrated bytes as four frames");
    }
    world();
    expansion_board_type = 0;
    rx(0x252, 1, 5);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(i2c_register == 0x12 && i2c_param == 5, "any other board: the raw chunk is read from register 0x12 with the chunk index");
    expect_chunk(failures, 0x253, 0xC0, "and relayed as four frames");
    world();
    rx(0x254, 1, 6);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(i2c_register == 0x13 && i2c_param == 6, "the calibrated chunk is register 0x13");
    expect_chunk(failures, 0x255, 0xC0, "and relayed as four frames");
    world();
    i2c_read_ok = 0;
    rx(0x252, 1, 5);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0, "no chunk when the expansion board did not answer");
    world();
    expansion_board_type = 6;
    mlx_sensor_variant = MLX_VARIANT_NONE;
    rx(0x252, 1, 5);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0, "no chunk when there is no sensor variant");
    world();
    rx(0x252, 0, 5);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0, "a chunk request without its index byte is ignored");
    world();
    fake_tx_mailboxes_free = 0;
    expansion_board_type = 6;
    mlx_sensor_variant = MLX_VARIANT_90640;
    rx(0x252, 1, 5);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0, "no frame when no transmit mailbox is free (the chunk is not sent out of order)");
    world();
    rx(0x256, 1, 0);
    Handle_CAN_ThermalInspection();
    TEST_ASSERT(fake_tx_count == 0 && i2c_write_calls == 0, "another identifier does nothing");
}

// ---- the lifetime thermal statistics ---------------------------------------------------------------------------------------------------------------
static void statistics(int *failures)
{
    world();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_write_calls = 0;
    ThermalStats_LoadFromFram();
    TEST_ASSERT(fake_fram_write_calls == 0, "a blank F-RAM is loaded as zeros and nothing is written yet");
    rx(0x1AA, 0, 0);
    ThermalStats_HandleQuery();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x1AB && fake_tx[0].dlc == 8, "0x1AA is answered with eight bytes on 0x1AB");
    uint8_t blank_answer[8];
    memcpy(blank_answer, fake_tx[0].data, 8);

    // the heater is on for two minutes: the time is counted, and saved once a minute at most
    fake_pin_set(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
    for (uint32_t t = 0; t <= 120000; t += 1000) {
        fake_tick = t;
        ThermalStats_Service(1);
    }
    TEST_ASSERT(fake_fram_write_calls > 0, "the heater time is saved after a minute");
    int writes_after_two_minutes = fake_fram_write_calls;
    for (uint32_t t = 121000; t <= 130000; t += 1000) {
        fake_tick = t;
        ThermalStats_Service(1);
    }
    TEST_ASSERT(fake_fram_write_calls == writes_after_two_minutes, "and not again within the minute");
    fake_hal_reset_recording();
    ThermalStats_HandleQuery();
    TEST_ASSERT(memcmp(blank_answer, fake_tx[0].data, 8) != 0, "the answer reflects the heater time counted");

    // may_write = 0 never writes
    fake_pin_set(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_SET);
    int before = fake_fram_write_calls;
    for (uint32_t t = 200000; t <= 400000; t += 1000) {
        fake_tick = t;
        ThermalStats_Service(0);
    }
    TEST_ASSERT(fake_fram_write_calls == before, "with the F-RAM busy (may_write = 0) nothing is written");

    // a fault is saved at once and is counted in the answer
    uint8_t before_fault[8];
    fake_hal_reset_recording();
    ThermalStats_HandleQuery();
    memcpy(before_fault, fake_tx[0].data, 8);
    before = fake_fram_write_calls;
    ThermalStats_NoteFault(2);
    TEST_ASSERT(fake_fram_write_calls > before, "a fault is written to the F-RAM at once");
    fake_hal_reset_recording();
    ThermalStats_HandleQuery();
    TEST_ASSERT(memcmp(before_fault, fake_tx[0].data, 8) != 0, "and counted in the answer");

    // what was saved comes back after a restart
    uint8_t saved_answer[8];
    memcpy(saved_answer, fake_tx[0].data, 8);
    ThermalStats_LoadFromFram();
    fake_hal_reset_recording();
    ThermalStats_HandleQuery();
    TEST_ASSERT(memcmp(saved_answer, fake_tx[0].data, 8) == 0, "the statistics loaded after a restart are the ones that were saved");

    // no F-RAM: counted in RAM only
    fake_fram_fail_reads = 1;
    ThermalStats_LoadFromFram();
    fake_fram_fail_reads = 0;
    fake_hal_reset_recording();
    ThermalStats_HandleQuery();
    TEST_ASSERT(fake_tx_count == 1, "without an F-RAM the query is still answered");
    fake_hal_reset_recording();
    fake_tx_mailboxes_free = 0;
    ThermalStats_HandleQuery();
    TEST_ASSERT(fake_tx_count == 0, "no answer when no transmit mailbox is free");
}

int main(void)
{
    int fails = 0;
    int *failures = &fails;
    printf("URTC host logic tests - the sensor loops, the thermal camera commands and the thermal statistics\n");
    telemetry(failures);
    endstop(failures);
    inspection_trigger(failures);
    inspection_status(failures);
    inspection_chunks(failures);
    statistics(failures);
    if (fails == 0) {
        printf("  OK   all sensor, thermal inspection and thermal statistics host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", fails);
    return 1;
}

// =============================================================================
// URTC Firmware - Host logic tests for the application as a whole: the start-up and the main loop (STM32F303CC_main.c with every module of the master)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Every module of the master firmware is linked into one binary against the fake HAL and the real main() is run (renamed) from the power-up to the
// main loop, with the identity jumpers of a tool, the F-RAM blank and a millisecond of time for every read of the tick. The loop is left through the
// watchdog refresh, and a hook that runs on every refresh lets a test act on the state in the middle of the run (raise a bus error, a latched error, an
// AOI strobe request, a night-mode command) and watch what the loop does with it. The frames the loop sends on the CAN bus and on the display are counted
// on the way, so the long runs do not fill the recording buffers.
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_ads1115.h"
#include "MLX90641_API.h"
#include "test_runner.h"

int firmware_main_entry(void);

// ---- the MLX90641 library (C++ in the firmware), replaced by recorders --------------------------------------------------------------------------
int MLX90641_DumpEE(uint8_t slaveAddr, uint16_t *eeData) { (void)slaveAddr; (void)eeData; return -1; }
int MLX90641_ExtractParameters(uint16_t *eeData, paramsMLX90641 *p) { (void)eeData; (void)p; return 0; }
int MLX90641_TriggerMeasurement(uint8_t slaveAddr) { (void)slaveAddr; return -1; }
int MLX90641_GetFrameData(uint8_t slaveAddr, uint16_t *frame) { (void)slaveAddr; (void)frame; return -1; }
float MLX90641_GetTa(uint16_t *frame, const paramsMLX90641 *p) { (void)frame; (void)p; return 25.0f; }
void MLX90641_CalculateTo(uint16_t *frame, const paramsMLX90641 *p, float e, float tr, float *result) { (void)frame; (void)p; (void)e; (void)tr; (void)result; }

// ---- what the hook of the watchdog sees ---------------------------------------------------------------------------------------------------------
static int tx_by_id[0x800];
static int i2c_frames, night_sequences, strobe_seen_on, strobe_seen_off_after_on, ring_red_when_on;
static uint8_t last_cmds[3];
static void (*scenario)(int refreshes);

static void sink(int refreshes)
{
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id < 0x800) tx_by_id[fake_tx[i].id]++;
    fake_tx_count = 0;
    for (int i = 0; i < fake_i2c_frame_count; i++) {
        const FakeI2cFrame *f = &fake_i2c_frames[i];
        i2c_frames++;
        if (f->len == 2 && f->data[0] == 0x00) {
            last_cmds[0] = last_cmds[1]; last_cmds[1] = last_cmds[2]; last_cmds[2] = f->data[1];
            if (last_cmds[0] == 0xAF && last_cmds[1] == 0x81 && last_cmds[2] == 0x0F) night_sequences++;
        }
    }
    fake_i2c_frame_count = 0;
    fake_gpio_write_count = 0;
    if (aoi_strobe_active) { strobe_seen_on++; if (ring_pixels[0].R == 10) ring_red_when_on++; }
    else if (strobe_seen_on) strobe_seen_off_after_on++;
    if (scenario) scenario(refreshes);
}

static void power_up_state(void)
{
    // the values the globals have when the board is switched on
    active_tool = TOOL_INVALID;
    system_error_flag = 0; boot_sequence_active = 1; can_bus_error_flag = 0;
    steps_remaining = 0; step_pulse_high = 0; total_steps_setpoint = 1; solder_spool_position = 0;
    current_temperature = target_temperature = 0; sensor_analog_reading = 0; sensor_digital_reading = 0;
    drill_speed = 0; drill_rpm_pulses = drill_actual_rpm = 0; layer_fan_duty = 0; layer_fan_rpm_pulses = layer_fan_actual_rpm = 0;
    hotend_fan_duty = 0; hotend_fan_rpm_pulses = hotend_fan_actual_rpm = 0; laser_power_setpoint = 0; weld_pulse_active = 0;
    aoi_mode = 0; aoi_strobe_period = 0; aoi_strobe_active = 0; trigger_aoi_strobe_flag = 0;
    oled_night_mode = oled_mode_pending_flag = oled_night_mode_cmd = 0; update_led_ring_flag = 0;
    endstop_triggered = 0; probe_impact_counter = 0; can_led_tick = 0; led_host_override_active = 0;
    expansion_board_type = 0; free_tool_selection = 0; device_serial_number = 0; mlx_sensor_variant = MLX_VARIANT_NONE;
    memset((void *)ring_pixels, 0, sizeof ring_pixels);
    memset((void *)&led_state_pixel, 0, sizeof led_state_pixel);
}

static void jumpers(int value)
{
    fake_pin_set(ID0_PORT, ID0_PIN, (value & 0x01) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID1_PORT, ID1_PIN, (value & 0x02) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID2_PORT, ID2_PIN, (value & 0x04) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID3_PORT, ID3_PIN, (value & 0x08) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID4_PORT, ID4_PIN, (value & 0x10) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static int boot(int tool, int refreshes, void (*hook)(int))
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    fake_adc_value = 2000;
    power_up_state();
    jumpers(tool);
    memset(tx_by_id, 0, sizeof tx_by_id);
    i2c_frames = night_sequences = strobe_seen_on = strobe_seen_off_after_on = ring_red_when_on = 0;
    memset(last_cmds, 0, sizeof last_cmds);
    fake_tick = 1000;
    fake_tick_step = 1;
    scenario = hook;
    fake_iwdg_hook = sink;
    fake_iwdg_exit_after = refreshes;
    fake_reset_jmp_armed = 1;
    int left = setjmp(fake_reset_jmp);
    if (left == 0) firmware_main_entry();
    fake_reset_jmp_armed = 0;
    sink(0);
    return left;
}

static void test_power_up(int *failures)
{
    fake_rcc_flags = RCC_FLAG_IWDGRST | RCC_FLAG_PORRST;
    int left = boot(TOOL_SOLDERING_IRON, 400, 0);
    TEST_ASSERT(left == 2 && fake_iwdg_refreshes == 400, "the application starts and runs its main loop until the test leaves it");
    TEST_ASSERT(last_reset_cause == 0x09 && fake_rcc_flags == 0 && fake_rcc_flag_clears == 1, "the cause of the reset (watchdog and power-on) is read, kept and the flags are cleared");
    TEST_ASSERT(active_tool == TOOL_SOLDERING_IRON && raw_id_pin_value == 0, "the tool is the one the jumpers say");
    TEST_ASSERT(boot_sequence_active == 0, "the splash screen of five seconds ends and the boot sequence is over");
    TEST_ASSERT(fake_scb.VTOR == 0x08008000UL && fake_hal_inits == 1, "the vector table points at the application, and the HAL is started");
    TEST_ASSERT(fake_rcc_calls == 3 && fake_rcc_osc.PLL.PLLMUL == RCC_PLL_MUL16, "the clocks are configured");
    TEST_ASSERT(fake_iwdg_inits == 1 && fake_iwdg_prescaler == IWDG_PRESCALER_32 && fake_iwdg_reload == 999, "the watchdog is armed for one second");
    TEST_ASSERT(fake_can_inits == 1 && fake_can_starts == 1 && fake_adc_inits == 1 && fake_adc_channel.Channel == ADC_CHANNEL_1, "the CAN controller is started and the ADC reads the thermocouple channel");
    TEST_ASSERT(fake_tim_base_inits == 1 && fake_tim_base_starts_it == 1 && fake_spi_inits == 1 && fake_dma_inits == 1, "the step timer, the SPI and the DMA of the status LED are set up");
    TEST_ASSERT(fake_i2c_probes == 1 && i2c_frames > 100, "the display is probed, the splash screen and the tool screen are drawn");
    TEST_ASSERT(fake_gpio_toggle_count >= 4, "the diagnostic LED blinks");
    TEST_ASSERT(tx_by_id[0x135] >= 10, "the temperature of the iron goes out on 0x135 about every 150 ms");
    TEST_ASSERT(system_error_flag == 0 && target_temperature == 0, "nothing is hot and no error is latched");
}

static const struct { int tool; uint32_t id; } TELEMETRY[] = {
    {TOOL_SOLDERING_IRON, 0x135}, {TOOL_HOTAIR_REWORK, 0x135}, {TOOL_3D_PRINTER, 0x175}, {TOOL_VACUUM_PICKUP, 0x145},
    {TOOL_FLYING_PROBE, 0x243}, {TOOL_DRILL, 0x147}, {TOOL_LASER_ENGRAVER, 0x165}, {TOOL_AOI_INSPECTION, 0x155},
};

static void test_every_tool(int *failures)
{
    int boots = 0, identified = 0, ended = 0;
    for (int tool = 0; tool <= TOOL_ULTRASONIC_WELDER; tool++) {
        int left = boot(tool, 300, 0);
        boots += left == 2;
        identified += active_tool == (ToolMode_t)tool;
        ended += boot_sequence_active == 0;
        uint32_t expected = 0;
        for (unsigned i = 0; i < sizeof TELEMETRY / sizeof TELEMETRY[0]; i++) if (TELEMETRY[i].tool == tool) expected = TELEMETRY[i].id;
        if (expected) {
            TEST_ASSERT(tx_by_id[expected] >= 5, "the tool sends its own telemetry frame every 150 ms");
        }
        if (tool == TOOL_3D_PRINTER) TEST_ASSERT(tx_by_id[0x177] >= 5 && tx_by_id[0x179] >= 5 && tx_by_id[0x175] >= 5, "the printer sends three: the nozzle and the two fans");
        if (tool == TOOL_PASTE_DISPENSER || tool == TOOL_SCREWDRIVER || tool == TOOL_GRIPPER_NEMA) TEST_ASSERT(tx_by_id[0x135] == 0 && tx_by_id[0x145] == 0 && tx_by_id[0x147] == 0, "a tool with no telemetry of its own sends none of the others");
    }
    TEST_ASSERT(boots == TOOL_ULTRASONIC_WELDER + 1 && identified == boots && ended == boots, "all 25 tools boot, are identified by their jumpers and leave the boot sequence");
}

static void raise_bus_error(int refreshes) { if (refreshes == 80) can_bus_error_flag = 1; }
static void test_bus_error(int *failures)
{
    boot(TOOL_SOLDERING_IRON, 200, raise_bus_error);
    TEST_ASSERT(can_bus_error_flag == 0 && fake_can_abort_count >= 1 && fake_can_starts == 2, "a CAN bus error is cleared by aborting the pending requests and starting the controller again");
}

static void raise_latched_error(int refreshes) { if (refreshes == 80) system_error_flag = 1; }
static void test_error_blink(int *failures)
{
    boot(TOOL_SOLDERING_IRON, 400, 0);
    int calm = fake_gpio_toggle_count;
    boot(TOOL_SOLDERING_IRON, 400, raise_latched_error);
    TEST_ASSERT(fake_gpio_toggle_count > 2 * calm, "with an error latched the diagnostic LED blinks five times faster (every 100 ms instead of every 500 ms)");
}

static void request_strobe(int refreshes)
{
    if (refreshes == 80) { aoi_strobe_period = 60; ring_color_r = 10; ring_color_g = 20; ring_color_b = 30; trigger_aoi_strobe_flag = 1; }
}
static void test_strobe(int *failures)
{
    boot(TOOL_AOI_INSPECTION, 400, request_strobe);
    TEST_ASSERT(trigger_aoi_strobe_flag == 0 && strobe_seen_on >= 1, "an AOI strobe request is taken: the flag is cleared and the strobe turns on");
    TEST_ASSERT(ring_red_when_on >= 1, "with the colour that was asked for");
    TEST_ASSERT(strobe_seen_off_after_on >= 1 && aoi_strobe_active == 0 && ring_pixels[0].R == 0 && ring_pixels[7].B == 0, "and turns off by itself after its period, with the ring dark again");
}

static void request_night(int refreshes) { if (refreshes == 80) { oled_night_mode_cmd = 0x01; oled_mode_pending_flag = 1; } }
static void test_night_mode(int *failures)
{
    boot(TOOL_SOLDERING_IRON, 300, request_night);
    TEST_ASSERT(oled_mode_pending_flag == 0 && oled_night_mode == 0x01 && night_sequences == 1, "a night-mode command from the host is carried out once: display on at the lowest contrast");
}

static void test_status_led(int *failures)
{
    boot(TOOL_SOLDERING_IRON, 200, 0);
    TEST_ASSERT(led_state_pixel.G == 255 && led_state_pixel.R == 0, "with no bus activity the status LED is green");
    TEST_ASSERT(update_led_ring_flag == 0 && fake_spi_dma_count >= 1 && fake_spi_dma_len == 109, "and the colour was sent to the LED over SPI and DMA");
}

#undef main      // the suite renames the main() of STM32F303CC_main.c (-Dmain=firmware_main_entry); the main of this test is the real one
int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the whole master firmware: power-up and main loop\n");
    test_power_up(&failures);
    test_every_tool(&failures);
    test_bus_error(&failures);
    test_error_blink(&failures);
    test_strobe(&failures);
    test_night_mode(&failures);
    test_status_led(&failures);
    if (failures == 0) {
        printf("  OK   all firmware main host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

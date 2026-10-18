// =============================================================================
// URTC Firmware - Host logic tests for firmware_persistence.c and firmware_fram.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The saved state that survives a power cycle: the record checksum against an
// independent CRC-8/SMBUS, what a load accepts and rejects (blank chip, other
// layout, corruption, absent chip), that a load restores passive configuration
// but never re-arms an actuator, what a save records for each tool, and the
// change detection, the 3 s write limit and the retry after a failed write.
// The real sources are compiled against the fake HAL and its 8 KiB F-RAM.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_persistence.h"
#include "firmware_fram.h"

uint8_t SavedState_Checksum(const SavedState_t *s);

I2C_HandleTypeDef hi2c2;
volatile ToolMode_t active_tool;
volatile uint16_t target_temperature;
volatile uint8_t drill_speed, laser_power_setpoint, layer_fan_duty, hotend_fan_duty;
volatile LED_RGB_t led_state_pixel;
volatile LED_RGB_t ring_pixels[8];
volatile uint8_t oled_night_mode, system_error_flag;
volatile uint8_t expansion_board_type, free_tool_selection, device_serial_number, mlx_sensor_variant;
volatile int32_t solder_spool_position;
SavedState_t recovered_state, last_saved_state;
uint8_t had_valid_recovered_state;

// Independent bitwise CRC-8/SMBUS (polynomial 0x07, init 0) over all bytes but the last.
static uint8_t ref_crc8(const void *record, size_t len)
{
    const uint8_t *p = record;
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}

static void reset_world(void)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    fake_fram_write_calls = 0;
    memset(&recovered_state, 0, sizeof recovered_state);
    memset(&last_saved_state, 0, sizeof last_saved_state);
    had_valid_recovered_state = 0;
    active_tool = TOOL_SOLDERING_IRON;
    target_temperature = 0;
    drill_speed = laser_power_setpoint = layer_fan_duty = hotend_fan_duty = 0;
    memset((void *)&led_state_pixel, 0, sizeof led_state_pixel);
    memset((void *)ring_pixels, 0, sizeof ring_pixels);
    oled_night_mode = system_error_flag = 0;
    expansion_board_type = free_tool_selection = device_serial_number = mlx_sensor_variant = 0;
    solder_spool_position = 0;
    fake_pin_set(DRILL_FRIN_PORT, DRILL_FRIN_PIN, GPIO_PIN_RESET);
    fake_pin_set(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET);
    static uint32_t base = 0;
    base += 1000000u;          // every scenario starts long after the previous save, so the write limiter is not in the way
    fake_tick = base;
    fake_tick_step = 0;
}

static SavedState_t sample_record(void)
{
    SavedState_t s;
    memset(&s, 0, sizeof s);
    s.magic = SAVEDSTATE_MAGIC;
    s.struct_version = SAVEDSTATE_VERSION;
    s.active_tool_at_save = TOOL_3D_PRINTER;
    s.solder_setpoint = 350;
    s.printer_nozzle_setpoint = 210;
    s.drill_speed = 120;
    s.drill_direction = 1;
    s.laser_power = 77;
    s.laser_interlock = 1;
    s.layer_fan_power = 90;
    s.hotend_fan_power = 100;
    s.status_r = 1; s.status_g = 2; s.status_b = 3;
    s.ring_r = 4; s.ring_g = 5; s.ring_b = 6; s.ring_on = 1;
    s.oled_night_mode = 1;
    s.had_critical_error = 1;
    s.expansion_board_type = 4;
    s.free_tool_selection = 12;
    s.device_serial_number = 0xC3;
    s.mlx_sensor_variant = 2;
    s.solder_spool_position = -12345;
    s.checksum = ref_crc8(&s, sizeof s - 1);
    return s;
}

static void put_record(const SavedState_t *s) { memcpy(fake_fram + SAVEDSTATE_FRAM_ADDR, s, sizeof *s); }

static void test_checksum(int *failures)
{
    SavedState_t s = sample_record();
    TEST_ASSERT(SavedState_Checksum(&s) == s.checksum, "the firmware checksum equals an independent CRC-8/SMBUS of the record");
    int every_byte = 1;
    for (size_t i = 0; i < sizeof s - 1; i++) {
        SavedState_t t = s;
        ((uint8_t *)&t)[i] ^= 0x01;
        if (SavedState_Checksum(&t) == s.checksum) every_byte = 0;
    }
    TEST_ASSERT(every_byte, "a single-bit change in any byte of the record changes the checksum");
    SavedState_t t = s;
    t.checksum ^= 0xFF;
    TEST_ASSERT(SavedState_Checksum(&t) == s.checksum, "the checksum does not cover itself");
    SavedState_t z;
    memset(&z, 0, sizeof z);
    TEST_ASSERT(SavedState_Checksum(&z) == 0, "an all-zero record has checksum 0 (CRC-8 with a zero start value)");
}

static void test_load(int *failures)
{
    // Blank chip: nothing is accepted and nothing is touched.
    reset_world();
    solder_spool_position = 5;
    SavedState_Load();
    TEST_ASSERT(!had_valid_recovered_state && solder_spool_position == 5 && expansion_board_type == 0, "a blank F-RAM is not a saved state");

    // Chip absent / not answering.
    reset_world();
    SavedState_t s = sample_record();
    put_record(&s);
    fake_fram_fail_reads = 1;
    SavedState_Load();
    TEST_ASSERT(!had_valid_recovered_state && expansion_board_type == 0, "an F-RAM that does not answer leaves the defaults");

    // Wrong magic, wrong layout version, corrupted record.
    reset_world();
    SavedState_t bad = s; bad.magic ^= 1; bad.checksum = ref_crc8(&bad, sizeof bad - 1); put_record(&bad);
    SavedState_Load();
    TEST_ASSERT(!had_valid_recovered_state, "a record with the wrong magic is ignored even with a good checksum");
    reset_world();
    bad = s; bad.struct_version = SAVEDSTATE_VERSION + 1; bad.checksum = ref_crc8(&bad, sizeof bad - 1); put_record(&bad);
    SavedState_Load();
    TEST_ASSERT(!had_valid_recovered_state, "a record of another layout version is ignored");
    reset_world();
    bad = s; bad.solder_setpoint ^= 0x0100; put_record(&bad);
    SavedState_Load();
    TEST_ASSERT(!had_valid_recovered_state && device_serial_number == 0, "a record whose checksum does not match (an interrupted write) is ignored");

    // A good record: passive configuration is restored, setpoints are only exposed.
    reset_world();
    put_record(&s);
    target_temperature = 0; drill_speed = 0; laser_power_setpoint = 0; system_error_flag = 0;
    SavedState_Load();
    TEST_ASSERT(had_valid_recovered_state == 1, "a good record is accepted");
    TEST_ASSERT(memcmp(&recovered_state, &s, sizeof s) == 0, "the whole record is kept in recovered_state");
    TEST_ASSERT(memcmp(&last_saved_state, &s, sizeof s) == 0, "last_saved_state starts as what is really on the chip");
    TEST_ASSERT(led_state_pixel.R == 1 && led_state_pixel.G == 2 && led_state_pixel.B == 3, "the status LED colour is restored");
    int ring = 1;
    for (int i = 0; i < 8; i++) if (ring_pixels[i].R != 4 || ring_pixels[i].G != 5 || ring_pixels[i].B != 6) ring = 0;
    TEST_ASSERT(ring, "all eight ring pixels get the saved ring colour");
    TEST_ASSERT(oled_night_mode == 1 && expansion_board_type == 4 && free_tool_selection == 12 && device_serial_number == 0xC3 && mlx_sensor_variant == 2, "passive configuration (night mode, expansion type, free tool, serial, sensor variant) is restored");
    TEST_ASSERT(solder_spool_position == -12345, "the wire feeder position is restored with its sign");
    TEST_ASSERT(target_temperature == 0 && drill_speed == 0 && laser_power_setpoint == 0 && layer_fan_duty == 0 && hotend_fan_duty == 0, "no actuator setpoint is re-applied by a load");
    TEST_ASSERT(system_error_flag == 0, "a saved critical error does not re-latch the fault on its own (it is only reported)");
}

static void test_fram_chunks(int *failures)
{
    reset_world();
    uint8_t blob[100];
    for (int i = 0; i < 100; i++) blob[i] = (uint8_t)i;
    TEST_ASSERT(FRAM_WriteBytes(10, blob, 100), "a 100-byte write succeeds");
    int chunk_ok = fake_fram_write_calls >= 4;
    uint32_t total = 0;
    for (int i = 0; i < fake_fram_write_calls; i++) { total += fake_fram_write_sizes[i]; if (fake_fram_write_sizes[i] > FRAM_WRITE_CHUNK) chunk_ok = 0; }
    TEST_ASSERT(chunk_ok && total == 100, "the write is split into transactions of at most FRAM_WRITE_CHUNK bytes");
    TEST_ASSERT(memcmp(fake_fram + 10, blob, 100) == 0, "all bytes land where they belong");
    uint8_t back[100];
    TEST_ASSERT(FRAM_ReadBytes(10, back, 100) && memcmp(back, blob, 100) == 0, "reading them back returns the same bytes");
    fake_fram_fail_writes = 1;
    TEST_ASSERT(!FRAM_WriteBytes(0, blob, 4), "a failed transaction is reported");
    fake_fram_fail_writes = 0; fake_fram_fail_reads = 1;
    TEST_ASSERT(!FRAM_ReadBytes(0, back, 4), "a failed read is reported");
    fake_fram_fail_reads = 0;
    reset_world();
    FRAM_WriteBytes(0, blob, 4);
    TEST_ASSERT(fake_fram_write_calls == 1 && fake_fram_write_sizes[0] == 4, "a small write inside one chunk is one transaction");
    fake_fram_write_calls = 0;
    FRAM_WriteBytes(30, blob, 3);
    TEST_ASSERT(fake_fram_write_calls == 2 && fake_fram_write_sizes[0] == 2 && fake_fram_write_sizes[1] == 1, "a write that crosses a chunk boundary is split exactly at it");
}

static SavedState_t stored(void)
{
    SavedState_t s;
    memcpy(&s, fake_fram + SAVEDSTATE_FRAM_ADDR, sizeof s);
    return s;
}

static void test_save(int *failures)
{
    // First change is written, and the record is what each tool contributes.
    reset_world();
    active_tool = TOOL_SOLDERING_IRON; target_temperature = 345;
    led_state_pixel.R = 9;
    SavedState_MaybeSave();
    SavedState_t s = stored();
    TEST_ASSERT(s.magic == SAVEDSTATE_MAGIC && s.struct_version == SAVEDSTATE_VERSION, "the record starts with the magic and the layout version");
    TEST_ASSERT(s.checksum == ref_crc8(&s, sizeof s - 1), "the stored record carries a valid checksum");
    TEST_ASSERT(s.active_tool_at_save == TOOL_SOLDERING_IRON && s.solder_setpoint == 345 && s.printer_nozzle_setpoint == 0, "soldering iron: the setpoint is saved as the iron's, not the nozzle's");
    TEST_ASSERT(s.status_r == 9, "the status LED colour is saved");

    reset_world();
    active_tool = TOOL_3D_PRINTER; target_temperature = 215; layer_fan_duty = 80; hotend_fan_duty = 100;
    SavedState_MaybeSave();
    s = stored();
    TEST_ASSERT(s.printer_nozzle_setpoint == 215 && s.solder_setpoint == 0 && s.layer_fan_power == 80 && s.hotend_fan_power == 100, "3D printer: nozzle setpoint and both fans are saved");

    reset_world();
    active_tool = TOOL_DRILL; drill_speed = 200; target_temperature = 300;
    fake_pin_set(DRILL_FRIN_PORT, DRILL_FRIN_PIN, GPIO_PIN_SET);
    SavedState_MaybeSave();
    s = stored();
    TEST_ASSERT(s.drill_speed == 200 && s.drill_direction == 1 && s.solder_setpoint == 0 && s.printer_nozzle_setpoint == 0, "drill: speed and the direction read from the pin, no temperature");

    reset_world();
    active_tool = TOOL_LASER_ENGRAVER; laser_power_setpoint = 60;
    fake_pin_set(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_SET);
    SavedState_MaybeSave();
    s = stored();
    TEST_ASSERT(s.laser_power == 60 && s.laser_interlock == 1 && s.drill_speed == 0, "laser: power and interlock state are saved");

    reset_world();
    active_tool = TOOL_ELECTROMAGNET; system_error_flag = 1; device_serial_number = 7; solder_spool_position = -5;
    ring_pixels[0].G = 200;
    SavedState_MaybeSave();
    s = stored();
    TEST_ASSERT(s.had_critical_error == 1 && s.device_serial_number == 7 && s.solder_spool_position == -5, "the critical-error flag, the serial number and the feeder position are saved whatever the tool");
    TEST_ASSERT(s.ring_g == 200 && s.ring_on == 1, "the ring colour is saved and marked on");

    // Change detection.
    reset_world();
    active_tool = TOOL_SOLDERING_IRON; target_temperature = 300;
    SavedState_MaybeSave();
    int writes_after_first = fake_fram_write_calls;
    uint32_t saved_at = fake_tick;
    fake_tick = saved_at + 1000;
    SavedState_MaybeSave();
    TEST_ASSERT(fake_fram_write_calls == writes_after_first, "nothing changed: no write");

    // Rate limit: a change inside 3 s is held back and picked up later.
    target_temperature = 310;
    fake_tick = saved_at + 2999;
    SavedState_MaybeSave();
    TEST_ASSERT(fake_fram_write_calls == writes_after_first, "a change 2999 ms after the previous save is held back");
    fake_tick = saved_at + 3000;
    SavedState_MaybeSave();
    TEST_ASSERT(fake_fram_write_calls > writes_after_first, "exactly 3000 ms after the previous save it is written");
    TEST_ASSERT(stored().solder_setpoint == 310, "and what is written is the latest value");

    // A failed write is retried and does not pretend to have saved.
    reset_world();
    active_tool = TOOL_SOLDERING_IRON; target_temperature = 250;
    fake_fram_fail_writes = 1;
    SavedState_MaybeSave();
    TEST_ASSERT(last_saved_state.solder_setpoint == 0, "after a failed write the 'last saved' copy is not updated");
    fake_fram_fail_writes = 0;
    SavedState_MaybeSave();
    TEST_ASSERT(stored().solder_setpoint == 250 && stored().magic == SAVEDSTATE_MAGIC, "the next call writes it once the chip answers again (no 3 s wait after a failure)");

    // Round trip: what one boot saves, the next boot loads.
    reset_world();
    active_tool = TOOL_3D_PRINTER; target_temperature = 200; layer_fan_duty = 33;
    expansion_board_type = 3; free_tool_selection = 10; device_serial_number = 99; mlx_sensor_variant = 1; solder_spool_position = 4242; oled_night_mode = 1;
    led_state_pixel.R = 11; led_state_pixel.G = 22; led_state_pixel.B = 33;
    SavedState_MaybeSave();
    expansion_board_type = free_tool_selection = device_serial_number = mlx_sensor_variant = 0;
    solder_spool_position = 0; oled_night_mode = 0; memset((void *)&led_state_pixel, 0, sizeof led_state_pixel);
    target_temperature = 0; layer_fan_duty = 0;
    SavedState_Load();
    TEST_ASSERT(had_valid_recovered_state && expansion_board_type == 3 && free_tool_selection == 10 && device_serial_number == 99 && mlx_sensor_variant == 1 && solder_spool_position == 4242 && oled_night_mode == 1 && led_state_pixel.G == 22, "a saved state loads back with the same configuration");
    TEST_ASSERT(recovered_state.printer_nozzle_setpoint == 200 && recovered_state.layer_fan_power == 33 && target_temperature == 0 && layer_fan_duty == 0, "the heater and fan values come back as information only, not as live setpoints");
}

void run_persistence_tests(int *failures)
{
    test_checksum(failures);
    test_load(failures);
    test_fram_chunks(failures);
    test_save(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_persistence.c / firmware_fram.c\n");
    run_persistence_tests(&failures);
    if (failures == 0) {
        printf("  OK   all persistence host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

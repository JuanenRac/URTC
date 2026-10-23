// =============================================================================
// URTC Firmware - Host logic tests for the tool identification (firmware_identify.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// At power-up the head reads five jumper pins (a closed jumper reads low) to know which tool it carries, and everything that follows
// depends on the answer. The real firmware_identify.c is compiled against the fake HAL, with the pins and the F-RAM of the fake. What is
// asserted: the value 0..24 is the tool, a value without a tool (25..30) or an unreadable free configuration (31) puts the head in the
// inert state with the error latched and every output in its safe position, and the free configuration (value 31) takes the tool the
// person stored in the F-RAM only when that record is intact.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_identify.h"
#include "test_runner.h"

static void set_id_pins(unsigned value)
{
    // a closed jumper reads low: bit set -> pin low
    fake_pin_set(ID0_PORT, ID0_PIN, (value & 0x01) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID1_PORT, ID1_PIN, (value & 0x02) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID2_PORT, ID2_PIN, (value & 0x04) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID3_PORT, ID3_PIN, (value & 0x08) ? GPIO_PIN_RESET : GPIO_PIN_SET);
    fake_pin_set(ID4_PORT, ID4_PIN, (value & 0x10) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void world(void)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = 0;
    fake_tick = 0;
    fake_tick_step = 0;
    active_tool = TOOL_INVALID;
    system_error_flag = 0;
    raw_id_pin_value = 0xEE;
}

static int pin_level(GPIO_TypeDef *port, uint16_t pin)
{
    int level = -1;
    for (int i = 0; i < fake_gpio_write_count; i++) {
        if (fake_gpio_writes[i].port == port && fake_gpio_writes[i].pin == pin) {
            level = (int)fake_gpio_writes[i].state;
        }
    }
    return level;
}

// PB6 is the stepper enable (active low) in the motor tools and the laser interlock in the laser tool, so no level is right for both. In the
// inert state the stepper driver is the part that can move something: the pin must end HIGH (driver disabled), written once so nothing undoes it.
// The laser is held off because no laser handler runs without the laser tool selected.
static int safe_outputs(void)
{
    int pin_writes = 0;
    for (int i = 0; i < fake_gpio_write_count; i++) {
        if (fake_gpio_writes[i].port == TMC_ENN_PORT && fake_gpio_writes[i].pin == TMC_ENN_PIN) pin_writes++;
    }
    return pin_level(TMC_ENN_PORT, TMC_ENN_PIN) == GPIO_PIN_SET && pin_writes == 1 &&  // stepper driver disabled, no later write undoes it
           pin_level(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN) == GPIO_PIN_RESET &&           // drill brake engaged
           pin_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_RESET;                     // heater off
}

static void store_selection(uint8_t selection, int damage)
{
    SavedState_t state;
    memset(&state, 0, sizeof state);
    state.magic = SAVEDSTATE_MAGIC;
    state.struct_version = SAVEDSTATE_VERSION;
    state.free_tool_selection = selection;
    state.checksum = SavedState_Checksum(&state);
    if (damage == 1) state.checksum ^= 0x5A;
    if (damage == 2) state.magic ^= 1;
    if (damage == 3) state.struct_version = SAVEDSTATE_VERSION + 1;
    memcpy(&fake_fram[SAVEDSTATE_FRAM_ADDR], &state, sizeof state);
}

int main(void)
{
    int fails = 0;
    int *failures = &fails;
    printf("URTC host logic tests - the tool identification\n");

    // every value that is a tool
    for (unsigned value = 0; value <= 24; value++) {
        world();
        set_id_pins(value);
        Identify_PhysicalTool();
        TEST_ASSERT((unsigned)active_tool == value && raw_id_pin_value == value && system_error_flag == 0 && fake_gpio_write_count == 0, "a jumper value of 0 to 24 is that tool, with no error and no output touched");
    }

    // the reading must be the same three times in a row; the delay between two readings is 2 ms
    world();
    set_id_pins(10);
    Identify_PhysicalTool();
    TEST_ASSERT(fake_tick == 4, "three equal readings take two waits of 2 ms (none after the third)");

    // values without a tool
    for (unsigned value = 25; value <= 30; value++) {
        world();
        set_id_pins(value);
        Identify_PhysicalTool();
        TEST_ASSERT(active_tool == TOOL_INVALID && system_error_flag == 1 && raw_id_pin_value == value, "a value without a tool is the inert state with the error latched");
        TEST_ASSERT(safe_outputs(), "and every output goes to its safe position (stepper driver disabled, brake on, heater off)");
    }

    // free configuration: value 31 and the tool the person stored
    for (unsigned selection = 1; selection <= 25; selection++) {
        world();
        store_selection((uint8_t)selection, 0);
        set_id_pins(31);
        Identify_PhysicalTool();
        TEST_ASSERT((unsigned)active_tool == selection - 1 && system_error_flag == 0, "free configuration: the stored selection 1..25 is the tool 0..24");
    }
    world();
    store_selection(0, 0);
    set_id_pins(31);
    Identify_PhysicalTool();
    TEST_ASSERT(active_tool == TOOL_INVALID && system_error_flag == 1 && safe_outputs(), "free configuration with no selection (0) is the inert state");
    world();
    store_selection(26, 0);
    set_id_pins(31);
    Identify_PhysicalTool();
    TEST_ASSERT(active_tool == TOOL_INVALID && system_error_flag == 1 && safe_outputs(), "a selection above 25 is the inert state");
    for (int damage = 1; damage <= 3; damage++) {
        world();
        store_selection(5, damage);
        set_id_pins(31);
        Identify_PhysicalTool();
        TEST_ASSERT(active_tool == TOOL_INVALID && system_error_flag == 1 && safe_outputs(), "a record with a bad checksum, magic number or version is not trusted");
    }
    world();
    set_id_pins(31);
    Identify_PhysicalTool();
    TEST_ASSERT(active_tool == TOOL_INVALID && system_error_flag == 1, "a blank F-RAM is the inert state");
    world();
    store_selection(5, 0);
    fake_fram_fail_reads = 1;
    set_id_pins(31);
    Identify_PhysicalTool();
    TEST_ASSERT(active_tool == TOOL_INVALID && system_error_flag == 1 && safe_outputs(), "an F-RAM that does not answer is the inert state");

    if (fails == 0) {
        printf("  OK   all tool identification host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", fails);
    return 1;
}

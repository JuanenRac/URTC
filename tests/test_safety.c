// =============================================================================
// URTC Firmware - Host logic tests for firmware_safety.c (clear-error command, communication watchdog of the steppers and the electromagnet)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The glue compiled against the fake HAL with the real switches (fw_features.c, firmware_features.c, the fake F-RAM): with the switches off
// nothing changes whatever the silence; with them on a latched error is lifted only when the tool is cool and every actuator is at zero, a stepper
// move is cut after the silence its length allows, and the electromagnet lets go only with both switches on. Nothing here has run on a board.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_features.h"
#include "firmware_fram.h"
#include "firmware_safety.h"
#include "urtc_can_ids.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
I2C_HandleTypeDef hi2c2;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag, system_error_reason;
volatile uint8_t thermal_fault_code;
volatile uint16_t current_temperature, target_temperature;
volatile uint8_t last_reset_cause = 0x08;
volatile uint8_t laser_power_setpoint, drill_speed, layer_fan_duty, uv_curing_duty, hotair_blower_duty;
volatile uint32_t steps_remaining;

static int guards_reset;
void Control_ThermalGuardsReset(void) { guards_reset++; }

static void write_mask(uint8_t mask)
{
    FeatConfig_t c;
    Feat_ConfigBlank(&c);
    c.mask = mask;
    Feat_ConfigSeal(&c);
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &c, sizeof c);
}

// A power-up with this mask in the F-RAM: the switches are read once.
static void boot_with(uint8_t mask)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    write_mask(mask);
    fake_tick = 100000;
    fake_tick_step = 0;
    active_tool = TOOL_SOLDERING_IRON;
    system_error_flag = 0;
    system_error_reason = 0;
    thermal_fault_code = 0;
    current_temperature = 25;
    target_temperature = 0;
    laser_power_setpoint = drill_speed = layer_fan_duty = uv_curing_duty = hotair_blower_duty = 0;
    steps_remaining = 0;
    guards_reset = 0;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    Features_Init();
    Safety_NoteHostFrame();
}

static void frame(uint32_t id, const uint8_t *data, uint32_t dlc)
{
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
}

// The fake pin read does not follow the writes: what the firmware drove is what the recorder saw.
static int coil_released(void)
{
    for (int i = 0; i < fake_gpio_write_count; i++) {
        if (fake_gpio_writes[i].port == GPIOA && fake_gpio_writes[i].pin == GPIO_PIN_1 && fake_gpio_writes[i].state == GPIO_PIN_RESET) return 1;
    }
    return 0;
}

static const uint8_t CLER[8] = { 0x43, 0x4C, 0x45, 0x52, 0, 0, 0, 0 };   // eight bytes so that a longer frame can be built from it

static void raise_error(uint8_t reason)
{
    RAISE_SYSTEM_ERROR(reason);
}

static void test_the_first_reason_wins(int *failures)
{
    boot_with(0);
    TEST_ASSERT(Safety_ErrorReason() == URTC_ERROR_REASON_NONE && Safety_LastClearResult() == 0, "no error, no clear since power-up");
    raise_error(URTC_ERROR_REASON_THERMAL);
    raise_error(URTC_ERROR_REASON_COMMS_WATCHDOG);
    TEST_ASSERT(system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_THERMAL, "the second reason does not replace the first");
    system_error_flag = 1; system_error_reason = 0;
    TEST_ASSERT(Safety_ErrorReason() == URTC_ERROR_REASON_UNKNOWN, "an error without a recorded reason is reported as unknown, not as none");
}

static void test_clear_with_the_switch_off(int *failures)
{
    boot_with(0);
    raise_error(URTC_ERROR_REASON_COMMS_WATCHDOG);
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    TEST_ASSERT(Safety_HandleClearError() == 1, "the command with its magic is answered");
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_SWITCH_OFF, "with the switch off the error stays and the answer says why");
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 3);
    TEST_ASSERT(Safety_HandleClearError() == 0, "a short frame is ignored");
    const uint8_t wrong[8] = { 0x43, 0x4C, 0x45, 0x53, 0, 0, 0, 0 };
    frame(URTC_CAN_ID_CLEAR_ERROR, wrong, 4);
    TEST_ASSERT(Safety_HandleClearError() == 0, "a wrong magic is ignored");
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 5);
    TEST_ASSERT(Safety_HandleClearError() == 0, "a long frame is ignored");
}

static void test_clear_with_the_switch_on(int *failures)
{
    boot_with(FEAT_CLEAR_ERROR);
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    TEST_ASSERT(Safety_HandleClearError() == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_NO_ERROR, "no error, nothing to lift");

    raise_error(URTC_ERROR_REASON_THERMAL);
    thermal_fault_code = 2;
    current_temperature = 50;
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_HOT, "50 C is not below the limit");

    current_temperature = 49;
    target_temperature = 200;
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_ACTUATOR_ON, "a heater set-point that is not zero keeps the error");

    target_temperature = 0;
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 0 && Safety_ErrorReason() == URTC_ERROR_REASON_NONE && thermal_fault_code == 0 && guards_reset == 1 &&
                Safety_LastClearResult() == URTC_CLEAR_ERROR_CLEARED, "cool and quiet: the error, its reason and the thermal fault are lifted and the guards start afresh");
    Safety_HandleClearError();
    TEST_ASSERT(Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_NO_ERROR, "a second command finds nothing to lift");
}

static void test_clear_every_actuator_and_the_special_cases(int *failures)
{
    volatile uint8_t *byte_actuators[] = { &laser_power_setpoint, &drill_speed, &layer_fan_duty, &uv_curing_duty, &hotair_blower_duty };
    int kept = 0;
    for (unsigned i = 0; i < sizeof byte_actuators / sizeof byte_actuators[0]; i++) {
        boot_with(FEAT_CLEAR_ERROR);
        raise_error(URTC_ERROR_REASON_COMMS_WATCHDOG);
        *byte_actuators[i] = 1;
        frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
        Safety_HandleClearError();
        if (system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_ACTUATOR_ON) kept++;
    }
    TEST_ASSERT(kept == 5, "each of the five actuators that is not at zero keeps the error");

    boot_with(FEAT_CLEAR_ERROR);
    raise_error(URTC_ERROR_REASON_COMMS_WATCHDOG);
    steps_remaining = 10;
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_ACTUATOR_ON, "a stepper move left over keeps the error");

    boot_with(FEAT_CLEAR_ERROR);
    active_tool = TOOL_ELECTROMAGNET;
    raise_error(URTC_ERROR_REASON_COMMS_WATCHDOG);
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_ACTUATOR_ON, "an energised coil is an actuator that is on");

    boot_with(FEAT_CLEAR_ERROR);
    active_tool = TOOL_INVALID;
    raise_error(URTC_ERROR_REASON_TOOL_ID);
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_NOT_CLEARABLE, "the error of a missing tool is never lifted");

    boot_with(FEAT_CLEAR_ERROR);
    raise_error(URTC_ERROR_REASON_SENSOR);
    current_temperature = 20;
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 1 && Safety_LastClearResult() == URTC_CLEAR_ERROR_REFUSED_HOT, "a heater tool whose reading was not trusted counts as hot");

    boot_with(FEAT_CLEAR_ERROR);
    active_tool = TOOL_DRILL;
    raise_error(URTC_ERROR_REASON_COMMS_WATCHDOG);
    current_temperature = 300;   // a tool without a heater has nothing that can be hot
    frame(URTC_CAN_ID_CLEAR_ERROR, CLER, 4);
    Safety_HandleClearError();
    TEST_ASSERT(system_error_flag == 0 && Safety_LastClearResult() == URTC_CLEAR_ERROR_CLEARED, "a drill is lifted whatever the stale temperature variable holds");
}

static void test_stepper_watchdog(int *failures)
{
    // switch off: a move that runs for ever with a silent host is not touched
    boot_with(0);
    steps_remaining = 1000000;
    Safety_NoteStepperMove(1000000, 0);
    fake_tick += 10000000;
    Safety_Service();
    TEST_ASSERT(steps_remaining == 1000000 && system_error_flag == 0, "with the switch off a silent host cuts nothing");

    // switch on: the limit of a move of 4000 steps at the default speed is 4000 ms
    boot_with(FEAT_COMMS_WATCHDOG);
    steps_remaining = 4000;
    Safety_NoteStepperMove(4000, 0);
    fake_tick += 4000;
    Safety_Service();
    TEST_ASSERT(steps_remaining == 4000 && system_error_flag == 0, "at the limit the move goes on");
    fake_tick += 1;
    Safety_Service();
    TEST_ASSERT(steps_remaining == 0 && system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_STEPPER_SILENCE, "one millisecond over the limit the move is cut and the reason recorded");

    // a frame of the host keeps the move alive
    boot_with(FEAT_COMMS_WATCHDOG);
    steps_remaining = 4000;
    Safety_NoteStepperMove(4000, 0);
    for (int i = 0; i < 20; i++) {
        fake_tick += 3000;
        Safety_NoteHostFrame();
        Safety_Service();
    }
    TEST_ASSERT(steps_remaining == 4000 && system_error_flag == 0, "while the host speaks the move is never cut");

    // a short move is never cut under the floor
    boot_with(FEAT_COMMS_WATCHDOG);
    steps_remaining = 10;
    Safety_NoteStepperMove(10, 0);
    fake_tick += URTC_STEPPER_SILENCE_MIN_MS;
    Safety_Service();
    TEST_ASSERT(steps_remaining == 10 && system_error_flag == 0, "a short move waits at least the floor");

    // no move: silence alone cuts nothing
    boot_with(FEAT_COMMS_WATCHDOG);
    fake_tick += 100000000;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "with no move running a silent host is not an error");

    // the speed of a move is taken into account: 4000 steps at 2000 steps/s last 2 s, the limit is the floor
    boot_with(FEAT_COMMS_WATCHDOG);
    steps_remaining = 4000;
    Safety_NoteStepperMove(4000, 2000);
    fake_tick += URTC_STEPPER_SILENCE_MIN_MS + 1;
    Safety_Service();
    TEST_ASSERT(steps_remaining == 0 && system_error_flag == 1, "a fast move has a short limit");
}

static void test_electromagnet_policy(int *failures)
{
    // default: hold, whatever the silence, whatever the switches of the watchdog
    boot_with(0);
    active_tool = TOOL_ELECTROMAGNET;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    fake_tick += 100000000;
    Safety_Service();
    TEST_ASSERT(!coil_released() && system_error_flag == 0, "all switches off: the coil is held");

    boot_with(FEAT_COMMS_WATCHDOG);
    active_tool = TOOL_ELECTROMAGNET;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    fake_tick += 100000000;
    Safety_Service();
    TEST_ASSERT(!coil_released() && system_error_flag == 0, "the watchdog without the release policy still holds the coil");

    boot_with(FEAT_MAGNET_RELEASE);
    active_tool = TOOL_ELECTROMAGNET;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    fake_tick += 100000000;
    Safety_Service();
    TEST_ASSERT(!coil_released() && system_error_flag == 0, "the release policy without the watchdog still holds the coil");

    boot_with(FEAT_COMMS_WATCHDOG | FEAT_MAGNET_RELEASE);
    active_tool = TOOL_ELECTROMAGNET;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
    fake_tick += URTC_MAGNET_SILENCE_MS;
    Safety_Service();
    TEST_ASSERT(!coil_released(), "at the silence limit the coil is still held");
    fake_tick += 1;
    Safety_Service();
    TEST_ASSERT(coil_released() && system_error_flag == 1 && Safety_ErrorReason() == URTC_ERROR_REASON_MAGNET_RELEASE, "with both switches on a long silence lets go and says why");

    boot_with(FEAT_COMMS_WATCHDOG | FEAT_MAGNET_RELEASE);
    active_tool = TOOL_ELECTROMAGNET;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
    fake_tick += 100000000;
    Safety_Service();
    TEST_ASSERT(system_error_flag == 0, "a coil that is not energised has nothing to release");

    boot_with(FEAT_COMMS_WATCHDOG | FEAT_MAGNET_RELEASE);
    active_tool = TOOL_SOLDERING_IRON;
    fake_pin_set(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);   // the same pin heats an iron: it is not a coil
    fake_tick += 100000000;
    Safety_Service();
    TEST_ASSERT(!coil_released() && system_error_flag == 0, "the release policy touches the electromagnet tool only");
}

static void test_the_supported_mask(int *failures)
{
    TEST_ASSERT(FEAT_SUPPORTED == 0x7F && FEAT_CLEAR_ERROR == URTC_FEATURE_CLEAR_ERROR && FEAT_COMMS_WATCHDOG == URTC_FEATURE_COMMS_WATCHDOG &&
                FEAT_MAGNET_RELEASE == URTC_FEATURE_MAGNET_RELEASE, "the firmware switches are the ones of the specification");
    boot_with(0);
    for (uint8_t b = 1; b != 0 && b <= FEAT_SUPPORTED; b <<= 1) TEST_ASSERT(!Features_Active(b), "every switch is off on a blank record");
    boot_with(0x80);
    TEST_ASSERT(Features_Running() == 0, "a record with a bit this firmware does not have is not trusted: all off");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_safety.c\n");
    test_the_first_reason_wins(&failures);
    test_clear_with_the_switch_off(&failures);
    test_clear_with_the_switch_on(&failures);
    test_clear_every_actuator_and_the_special_cases(&failures);
    test_stepper_watchdog(&failures);
    test_electromagnet_policy(&failures);
    test_the_supported_mask(&failures);
    if (failures == 0) {
        printf("  OK   all safety host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

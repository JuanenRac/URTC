// =============================================================================
// URTC Firmware - Host logic tests for the interrupt handlers and the safe state of the multi-head module
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// firmware_interrupts_can.c, _gpio.c, _timer.c and firmware_multinode_platform.c are compiled against the fake HAL and called the way the vector table
// calls them: the entry points hand the interrupt to the HAL, the pin callback counts the pulses of the tachometers and the probe with their 2 ms
// debounce, and the step timer moves the stepper one edge at a time and, on a latched error, cuts the outputs of every tool that can hurt someone.
// HardFault_Handler (firmware_interrupts_fault.c) never returns and is linked, not called.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_multinode.h"
#include "test_runner.h"

void SysTick_Handler(void);
void TIM3_IRQHandler(void);
void CAN_RX0_IRQHandler(void);
void CAN_SCE_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan_e);
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin);
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim);

static void world(void)
{
    fake_hal_reset_recording();
    fake_tick_step = 0;
    system_error_flag = 0;
    can_bus_error_flag = 0;
    probe_impact_counter = 0;
    drill_rpm_pulses = layer_fan_rpm_pulses = hotend_fan_rpm_pulses = 0;
    steps_remaining = 0;
    step_pulse_high = 0;
    active_tool = TOOL_SOLDERING_IRON;
    htim1.Instance = 0;
}

static int wrote(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    for (int i = 0; i < fake_gpio_write_count; i++) {
        if (fake_gpio_writes[i].port == port && fake_gpio_writes[i].pin == pin && fake_gpio_writes[i].state == state) return 1;
    }
    return 0;
}

static void test_entry_points(int *failures)
{
    world();
    SysTick_Handler();
    TEST_ASSERT(fake_inc_tick_count == 1, "the system tick handler advances the HAL tick");
    TIM3_IRQHandler();
    TEST_ASSERT(fake_tim_irq_count == 1, "timer 3 is handed to the HAL");
    CAN_RX0_IRQHandler();
    CAN_SCE_IRQHandler();
    TEST_ASSERT(fake_can_irq_count == 2, "both CAN vectors are handed to the HAL");
    EXTI3_IRQHandler();
    TEST_ASSERT(fake_exti_irq_count == 1 && fake_exti_last_pin == GPIO_PIN_3, "line 3 of the external interrupts is pin 3");
    EXTI9_5_IRQHandler();
    TEST_ASSERT(fake_exti_irq_count == 2 && fake_exti_last_pin == GPIO_PIN_6, "the shared line 5 to 9 is read as pin 6, the fan tachometer");
    HAL_CAN_ErrorCallback(&hcan);
    TEST_ASSERT(can_bus_error_flag == 1, "a CAN error raises the bus error flag");
}

static void test_probe(int *failures)
{
    world();
    active_tool = TOOL_SCAN_PROBE;
    fake_tick = 10000;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x095 && fake_tx[0].dlc == 1 && fake_tx[0].data[0] == 0x01, "an impact is reported at once on 0x095");
    TEST_ASSERT(probe_impact_counter == 1, "and counted");
    fake_tick = 10001;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(fake_tx_count == 1 && probe_impact_counter == 1, "a second edge within 2 ms is a bounce and is ignored");
    fake_tick = 10002;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(fake_tx_count == 2 && probe_impact_counter == 2, "after 2 ms it is a new impact");
    fake_tick = 10100;
    fake_tx_mailboxes_free = 0;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(fake_can_abort_count == 3 && fake_tx_count == 3 && probe_impact_counter == 3, "with every mailbox full the oldest requests are aborted so the impact still gets out");
    HAL_GPIO_EXTI_Callback(GPIO_PIN_13);
    TEST_ASSERT(probe_impact_counter == 3 && drill_rpm_pulses == 0, "another pin is not the probe");
}

static void test_tachometers(int *failures)
{
    world();
    active_tool = TOOL_DRILL;
    fake_tick = 20000;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    fake_tick = 20001;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    fake_tick = 20002;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(drill_rpm_pulses == 2 && fake_tx_count == 0, "the drill counts a pulse every 2 ms at most, and sends nothing");

    world();
    active_tool = TOOL_3D_PRINTER;
    fake_tick = 30000;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    fake_tick = 30001;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    fake_tick = 30003;
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(layer_fan_rpm_pulses == 2 && drill_rpm_pulses == 0, "the printer counts the layer fan pulses with the same debounce");
    fake_tick = 40000;
    HAL_GPIO_EXTI_Callback(HOTEND_FAN_FG_PIN);
    fake_tick = 40001;
    HAL_GPIO_EXTI_Callback(HOTEND_FAN_FG_PIN);
    fake_tick = 40002;
    HAL_GPIO_EXTI_Callback(HOTEND_FAN_FG_PIN);
    TEST_ASSERT(hotend_fan_rpm_pulses == 2, "and the hotend fan pulses");

    active_tool = TOOL_SOLDERING_IRON;
    fake_tick = 50000;
    HAL_GPIO_EXTI_Callback(HOTEND_FAN_FG_PIN);
    HAL_GPIO_EXTI_Callback(TOUCH_IN_PIN);
    TEST_ASSERT(hotend_fan_rpm_pulses == 2 && layer_fan_rpm_pulses == 2 && drill_rpm_pulses == 0 && probe_impact_counter == 0, "a tool without those inputs counts nothing");
}

static void test_step_timer(int *failures)
{
    world();
    TIM_HandleTypeDef other = {0};
    other.Instance = (void *)&other;
    steps_remaining = 3;
    HAL_TIM_PeriodElapsedCallback(&other);
    TEST_ASSERT(fake_gpio_write_count == 0 && steps_remaining == 3, "another timer does not move the stepper");

    TIM_HandleTypeDef t3 = {0};
    t3.Instance = TIM3;
    steps_remaining = 2;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(wrote(STEP_PORT, STEP_PIN, GPIO_PIN_SET) && step_pulse_high == 1 && steps_remaining == 2, "the first edge raises the step line and does not count");
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(wrote(STEP_PORT, STEP_PIN, GPIO_PIN_RESET) && step_pulse_high == 0 && steps_remaining == 1, "the second edge lowers it and counts one step");
    HAL_TIM_PeriodElapsedCallback(&t3);
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(steps_remaining == 0 && wrote(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET), "when the last step is done the driver is switched off");

    world();
    active_tool = TOOL_GRIPPER_NEMA;
    steps_remaining = 1;
    HAL_TIM_PeriodElapsedCallback(&t3);
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(steps_remaining == 0 && !wrote(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET), "a gripper keeps its driver enabled to hold the position");
    world();
    active_tool = TOOL_3D_PRINTER;
    steps_remaining = 1;
    HAL_TIM_PeriodElapsedCallback(&t3);
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(!wrote(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET), "so does the printer, which holds the axis");

    world();
    active_tool = TOOL_CRIMPING_ACTUATOR;
    steps_remaining = 1;
    HAL_TIM_PeriodElapsedCallback(&t3);
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(wrote(EXP_TMC_STEP_PORT, EXP_TMC_STEP_PIN, GPIO_PIN_SET) && wrote(EXP_TMC_STEP_PORT, EXP_TMC_STEP_PIN, GPIO_PIN_RESET), "the crimping actuator steps the driver of the expansion connector");
    TEST_ASSERT(wrote(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN, GPIO_PIN_SET), "and switches that driver off when it is done");

    world();
    steps_remaining = 0;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(fake_gpio_write_count == 0, "with nothing to do the timer touches nothing");
}

static void test_error_state(int *failures)
{
    TIM_HandleTypeDef t3 = {0};
    t3.Instance = TIM3;
    world();
    system_error_flag = 1;
    steps_remaining = 500;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(steps_remaining == 0 && wrote(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET), "a latched error stops the motion and disables the driver");
    TEST_ASSERT(fake_tim_compare[0] == 0xFFFFFFFFu, "the PWM of timer 1 is not touched while the timer has no instance");

    world();
    system_error_flag = 1;
    htim1.Instance = TIM3;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(fake_tim_compare[0] == 0, "with timer 1 running its output is set to zero");

    world();
    system_error_flag = 1;
    active_tool = TOOL_LASER_ENGRAVER;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(wrote(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET), "the laser interlock is opened");
    world();
    system_error_flag = 1;
    active_tool = TOOL_DRILL;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(wrote(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET), "the drill brake line is released");
    world();
    system_error_flag = 1;
    active_tool = TOOL_CRIMPING_ACTUATOR;
    HAL_TIM_PeriodElapsedCallback(&t3);
    TEST_ASSERT(wrote(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN, GPIO_PIN_SET), "the crimping driver of the expansion connector is disabled");
}

static void test_multinode_platform(int *failures)
{
    world();
    target_temperature = 300;
    laser_power_setpoint = 200;
    steps_remaining = 99;
    MultiNode_PlatformSafeState();
    TEST_ASSERT(target_temperature == 0 && laser_power_setpoint == 0 && steps_remaining == 0, "the safe state clears the heater target, the laser power and the motion");
    TEST_ASSERT(fake_tim_compare[0] == 0, "both PWM outputs are at zero");
    TEST_ASSERT(wrote(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET) && wrote(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET) && wrote(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET), "the driver is disabled and the drill brake and the iron heater lines are low");
    TEST_ASSERT(!wrote(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET), "the laser interlock is written only when the laser is the tool");

    world();
    active_tool = TOOL_LASER_ENGRAVER;
    MultiNode_PlatformSafeState();
    TEST_ASSERT(wrote(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET), "with the laser as the tool the interlock is opened too");

    fake_uid[0] = 0x00000001u; fake_uid[1] = 0x00000000u; fake_uid[2] = 0x00000000u;
    TEST_ASSERT(MultiNode_PlatformSerial() == 1u, "the serial of a chip with only the first word set is that word");
    fake_uid[0] = 0; fake_uid[1] = 0x00000001u;
    TEST_ASSERT(MultiNode_PlatformSerial() == (1u << 11), "the second word is rotated left by 11");
    fake_uid[1] = 0x80000000u;
    TEST_ASSERT(MultiNode_PlatformSerial() == (1u << 10), "and the bit that leaves on the left comes back on the right");
    fake_uid[1] = 0; fake_uid[2] = 0x00000001u;
    TEST_ASSERT(MultiNode_PlatformSerial() == (1u << 22), "the third word is rotated left by 22");
    fake_uid[0] = 0x12345678u; fake_uid[1] = 0x9ABCDEF0u; fake_uid[2] = 0x0FEDCBA9u;
    uint32_t expected = 0x12345678u ^ ((0x9ABCDEF0u << 11) | (0x9ABCDEF0u >> 21)) ^ ((0x0FEDCBA9u << 22) | (0x0FEDCBA9u >> 10));
    TEST_ASSERT(MultiNode_PlatformSerial() == expected, "three words combine as the first, the second rotated by 11 and the third by 22");
    fake_uid[0] ^= 1;
    TEST_ASSERT(MultiNode_PlatformSerial() == (expected ^ 1u), "a change in one word changes the serial");
}

void run_interrupts_tests(int *failures)
{
    test_entry_points(failures);
    test_probe(failures);
    test_tachometers(failures);
    test_step_timer(failures);
    test_error_state(failures);
    test_multinode_platform(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the interrupt handlers and the safe state of the multi-head module\n");
    run_interrupts_tests(&failures);
    if (failures == 0) {
        printf("  OK   all interrupt host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

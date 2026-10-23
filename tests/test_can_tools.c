// =============================================================================
// URTC Firmware - Host logic tests for the CAN handlers of the tools (firmware_can_*.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Each tool of the head has a handler that turns a CAN frame into the state the control loops read and the pins and timers that
// drive the hardware. The real handlers are compiled against the fake HAL and the shared state of tests/host_hal/fake_state.c;
// the three modules that talk to the expansion board (its I2C link, the ADS1115 and the motion planner) are replaced by
// recorders, so what is asserted is exactly what each handler decides: the length checks, the boot-sequence gate, the clamps,
// the scaling of a duty into the timer compare value, the direction-change rule of the steppers and the replies it sends.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_can_aoi.h"
#include "firmware_can_crimping.h"
#include "firmware_can_drill.h"
#include "firmware_can_electromagnet.h"
#include "firmware_can_flyingprobe.h"
#include "firmware_can_hotair.h"
#include "firmware_can_laser.h"
#include "firmware_can_motion.h"
#include "firmware_can_pastejetting.h"
#include "firmware_can_printer3d.h"
#include "firmware_can_soldering.h"
#include "firmware_can_uvcuring.h"
#include "firmware_can_weldpulse.h"
#include "test_runner.h"

void WeldPulse_Tick(void);

// ---- recorders for the modules this suite does not link --------------------------------------------------------------------
static uint8_t i2c_writes, i2c_reg, i2c_len, i2c_data[8];
static uint8_t slave_read_ok, slave_read_value[2];
uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len)
{
    i2c_writes++;
    i2c_reg = reg;
    i2c_len = len;
    memset(i2c_data, 0, sizeof i2c_data);
    if (len > 0 && len <= sizeof i2c_data) {
        memcpy(i2c_data, data, len);
    }
    return 1;
}
uint8_t ExpansionI2C_SlaveReadRegister(uint8_t reg, uint8_t *data, uint8_t len)
{
    i2c_reg = reg;
    if (!slave_read_ok) {
        return 0;
    }
    memcpy(data, slave_read_value, len);
    return 1;
}
static int ads_configured, ads_triggered, ads_read;
static uint16_t ads_config;
static int16_t ads_result;
void ADS1115_Direct_Configure(uint16_t config_reg) { ads_configured++; ads_config = config_reg; }
void ADS1115_Direct_TriggerConversion(void) { ads_triggered++; }
int16_t ADS1115_Direct_ReadResult(void) { ads_read++; return ads_result; }
static uint32_t noted_steps;
static int noted;
void Features_NoteMove(uint32_t steps) { noted++; noted_steps = steps; }

// ---- helpers ----------------------------------------------------------------------------------------------------------------
static void quiet(void)
{
    fake_hal_reset_recording();
    i2c_writes = 0;
    ads_configured = ads_triggered = ads_read = 0;
    noted = 0;
    boot_sequence_active = 0;
    fake_tick = 1000;
    fake_tick_step = 0;
}

// A frame with `dlc` as its data length code; the data come from `bytes` (`available` of them): a test may announce a longer or
// shorter frame than the array it takes the bytes from, and the rest is zero.
static void rx_sized(uint32_t id, uint32_t dlc, const uint8_t *bytes, size_t available)
{
    memset(&rxHeader, 0, sizeof rxHeader);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
    memset(rxData, 0, sizeof rxData);
    if (bytes != NULL) {
        size_t n = dlc < available ? dlc : available;
        memcpy(rxData, bytes, n > 8 ? 8 : n);
    }
}
#define RX(id, dlc, array) rx_sized((id), (dlc), (array), sizeof(array))
#define rx(id, dlc, none) rx_sized((id), (dlc), (none), 0)

// The last level written to a pin since quiet(), or -1 when it was never written.
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

#define DUTY_20KHZ(x) (((uint32_t)(x) * 3199u) / 255u)
#define DUTY_PRINTER(x) (((uint32_t)(x) * 2559u) / 255u)

// ---- soldering iron (0x130, 0x131, 0x132) --------------------------------------------------------------------------------------
static void soldering(int *failures)
{
    quiet();
    target_temperature = 0;
    solder_iron_last_kick_tick = 0;
    const uint8_t set400[2] = { 0x01, 0x90 };
    RX(0x130, 2, set400);
    Handle_CAN_SolderingIron();
    TEST_ASSERT(target_temperature == 400 && solder_iron_last_kick_tick == 1000, "0x130 sets the set-point (big-endian) and kicks the watchdog timer");

    // pre-warming during the splash is allowed: the heater loop supervises it
    target_temperature = 0;
    boot_sequence_active = 1;
    Handle_CAN_SolderingIron();
    TEST_ASSERT(target_temperature == 400, "0x130 is not gated by the boot sequence (the heater loop supervises it)");
    boot_sequence_active = 0;

    target_temperature = 123;
    RX(0x130, 1, set400);
    Handle_CAN_SolderingIron();
    TEST_ASSERT(target_temperature == 123, "0x130 with one byte is ignored");

    // the feeder position
    solder_spool_position = 123456;
    const uint8_t wrong[4] = { 0x52, 0x53, 0x50, 0x31 };
    quiet();
    RX(0x131, 4, wrong);
    Handle_CAN_SolderingIron();
    TEST_ASSERT(solder_spool_position == 123456, "0x131 without the magic payload does not reset the position");
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x132 && fake_tx[0].dlc == 4 && fake_tx[0].data[0] == 0 && fake_tx[0].data[1] == 0x01 &&
                fake_tx[0].data[2] == 0xE2 && fake_tx[0].data[3] == 0x40, "0x131 still answers 0x132 with the position, big-endian (123456 = 0x0001E240)");

    quiet();
    const uint8_t rsp0[4] = { 0x52, 0x53, 0x50, 0x30 };
    RX(0x131, 4, rsp0);
    Handle_CAN_SolderingIron();
    TEST_ASSERT(solder_spool_position == 0 && fake_tx_count == 1 && fake_tx[0].data[3] == 0, "0x131 with RSP0 resets the position and answers the new value");

    quiet();
    solder_spool_position = -1;
    rx(0x132, 0, NULL);
    Handle_CAN_SolderingIron();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x132 && fake_tx[0].data[0] == 0xFF && fake_tx[0].data[3] == 0xFF, "0x132 answers a negative position as two's complement");

    quiet();
    fake_tx_mailboxes_free = 0;
    rx(0x132, 0, NULL);
    Handle_CAN_SolderingIron();
    TEST_ASSERT(fake_tx_count == 0, "no answer when no transmit mailbox is free");
}

// ---- hot air (0x1E0) ------------------------------------------------------------------------------------------------------------
static void hotair(int *failures)
{
    quiet();
    target_temperature = 0;
    const uint8_t cmd[3] = { 0x01, 0x2C, 0x80 };
    RX(0x1E0, 3, cmd);
    Handle_CAN_HotAirRework();
    TEST_ASSERT(target_temperature == 300 && hotair_blower_duty == 0x80 && solder_iron_last_kick_tick == 1000 && hotair_blower_last_kick_tick == 1000, "0x1E0 sets the set-point, the blower duty and both timers");
    TEST_ASSERT(fake_tim_compare[0] == DUTY_20KHZ(0x80), "the blower duty is scaled to the 20 kHz timer period");

    quiet();
    target_temperature = 7;
    boot_sequence_active = 1;
    RX(0x1E0, 3, cmd);
    Handle_CAN_HotAirRework();
    TEST_ASSERT(target_temperature == 7, "0x1E0 is refused during the boot sequence");
    boot_sequence_active = 0;
    RX(0x1E0, 2, cmd);
    Handle_CAN_HotAirRework();
    TEST_ASSERT(target_temperature == 7, "0x1E0 with two bytes is ignored");
    RX(0x1E1, 3, cmd);
    Handle_CAN_HotAirRework();
    TEST_ASSERT(target_temperature == 7, "another identifier is ignored");
}

// ---- laser (0x160) --------------------------------------------------------------------------------------------------------------
static void laser(int *failures)
{
    quiet();
    const uint8_t on[2] = { 0xFF, 0x01 };
    RX(0x160, 2, on);
    Handle_CAN_Laser();
    TEST_ASSERT(laser_power_setpoint == 0xFF && laser_last_kick_tick == 1000, "0x160 stores the power and kicks the watchdog timer");
    TEST_ASSERT(fake_tim_compare[0] == 3199u && pin_level(TMC_ENN_PORT, LASER_SAFETY_PIN) == GPIO_PIN_SET, "enabled: full power is the full period and the safety pin is raised");

    quiet();
    const uint8_t off[2] = { 0xFF, 0x00 };
    RX(0x160, 2, off);
    Handle_CAN_Laser();
    TEST_ASSERT(fake_tim_compare[0] == 0u && pin_level(TMC_ENN_PORT, LASER_SAFETY_PIN) == GPIO_PIN_RESET, "disabled (second byte not 1): no power and the safety pin is dropped");

    quiet();
    const uint8_t two[2] = { 0x80, 0x02 };
    RX(0x160, 2, two);
    Handle_CAN_Laser();
    TEST_ASSERT(fake_tim_compare[0] == 0u && pin_level(TMC_ENN_PORT, LASER_SAFETY_PIN) == GPIO_PIN_RESET, "only an explicit 1 enables the laser");

    quiet();
    laser_power_setpoint = 5;
    boot_sequence_active = 1;
    RX(0x160, 2, on);
    Handle_CAN_Laser();
    TEST_ASSERT(laser_power_setpoint == 5 && fake_gpio_write_count == 0, "0x160 is refused during the boot sequence and touches no pin");
    boot_sequence_active = 0;
    RX(0x160, 1, on);
    Handle_CAN_Laser();
    TEST_ASSERT(laser_power_setpoint == 5, "0x160 with one byte is ignored");
}

// ---- drill (0x140) --------------------------------------------------------------------------------------------------------------
static void drill(int *failures)
{
    quiet();
    const uint8_t spin[2] = { 0x80, 0x01 };
    RX(0x140, 2, spin);
    Handle_CAN_Drill();
    TEST_ASSERT(drill_speed == 0x80 && drill_last_kick_tick == 1000 && fake_tim_compare[0] == DUTY_20KHZ(0x80), "0x140 stores the speed, kicks the timer and scales the duty");
    TEST_ASSERT(pin_level(DRILL_FRIN_PORT, DRILL_FRIN_PIN) == GPIO_PIN_SET && pin_level(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN) == GPIO_PIN_SET, "forward and brake released while it turns");

    quiet();
    const uint8_t stop[2] = { 0x00, 0x00 };
    RX(0x140, 2, stop);
    Handle_CAN_Drill();
    TEST_ASSERT(fake_tim_compare[0] == 0u && pin_level(DRILL_FRIN_PORT, DRILL_FRIN_PIN) == GPIO_PIN_RESET && pin_level(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN) == GPIO_PIN_RESET, "speed 0 brakes and drops the direction pin");

    quiet();
    drill_speed = 9;
    boot_sequence_active = 1;
    RX(0x140, 2, spin);
    Handle_CAN_Drill();
    TEST_ASSERT(drill_speed == 9 && fake_gpio_write_count == 0, "0x140 is refused during the boot sequence");
    boot_sequence_active = 0;
    RX(0x140, 1, spin);
    Handle_CAN_Drill();
    TEST_ASSERT(drill_speed == 9, "0x140 with one byte is ignored");
}

// ---- UV curing (0x1D0) and electromagnet (0x1B0) ------------------------------------------------------------------------------
static void uv_and_magnet(int *failures)
{
    quiet();
    const uint8_t duty[1] = { 0xC8 };
    RX(0x1D0, 1, duty);
    Handle_CAN_UVCuring();
    TEST_ASSERT(uv_curing_duty == 0xC8 && uv_curing_last_kick_tick == 1000 && fake_tim_compare[0] == DUTY_20KHZ(0xC8), "0x1D0 stores the duty, kicks the timer and scales it");
    quiet();
    uv_curing_duty = 3;
    boot_sequence_active = 1;
    RX(0x1D0, 1, duty);
    Handle_CAN_UVCuring();
    TEST_ASSERT(uv_curing_duty == 3, "0x1D0 is refused during the boot sequence");
    boot_sequence_active = 0;
    rx(0x1D0, 0, NULL);
    Handle_CAN_UVCuring();
    TEST_ASSERT(uv_curing_duty == 3, "0x1D0 with no data is ignored");

    quiet();
    const uint8_t on[1] = { 0x01 };
    const uint8_t any[1] = { 0x7F };
    const uint8_t off[1] = { 0x00 };
    RX(0x1B0, 1, on);
    Handle_CAN_Electromagnet();
    TEST_ASSERT(pin_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_SET, "0x1B0 with 1 energizes the magnet");
    quiet();
    RX(0x1B0, 1, any);
    Handle_CAN_Electromagnet();
    TEST_ASSERT(pin_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_SET, "any non-zero byte energizes it");
    quiet();
    RX(0x1B0, 1, off);
    Handle_CAN_Electromagnet();
    TEST_ASSERT(pin_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_RESET, "0 releases it");
    quiet();
    boot_sequence_active = 1;
    RX(0x1B0, 1, on);
    Handle_CAN_Electromagnet();
    TEST_ASSERT(fake_gpio_write_count == 0, "0x1B0 is refused during the boot sequence");
}

// ---- AOI inspection light (0x150) -----------------------------------------------------------------------------------------------
static void aoi(int *failures)
{
    quiet();
    ring_color_r = 10;
    ring_color_g = 20;
    ring_color_b = 30;
    trigger_aoi_strobe_flag = 0;
    update_led_ring_flag = 0;
    const uint8_t strobe[3] = { 0x01, 0x01, 0xF4 };
    RX(0x150, 3, strobe);
    Handle_CAN_AOI();
    TEST_ASSERT(aoi_mode == 1 && aoi_strobe_period == 500 && trigger_aoi_strobe_flag == 1 && update_led_ring_flag == 0, "mode 1 stores the period (big-endian) and asks for one strobe");

    const uint8_t solid[3] = { 0x02, 0x00, 0x00 };
    RX(0x150, 3, solid);
    Handle_CAN_AOI();
    int all = 1;
    for (int i = 0; i < 8; i++) {
        all = all && ring_pixels[i].R == 10 && ring_pixels[i].G == 20 && ring_pixels[i].B == 30;
    }
    TEST_ASSERT(all && update_led_ring_flag == 1, "mode 2 lights the whole ring with the ring colour");

    update_led_ring_flag = 0;
    const uint8_t dark[3] = { 0x00, 0x00, 0x00 };
    RX(0x150, 3, dark);
    Handle_CAN_AOI();
    all = 1;
    for (int i = 0; i < 8; i++) {
        all = all && ring_pixels[i].R == 0 && ring_pixels[i].G == 0 && ring_pixels[i].B == 0;
    }
    TEST_ASSERT(all && update_led_ring_flag == 1, "any other mode turns the ring off");

    aoi_mode = 77;
    RX(0x150, 2, solid);
    Handle_CAN_AOI();
    TEST_ASSERT(aoi_mode == 77, "0x150 with two bytes is ignored");
}

// ---- 3D printer (0x170, 0x173, 0x178) --------------------------------------------------------------------------------------------
static void printer(int *failures)
{
    quiet();
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_RESET);
    steps_remaining = 0;
    const uint8_t cmd[6] = { 0x00, 0xC8, 0x01, 0x00, 0x03, 0xE8 };
    RX(0x170, 6, cmd);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(target_temperature == 200 && hotend_heater_last_kick_tick == 1000, "0x170 sets the hotend set-point and kicks the timer");
    TEST_ASSERT(pin_level(DIR_PORT, DIR_PIN) == GPIO_PIN_SET && total_steps_setpoint == 1000 && steps_remaining == 1000 && step_pulse_high == 0, "forward, 1000 steps (24-bit, big-endian)");
    TEST_ASSERT(pin_level(STEP_PORT, STEP_PIN) == GPIO_PIN_RESET && pin_level(TMC_ENN_PORT, TMC_ENN_PIN) == GPIO_PIN_RESET, "the step pin is forced low and the driver enabled (active low)");

    // reversing while a move is running stops the move first
    quiet();
    steps_remaining = 55;
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_SET);
    const uint8_t reverse_stop[6] = { 0x00, 0xC8, 0x00, 0x00, 0x00, 0x00 };
    RX(0x170, 6, reverse_stop);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(steps_remaining == 0 && pin_level(DIR_PORT, DIR_PIN) == GPIO_PIN_RESET, "a change of direction and zero steps ends the move");

    quiet();
    steps_remaining = 55;
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_SET);
    const uint8_t reverse_move[6] = { 0x00, 0xC8, 0x00, 0x00, 0x00, 0x0A };
    RX(0x170, 6, reverse_move);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(steps_remaining == 10, "a change of direction drops the old move and starts the new one");

    // during the boot sequence the heater is pre-warmed, never moved
    quiet();
    boot_sequence_active = 1;
    steps_remaining = 0;
    target_temperature = 0;
    RX(0x170, 6, cmd);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(target_temperature == 200 && steps_remaining == 0 && fake_gpio_write_count == 0, "during the boot sequence only the set-point is taken");
    boot_sequence_active = 0;
    target_temperature = 5;
    RX(0x170, 5, cmd);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(target_temperature == 5, "0x170 with five bytes is ignored");

    quiet();
    const uint8_t fan[1] = { 0x80 };
    RX(0x173, 1, fan);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(layer_fan_duty == 0x80 && layer_fan_last_kick_tick == 1000 && fake_tim_compare[0] == DUTY_PRINTER(0x80), "0x173 sets the layer fan duty on the 25 kHz period");
    quiet();
    const uint8_t hot[1] = { 0xFF };
    RX(0x178, 1, hot);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(hotend_fan_duty == 0xFF && hotend_fan_last_kick_tick == 1000 && fake_tim_compare[0] == 2559u, "0x178 sets the hotend fan duty");
    layer_fan_duty = 1;
    rx(0x173, 0, NULL);
    Handle_CAN_3DPrinter();
    TEST_ASSERT(layer_fan_duty == 1, "0x173 with no data is ignored");
}

// ---- paste jetting, crimping, flying probe (the expansion board) --------------------------------------------------------------
static void expansion(int *failures)
{
    quiet();
    const uint8_t pwm[3] = { 0x01, 0x02, 0x03 };
    RX(0x230, 3, pwm);
    Handle_CAN_PasteJetting();
    TEST_ASSERT(i2c_writes == 1 && i2c_reg == 0x30 && i2c_len == 3 && i2c_data[2] == 3, "0x230 writes the PWM configuration register of the expansion board");
    RX(0x231, 3, pwm);
    Handle_CAN_PasteJetting();
    TEST_ASSERT(i2c_writes == 2 && i2c_reg == 0x31, "0x231 writes the pulse register");
    RX(0x231, 2, pwm);
    Handle_CAN_PasteJetting();
    TEST_ASSERT(i2c_writes == 2, "a frame of the wrong length writes nothing");
    boot_sequence_active = 1;
    RX(0x230, 3, pwm);
    Handle_CAN_PasteJetting();
    TEST_ASSERT(i2c_writes == 2, "refused during the boot sequence");
    boot_sequence_active = 0;

    // crimping uses the stepper driver of the expansion board
    quiet();
    fake_pin_set(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN, GPIO_PIN_RESET);
    steps_remaining = 0;
    const uint8_t crimp[5] = { 0x01, 0x00, 0x01, 0x00, 0x00 };
    RX(0x1F0, 5, crimp);
    Handle_CAN_CrimpingActuator();
    TEST_ASSERT(steps_remaining == 65536u && total_steps_setpoint == 65536u && pin_level(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN) == GPIO_PIN_SET, "0x1F0: forward, 65536 steps (32-bit, big-endian)");
    TEST_ASSERT(pin_level(EXP_TMC_STEP_PORT, EXP_TMC_STEP_PIN) == GPIO_PIN_RESET && pin_level(EXP_TMC_EN_PORT, EXP_TMC_EN_PIN) == GPIO_PIN_RESET, "the expansion driver is enabled with the step pin low");
    quiet();
    steps_remaining = 9;
    fake_pin_set(EXP_TMC_DIR_PORT, EXP_TMC_DIR_PIN, GPIO_PIN_SET);
    const uint8_t back_stop[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    RX(0x1F0, 5, back_stop);
    Handle_CAN_CrimpingActuator();
    TEST_ASSERT(steps_remaining == 0, "a change of direction with zero steps ends the move");
    quiet();
    boot_sequence_active = 1;
    steps_remaining = 3;
    RX(0x1F0, 5, crimp);
    Handle_CAN_CrimpingActuator();
    TEST_ASSERT(steps_remaining == 3 && fake_gpio_write_count == 0, "0x1F0 is refused during the boot sequence");
    boot_sequence_active = 0;
    RX(0x1F0, 4, crimp);
    Handle_CAN_CrimpingActuator();
    TEST_ASSERT(steps_remaining == 3, "0x1F0 with four bytes is ignored");

    // flying probe: the ADS1115 is on the expansion board (relayed) or direct on the head (board type 5)
    quiet();
    expansion_board_type = 0;
    const uint8_t cfg[2] = { 0x45, 0x83 };
    RX(0x240, 2, cfg);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(i2c_writes == 1 && i2c_reg == 0x20 && i2c_len == 2 && i2c_data[0] == 0x45 && ads_configured == 0, "0x240 relays the configuration through the expansion board");
    rx(0x241, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(i2c_writes == 2 && i2c_reg == 0x21 && i2c_len == 0, "0x241 relays the trigger");
    expansion_board_type = 5;
    RX(0x240, 2, cfg);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(ads_configured == 1 && ads_config == 0x4583 && i2c_writes == 2, "board type 5: the configuration goes to the ADS1115 directly");
    rx(0x241, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(ads_triggered == 1 && i2c_writes == 2, "board type 5: the trigger goes to the ADS1115 directly");
    boot_sequence_active = 1;
    RX(0x240, 2, cfg);
    Handle_CAN_FlyingProbe();
    rx(0x241, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(ads_configured == 1 && ads_triggered == 1, "0x240 and 0x241 are refused during the boot sequence");
    boot_sequence_active = 0;
    RX(0x240, 3, cfg);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(ads_configured == 1, "0x240 needs exactly two bytes");

    // 0x242 reads the result (it is not gated by the boot sequence)
    quiet();
    ads_result = -2;
    rx(0x242, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x242 && fake_tx[0].dlc == 2 && fake_tx[0].data[0] == 0xFF && fake_tx[0].data[1] == 0xFE, "board type 5: the direct result is answered as a big-endian int16");
    quiet();
    expansion_board_type = 0;
    slave_read_ok = 1;
    slave_read_value[0] = 0x12;
    slave_read_value[1] = 0x34;
    rx(0x242, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].data[0] == 0x12 && fake_tx[0].data[1] == 0x34 && i2c_reg == 0x22, "otherwise the result is read from the expansion board");
    quiet();
    slave_read_ok = 0;
    rx(0x242, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(fake_tx_count == 0, "no answer when the expansion board did not answer");
    quiet();
    slave_read_ok = 1;
    fake_tx_mailboxes_free = 0;
    rx(0x242, 0, NULL);
    Handle_CAN_FlyingProbe();
    TEST_ASSERT(fake_tx_count == 0, "no answer when no transmit mailbox is free");
}

// ---- welding pulses (0x1C0 spot welder, 0x200 ultrasonic) -------------------------------------------------------------------
static void weld(int *failures)
{
    const uint8_t fire[3] = { 0x01, 0x00, 0x64 };
    quiet();
    weld_pulse_active = 0;
    active_tool = TOOL_SPOT_WELDER;
    fake_pin_set(GPIOB, TCRT_D0_DIG_PIN, GPIO_PIN_RESET);
    RX(0x1C0, 3, fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 1 && weld_pulse_duration_ms == 100 && weld_pulse_start_tick == 1000 && pin_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_SET, "0x1C0 starts a pulse of the requested length");

    // a pulse in progress is not restarted
    fake_tick = 1050;
    fake_hal_reset_recording();
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_start_tick == 1000 && fake_gpio_write_count == 0, "a new fire command during a pulse is dropped, not queued");
    WeldPulse_Tick();
    TEST_ASSERT(weld_pulse_active == 1, "the pulse is still on before its time is up");
    fake_tick = 1100;
    WeldPulse_Tick();
    TEST_ASSERT(weld_pulse_active == 0 && pin_level(T12_PWM_PORT, T12_PWM_PIN) == GPIO_PIN_RESET, "the pulse ends when its length has passed");
    fake_hal_reset_recording();
    WeldPulse_Tick();
    TEST_ASSERT(fake_gpio_write_count == 0, "the tick does nothing without a pulse");

    // the pulse is judged against the tick even across the wrap of the 32-bit counter
    quiet();
    fake_tick = 0xFFFFFFF0u;
    RX(0x1C0, 3, fire);
    Handle_CAN_WeldPulse();
    fake_tick = 0x00000050u;  // 0x60 ticks later (96) is still short of 100
    WeldPulse_Tick();
    TEST_ASSERT(weld_pulse_active == 1, "across the wrap of the counter 96 ms is not yet 100 ms");
    fake_tick = 0x00000054u;
    WeldPulse_Tick();
    TEST_ASSERT(weld_pulse_active == 0, "and 100 ms is");

    // only an explicit fire command, the right tool, the foot-switch input low and a clamp at 2 s
    quiet();
    weld_pulse_active = 0;
    const uint8_t not_fire[3] = { 0x02, 0x00, 0x64 };
    RX(0x1C0, 3, not_fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "only 0x01 as first byte fires");
    fake_pin_set(GPIOB, TCRT_D0_DIG_PIN, GPIO_PIN_SET);
    RX(0x1C0, 3, fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "a spot weld needs the electrode-contact input low");
    fake_pin_set(GPIOB, TCRT_D0_DIG_PIN, GPIO_PIN_RESET);
    active_tool = TOOL_DRILL;
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "0x1C0 only fires for the spot welder");
    active_tool = TOOL_SPOT_WELDER;
    boot_sequence_active = 1;
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "refused during the boot sequence");
    boot_sequence_active = 0;
    RX(0x1C0, 2, fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "0x1C0 with two bytes is ignored");
    const uint8_t huge[3] = { 0x01, 0xFF, 0xFF };
    RX(0x1C0, 3, huge);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 1 && weld_pulse_duration_ms == 2000, "a pulse is limited to 2000 ms");

    // the ultrasonic welder has its own identifier and needs no foot switch
    quiet();
    weld_pulse_active = 0;
    active_tool = TOOL_ULTRASONIC_WELDER;
    RX(0x1C0, 3, fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "0x1C0 does not fire the ultrasonic welder");
    RX(0x200, 3, fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 1 && weld_pulse_duration_ms == 100, "0x200 fires the ultrasonic welder");
    weld_pulse_active = 0;
    RX(0x200, 3, huge);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_duration_ms == 2000, "the ultrasonic pulse is limited to 2000 ms too");
    weld_pulse_active = 0;
    RX(0x200, 3, not_fire);
    Handle_CAN_WeldPulse();
    TEST_ASSERT(weld_pulse_active == 0, "only 0x01 fires the ultrasonic welder");
    weld_pulse_active = 0;
    active_tool = TOOL_DRILL;
}

// ---- the stepper motion tools (0x120) ----------------------------------------------------------------------------------------
static void motion(int *failures)
{
    quiet();
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_RESET);
    steps_remaining = 0;
    active_tool = TOOL_PASTE_DISPENSER;
    const uint8_t move[5] = { 0x01, 0x00, 0x00, 0x01, 0x00 };
    RX(0x120, 5, move);
    Handle_CAN_MotionTools();
    TEST_ASSERT(steps_remaining == 256u && total_steps_setpoint == 256u && noted == 1 && noted_steps == 256u, "0x120: 256 steps (32-bit, big-endian) and the planner is told");
    TEST_ASSERT(pin_level(DIR_PORT, DIR_PIN) == GPIO_PIN_SET && pin_level(STEP_PORT, STEP_PIN) == GPIO_PIN_RESET && pin_level(TMC_ENN_PORT, TMC_ENN_PIN) == GPIO_PIN_RESET && step_pulse_high == 0, "direction set, step pin low, driver enabled");

    // the feeder of the soldering iron keeps an open-loop position
    quiet();
    active_tool = TOOL_SOLDERING_IRON;
    solder_spool_position = 0;
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_SET);
    RX(0x120, 5, move);
    Handle_CAN_MotionTools();
    TEST_ASSERT(solder_spool_position == 256, "soldering iron: a forward move adds to the feeder position");
    const uint8_t back[5] = { 0x00, 0x00, 0x00, 0x00, 0x64 };
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_RESET);
    RX(0x120, 5, back);
    Handle_CAN_MotionTools();
    TEST_ASSERT(solder_spool_position == 156, "and a reverse move subtracts");
    active_tool = TOOL_DRILL;
    RX(0x120, 5, move);
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_SET);
    Handle_CAN_MotionTools();
    TEST_ASSERT(solder_spool_position == 156, "other tools leave the feeder position alone");

    // a change of direction cuts the move that was running; zero steps is a stop
    quiet();
    steps_remaining = 40;
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_SET);
    const uint8_t stop_back[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    RX(0x120, 5, stop_back);
    Handle_CAN_MotionTools();
    TEST_ASSERT(steps_remaining == 0 && noted == 0 && pin_level(DIR_PORT, DIR_PIN) == GPIO_PIN_RESET, "0 steps is a stop and does not plan a move");

    quiet();
    steps_remaining = 40;
    fake_pin_set(DIR_PORT, DIR_PIN, GPIO_PIN_RESET);
    RX(0x120, 5, move);
    Handle_CAN_MotionTools();
    TEST_ASSERT(steps_remaining == 256u, "a new move replaces the old one");

    quiet();
    boot_sequence_active = 1;
    steps_remaining = 3;
    RX(0x120, 5, move);
    Handle_CAN_MotionTools();
    TEST_ASSERT(steps_remaining == 3 && fake_gpio_write_count == 0 && noted == 0, "0x120 is refused during the boot sequence");
    boot_sequence_active = 0;
    RX(0x120, 4, move);
    Handle_CAN_MotionTools();
    TEST_ASSERT(steps_remaining == 3, "0x120 with four bytes is ignored");
    const uint8_t largest[5] = { 0x01, 0xFF, 0xFF, 0xFF, 0xFF };
    RX(0x120, 5, largest);
    Handle_CAN_MotionTools();
    TEST_ASSERT(steps_remaining == 0xFFFFFFFFu, "the largest step count is taken whole");
    active_tool = TOOL_DRILL;
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the CAN handlers of the tools\n");
    soldering(&failures);
    hotair(&failures);
    laser(&failures);
    drill(&failures);
    uv_and_magnet(&failures);
    aoi(&failures);
    printer(&failures);
    expansion(&failures);
    weld(&failures);
    motion(&failures);
    if (failures == 0) {
        printf("  OK   all tool handler host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

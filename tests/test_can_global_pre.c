// =============================================================================
// URTC Firmware - Host logic tests for firmware_can_global_pre.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The read-only / configuration CAN commands that are answered before the
// error gate: bootloader entry (0x7F0, magic payload + inline actuator
// shutdown), version query (0x7F8 -> 0x7F9 and the 0x7E9 patch companion),
// error counters, active tool, persisted state, and the 0x1A0..0x1A7
// configuration registers. firmware_can_global_pre.c is compiled unchanged
// against the fake HAL in tests/host_hal/; every CAN transmit and actuator
// write is recorded and asserted on.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_can_global.h"
#include "firmware_expansion_i2c.h"
#include "firmware_blackbox.h"
#include "firmware_thermal_stats.h"
#include "urtc_version_wire.h"
#include "vectors/urtc_fw_vectors.h" // written by URTC-COMMON from its conformance vectors

// ---- globals the firmware module links against ------------------------------
CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
TIM_HandleTypeDef htim1, htim2, htim3;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag, can_bus_error_flag, boot_sequence_active;
volatile uint8_t thermal_fault_code, last_reset_cause;
uint8_t had_valid_recovered_state;
SavedState_t recovered_state;
volatile uint8_t expansion_board_type, free_tool_selection, device_serial_number, mlx_sensor_variant;
uint8_t raw_id_pin_value;

// ---- stubs for collaborators ------------------------------------------------
static int i2c_relay_calls;
static uint8_t i2c_relay_reg, i2c_relay_val;
uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len)
{
    (void)len;
    i2c_relay_calls++;
    i2c_relay_reg = reg;
    i2c_relay_val = data[0];
    return 1;
}
static int blackbox_calls, blackbox_chunk;
void BlackBox_HandleReadbackRequest(uint8_t chunk_index) { blackbox_calls++; blackbox_chunk = chunk_index; }
static int stats_calls;
void ThermalStats_HandleQuery(void) { stats_calls++; }

// ---- helpers ----------------------------------------------------------------
static void send(uint32_t id, uint32_t dlc, const uint8_t *data)
{
    fake_hal_reset_recording();
    i2c_relay_calls = blackbox_calls = stats_calls = 0;
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
    Handle_CAN_GlobalCommands_PreErrorGate();
}

static int gpio_wrote(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    for (int i = 0; i < fake_gpio_write_count; i++)
        if (fake_gpio_writes[i].port == port && fake_gpio_writes[i].pin == pin && fake_gpio_writes[i].state == state) return 1;
    return 0;
}

static const FakeTxFrame *find_tx(uint32_t id)
{
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == id) return &fake_tx[i];
    return 0;
}

static int send_expect_reset(uint32_t dlc, const uint8_t *data)
{
    fake_hal_reset_recording();
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc);
    rxHeader.StdId = 0x7F0;
    rxHeader.DLC = dlc;
    fake_reset_jmp_armed = 1;
    if (setjmp(fake_reset_jmp) == 0) {
        Handle_CAN_GlobalCommands_PreErrorGate();
    }
    fake_reset_jmp_armed = 0;
    return fake_reset_count;
}

void run_can_global_pre_tests(int *failures)
{
    // ---- bootloader entry: magic payload gates the reset ----------------------
    const uint8_t magic[4] = { 0xB0, 0x07, 0x1D, 0x5A };
    active_tool = TOOL_SOLDERING_IRON;
    TEST_ASSERT(send_expect_reset(4, magic) == 1, "0x7F0 with the exact magic resets into the bootloader");
    TEST_ASSERT(fake_tim_compare[0] == 0, "reset path forces the PWM compare to 0 first");
    TEST_ASSERT(gpio_wrote(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET), "reset path disables stepper drivers (ENN high)");
    TEST_ASSERT(gpio_wrote(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET), "reset path brakes the drill");
    TEST_ASSERT(gpio_wrote(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET), "reset path switches the heater off");
    TEST_ASSERT(!gpio_wrote(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET), "non-laser tools: the shared PB6 pin is not re-driven low");

    active_tool = TOOL_LASER_ENGRAVER;
    TEST_ASSERT(send_expect_reset(4, magic) == 1, "0x7F0 with the magic also resets with the laser tool active");
    TEST_ASSERT(gpio_wrote(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET), "laser tool: interlock line driven to its locked level");
    active_tool = TOOL_SOLDERING_IRON;

    for (int i = 0; i < 4; i++) {
        uint8_t w[4];
        memcpy(w, magic, 4);
        w[i] ^= 0xFF;
        if (send_expect_reset(4, w) != 0) { TEST_ASSERT(0, "any corrupted magic byte must not reset"); break; }
        if (fake_gpio_write_count != 0) { TEST_ASSERT(0, "a rejected 0x7F0 must not touch any actuator"); break; }
    }
    // The payload URTC-COMMON writes down for 0x7F0 is the one this firmware resets on, and no other payload of the shared
    // vectors (the downgrade, the F-RAM erase, the feeder reset) resets it.
    {
        int seen = 0, bad = 0;
        for (int i = 0; i < URTC_VEC_COUNT(URTC_VEC_MAGIC); i++) {
            const urtc_vec_magic_t *m = &URTC_VEC_MAGIC[i];
            int resets = send_expect_reset(4, m->data);
            if (m->id == 0x7F0) { seen++; if (resets != 1) bad++; }
            else if (resets != 0) bad++;
        }
        TEST_ASSERT(seen == 1 && bad == 0, "only the 0x7F0 payload of the shared vectors resets into the bootloader");
    }
    active_tool = TOOL_SOLDERING_IRON;

    uint8_t wrong[4] = { 0xB0, 0x07, 0x1D, 0x5B };
    TEST_ASSERT(send_expect_reset(4, wrong) == 0, "one wrong magic byte does not reset");
    TEST_ASSERT(send_expect_reset(3, magic) == 0, "short magic frame (DLC 3) does not reset");
    TEST_ASSERT(send_expect_reset(0, 0) == 0, "empty 0x7F0 does not reset");
    uint8_t magic8[8] = { 0xB0, 0x07, 0x1D, 0x5A, 0, 0, 0, 0 };
    TEST_ASSERT(send_expect_reset(8, magic8) == 0, "magic with DLC 8 does not reset (DLC must be exactly 4)");

    // ---- version query 0x7F8 -> 0x7F9 + 0x7E9 ---------------------------------
    send(0x7F8, 0, 0);
    const FakeTxFrame *v = find_tx(0x7F9);
    TEST_ASSERT(v && v->dlc == 8, "0x7F8 is answered with an 8-byte 0x7F9");
    if (v) {
        TEST_ASSERT(v->data[0] == 0x00, "0x7F9 byte0 = 0 (application answering)");
        uint32_t hw = ((uint32_t)v->data[1] << 24) | ((uint32_t)v->data[2] << 16) | ((uint32_t)v->data[3] << 8) | v->data[4];
        TEST_ASSERT(hw == THIS_HARDWARE_ID, "0x7F9 carries the big-endian hardware id");
        TEST_ASSERT(((v->data[5] << 8) | v->data[6]) == FIRMWARE_VERSION_MAJOR, "0x7F9 major is big-endian 16-bit");
        TEST_ASSERT(v->data[7] == FIRMWARE_VERSION_MINOR, "0x7F9 minor in byte 7");
    }
    const FakeTxFrame *p = find_tx(URTC_CAN_ID_VERSION_PATCH);
    TEST_ASSERT(p && p->dlc == URTC_VERSION_PATCH_FRAME_DLC, "0x7E9 patch companion follows with DLC 3");
    if (p) {
        TEST_ASSERT(p->data[0] == 0x00, "0x7E9 byte0 = application");
        TEST_ASSERT(p->data[1] == FIRMWARE_VERSION_PATCH, "0x7E9 byte1 carries the firmware PATCH number");
        TEST_ASSERT(p->data[2] == 0x00, "application sets no end-update-patch flag");
    }
    TEST_ASSERT(fake_tx_count == 2, "version query sends exactly two frames");
    TEST_ASSERT(fake_tx_count == 2 && fake_tx[0].id == 0x7F9 && fake_tx[1].id == 0x7E9, "0x7F9 first, patch companion right after");

    fake_hal_reset_recording();
    fake_tx_mailboxes_free = 1;
    rxHeader.StdId = 0x7F8; rxHeader.DLC = 0;
    Handle_CAN_GlobalCommands_PreErrorGate();
    TEST_ASSERT(fake_tx_count == 2, "one free mailbox still queues both frames in this model");
    fake_hal_reset_recording();
    fake_tx_mailboxes_free = 0;
    Handle_CAN_GlobalCommands_PreErrorGate();
    TEST_ASSERT(fake_tx_count == 0, "no free TX mailbox: nothing is queued (no blocking)");

    // ---- error counters 0x7FB -> 0x7FC ----------------------------------------
    can_regs.ESR = (0x2Au << 16) | (0x07u << 24);
    send(0x7FB, 0, 0);
    const FakeTxFrame *e = find_tx(0x7FC);
    TEST_ASSERT(e && e->dlc == 2 && e->data[0] == 0x2A && e->data[1] == 0x07, "0x7FC = TEC (ESR 23:16) then REC (ESR 31:24)");

    // ---- active tool 0x110 -> 0x111 -------------------------------------------
    active_tool = TOOL_LASER_ENGRAVER;
    system_error_flag = 1; can_bus_error_flag = 0; boot_sequence_active = 1;
    thermal_fault_code = 3; last_reset_cause = 1;
    send(0x110, 0, 0);
    const FakeTxFrame *t = find_tx(0x111);
    TEST_ASSERT(t && t->dlc == 6, "0x110 is answered with a 6-byte 0x111");
    if (t) {
        TEST_ASSERT(t->data[0] == (uint8_t)TOOL_LASER_ENGRAVER, "0x111 byte0 = active tool");
        TEST_ASSERT(t->data[1] == 1 && t->data[2] == 0 && t->data[3] == 1, "0x111 flags: system error, CAN error, boot sequence");
        TEST_ASSERT(t->data[4] == 3, "0x111 byte4 = latched thermal fault code");
        TEST_ASSERT(t->data[5] == 1, "0x111 byte5 = last reset cause");
    }
    system_error_flag = 0; boot_sequence_active = 0; thermal_fault_code = 0; last_reset_cause = 0;
    active_tool = TOOL_SOLDERING_IRON;

    // ---- persisted state 0x190 -> 0x191 ---------------------------------------
    memset(&recovered_state, 0, sizeof recovered_state);
    had_valid_recovered_state = 1;
    recovered_state.active_tool_at_save = 5;
    recovered_state.had_critical_error = 1;
    recovered_state.solder_setpoint = 0x0123;
    recovered_state.printer_nozzle_setpoint = 0x0456;
    recovered_state.drill_speed = 0;
    recovered_state.laser_power = 77;
    recovered_state.laser_interlock = 1;
    recovered_state.layer_fan_power = 9;
    send(0x190, 0, 0);
    const FakeTxFrame *s = find_tx(0x191);
    TEST_ASSERT(s && s->dlc == 8, "0x190 is answered with an 8-byte 0x191");
    if (s) {
        TEST_ASSERT(s->data[0] == 1 && s->data[1] == 5 && s->data[2] == 1, "0x191 header bytes");
        TEST_ASSERT(s->data[3] == 0x01 && s->data[4] == 0x23, "solder setpoint wins over the nozzle setpoint");
        TEST_ASSERT(s->data[5] == 77, "no drill speed saved: laser power is reported");
        TEST_ASSERT(s->data[6] == 1 && s->data[7] == 9, "interlock/direction and fan power");
    }
    recovered_state.solder_setpoint = 0;
    recovered_state.drill_speed = 40;
    send(0x190, 0, 0);
    s = find_tx(0x191);
    TEST_ASSERT(s && s->data[3] == 0x04 && s->data[4] == 0x56, "no solder setpoint: nozzle setpoint is reported");
    TEST_ASSERT(s && s->data[5] == 40, "drill speed wins over laser power");

    // ---- expansion board type 0x1A0/0x1A1 -------------------------------------
    expansion_board_type = 2;
    uint8_t d6[1] = { 6 };
    send(0x1A0, 1, d6);
    TEST_ASSERT(expansion_board_type == 6, "0x1A0 accepts the highest valid expansion type (6)");
    const FakeTxFrame *x = find_tx(0x1A1);
    TEST_ASSERT(x && x->dlc == 1 && x->data[0] == 6, "0x1A0 answers with the stored type on 0x1A1");
    uint8_t d7[1] = { 7 };
    send(0x1A0, 1, d7);
    TEST_ASSERT(expansion_board_type == 6, "0x1A0 with 7 is rejected, value unchanged");
    x = find_tx(0x1A1);
    TEST_ASSERT(x && x->data[0] == 6, "a rejected write still answers with the current value");
    send(0x1A0, 0, 0);
    TEST_ASSERT(expansion_board_type == 6, "0x1A0 with DLC 0 does not write");
    send(0x1A1, 0, 0);
    x = find_tx(0x1A1);
    TEST_ASSERT(x && x->data[0] == 6, "0x1A1 is a pure query");

    // ---- free tool selection 0x1A2/0x1A3 --------------------------------------
    raw_id_pin_value = 0x1F;
    uint8_t d25[1] = { 25 };
    send(0x1A2, 1, d25);
    TEST_ASSERT(free_tool_selection == 25, "0x1A2 accepts 25 (last tool profile)");
    x = find_tx(0x1A3);
    TEST_ASSERT(x && x->dlc == 2 && x->data[0] == 0x1F && x->data[1] == 25, "0x1A3 pairs the jumper reading with the selection");
    uint8_t d26[1] = { 26 };
    send(0x1A2, 1, d26);
    TEST_ASSERT(free_tool_selection == 25, "0x1A2 with 26 is rejected");

    // ---- serial number 0x1A4/0x1A5 --------------------------------------------
    uint8_t d_sn[1] = { 0xC3 };
    send(0x1A4, 1, d_sn);
    TEST_ASSERT(device_serial_number == 0xC3, "0x1A4 stores the serial number");
    x = find_tx(0x1A5);
    TEST_ASSERT(x && x->dlc == 2 && x->data[0] == URTC_PERIPHERAL_TYPE && x->data[1] == 0xC3, "0x1A5 = fixed peripheral type + serial");
    send(0x1A4, 0, 0);
    TEST_ASSERT(device_serial_number == 0xC3, "0x1A4 with DLC 0 keeps the serial");

    // ---- MLX sensor variant 0x1A6/0x1A7 ---------------------------------------
    expansion_board_type = 6;
    uint8_t d2[1] = { 2 };
    send(0x1A6, 1, d2);
    TEST_ASSERT(mlx_sensor_variant == 2, "0x1A6 stores the sensor variant");
    TEST_ASSERT(i2c_relay_calls == 0, "direct-sensor board (type 6): nothing relayed to the slave chip");
    expansion_board_type = 3;
    uint8_t d1[1] = { 1 };
    send(0x1A6, 1, d1);
    TEST_ASSERT(i2c_relay_calls == 1 && i2c_relay_reg == 0x14 && i2c_relay_val == 1, "slave-chip board (type 3): variant relayed to register 0x14");
    expansion_board_type = 4;
    send(0x1A6, 1, d1);
    TEST_ASSERT(i2c_relay_calls == 1, "slave-chip board (type 4) relays too");
    uint8_t d4[1] = { 4 };
    send(0x1A6, 1, d4);
    TEST_ASSERT(mlx_sensor_variant == 1 && i2c_relay_calls == 0, "variant 4 is rejected and not relayed");
    x = find_tx(0x1A7);
    TEST_ASSERT(x && x->dlc == 1 && x->data[0] == 1, "0x1A7 reports the stored variant");

    // ---- delegated diagnostics -----------------------------------------------
    uint8_t chunk[1] = { 3 };
    send(0x1A8, 1, chunk);
    TEST_ASSERT(blackbox_calls == 1 && blackbox_chunk == 3, "0x1A8 forwards the chunk index to the black box");
    send(0x1A8, 0, 0);
    TEST_ASSERT(blackbox_calls == 0, "0x1A8 without payload is ignored");
    send(0x1AA, 0, 0);
    TEST_ASSERT(stats_calls == 1, "0x1AA forwards to the thermal stats query");

    // ---- unrelated IDs are ignored --------------------------------------------
    send(0x321, 8, magic8);
    TEST_ASSERT(fake_tx_count == 0 && fake_gpio_write_count == 0, "an unknown ID produces no traffic and no actuation");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - firmware_can_global_pre.c\n");
    run_can_global_pre_tests(&failures);
    if (failures == 0) {
        printf("  OK   all CAN global (pre-error-gate) host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

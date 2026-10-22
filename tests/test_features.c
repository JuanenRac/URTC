// =============================================================================
// URTC Firmware - Host logic tests for fw_features.c and firmware_features.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The four optional functions and their switches, with the real glue compiled against the fake
// HAL: every switch is off on a blank F-RAM, 0x1AE writes the record and acts only after the next
// power-up, the health frame goes out once a second only while its switch runs, the 0x120
// extension is planned only while the ramps switch runs, and the heater PID answers 0x1BA, stores
// its gains and autotunes a simulated tip through ThermalPid_Heater().
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_features.h"
#include "firmware_fram.h"
#include "health_profile.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
I2C_HandleTypeDef hi2c2;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag;
volatile uint16_t current_temperature, target_temperature;

static int hsi_writes;
static uint8_t hsi_last_written;
static int hsi_measure_ready;
static uint32_t hsi_expected, hsi_measured;
uint8_t Features_PlatformHsiMeasure(uint32_t *e, uint32_t *m) { if (!hsi_measure_ready) return 0; *e = hsi_expected; *m = hsi_measured; return 1; }
void Features_PlatformHsiWriteTrim(uint8_t trim) { hsi_writes++; hsi_last_written = trim; }
int16_t Features_PlatformMcuTempC(uint16_t *vdda) { *vdda = 3290; return 41; }

static void reset_world(void)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram);
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    fake_tick = 5000;
    fake_tick_step = 0;
    can_regs.ESR = 0;
    active_tool = TOOL_SOLDERING_IRON;
    system_error_flag = 0;
    current_temperature = 25;
    target_temperature = 0;
    hsi_writes = 0;
    hsi_measure_ready = 0;
    rxHeader.StdId = 0;
}

static void send(uint32_t id, const uint8_t *data, uint32_t dlc)
{
    fake_hal_reset_recording();
    memset(rxData, 0, sizeof rxData);
    if (data) memcpy(rxData, data, dlc);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
}

static const FakeTxFrame *find_tx(uint32_t id)
{
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == id) return &fake_tx[i];
    return 0;
}

static void write_mask(uint8_t mask)
{
    FeatConfig_t c;
    Feat_ConfigBlank(&c);
    c.mask = mask;
    Feat_ConfigSeal(&c);
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &c, sizeof c);
}

static void test_record(int *failures)
{
    FeatConfig_t c;
    Feat_ConfigBlank(&c);
    TEST_ASSERT(Feat_ConfigIsValid(&c) && Feat_ConfigMask(&c) == 0, "a blank record is valid and switches nothing on");
    c.mask = 0x0A; Feat_ConfigSeal(&c);
    TEST_ASSERT(Feat_ConfigMask(&c) == 0x0A, "a sealed record gives its mask");
    c.mask = 0x0B;
    TEST_ASSERT(!Feat_ConfigIsValid(&c) && Feat_ConfigMask(&c) == 0, "a record changed after sealing is not trusted");
    c.mask = 0x1A; Feat_ConfigSeal(&c);
    TEST_ASSERT(Feat_ConfigMask(&c) == 0, "a bit this firmware does not have makes the whole record untrusted");
    memset(&c, 0xFF, sizeof c);
    TEST_ASSERT(!Feat_ConfigIsValid(&c) && Feat_ConfigMask(&c) == 0, "erased F-RAM (0xFF) is every feature off");
    TEST_ASSERT(sizeof(FeatConfig_t) == 8, "the record is 8 bytes");

    uint8_t mask = 99;
    const uint8_t good[5] = { 0x46, 0x45, 0x41, 0x54, 0x0A };
    TEST_ASSERT(Feat_ParseCommand(5, good, &mask) && mask == 0x0A, "0x1AE with the magic and a supported mask");
    TEST_ASSERT(!Feat_ParseCommand(4, good, &mask), "a short 0x1AE is ignored");
    TEST_ASSERT(!Feat_ParseCommand(6, good, &mask), "a long 0x1AE is ignored");
    for (int i = 0; i < 4; i++) {
        uint8_t bad[5];
        memcpy(bad, good, 5);
        bad[i] ^= 0xFF;
        TEST_ASSERT(!Feat_ParseCommand(5, bad, &mask), "any corrupted magic byte is ignored");
    }
    const uint8_t over[5] = { 0x46, 0x45, 0x41, 0x54, 0x10 };
    TEST_ASSERT(!Feat_ParseCommand(5, over, &mask), "an unsupported bit is ignored");
}

static void test_switches(int *failures)
{
    reset_world();
    Features_Init();
    TEST_ASSERT(Features_Running() == 0, "a blank F-RAM: every feature off");
    for (uint8_t b = 1; b <= FEAT_SUPPORTED; b <<= 1) TEST_ASSERT(!Features_Active(b), "off by default");

    // query
    send(0x1AF, NULL, 0);
    Features_HandleCommand();
    const FakeTxFrame *r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 3 && r->data[0] == 0 && r->data[1] == 0 && r->data[2] == FEAT_SUPPORTED, "0x1AF: stored 0, running 0, supported 0x0F");

    // change: writes the record, reports it, does not act yet
    const uint8_t on[5] = { 0x46, 0x45, 0x41, 0x54, 0x0A };
    send(0x1AE, on, 5);
    Features_HandleCommand();
    r = find_tx(0x1AF);
    TEST_ASSERT(r && r->data[0] == 0x0A && r->data[1] == 0, "0x1AE: stored 0x0A, nothing running until the next power-up");
    TEST_ASSERT(!Features_Active(FEAT_HEALTH_PROFILE), "the change did not act in the middle of a job");
    Features_Init();
    TEST_ASSERT(Features_Active(FEAT_HEALTH_PROFILE) && Features_Active(FEAT_THERMAL_PID) && !Features_Active(FEAT_MOTION_RAMPS), "after the next power-up the mask is in force");

    // ignored commands write nothing
    int writes = fake_fram_write_calls;
    const uint8_t bad[5] = { 0x46, 0x45, 0x41, 0x55, 0x00 };
    send(0x1AE, bad, 5);
    Features_HandleCommand();
    TEST_ASSERT(fake_fram_write_calls == writes && fake_tx_count == 0, "a command with the wrong magic writes nothing and is not answered");

    // switching off
    const uint8_t off[5] = { 0x46, 0x45, 0x41, 0x54, 0x00 };
    send(0x1AE, off, 5);
    Features_HandleCommand();
    Features_Init();
    TEST_ASSERT(Features_Running() == 0, "all off again");

    // F-RAM that cannot be read: off
    write_mask(0x0F);
    fake_fram_fail_reads = 1;
    Features_Init();
    TEST_ASSERT(Features_Running() == 0, "an F-RAM that does not answer leaves every feature off");
    fake_fram_fail_reads = 0;
    Features_Init();
    TEST_ASSERT(Features_Running() == 0x0F, "and with the chip back they are on");
}

static void test_health(int *failures)
{
    reset_world();
    write_mask(FEAT_HEALTH_PROFILE);
    Features_Init();
    fake_tick = 5000; Features_Service();
    TEST_ASSERT(find_tx(0x1B8) == 0, "no frame before a second has passed");
    can_regs.ESR = (7u << 16) | (3u << 24);              // TEC 7, REC 3
    fake_tick = 5400; Features_Service();
    TEST_ASSERT(find_tx(0x1B8) == 0, "still none at 400 ms");
    fake_tick = 6100; fake_hal_reset_recording(); Features_Service();
    const FakeTxFrame *h = find_tx(0x1B8);
    TEST_ASSERT(h && h->dlc == 8, "one 8-byte 0x1B8 after a second");
    if (h) {
        TEST_ASSERT(h->data[0] == 7 && h->data[1] == 3, "TEC and REC from the error status register");
        TEST_ASSERT(h->data[3] == 16, "oscillator trim 16 (factory value) while the trim switch is off");
        TEST_ASSERT(h->data[5] == 81 && h->data[6] == 0x0C && h->data[7] == 0xDA, "temperature + 40 and supply from the platform hooks");
        TEST_ASSERT(h->data[4] >= 100, "loop maximum shows the longest pass (about 700 ms between the calls)");
    }
    // bus-off counted on the edge only
    can_regs.ESR = (1u << 2);
    fake_tick = 6200; Features_Service();
    fake_tick = 6300; Features_Service();
    fake_tick = 7200; fake_hal_reset_recording(); Features_Service();
    h = find_tx(0x1B8);
    TEST_ASSERT(h && h->data[2] == 1, "one bus-off event is counted once however long the flag stays");
    can_regs.ESR = 0;
    fake_tick = 7300; Features_Service();
    can_regs.ESR = (1u << 2);
    fake_tick = 7400; Features_Service();
    fake_tick = 8300; fake_hal_reset_recording(); Features_Service();
    h = find_tx(0x1B8);
    TEST_ASSERT(h && h->data[2] == 2, "a second event is counted");
    fake_tx_mailboxes_free = 0;
    fake_tick = 9400; fake_hal_reset_recording(); fake_tx_mailboxes_free = 0; Features_Service();
    TEST_ASSERT(fake_tx_count == 0, "no mailbox free: nothing is sent and nothing breaks");

    // switch off: never a frame
    reset_world();
    Features_Init();
    for (uint32_t t = 5000; t < 20000; t += 250) { fake_tick = t; Features_Service(); }
    TEST_ASSERT(find_tx(0x1B8) == 0, "health switch off: no frame at all");
}

static void test_hsi(int *failures)
{
    reset_world();
    write_mask(FEAT_HSI_AUTOTRIM);
    Features_Init();
    hsi_measure_ready = 1; hsi_expected = 1000000; hsi_measured = 1009000;   // 0.9 % fast
    for (int i = 0; i < 16; i++) { fake_tick += 10; Features_Service(); }
    TEST_ASSERT(hsi_writes == 1 && hsi_last_written == 15, "a fast oscillator: one step down, written once");
    reset_world();
    Features_Init();
    hsi_measure_ready = 1; hsi_expected = 1000000; hsi_measured = 1009000;
    for (int i = 0; i < 64; i++) { fake_tick += 10; Features_Service(); }
    TEST_ASSERT(hsi_writes == 0, "trim switch off: the register is never touched");
}

static void test_motion(int *failures)
{
    reset_world();
    Features_Init();
    const uint8_t frame8[8] = { 1, 0, 0, 3, 0xE8, 0x03, 0x20, 2 };
    send(0x120, frame8, 8);
    Features_NoteMove(1000);
    TEST_ASSERT(Features_MotionPlan()->steps == 0, "ramps switch off: no plan");
    write_mask(FEAT_MOTION_RAMPS);
    Features_Init();
    Features_NoteMove(1000);
    const MpPlan_t *p = Features_MotionPlan();
    TEST_ASSERT(p->steps == 1000 && p->profile == MP_SCURVE && p->peak_sps == 800, "ramps on, eight bytes: S-curve plan at 800 steps/s");
    send(0x120, frame8, 5);
    Features_NoteMove(500);
    TEST_ASSERT(Features_MotionPlan()->steps == 1000, "ramps on, five bytes: the old form is left alone");
    uint8_t speed0[8] = { 1, 0, 0, 3, 0xE8, 0, 0, 1 };
    send(0x120, speed0, 8);
    Features_NoteMove(1000);
    TEST_ASSERT(Features_MotionPlan()->peak_sps == MP_DEFAULT_CRUISE_SPS, "speed 0 keeps the default cruise speed");
}

// A simulated tip for the PID glue: heater on or off each call, 20 ms apart.
static double tip_temp, tip_sensor;
static void tip_step(int heater_on, double dt)
{
    double heat = heater_on ? 60.0 : 0.0;
    tip_temp += dt * (heat - 0.126 * (tip_temp - 25.0)) / 1.2;
    tip_sensor += dt * (tip_temp - tip_sensor) / 0.3;
}

static void test_pid(int *failures)
{
    // switch off: the command is answered with state OFF and changes nothing
    reset_world();
    Features_Init();
    const uint8_t set[7] = { 0x03, 0x20, 0x00, 0x14, 0x00, 0x96, 0x00 };
    send(0x1B9, set, 7);
    int writes = fake_fram_write_calls;
    Features_HandlePidCommand();
    const FakeTxFrame *r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[6] == PID_STATE_OFF_VALUE && fake_fram_write_calls == writes, "PID switch off: answered with OFF, nothing stored");

    // on: defaults, then set gains
    reset_world();
    write_mask(FEAT_THERMAL_PID);
    Features_Init();
    send(0x1BA, NULL, 0);
    Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->dlc == 8 && r->data[0] == 0x03 && r->data[1] == 0x20 && r->data[6] == PID_STATE_RUNNING_VALUE, "query: default gains 8.00, 0.20, 1.50, running");
    const uint8_t gains[7] = { 0x04, 0xB0, 0x00, 0x1E, 0x00, 0xC8, 0x00 };
    send(0x1B9, gains, 7);
    Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[0] == 0x04 && r->data[1] == 0xB0 && r->data[3] == 0x1E && r->data[5] == 0xC8, "gains set and reported");
    Features_Init();
    send(0x1BA, NULL, 0);
    Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[0] == 0x04 && r->data[1] == 0xB0, "gains survive a power cycle (F-RAM)");
    const uint8_t huge[7] = { 0xFF, 0xFF, 0, 0, 0, 0, 0 };
    send(0x1B9, huge, 7);
    Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[0] == 0x04 && r->data[1] == 0xB0, "absurd gains are refused");
    const uint8_t shortf[7] = { 0 };
    send(0x1B9, shortf, 6);
    Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[0] == 0x04, "a short 0x1B9 changes nothing");
    memset(&fake_fram[992 + 3], 0x00, 3);   // damage the stored record
    Features_Init();
    send(0x1BA, NULL, 0);
    Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[0] == 0x03 && r->data[1] == 0x20, "a damaged record falls back to the defaults");

    // autotune refused without a heater tool, a setpoint or with an error
    const uint8_t start[7] = { 0, 0, 0, 0, 0, 0, 1 };
    active_tool = TOOL_DRILL; target_temperature = 320; system_error_flag = 0;
    send(0x1B9, start, 7); Features_HandlePidCommand();
    TEST_ASSERT(find_tx(0x1BA)->data[6] == PID_STATE_RUNNING_VALUE, "autotune not started on a tool without a heater");
    active_tool = TOOL_SOLDERING_IRON; target_temperature = 50;
    send(0x1B9, start, 7); Features_HandlePidCommand();
    TEST_ASSERT(find_tx(0x1BA)->data[6] == PID_STATE_RUNNING_VALUE, "autotune not started below the minimum setpoint");
    target_temperature = 320; system_error_flag = 1;
    send(0x1B9, start, 7); Features_HandlePidCommand();
    TEST_ASSERT(find_tx(0x1BA)->data[6] == PID_STATE_RUNNING_VALUE, "autotune not started while the board has an error");
    system_error_flag = 0;

    // autotune on the simulated tip
    tip_temp = tip_sensor = 25.0;
    send(0x1B9, start, 7); Features_HandlePidCommand();
    TEST_ASSERT(find_tx(0x1BA)->data[6] == PID_STATE_AUTOTUNING_VALUE, "autotune started");
    uint32_t now = 100000;
    int heater = 0;
    for (int i = 0; i < 50 * 600; i++) {
        now += 20;
        current_temperature = (uint16_t)(tip_sensor + 0.5);
        heater = ThermalPid_Heater(current_temperature, 320, now);
        tip_step(heater, 0.02);
        if (i % 50 == 0) {
            send(0x1BA, NULL, 0); Features_HandlePidCommand();
            if (find_tx(0x1BA)->data[6] != PID_STATE_AUTOTUNING_VALUE) break;
        }
    }
    send(0x1BA, NULL, 0); Features_HandlePidCommand();
    r = find_tx(0x1BA);
    TEST_ASSERT(r && r->data[6] == PID_STATE_DONE_VALUE, "autotune finished and the gains were stored");
    uint16_t kp = (uint16_t)((r->data[0] << 8) | r->data[1]);
    TEST_ASSERT(kp > 100 && kp < 20000, "the gains are in a sane range");

    // and the tuned PID holds the setpoint through the time-proportional output
    double max_t = 0, err_sum = 0; int n = 0;
    for (int i = 0; i < 50 * 240; i++) {
        now += 20;
        current_temperature = (uint16_t)(tip_sensor + 0.5);
        heater = ThermalPid_Heater(current_temperature, 320, now);
        tip_step(heater, 0.02);
        if (tip_temp > max_t) max_t = tip_temp;
        if (i > 50 * 150) { double e = tip_sensor - 320.0; err_sum += e < 0 ? -e : e; n++; }
    }
    TEST_ASSERT(err_sum / n < 6.0, "PID through the time window: mean error below 6 C once settled");
    TEST_ASSERT(max_t < 320.0 + 45.0, "and no large overshoot");

    // setpoint 0: heater off
    int any_on = 0;
    for (int i = 0; i < 100; i++) { now += 20; if (ThermalPid_Heater(100, 0, now)) any_on = 1; }
    TEST_ASSERT(!any_on, "target 0: the heater never turns on");

    // an abort of the autotune
    target_temperature = 320;
    send(0x1B9, start, 7); Features_HandlePidCommand();
    const uint8_t abort_frame[7] = { 0, 0, 0, 0, 0, 0, 2 };
    send(0x1B9, abort_frame, 7); Features_HandlePidCommand();
    TEST_ASSERT(find_tx(0x1BA)->data[6] == PID_STATE_RUNNING_VALUE, "autotune aborted: back to running");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - optional features\n");
    test_record(&failures);
    test_switches(&failures);
    test_health(&failures);
    test_hsi(&failures);
    test_motion(&failures);
    test_pid(&failures);
    if (failures == 0) {
        printf("  OK   all optional feature host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

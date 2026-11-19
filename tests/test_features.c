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
#include "can_bit_timing.h"
#include "health_profile.h"
#include "urtc_can_ids.h"

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
I2C_HandleTypeDef hi2c2;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag;
volatile uint16_t current_temperature, target_temperature;
volatile uint8_t last_reset_cause = 0x08;   // power-on, as the real one latches at boot

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
    c.mask = 0x9A; Feat_ConfigSeal(&c);
    TEST_ASSERT(Feat_ConfigMask(&c) == 0x9A, "all eight bits are switches of this firmware: a sealed record with the eighth bit is trusted");
    c.mask = 0xFF; Feat_ConfigSeal(&c);
    TEST_ASSERT(Feat_ConfigMask(&c) == 0xFF && FEAT_SUPPORTED == 0xFF, "a record with every switch on is trusted");
    memset(&c, 0xFF, sizeof c);
    TEST_ASSERT(!Feat_ConfigIsValid(&c) && Feat_ConfigMask(&c) == 0, "erased F-RAM (0xFF) is every feature off");
    TEST_ASSERT(sizeof(FeatConfig_t) == 16 && sizeof(FeatConfigV1_t) == 8, "the record is 16 bytes (8 in the first format)");
    TEST_ASSERT(FEAT_CONFIG_RAW_SIZE == 16 && FEAT_CONFIG_FRAM_ADDR + FEAT_CONFIG_RAW_SIZE <= 992, "the record ends before the heater PID gains at address 992");

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
    const uint8_t every[5] = { 0x46, 0x45, 0x41, 0x54, 0xFF };
    TEST_ASSERT(Feat_ParseCommand(5, every, &mask) && mask == 0xFF, "the eighth bit (following the emergency stop) is a supported switch: all eight are accepted");
}

static void put_v1(uint8_t mask, int sound)
{
    FeatConfigV1_t o;
    memset(&o, 0, sizeof o);
    o.magic = FEAT_CONFIG_MAGIC;
    o.struct_version = 1;
    o.mask = mask;
    uint8_t crc = 0;
    const uint8_t *p = (const uint8_t *)&o;
    for (unsigned i = 0; i < sizeof(o) - 1; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    o.checksum = sound ? crc : (uint8_t)(crc ^ 0x55);
    memset(&fake_fram[FEAT_CONFIG_FRAM_ADDR], 0xFF, FEAT_CONFIG_RAW_SIZE);   // the second half of the 16 bytes was never written by the old firmware
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &o, sizeof o);
}

static void test_second_byte_and_formats(int *failures)
{
    FeatConfig_t c;
    uint8_t raw[FEAT_CONFIG_RAW_SIZE];

    // the record of the first format is still read: a board configured before the second byte keeps its switches
    reset_world();
    put_v1(0x2A, 1);
    memcpy(raw, &fake_fram[FEAT_CONFIG_FRAM_ADDR], sizeof raw);
    TEST_ASSERT(Feat_ConfigDecode(raw, &c) && Feat_ConfigMask(&c) == 0x2A && Feat_ConfigMaskExt(&c) == 0 && c.struct_version == FEAT_CONFIG_VERSION, "a sound record of version 1 is read; the second byte is zero");
    int writes_before = fake_fram_write_calls;
    Features_Init();
    TEST_ASSERT(Features_Running() == 0x2A && Features_RunningExt() == 0, "a board configured in the first format keeps its switches after the update and the new ones are off");
    TEST_ASSERT(fake_fram_write_calls == writes_before, "reading a record of version 1 writes nothing at power-up");

    // a damaged record of version 1 is every feature off
    put_v1(0x2A, 0);
    memcpy(raw, &fake_fram[FEAT_CONFIG_FRAM_ADDR], sizeof raw);
    TEST_ASSERT(!Feat_ConfigDecode(raw, &c) && Feat_ConfigMask(&c) == 0 && Feat_ConfigMaskExt(&c) == 0, "a version 1 record with a bad checksum switches nothing on");

    // a record of version 2 with both bytes
    Feat_ConfigBlank(&c);
    c.mask = 0x20; c.mask_ext = FEAT_EXT_WATCHDOG_HOST_ONLY; Feat_ConfigSeal(&c);
    memcpy(raw, &c, sizeof raw);
    FeatConfig_t d;
    TEST_ASSERT(Feat_ConfigDecode(raw, &d) && Feat_ConfigMask(&d) == 0x20 && Feat_ConfigMaskExt(&d) == FEAT_EXT_WATCHDOG_HOST_ONLY, "a sound record of version 2 gives both bytes");

    // anything else: every feature off, both bytes
    raw[4] = 3;
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d) && Feat_ConfigMask(&d) == 0 && Feat_ConfigMaskExt(&d) == 0, "a version this firmware does not know is every feature off");
    memcpy(raw, &c, sizeof raw);
    raw[15] ^= 0x01;
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d) && Feat_ConfigMaskExt(&d) == 0, "a record of version 2 with a bad checksum is every feature off");
    memcpy(raw, &c, sizeof raw);
    raw[7] = 0x80;
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d), "a change in the reserved bytes breaks the checksum");
    Feat_ConfigBlank(&c);
    c.mask_ext = 0x04; Feat_ConfigSeal(&c);
    memcpy(raw, &c, sizeof raw);
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d) && Feat_ConfigMask(&d) == 0, "a bit of the second byte that this firmware does not have: the record is not trusted");
    memset(raw, 0xFF, sizeof raw);
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d) && Feat_ConfigMask(&d) == 0, "erased F-RAM is every feature off");
    memset(raw, 0x00, sizeof raw);
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d), "zeroed F-RAM is every feature off");

    // the commands
    uint8_t mask, ext, extended;
    const uint8_t five[5] = { 0x46, 0x45, 0x41, 0x54, 0x20 };
    const uint8_t six[6] = { 0x46, 0x45, 0x41, 0x54, 0x20, 0x01 };
    TEST_ASSERT(Feat_ParseCommandEx(5, five, &mask, &ext, &extended) && mask == 0x20 && ext == 0 && !extended, "the five-byte form sets the second byte to zero");
    TEST_ASSERT(Feat_ParseCommandEx(6, six, &mask, &ext, &extended) && mask == 0x20 && ext == 1 && extended, "the six-byte form carries both bytes");
    const uint8_t bad_ext[6] = { 0x46, 0x45, 0x41, 0x54, 0x20, 0x04 };
    const uint8_t update_switch[6] = { 0x46, 0x45, 0x41, 0x54, 0x00, 0x02 };
    TEST_ASSERT(!Feat_ParseCommandEx(6, bad_ext, &mask, &ext, &extended), "a bit of the second byte that this firmware does not have is ignored");
    TEST_ASSERT(Feat_ParseCommandEx(6, update_switch, &mask, &ext, &extended) && ext == FEAT_EXT_ALLOW_UPDATE_ACTIVE_ACTUATOR && FEAT_EXT_SUPPORTED == (FEAT_EXT_WATCHDOG_HOST_ONLY | FEAT_EXT_ALLOW_UPDATE_ACTIVE_ACTUATOR), "the switch that lets an update end while a tool is active is accepted and is off in a blank record");
    TEST_ASSERT(!Feat_ParseCommandEx(4, six, &mask, &ext, &extended) && !Feat_ParseCommandEx(7, six, &mask, &ext, &extended), "a short or a long 0x1AE is ignored");
    const uint8_t bad_magic[6] = { 0x46, 0x45, 0x41, 0x55, 0x20, 0x01 };
    TEST_ASSERT(!Feat_ParseCommandEx(6, bad_magic, &mask, &ext, &extended), "a six-byte command with a wrong magic is ignored");
    const uint8_t two = 2, zero = 0;
    TEST_ASSERT(Feat_QueryWantsExtended(1, &two) && !Feat_QueryWantsExtended(1, &zero) && !Feat_QueryWantsExtended(0, NULL) && !Feat_QueryWantsExtended(2, six), "only the one-byte query 02 asks for the six-byte answer");
    uint8_t out6[6];
    Feat_EncodeResponseExt(0x20, 0x00, 0x01, 0x00, out6);
    TEST_ASSERT(out6[0] == 0x20 && out6[1] == 0 && out6[2] == FEAT_SUPPORTED && out6[3] == 1 && out6[4] == 0 && out6[5] == FEAT_EXT_SUPPORTED, "the six-byte answer");

    // through the glue: each host is answered in its own form
    reset_world();
    Features_Init();
    send(0x1AE, five, 5);
    Features_HandleCommand();
    const FakeTxFrame *r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 3 && r->data[0] == 0x20, "a five-byte 0x1AE is answered with three bytes");
    send(0x1AE, six, 6);
    Features_HandleCommand();
    r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 6 && r->data[0] == 0x20 && r->data[1] == 0 && r->data[3] == 1 && r->data[4] == 0 && r->data[5] == FEAT_EXT_SUPPORTED, "a six-byte 0x1AE is answered with six: stored now, running only after power-up");
    Features_Init();
    TEST_ASSERT(Features_Running() == 0x20 && Features_RunningExt() == 1 && Features_ActiveExt(FEAT_EXT_WATCHDOG_HOST_ONLY), "after the next power-up both bytes are in force");
    memcpy(raw, &fake_fram[FEAT_CONFIG_FRAM_ADDR], sizeof raw);
    TEST_ASSERT(raw[4] == FEAT_CONFIG_VERSION && raw[6] == 1, "the record is written in the current format");
    send(0x1AF, &two, 1);
    Features_HandleCommand();
    r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 6 && r->data[0] == 0x20 && r->data[1] == 0x20 && r->data[3] == 1 && r->data[4] == 1, "the query 02 gets the six-byte answer with both masks in force");
    send(0x1AF, NULL, 0);
    Features_HandleCommand();
    r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 3, "an empty query is answered with three bytes as before");
    const uint8_t other = 7;
    send(0x1AF, &other, 1);
    Features_HandleCommand();
    r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 3, "a one-byte query that is not 02 is answered with three bytes");
    send(0x1AE, five, 5);
    Features_HandleCommand();
    Features_Init();
    TEST_ASSERT(Features_Running() == 0x20 && Features_RunningExt() == 0, "the five-byte form puts every switch of the second byte off");

    // a version 1 record is replaced by a version 2 one the first time the host writes
    reset_world();
    put_v1(0x0A, 1);
    Features_Init();
    send(0x1AE, six, 6);
    Features_HandleCommand();
    memcpy(raw, &fake_fram[FEAT_CONFIG_FRAM_ADDR], sizeof raw);
    TEST_ASSERT(raw[4] == FEAT_CONFIG_VERSION && Feat_ConfigDecode(raw, &c) && Feat_ConfigMask(&c) == 0x20 && Feat_ConfigMaskExt(&c) == 1, "the first write replaces the old record by one of the current format");

    // a chip that refuses the write: the answer says what the chip really holds
    reset_world();
    Features_Init();
    fake_fram_fail_writes = 1;
    send(0x1AE, six, 6);
    Features_HandleCommand();
    r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 6 && r->data[0] == 0 && r->data[3] == 0, "a write that fails reports what the chip really holds");
    fake_fram_fail_writes = 0;
}

static void test_switches(int *failures)
{
    reset_world();
    Features_Init();
    TEST_ASSERT(Features_Running() == 0, "a blank F-RAM: every feature off");
    for (uint8_t b = 1; b != 0; b <<= 1) TEST_ASSERT(!Features_Active(b), "off by default");

    // query
    send(0x1AF, NULL, 0);
    Features_HandleCommand();
    const FakeTxFrame *r = find_tx(0x1AF);
    TEST_ASSERT(r && r->dlc == 3 && r->data[0] == 0 && r->data[1] == 0 && r->data[2] == FEAT_SUPPORTED, "0x1AF: stored 0, running 0, supported 0xFF");

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

    // the identity frame 0x1BB comes after every tenth health frame, never before
    reset_world();
    write_mask(FEAT_HEALTH_PROFILE);
    Features_Init();
    fake_tick = 5000; Features_Service();
    int info_seen = 0;
    for (int i = 1; i <= 10; i++) {
        fake_tick = 5000u + (uint32_t)i * 1100u; fake_hal_reset_recording(); Features_Service();
        const FakeTxFrame *info = find_tx(0x1BB);
        if (i < 10) { TEST_ASSERT(info == 0, "no identity frame before the tenth health frame"); continue; }
        info_seen = info != 0;
        TEST_ASSERT(info && info->dlc == 8, "an 8-byte 0x1BB after the tenth health frame");
        if (info) {
            TEST_ASSERT(info->data[0] == URTC_PROTOCOL_VERSION && info->data[1] == 0x08, "protocol byte and the reset cause the board latched at boot");
            TEST_ASSERT(info->data[2] == FIRMWARE_VERSION_MINOR && info->data[3] == FIRMWARE_VERSION_PATCH && info->data[4] == FIRMWARE_VERSION_BUILD, "firmware version");
            TEST_ASSERT(info->data[5] == Health_BusRateCode(URTC_CAN_BITRATE_BPS), "the bus rate the board was built for is declared in byte 5");
            uint32_t up = ((uint32_t)info->data[6] << 8) | info->data[7];
            TEST_ASSERT(up == (5000u + 10u * 1100u) / 1000u, "uptime in seconds from the tick");
        }
        TEST_ASSERT(find_tx(0x1B8) != 0, "the health frame goes out in the same pass");
    }
    TEST_ASSERT(info_seen, "the identity frame went out");
    fake_tick = 5000u + 11u * 1100u; fake_hal_reset_recording(); Features_Service();
    TEST_ASSERT(find_tx(0x1BB) == 0, "and the count starts again");

    // switch off: never a frame
    reset_world();
    Features_Init();
    for (uint32_t t = 5000; t < 20000; t += 250) { fake_tick = t; Features_Service(); }
    TEST_ASSERT(find_tx(0x1B8) == 0 && find_tx(0x1BB) == 0, "health switch off: no frame at all");
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

static void test_estop_timeout_in_the_record(int *failures)
{
    FeatConfig_t c, d;
    uint8_t raw[FEAT_CONFIG_RAW_SIZE];
    Feat_ConfigBlank(&c);
    TEST_ASSERT(Feat_ConfigEstopTimeoutMs(&c) == 0, "a blank record has no configured silence of the master (the default)");
    c.mask = FEAT_ESTOP_FOLLOW; c.estop_alive_timeout_ms = 250; Feat_ConfigSeal(&c);
    memcpy(raw, &c, sizeof raw);
    TEST_ASSERT(Feat_ConfigDecode(raw, &d) && Feat_ConfigEstopTimeoutMs(&d) == 250 && Feat_ConfigMask(&d) == FEAT_ESTOP_FOLLOW, "a sound record gives its timeout");
    TEST_ASSERT(raw[7] == 250 && raw[8] == 0, "the timeout is two bytes, little endian, at the start of the reserved area");
    raw[8] = 1;
    TEST_ASSERT(!Feat_ConfigDecode(raw, &d) && Feat_ConfigEstopTimeoutMs(&d) == 0, "a changed timeout breaks the checksum and reads as the default");
    Feat_ConfigBlank(&c);
    c.estop_alive_timeout_ms = 250;   // not sealed: invalid
    TEST_ASSERT(Feat_ConfigEstopTimeoutMs(&c) == 0, "an invalid record gives no timeout");

    // a change of the switches through 0x1AE keeps the timeout that is in the record
    reset_world();
    Feat_ConfigBlank(&c);
    c.mask = FEAT_ESTOP_FOLLOW; c.estop_alive_timeout_ms = 300; Feat_ConfigSeal(&c);
    memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &c, sizeof c);
    Features_Init();
    TEST_ASSERT(Features_EstopTimeoutMs() == 300, "the timeout is read once at power-up");
    const uint8_t change[5] = { 0x46, 0x45, 0x41, 0x54, 0x20 };
    send(0x1AE, change, 5);
    Features_HandleCommand();
    memcpy(raw, &fake_fram[FEAT_CONFIG_FRAM_ADDR], sizeof raw);
    TEST_ASSERT(Feat_ConfigDecode(raw, &d) && Feat_ConfigMask(&d) == 0x20 && Feat_ConfigEstopTimeoutMs(&d) == 300, "a change of the switches does not lose the timeout");
    Features_Init();
    TEST_ASSERT(Features_EstopTimeoutMs() == 300, "and it is still there after the next power-up");

    // a version 1 record has no timeout
    reset_world();
    put_v1(0x80, 1);
    Features_Init();
    TEST_ASSERT(Features_EstopTimeoutMs() == 0, "a record of the first format gives the default");
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - optional features\n");
    test_record(&failures);
    test_switches(&failures);
    test_second_byte_and_formats(&failures);
    test_health(&failures);
    test_hsi(&failures);
    test_motion(&failures);
    test_pid(&failures);
    test_estop_timeout_in_the_record(&failures);
    if (failures == 0) {
        printf("  OK   all optional feature host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

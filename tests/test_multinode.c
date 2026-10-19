// =============================================================================
// URTC Firmware - Host logic tests for the multinode extension (multinode.c, firmware_multinode.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Several tool heads on one CAN bus, behind a persistent switch that is OFF by default.
//   * With the switch off (never written, erased F-RAM, damaged record, any value but 1) the
//     receive path is the single-head path: every identifier reaches the same handlers as
//     before, nothing is sent, telemetry is never gated.
//   * With the switch on: node address, discovery, SELECT with its timeout, per-node status,
//     the rule that a node which is not selected never actuates, conflicts, SAFE_STOP.
// The real multinode.c, firmware_multinode.c, firmware_can_dispatch.c and firmware_fram.c run
// against the fake HAL; the tool handlers are recorders. Host only: nothing here ran on a board.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_runner.h"
#include "firmware_common.h"
#include "firmware_can_global.h"
#include "firmware_can_slavebridge.h"
#include "firmware_can_soldering.h"
#include "firmware_can_motion.h"
#include "firmware_can_drill.h"
#include "firmware_can_aoi.h"
#include "firmware_can_laser.h"
#include "firmware_can_printer3d.h"
#include "firmware_can_electromagnet.h"
#include "firmware_can_uvcuring.h"
#include "firmware_can_weldpulse.h"
#include "firmware_can_hotair.h"
#include "firmware_can_crimping.h"
#include "firmware_can_pastejetting.h"
#include "firmware_can_flyingprobe.h"
#include "firmware_can_thermalinspection.h"
#include "firmware_multinode.h"
#include "firmware_fram.h"
#include "multinode.h"

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_m);

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
I2C_HandleTypeDef hi2c2;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool;
volatile uint8_t system_error_flag, can_bus_error_flag, boot_sequence_active;
volatile uint8_t thermal_fault_code, last_reset_cause;
volatile uint32_t can_led_tick;
uint8_t raw_id_pin_value;

// ---- recorders -------------------------------------------------------------
static char trace[256];
static int trace_len;
static void note(const char *name)
{
    int n = (int)strlen(name);
    if (trace_len + n + 1 < (int)sizeof trace) {
        memcpy(trace + trace_len, name, (size_t)n);
        trace_len += n;
        trace[trace_len++] = ' ';
        trace[trace_len] = 0;
    }
}
void Handle_CAN_GlobalCommands_PreErrorGate(void) { note("pre"); }
void Handle_CAN_GlobalCommands_PostErrorGate(void) { note("post"); }
void Handle_CAN_SlaveBridge(void) { note("bridge"); }
void Handle_CAN_SolderingIron(void) { note("solder"); }
void Handle_CAN_MotionTools(void) { note("motion"); }
void Handle_CAN_Drill(void) { note("drill"); }
void Handle_CAN_AOI(void) { note("aoi"); }
void Handle_CAN_Laser(void) { note("laser"); }
void Handle_CAN_3DPrinter(void) { note("3dp"); }
void Handle_CAN_Electromagnet(void) { note("magnet"); }
void Handle_CAN_UVCuring(void) { note("uv"); }
void Handle_CAN_WeldPulse(void) { note("weld"); }
void Handle_CAN_HotAirRework(void) { note("hotair"); }
void Handle_CAN_CrimpingActuator(void) { note("crimp"); }
void Handle_CAN_PasteJetting(void) { note("jet"); }
void Handle_CAN_FlyingProbe(void) { note("probe"); }
void Handle_CAN_ThermalInspection(void) { note("thermal"); }

static int safe_state_calls;
void MultiNode_PlatformSafeState(void) { safe_state_calls++; }
static uint32_t board_serial = 0x01020304u;
uint32_t MultiNode_PlatformSerial(void) { return board_serial; }

// ---- helpers ---------------------------------------------------------------
static void reset_world(void)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0xFF, sizeof fake_fram); // a blank (erased) chip
    fake_fram_fail_reads = fake_fram_fail_writes = 0;
    fake_tick = 100000;
    fake_tick_step = 0;
    active_tool = TOOL_DRILL;
    system_error_flag = can_bus_error_flag = boot_sequence_active = 0;
    thermal_fault_code = last_reset_cause = 0;
    raw_id_pin_value = 5;
    safe_state_calls = 0;
    trace_len = 0;
    trace[0] = 0;
    fake_rx_queue_count = 0;
}

static void deliver_dlc(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    trace_len = 0;
    trace[0] = 0;
    memset(&fake_rx_queue[0], 0, sizeof fake_rx_queue[0]);
    fake_rx_queue[0].hdr.StdId = id;
    fake_rx_queue[0].hdr.DLC = dlc;
    if (data) memcpy(fake_rx_queue[0].data, data, dlc);
    fake_rx_queue_count = 1;
    HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
}
static void deliver(uint32_t id) { deliver_dlc(id, NULL, 1); }

static void select_node(uint8_t node, uint8_t seq)
{
    uint8_t d[3] = { 1, node, seq };
    deliver_dlc(MN_ID_SELECT, d, 3);
}
static void deselect_all(uint8_t seq)
{
    uint8_t d[3] = { 0, 0, seq };
    deliver_dlc(MN_ID_SELECT, d, 3);
}
static void hear_node(uint32_t id) { uint8_t d[8] = {0}; deliver_dlc(id, d, 8); }

static void write_config(uint8_t enabled, uint8_t instance)
{
    MnConfig_t c;
    Mn_ConfigBlank(&c);
    c.enabled = enabled;
    c.instance = instance;
    Mn_ConfigSeal(&c);
    memcpy(&fake_fram[MN_CONFIG_FRAM_ADDR], &c, sizeof c);
}

static void boot_enabled(uint8_t address, ToolMode_t tool)
{
    reset_world();
    raw_id_pin_value = address;
    active_tool = tool;
    write_config(1, 3);
    MultiNode_Init();
}

static int sent(uint32_t id)
{
    int n = 0;
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == id) n++;
    return n;
}
static const FakeTxFrame *last_sent(uint32_t id)
{
    for (int i = fake_tx_count - 1; i >= 0; i--) if (fake_tx[i].id == id) return &fake_tx[i];
    return NULL;
}
static void advance_ms(uint32_t ms) { fake_tick += ms; MultiNode_Service(); }

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

// The identifiers the single-head protocol uses (0x095..0x255 and the bootloader range): none may fall in the block.
static int is_single_head_block(uint32_t id) { return (id >= 0x095 && id <= 0x255) || (id >= 0x7E9 && id <= 0x7FF); }

void run_multinode_tests(int *failures)
{
    char label[128];

    // ---- constants and layout ----------------------------------------------------------------
    TEST_ASSERT(sizeof(MnConfig_t) == 8, "the F-RAM record is 8 packed bytes");
    TEST_ASSERT(MN_ID_FIRST == 0x400 && MN_ID_LAST == 0x49F, "the extension owns 0x400-0x49F");
    TEST_ASSERT(MN_ID_NODE_INFO + MN_NODE_MAX - 1 < MN_ID_NODE_STATUS && MN_ID_NODE_STATUS + MN_NODE_MAX - 1 < MN_ID_NODE_CMD &&
                MN_ID_NODE_CMD + MN_NODE_MAX - 1 < MN_ID_NODE_RESP && MN_ID_NODE_RESP + MN_NODE_MAX - 1 <= MN_ID_LAST,
                "the four ranged frames do not overlap and fit in the block");
    for (uint32_t id = MN_ID_FIRST; id <= MN_ID_LAST; id++) {
        if (is_single_head_block(id)) { TEST_ASSERT(0, "an extension identifier collides with the single-head protocol"); break; }
    }
    TEST_ASSERT(MN_ADDRESS_MASK == 31 && MN_NODE_MAX == 32 && MN_ADDRESS_BITS == 5, "five address bits");
    TEST_ASSERT(MN_CONFIG_FRAM_ADDR >= SAVEDSTATE_FRAM_ADDR + sizeof(SavedState_t) &&
                MN_CONFIG_FRAM_ADDR >= BLACKBOX_FRAM_ADDR + sizeof(BlackBoxRecord_t) &&
                MN_CONFIG_FRAM_ADDR >= THERMAL_STATS_FRAM_ADDR + 13 &&
                MN_CONFIG_FRAM_ADDR + sizeof(MnConfig_t) <= FRAM_SIZE_BYTES,
                "the switch record sits in free F-RAM, past the saved state, the thermal counters and the fault recorder");

    // ---- the persistent switch ----------------------------------------------------------------
    MnConfig_t c;
    Mn_ConfigBlank(&c);
    TEST_ASSERT(Mn_ConfigIsValid(&c) && !Mn_ConfigIsEnabled(&c), "a blank record is valid and off");
    TEST_ASSERT(c.checksum == ref_crc8(&c, sizeof c - 1), "the record checksum is CRC-8/SMBUS");
    c.enabled = 1; Mn_ConfigSeal(&c);
    TEST_ASSERT(Mn_ConfigIsEnabled(&c), "a sealed record with enabled = 1 is on");
    c.enabled = 2; Mn_ConfigSeal(&c);
    TEST_ASSERT(Mn_ConfigIsValid(&c) && !Mn_ConfigIsEnabled(&c), "any value but 1 is off");
    c.enabled = 1;
    TEST_ASSERT(!Mn_ConfigIsValid(&c) && !Mn_ConfigIsEnabled(&c), "changing a field without resealing is off");
    memset(&c, 0xFF, sizeof c);
    TEST_ASSERT(!Mn_ConfigIsEnabled(&c), "erased F-RAM (0xFF) is off");
    memset(&c, 0x00, sizeof c);
    TEST_ASSERT(!Mn_ConfigIsEnabled(&c), "zeroed F-RAM is off");

    // ---- switch off: the single-head path is untouched --------------------------------------------
    {
        static const char *const states[] = { "erased chip", "no chip", "damaged record", "written off", "wrong value" };
        for (int st = 0; st < 5; st++) {
            reset_world();
            if (st == 1) fake_fram_fail_reads = 1000;
            if (st == 2) { write_config(1, 0); fake_fram[MN_CONFIG_FRAM_ADDR + 7] ^= 0x55; }
            if (st == 3) write_config(0, 0);
            if (st == 4) { MnConfig_t w; Mn_ConfigBlank(&w); w.enabled = 7; Mn_ConfigSeal(&w); memcpy(&fake_fram[MN_CONFIG_FRAM_ADDR], &w, sizeof w); }
            MultiNode_Init();
            int bad_trace = 0, bad_tx = 0;
            for (uint32_t id = 0; id <= 0x7FF; id++) {
                deliver(id);
                // Exactly what the dispatcher does without the extension: pre, post, bridge, the drill handler.
                if (strcmp(trace, "pre post bridge drill ") != 0) bad_trace++;
                if (fake_tx_count != 0) bad_tx++;
            }
            snprintf(label, sizeof label, "switch off (%s): all 2048 identifiers reach the same handlers as a single head", states[st]);
            TEST_ASSERT(bad_trace == 0, label);
            snprintf(label, sizeof label, "switch off (%s): nothing is transmitted by receiving", states[st]);
            TEST_ASSERT(bad_tx == 0, label);
            for (int i = 0; i < 100; i++) advance_ms(500);
            snprintf(label, sizeof label, "switch off (%s): 50 s of service sends no frame", states[st]);
            TEST_ASSERT(fake_tx_count == 0, label);
            TEST_ASSERT(MultiNode_SingleHeadTxAllowed() == 1, "switch off: single-head telemetry is never gated");
            TEST_ASSERT(safe_state_calls == 0, "switch off: the safe state is never forced");
        }
        // Frames of the extension's own block are single-head noise while it is off: they reach the handlers, which ignore them.
        reset_world();
        MultiNode_Init();
        static const uint32_t mn_ids[] = { MN_ID_DISCOVER, MN_ID_SELECT, MN_ID_SAFE_STOP, 0x420, 0x445, 0x465, 0x485 };
        int passed = 0;
        for (unsigned i = 0; i < sizeof(mn_ids) / sizeof(mn_ids[0]); i++) {
            deliver(mn_ids[i]);
            if (strcmp(trace, "pre post bridge drill ") == 0) passed++;
        }
        TEST_ASSERT(passed == 7, "switch off: a SELECT, DISCOVER or SAFE_STOP changes nothing and is not consumed");
        TEST_ASSERT(safe_state_calls == 0, "switch off: SAFE_STOP is not obeyed");
        // The error gate still decides, as before.
        system_error_flag = 1;
        deliver(0x120);
        TEST_ASSERT(strcmp(trace, "pre ") == 0, "switch off: the critical-error gate is unchanged");
    }

    // ---- the enabling command ---------------------------------------------------------------------
    {
        reset_world();
        raw_id_pin_value = 9;
        MultiNode_Init();
        uint8_t q[1] = {0};
        rxHeader.StdId = 0x1AD; rxHeader.DLC = 0;
        MultiNode_HandleConfigCommand();
        const FakeTxFrame *r = last_sent(0x1AD);
        TEST_ASSERT(r && r->dlc == 4 && r->data[0] == 0 && r->data[1] == 0 && r->data[3] == 9, "a query on a fresh board answers: off, off, address 9");
        (void)q;
        int calls = fake_fram_write_calls;
        struct { uint8_t d[6]; uint8_t dlc; const char *why; } bad[] = {
            { {0x4D, 0x4E, 0x4F, 0x45, 1, 0}, 6, "wrong magic" },
            { {0x4D, 0x4E, 0x4F, 0x44, 1, 0}, 5, "short frame" },
            { {0x4D, 0x4E, 0x4F, 0x44, 2, 0}, 6, "enable value 2" },
            { {0x4D, 0x4E, 0x4F, 0x44, 1, 0}, 3, "magic cut short" },
            { {0xE3, 0xA5, 0xE0, 0xFF, 1, 0}, 6, "another command's magic" },
        };
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            fake_tx_count = 0;
            memcpy(rxData, bad[i].d, 6);
            rxHeader.StdId = 0x1AC; rxHeader.DLC = bad[i].dlc;
            MultiNode_HandleConfigCommand();
            snprintf(label, sizeof label, "enabling command refused: %s", bad[i].why);
            TEST_ASSERT(fake_fram_write_calls == calls && fake_tx_count == 0, label);
        }
        fake_tx_count = 0;
        const uint8_t good[6] = {0x4D, 0x4E, 0x4F, 0x44, 1, 4};
        memcpy(rxData, good, 6);
        rxHeader.StdId = 0x1AC; rxHeader.DLC = 6;
        MultiNode_HandleConfigCommand();
        r = last_sent(0x1AD);
        TEST_ASSERT(r && r->data[0] == 1 && r->data[1] == 0 && r->data[2] == 4 && r->data[3] == 9,
                    "the valid command is stored (on, instance 4) but the running state is still off until a restart");
        MnConfig_t stored;
        memcpy(&stored, &fake_fram[MN_CONFIG_FRAM_ADDR], sizeof stored);
        TEST_ASSERT(Mn_ConfigIsEnabled(&stored) && stored.instance == 4, "the F-RAM record holds the setting");
        deliver(0x120);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "before the restart the board still behaves as a single head");
        MultiNode_Init();
        deliver(0x120);
        TEST_ASSERT(trace_len == 0, "after the restart the extension is active (a listening node obeys no single-head frame)");
        // switching it off again
        fake_tick += 3000;
        select_node(9, 1);
        const uint8_t off[6] = {0x4D, 0x4E, 0x4F, 0x44, 0, 4};
        memcpy(rxData, off, 6);
        rxHeader.StdId = 0x1AC; rxHeader.DLC = 6;
        fake_tx_count = 0;
        MultiNode_HandleConfigCommand();
        r = last_sent(0x1AD);
        TEST_ASSERT(r && r->data[0] == 0 && r->data[1] == 1, "switching it off is stored at once and acts after the restart");
        MultiNode_Init();
        deliver(0x120);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "off again after the restart");
        // a failed F-RAM write reports the truth
        reset_world();
        MultiNode_Init();
        fake_fram_fail_writes = 1000;
        memcpy(rxData, good, 6);
        rxHeader.StdId = 0x1AC; rxHeader.DLC = 6;
        MultiNode_HandleConfigCommand();
        r = last_sent(0x1AD);
        TEST_ASSERT(r && r->data[0] == 0, "if the chip does not take the write, the answer says off");
    }

    // ---- switch on: listening, legacy mode, selection ----------------------------------------------
    {
        boot_enabled(4, TOOL_DRILL);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "listening (first 1.5 s): no single-head frame is obeyed");
        advance_ms(1000);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "still listening at 1.0 s");
        TEST_ASSERT(MultiNode_SingleHeadTxAllowed() == 0, "while listening the node sends no single-head telemetry");
        advance_ms(600);
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "heard nobody for 1.5 s: legacy mode, the single head is obeyed");
        TEST_ASSERT(MultiNode_SingleHeadTxAllowed() == 1, "legacy mode sends its telemetry");
        // the first status shows SELECTED
        fake_tx_count = 0;
        advance_ms(1000);
        const FakeTxFrame *st = last_sent(MN_ID_NODE_STATUS + 4);
        TEST_ASSERT(st && st->dlc == 4 && (st->data[0] & MN_STATE_SELECTED), "status frame 0x444 shows SELECTED in legacy mode");
    }
    {
        boot_enabled(4, TOOL_DRILL);
        hear_node(MN_ID_NODE_STATUS + 5); // another node, while listening
        advance_ms(2000);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "another node heard while listening: this node starts unselected and stays so");
        TEST_ASSERT(MultiNode_SingleHeadTxAllowed() == 0, "an unselected node sends no single-head telemetry");
        select_node(4, 1);
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "after SELECT of its address the node obeys");
        fake_tx_count = 0;
        advance_ms(1);
        const FakeTxFrame *st = last_sent(MN_ID_NODE_STATUS + 4);
        TEST_ASSERT(st && (st->data[0] & MN_STATE_SELECTED), "a selection is acknowledged at once in the status frame");
        advance_ms(1900);
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "1.9 s after SELECT it is still selected");
        select_node(4, 2); // the host repeats
        advance_ms(1900);
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "a repeated SELECT extends the selection");
        fake_tx_count = 0;
        advance_ms(200);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "a selection lapses after 2 s without a repeat (a host that died leaves no node selected)");
        st = last_sent(MN_ID_NODE_STATUS + 4);
        TEST_ASSERT(st && !(st->data[0] & MN_STATE_SELECTED), "the lapse shows in the next status frame");
        select_node(4, 3);
        select_node(5, 4);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "SELECT of another node deselects this one");
        select_node(4, 5);
        deselect_all(6);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "DESELECT_ALL deselects");
        // a lost SELECT leaves the node as it was
        select_node(4, 7);
        { uint8_t d[3] = { 1, 4, 8 }; deliver_dlc(MN_ID_SELECT, d, 2); }
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "a malformed SELECT changes nothing");
        { uint8_t d[3] = { 2, 4, 9 }; deliver_dlc(MN_ID_SELECT, d, 3); }
        { uint8_t d[3] = { 1, 32, 9 }; deliver_dlc(MN_ID_SELECT, d, 3); }
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "an out-of-range action or node address changes nothing");
        // bus-off: no frames reach the node for longer than the lapse
        advance_ms(3000);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "after a silence longer than the lapse (bus-off, dead host) the node is unselected");
    }

    // ---- the rule: a node that is not selected never actuates -----------------------------------------
    {
        static const ToolMode_t tools[] = { TOOL_SOLDERING_IRON, TOOL_DRILL, TOOL_LASER_ENGRAVER, TOOL_3D_PRINTER, TOOL_ELECTROMAGNET,
                                            TOOL_UV_CURING, TOOL_SPOT_WELDER, TOOL_HOTAIR_REWORK, TOOL_CRIMPING_ACTUATOR,
                                            TOOL_PASTE_JETTING, TOOL_FLYING_PROBE, TOOL_THERMAL_INSPECTION, TOOL_AOI_INSPECTION };
        static const uint32_t actuation[] = { 0x120, 0x130, 0x131, 0x140, 0x160, 0x170, 0x1B0, 0x1C0, 0x1D0, 0x1E0, 0x1F0, 0x200,
                                              0x210, 0x211, 0x240, 0x100, 0x7F0, 0x7FD, 0x7FE, 0x192, 0x1A0, 0x1AC };
        int leaked = 0;
        for (unsigned t = 0; t < sizeof(tools) / sizeof(tools[0]); t++) {
            boot_enabled(7, tools[t]);
            hear_node(MN_ID_NODE_INFO + 8);
            advance_ms(5000);
            for (unsigned i = 0; i < sizeof(actuation) / sizeof(actuation[0]); i++) {
                const uint8_t magic[4] = { 0xB0, 0x07, 0x1D, 0x5A };
                deliver_dlc(actuation[i], magic, 4);
                if (trace_len != 0) leaked++;
            }
            if (fake_reset_count != 0 || sent(0x7F9) || sent(0x111)) leaked++;
        }
        TEST_ASSERT(leaked == 0, "an unselected node of any tool profile lets no actuation, bootloader or configuration frame through");
        // The same frames from a selected node do arrive (the extension filters, it does not break the protocol).
        boot_enabled(7, TOOL_DRILL);
        hear_node(MN_ID_NODE_INFO + 8);
        select_node(7, 1);
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "a selected node obeys the same frame");
        deliver(0x7F0);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "and bootloader entry only reaches the selected node (the magic is still checked by the handler)");
        // The critical-error gate stays in force for the selected node.
        system_error_flag = 1;
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre ") == 0, "a selected node in fault still refuses actuation");
        system_error_flag = 0;
        // Extended identifiers are ignored when the extension is on.
        memset(&fake_rx_queue[0], 0, sizeof fake_rx_queue[0]);
        fake_rx_queue[0].hdr.StdId = 0x140;
        fake_rx_queue[0].hdr.IDE = 4;
        fake_rx_queue_count = 1;
        trace_len = 0; trace[0] = 0;
        HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
        TEST_ASSERT(trace_len == 0, "an extended-identifier frame is ignored when the extension is on");
    }

    // ---- discovery ---------------------------------------------------------------------------------
    {
        boot_enabled(4, TOOL_LASER_ENGRAVER);
        fake_tx_count = 0;
        advance_ms(8);
        const FakeTxFrame *info = last_sent(MN_ID_NODE_INFO + 4);
        TEST_ASSERT(info && info->dlc == 8, "a node announces itself once when it starts (address 4 x 2 ms later)");
        TEST_ASSERT(info && info->data[0] == TOOL_LASER_ENGRAVER && info->data[2] == 3 && info->data[3] == MN_VERSION &&
                    info->data[4] == 0x01 && info->data[5] == 0x02 && info->data[6] == 0x03 && info->data[7] == 0x04,
                    "NODE_INFO carries tool, instance, version and the 32-bit serial (big-endian)");
        fake_tx_count = 0;
        advance_ms(50);
        TEST_ASSERT(sent(MN_ID_NODE_INFO + 4) == 0, "no repeated announcement unprompted");
        const uint8_t disc[2] = { 1, 5 };
        deliver_dlc(MN_ID_DISCOVER, disc, 2);
        fake_tx_count = 0;
        advance_ms(19);
        TEST_ASSERT(sent(MN_ID_NODE_INFO + 4) == 0, "DISCOVER with slot 5 ms: address 4 answers at 20 ms, not before");
        advance_ms(1);
        TEST_ASSERT(sent(MN_ID_NODE_INFO + 4) == 1, "and answers at 20 ms");
        { const uint8_t d[2] = { 1, 0 };  deliver_dlc(MN_ID_DISCOVER, d, 2); advance_ms(200); }
        { const uint8_t d[2] = { 1, 51 }; deliver_dlc(MN_ID_DISCOVER, d, 2); advance_ms(200); }
        { const uint8_t d[2] = { 0, 2 };  deliver_dlc(MN_ID_DISCOVER, d, 2); advance_ms(200); }
        { const uint8_t d[3] = { 1, 2, 0 }; deliver_dlc(MN_ID_DISCOVER, d, 3); advance_ms(200); }
        TEST_ASSERT(sent(MN_ID_NODE_INFO + 4) == 1, "DISCOVER with a slot outside 1..50, version 0 or a wrong length is ignored");
        // the extension's own frames are never passed to the single-head handlers
        deliver_dlc(MN_ID_DISCOVER, disc, 2);
        TEST_ASSERT(trace_len == 0, "extension frames reach no single-head handler");
    }

    // ---- status ------------------------------------------------------------------------------------
    {
        boot_enabled(2, TOOL_SOLDERING_IRON);
        fake_tx_count = 0;
        for (int i = 0; i < 12; i++) advance_ms(500);
        TEST_ASSERT(sent(MN_ID_NODE_STATUS + 2) == 6, "one status frame per second (6 in 6 s)");
        system_error_flag = 1;
        thermal_fault_code = 3;
        fake_tx_count = 0;
        advance_ms(1000);
        const FakeTxFrame *st = last_sent(MN_ID_NODE_STATUS + 2);
        TEST_ASSERT(st && (st->data[0] & MN_STATE_FAULT) && st->data[1] == 3, "a latched fault shows as FAULT with its error code");
        TEST_ASSERT(st && st->data[2] == 0 && st->data[3] == 7, "the uptime in seconds is carried big-endian");
        fake_tx_mailboxes_free = 0;
        fake_tx_count = 0;
        advance_ms(2000);
        TEST_ASSERT(fake_tx_count == 0, "no mailbox free: nothing is sent, nothing is lost for good");
        fake_tx_mailboxes_free = 3;
        advance_ms(1);
        TEST_ASSERT(sent(MN_ID_NODE_STATUS + 2) == 1, "the status goes out as soon as a mailbox is free");
    }
    {
        // the millisecond counter wraps
        reset_world();
        raw_id_pin_value = 1;
        write_config(1, 0);
        fake_tick = 0xFFFFFE00u;
        MultiNode_Init();
        fake_tx_count = 0;
        for (int i = 0; i < 8; i++) advance_ms(500);
        TEST_ASSERT(sent(MN_ID_NODE_STATUS + 1) >= 3, "status keeps its period across the counter wrap");
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "legacy mode is reached across the counter wrap");
    }

    // ---- two boards on one address (conflict) ----------------------------------------------------------
    {
        boot_enabled(6, TOOL_DRILL);
        advance_ms(2000);
        deliver(0x140);
        TEST_ASSERT(strcmp(trace, "pre post bridge drill ") == 0, "baseline: legacy mode, obeying");
        hear_node(MN_ID_NODE_INFO + 6); // somebody else announces our address
        TEST_ASSERT(safe_state_calls == 1, "a conflict puts the tool in its safe state, once");
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "a node in conflict ignores commands");
        select_node(6, 1);
        deliver(0x140);
        TEST_ASSERT(trace_len == 0, "a node in conflict cannot be selected");
        hear_node(MN_ID_NODE_STATUS + 6);
        TEST_ASSERT(safe_state_calls == 1, "the safe state is not re-forced on every frame");
        fake_tx_count = 0;
        advance_ms(1);
        const FakeTxFrame *st = last_sent(MN_ID_NODE_STATUS + 6);
        TEST_ASSERT(st && (st->data[0] & MN_STATE_CONFLICT) && !(st->data[0] & MN_STATE_SELECTED), "the status reports CONFLICT and not SELECTED");
        TEST_ASSERT(MultiNode_SingleHeadTxAllowed() == 0, "a node in conflict sends no single-head telemetry");
        const uint8_t cmd[2] = { 0x01, 0x10 };
        fake_tx_count = 0;
        deliver_dlc(MN_ID_NODE_CMD + 6, cmd, 2);
        TEST_ASSERT(sent(MN_ID_NODE_RESP + 6) == 0, "a node in conflict answers no NODE_CMD");
        for (int i = 0; i < 10; i++) advance_ms(500);
        TEST_ASSERT(sent(MN_ID_NODE_STATUS + 6) >= 4, "it keeps announcing so the host sees the conflict");
    }

    // ---- SAFE_STOP -------------------------------------------------------------------------------------
    {
        boot_enabled(3, TOOL_LASER_ENGRAVER);
        hear_node(MN_ID_NODE_INFO + 1);          // unselected
        const uint8_t stop[2] = { 1, 0 };
        deliver_dlc(MN_ID_SAFE_STOP, stop, 1);
        TEST_ASSERT(safe_state_calls == 1, "an unselected node obeys SAFE_STOP");
        deliver_dlc(MN_ID_SAFE_STOP, stop, 0);
        deliver_dlc(MN_ID_SAFE_STOP, stop, 2);
        TEST_ASSERT(safe_state_calls == 1, "a SAFE_STOP of the wrong length is ignored");
        select_node(3, 1);
        deliver_dlc(MN_ID_SAFE_STOP, stop, 1);
        TEST_ASSERT(safe_state_calls == 2, "and so does the selected node");
        deliver(0x160);
        TEST_ASSERT(strcmp(trace, "pre post bridge laser ") == 0, "SAFE_STOP does not latch: the node leaves the safe state through the normal commands");
        TEST_ASSERT(!system_error_flag, "SAFE_STOP does not raise the critical-error latch");
    }

    // ---- NODE_CMD ----------------------------------------------------------------------------------------
    {
        boot_enabled(5, TOOL_3D_PRINTER);
        hear_node(MN_ID_NODE_INFO + 1);          // unselected: NODE_CMD works without selecting
        const uint8_t cmd[2] = { 0x01, 0x10 };
        fake_tx_count = 0;
        active_tool = TOOL_3D_PRINTER; system_error_flag = 0; thermal_fault_code = 2; last_reset_cause = 1;
        deliver_dlc(MN_ID_NODE_CMD + 5, cmd, 2);
        const FakeTxFrame *r = last_sent(MN_ID_NODE_RESP + 5);
        TEST_ASSERT(r && r->dlc == 8 && r->data[0] == 0x01 && r->data[1] == 0x10 && r->data[2] == TOOL_3D_PRINTER &&
                    r->data[3] == 0 && r->data[6] == 2 && r->data[7] == 1,
                    "NODE_CMD 0x110 is answered by NODE_RESP 0x485 with the six bytes of the single-head status answer");
        TEST_ASSERT(trace_len == 0, "the NODE_CMD is not handed to the single-head handlers");
        fake_tx_count = 0;
        deliver_dlc(MN_ID_NODE_CMD + 4, cmd, 2);
        TEST_ASSERT(fake_tx_count == 0, "a NODE_CMD for another address is ignored");
        const uint8_t act[4] = { 0x01, 0x40, 0x00, 0xFF };
        deliver_dlc(MN_ID_NODE_CMD + 5, act, 4);
        TEST_ASSERT(fake_tx_count == 0 && trace_len == 0, "an actuation func in a NODE_CMD is not accepted: the short form never actuates");
    }

    // ---- selection never needs the dispatcher's tool table ---------------------------------------------------
    {
        boot_enabled(0, TOOL_VACUUM_PICKUP);
        advance_ms(2000);
        deliver(0x145);
        TEST_ASSERT(strcmp(trace, "pre post bridge ") == 0, "address 0 works like any other (no handler for this tool)");
        boot_enabled(31, TOOL_INVALID);
        fake_tx_count = 0;
        advance_ms(2000);
        deliver(0x120);
        TEST_ASSERT(strcmp(trace, "pre post bridge ") == 0, "address 31 (free configuration) is a valid node address");
        advance_ms(1000);
        TEST_ASSERT(sent(MN_ID_NODE_INFO + 31) == 1 && sent(MN_ID_NODE_STATUS + 31) >= 1, "node 31 frames use 0x43F and 0x45F");
    }
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - multinode\n");
    run_multinode_tests(&failures);
    if (failures == 0) {
        printf("  OK   all multinode host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

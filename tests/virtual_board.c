// =============================================================================
// URTC Firmware - Virtual board: the real receive path behind an SLCAN-style text port
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// A host program (never part of the firmware) that wraps the real receive callback, the real
// routing table, the real global command handlers before the error gate, the fault recorder,
// the thermal counters and the optional features, compiled against the fake HAL. It reads one
// SLCAN line per frame on standard input and writes every frame the board would transmit as an
// SLCAN line on standard output, so a host tool or a fuzzer can talk to it like to an adapter:
//
//   tIIILdd...\r   a standard frame (identifier in three hex digits, length, data)  <-> same
//   S6\r O\r C\r   accepted and ignored (bit rate, open, close)                       -> \r
//   V\r            answered with the firmware version as an SLCAN version line
//   X\r            printed by the program when the frame made the board reset into its bootloader
//
// What is real: everything listed above. What is replaced by a recorder that does nothing: the
// per-tool command handlers, the second gate (OLED, expansion SPI, lights) and the multinode
// filter. The program is meant to be built with the address and undefined-behaviour sanitizers,
// so a frame that makes the receive path read or write out of bounds ends the run with a report.
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_can_global.h"
#include "firmware_features.h"
#include "firmware_thermal_stats.h"

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_m);

CAN_TypeDef can_regs;
CAN_HandleTypeDef hcan = { &can_regs };
I2C_HandleTypeDef hi2c2;
TIM_HandleTypeDef htim1, htim2, htim3;
CAN_RxHeaderTypeDef rxHeader;
uint8_t rxData[8];
volatile ToolMode_t active_tool = TOOL_SOLDERING_IRON;
volatile uint8_t system_error_flag, can_bus_error_flag, boot_sequence_active;
volatile uint8_t thermal_fault_code, last_reset_cause;
volatile uint32_t can_led_tick;
volatile uint16_t current_temperature, target_temperature;
volatile uint8_t endstop_triggered;
uint8_t had_valid_recovered_state;
SavedState_t recovered_state;
volatile uint8_t expansion_board_type, free_tool_selection, device_serial_number, mlx_sensor_variant;
uint8_t raw_id_pin_value;

uint8_t MultiNode_FilterRx(void) { return 0; }
uint8_t ExpansionI2C_SlaveWriteRegister(uint8_t reg, const uint8_t *data, uint8_t len) { (void)reg; (void)data; (void)len; return 1; }
void Handle_CAN_GlobalCommands_PostErrorGate(void) { Features_HandleCommand(); Features_HandlePidCommand(); }
void Handle_CAN_SlaveBridge(void) {}
void Handle_CAN_SolderingIron(void) {}
void Handle_CAN_MotionTools(void) {}
void Handle_CAN_Drill(void) {}
void Handle_CAN_AOI(void) {}
void Handle_CAN_Laser(void) {}
void Handle_CAN_3DPrinter(void) {}
void Handle_CAN_Electromagnet(void) {}
void Handle_CAN_UVCuring(void) {}
void Handle_CAN_WeldPulse(void) {}
void Handle_CAN_HotAirRework(void) {}
void Handle_CAN_CrimpingActuator(void) {}
void Handle_CAN_PasteJetting(void) {}
void Handle_CAN_FlyingProbe(void) {}
void Handle_CAN_ThermalInspection(void) {}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parses "tIIILdd..." into id, dlc, data; returns 1 when the line is a well-formed standard frame.
static int parse_frame(const char *line, uint32_t *id, uint32_t *dlc, uint8_t data[8])
{
    if (line[0] != 't') return 0;
    int a = hex_digit(line[1]), b = hex_digit(line[2]), c = hex_digit(line[3]), l = hex_digit(line[4]);
    if (a < 0 || b < 0 || c < 0 || l < 0 || l > 8 || a > 7) return 0;
    *id = (uint32_t)((a << 8) | (b << 4) | c);
    *dlc = (uint32_t)l;
    for (int i = 0; i < l; i++) {
        int hi = hex_digit(line[5 + 2 * i]), lo = hex_digit(line[6 + 2 * i]);
        if (hi < 0 || lo < 0) return 0;
        data[i] = (uint8_t)((hi << 4) | lo);
    }
    return line[5 + 2 * l] == 0;
}

static void print_tx(void)
{
    for (int i = 0; i < fake_tx_count; i++) {
        printf("t%03X%u", (unsigned)fake_tx[i].id, (unsigned)fake_tx[i].dlc);
        for (uint32_t k = 0; k < fake_tx[i].dlc && k < 8; k++) printf("%02X", fake_tx[i].data[k]);
        printf("\r\n");
    }
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int active = 0;
    if (argc > 1) active = atoi(argv[1]);              // tool profile to start with (0..24)
    if (active >= 0 && active <= 24) active_tool = (ToolMode_t)active;
    uint8_t features = 0;
    if (argc > 2) features = (uint8_t)strtoul(argv[2], NULL, 0);
    fake_hal_reset_recording();
    fake_tick = 100000;
    fake_tick_step = 1;
    memset(fake_fram, 0xFF, sizeof fake_fram);
    ThermalStats_LoadFromFram();
    if (features) {
        FeatConfig_t c;
        Feat_ConfigBlank(&c);
        c.mask = features & FEAT_SUPPORTED;
        Feat_ConfigSeal(&c);
        memcpy(&fake_fram[FEAT_CONFIG_FRAM_ADDR], &c, sizeof c);
    }
    Features_Init();

    char line[128];
    while (fgets(line, sizeof line, stdin)) {
        size_t n = strcspn(line, "\r\n");
        line[n] = 0;
        if (n == 0) continue;
        uint32_t id, dlc;
        uint8_t data[8];
        if (parse_frame(line, &id, &dlc, data)) {
            fake_hal_reset_recording();
            memset(&fake_rx_queue[0], 0, sizeof fake_rx_queue[0]);
            fake_rx_queue[0].hdr.StdId = id;
            fake_rx_queue[0].hdr.DLC = dlc;
            memcpy(fake_rx_queue[0].data, data, dlc);
            fake_rx_queue_count = 1;
            fake_reset_jmp_armed = 1;
            if (setjmp(fake_reset_jmp) == 0) {
                HAL_CAN_RxFifo0MsgPendingCallback(&hcan);
            } else {
                printf("X\r\n");
            }
            fake_reset_jmp_armed = 0;
            print_tx();
            fake_tx_count = 0;
            Features_Service();
            print_tx();
        } else if (line[0] == 'V') {
            printf("V%02d%02d\r\n", FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR);
        } else if (line[0] == 'S' || line[0] == 'O' || line[0] == 'C') {
            printf("\r\n");
        } else {
            printf("\a\r\n");   // an SLCAN adapter answers a line it does not understand with a bell
        }
        fflush(stdout);
    }
    return 0;
}

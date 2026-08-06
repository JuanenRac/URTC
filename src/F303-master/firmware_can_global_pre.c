// =============================================================================
// URTC Firmware - Global CAN commands, part 1 (answered regardless of active_tool)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Covers 0x7F0 (bootloader entry), 0x7F8 (version query), 0x110 (active
// tool query), 0x190/191 (F-RAM query), 0x1A0/1A1 (expansion board type) -
// everything positioned BEFORE the first error-gate in the original
// dispatcher, since these are read-only diagnostics or configuration, not
// actuation. See firmware_can_dispatch.c for where this is called from and
// why the error-gate itself has to stay there rather than move in here.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_global.h"

void Handle_CAN_GlobalCommands_PreErrorGate(void) {

        // CAN bootloader entry (0x7F0) - checked first, before even the
        // system_error_flag gate below, since being able to trigger an
        // update is exactly what you'd want if a bug were causing
        // persistent false errors and physical JTAG access isn't
        // convenient. Requires a specific magic payload, not just the ID
        // alone, so a corrupted/malformed frame can't accidentally trigger
        // a reset into update mode.
        if (rxHeader.StdId == 0x7F0 && rxHeader.DLC == 4 &&
            rxData[0] == 0xB0 && rxData[1] == 0x07 && rxData[2] == 0x1D && rxData[3] == 0x5A) {
            // Explicit, inline shutdown of every actuator - not deferred to
            // system_error_flag's usual next-loop-iteration handling, since
            // there won't BE a next iteration once NVIC_SystemReset() runs.
            __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
            HAL_GPIO_WritePin(TMC_ENN_PORT, TMC_ENN_PIN, GPIO_PIN_SET);   // stepper tools: HIGH disables
            HAL_GPIO_WritePin(DRILL_BRAKE_PORT, DRILL_BRAKE_PIN, GPIO_PIN_RESET); // drill: LOW brakes, own pin now
            HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN, GPIO_PIN_RESET); // heater off
            // TMC_ENN_PIN and LASER_SAFETY_PIN are the same physical pin
            // (PB6) - writing both unconditionally would have this second
            // write undo the SET above, leaving a stepper driver
            // re-enabled instead of disabled if that's what's actually
            // attached (the two functions need opposite polarities for
            // "safe", so there's no single value safe for both). The SET
            // above already covers every stepper tool correctly; only the
            // laser case needs to override it, matching
            // MX_GPIO_Post_Init's own handling of this same shared pin.
            if (active_tool == TOOL_LASER_ENGRAVER) {
                HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN, GPIO_PIN_RESET); // laser interlock: LOW locked
            }
            // A plain cycle-counted wait, not HAL_Delay(): HAL_Delay()
            // depends on SysTick advancing the millisecond tick, but
            // SysTick's priority is numerically lower than this CAN ISR's
            // (1), so it can't preempt an already-running ISR - HAL_Delay()
            // here would spin forever waiting for a tick that can't arrive.
            for (volatile uint32_t settle = 0; settle < 320000; settle++); // ~5ms @ 64MHz
            NVIC_SystemReset();
            // never reached
        }

        // Version query (0x7F8) - read-only, no magic payload needed since
        // nothing here can cause harm. Responds via 0x7F9: byte0=0 marks
        // this as the application answering directly (as opposed to the
        // bootloader answering from stored metadata, byte0=1 - see
        // BOOTLOADER.C), then HardwareID (4 bytes) + version major
        // (2 bytes) + version minor (1 byte), all big-endian.
        if (rxHeader.StdId == 0x7F8) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[8];
                txD[0] = 0x00; // answering as the running application
                txD[1] = (uint8_t)(THIS_HARDWARE_ID >> 24);
                txD[2] = (uint8_t)(THIS_HARDWARE_ID >> 16);
                txD[3] = (uint8_t)(THIS_HARDWARE_ID >> 8);
                txD[4] = (uint8_t)(THIS_HARDWARE_ID);
                txD[5] = (uint8_t)(FIRMWARE_VERSION_MAJOR >> 8);
                txD[6] = (uint8_t)(FIRMWARE_VERSION_MAJOR);
                txD[7] = (uint8_t)(FIRMWARE_VERSION_MINOR);
                txH.StdId = 0x7F9;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 8;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Active tool + quick state query (0x110) - read-only, no side
        // effects, same reasoning as 0x7F8 above: a host has no other way
        // to learn which of the 12 tool profiles this board is jumpered
        // for without this, short of already knowing which tool-specific
        // command/telemetry IDs get a response (which itself takes time
        // and doesn't distinguish "no response yet" from "wrong tool").
        // Answered even during a declared error, unlike most commands
        // below - this is exactly the kind of read-only diagnostic that's
        // most useful precisely when something's gone wrong.
        if (rxHeader.StdId == 0x110) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[4];
                txD[0] = (uint8_t)active_tool; // 0-11 = a real tool head, 12+ = no tool assigned to this jumper setting
                txD[1] = system_error_flag ? 0x01 : 0x00;
                txD[2] = can_bus_error_flag ? 0x01 : 0x00;
                txD[3] = boot_sequence_active ? 0x01 : 0x00;
                txH.StdId = 0x111;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 4;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Recovered persisted-state query (0x190/0x191) - read-only, answers
        // with whatever was loaded from the FM24CL64B at boot (see
        // SavedState_Load()). Deliberately does NOT re-apply anything
        // hazardous itself - this just reports it. A compact 8-byte
        // summary rather than the full ~26-byte saved struct, since a
        // single CAN frame can't carry that much - covers what a host
        // most likely needs to decide what to do next; the full record
        // stays in recovered_state in RAM if a future need justifies a
        // multi-frame dump. Positioned here, before the error gate below,
        // to actually match its own documented behavior (CANBUS.TXT) -
        // read-only diagnostics stay available exactly when something's
        // already gone wrong, same reasoning as 0x110 just above.
        if (rxHeader.StdId == 0x190) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[8] = {0};
                txD[0] = had_valid_recovered_state;
                txD[1] = recovered_state.active_tool_at_save;
                txD[2] = recovered_state.had_critical_error;
                uint16_t temp_setpoint = recovered_state.solder_setpoint
                    ? recovered_state.solder_setpoint : recovered_state.printer_nozzle_setpoint;
                txD[3] = (uint8_t)(temp_setpoint >> 8);
                txD[4] = (uint8_t)(temp_setpoint & 0xFF);
                txD[5] = recovered_state.drill_speed
                    ? recovered_state.drill_speed : recovered_state.laser_power;
                txD[6] = recovered_state.drill_direction || recovered_state.laser_interlock;
                txD[7] = recovered_state.layer_fan_power;
                txH.StdId = 0x191;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 8;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query expansion board type (0x1A0/0x1A1) - which of the 7
        // possible configurations (see EXPANSION.TXT) is physically
        // installed on CONN_EXPANSION. There's no electrical way to
        // sense this - the board has to be told, once, and this value
        // then persists across power cycles the same way any other saved
        // setting does (via the normal SavedState_MaybeSave()
        // change-detection on the next main loop pass, not forced
        // immediately here). Positioned here, before the error gate
        // below, for the same reason as 0x190 just above - this is
        // configuration/diagnostic, not an actuation command, so there's
        // no reason to gate it behind a fault clearing.
        if (rxHeader.StdId == 0x1A0 && rxHeader.DLC >= 1 && rxData[0] <= 6) {
            expansion_board_type = rxData[0];
        }
        if (rxHeader.StdId == 0x1A0 || rxHeader.StdId == 0x1A1) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[1] = {expansion_board_type};
                txH.StdId = 0x1A1;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query free tool configuration (0x1A2/0x1A3) - the register
        // Identify_PhysicalTool() consults when the ID-jumper reading is
        // 0x1F/11111b (see that function's own comment, and EEPROM.TXT).
        // 0=no tool selected, 1-12=one of the 12 currently supported tool
        // profiles (stored as id+1 - see the SavedState_t field's own
        // comment for why). Same positioning/persistence reasoning as
        // 0x1A0 just above: configuration, not actuation, so it isn't
        // gated behind the error block below, and persists via the
        // normal SavedState_MaybeSave() path rather than a forced
        // immediate write. Response carries the raw ID-jumper reading
        // (0-31) alongside the current selection, not just the
        // selection alone - lets a reader (the Tester) tell "jumpers
        // read something other than 0x1F" apart from "jumpers read
        // 0x1F but nothing's configured yet", which a bare selection
        // value on its own couldn't distinguish.
        if (rxHeader.StdId == 0x1A2 && rxHeader.DLC >= 1 && rxData[0] <= 25) {
            free_tool_selection = rxData[0];
        }
        if (rxHeader.StdId == 0x1A2 || rxHeader.StdId == 0x1A3) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[2] = {raw_id_pin_value, free_tool_selection};
                txH.StdId = 0x1A3;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 2;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query peripheral type + device serial number (0x1A4/0x1A5) -
        // see EEPROM.TXT section 6. Two very different kinds of value in
        // one response, same reasoning as 0x1A3 pairing the ID-jumper
        // reading with free_tool_selection above: URTC_PERIPHERAL_TYPE is
        // a fixed firmware-identity constant (0x03, never written - 0x1A4
        // only ever updates the serial byte below it, this firmware has
        // no code path that could change its own type at runtime), while
        // device_serial_number is a real F-RAM-backed, host-assigned
        // label purely for telling multiple otherwise-identical URTC
        // boards apart on the same CAN bus.
        if (rxHeader.StdId == 0x1A4 && rxHeader.DLC >= 1) {
            device_serial_number = rxData[0];
        }
        if (rxHeader.StdId == 0x1A4 || rxHeader.StdId == 0x1A5) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[2] = {URTC_PERIPHERAL_TYPE, device_serial_number};
                txH.StdId = 0x1A5;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 2;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Set/query MLX9064x sensor variant (0x1A6/0x1A7) - which of the
        // 3 family members (MLX_VARIANT_90640/90641/90642) is actually
        // populated, on either this board's own Basic+MLX9064x direct
        // connection or an Advanced variant's own slave-chip sensor bus.
        // Same "host has to tell us, no electrical way to sense it"
        // reasoning as expansion_board_type above - positioned here for
        // the same reason. On a write, and only if an expansion slave
        // chip is actually the relevant path (expansion_board_type==3 or
        // 4), the new value is also relayed live to the slave's own
        // REG_MLX_SENSOR_VARIANT over the I2C bridge - this board's own
        // direct MLX90641 support (expansion_board_type==6) just reads
        // mlx_sensor_variant directly, no relay needed for that path.
        if (rxHeader.StdId == 0x1A6 && rxHeader.DLC >= 1 && rxData[0] <= 2) {
            mlx_sensor_variant = rxData[0];
            if (expansion_board_type == 3 || expansion_board_type == 4) {
                ExpansionI2C_SlaveWriteRegister(0x14, &mlx_sensor_variant, 1); // REG_MLX_SENSOR_VARIANT, see slave_common.h
            }
        }
        if (rxHeader.StdId == 0x1A6 || rxHeader.StdId == 0x1A7) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[1] = {mlx_sensor_variant};
                txH.StdId = 0x1A7;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

}

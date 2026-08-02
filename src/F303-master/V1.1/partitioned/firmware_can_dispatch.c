// =============================================================================
// URTC Firmware - Main CAN receive dispatcher
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// The HAL callback fired for every received CAN frame. Structure (exactly
// matching the single-file build): populate rxHeader/rxData, then global
// commands answered regardless of fault state, then the first error-gate
// (blocks everything except 0x100 while system_error_flag is set), then
// global commands that ARE blocked during a fault, then the second
// error-gate (blocks the entire per-tool switch below during a fault),
// then dispatch to whichever of the 12 tool profiles is currently active.
//
// Adding a new tool profile (see the 16 more planned) means: write its own
// firmware_can_<toolname>.c/.h with a Handle_CAN_<ToolName>(void) function,
// then add one case here calling it. Nothing else in this file changes.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_global.h"
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

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan_m) {
    if (HAL_CAN_GetRxMessage(hcan_m, CAN_RX_FIFO0, &rxHeader, rxData) == HAL_OK) {

        can_led_tick = HAL_GetTick();

        Handle_CAN_GlobalCommands_PreErrorGate();

        // Blocks every incoming command except 0x100 (lighting) while a
        // critical error is declared - movement/power commands must not be
        // processed mid-fault, which would defeat the point of raising the
        // flag. 0x100 stays open because it touches nothing
        // actuation-relevant (just LED color/OLED mode), letting the master
        // command a distinct warning pattern on the LEDs to signal the
        // fault externally, rather than the board going dark on the
        // outside the moment it declares itself unsafe.
        if (system_error_flag && rxHeader.StdId != 0x100) {
            return;
        }

        Handle_CAN_GlobalCommands_PostErrorGate();

        // Blocks every incoming command except 0x100 (lighting) while a
        // critical error is active - nothing here is safe to let through
        // otherwise. A master that didn't notice the fault (or was itself
        // the cause of it) could keep driving motors or firing the laser
        // while the OLED reads SYSTEM BLOCKED. Lighting/night-mode above
        // still works since neither is hazardous either way.
        if (system_error_flag) {
            return;
        }

        switch (active_tool) {
            case TOOL_SOLDERING_IRON:
                Handle_CAN_SolderingIron();
                break;
            case TOOL_PASTE_DISPENSER:
            case TOOL_LIQUID_DISPENSER:
            case TOOL_SCREWDRIVER:
            case TOOL_GRIPPER_GIMBAL:
            case TOOL_GRIPPER_NEMA:
            case TOOL_SMT_PICKPLACE:     // doc #1 - rotary A-axis, plain NEMA8 stepper
            case TOOL_VACUUM_GRIPPER_LG: // doc #6 - open/close, plain stepper
                Handle_CAN_MotionTools();
                break;
            case TOOL_DRILL:
                Handle_CAN_Drill();
                break;
            case TOOL_AOI_INSPECTION:
                Handle_CAN_AOI();
                break;
            case TOOL_LASER_ENGRAVER:
                Handle_CAN_Laser();
                break;
            case TOOL_3D_PRINTER:
                Handle_CAN_3DPrinter();
                break;
            case TOOL_ELECTROMAGNET:     // doc #3
                Handle_CAN_Electromagnet();
                break;
            case TOOL_UV_CURING:         // doc #8
                Handle_CAN_UVCuring();
                break;
            case TOOL_SPOT_WELDER:       // doc #4
            case TOOL_ULTRASONIC_WELDER: // doc #15
                Handle_CAN_WeldPulse();
                break;
            case TOOL_HOTAIR_REWORK:     // doc #10
                Handle_CAN_HotAirRework();
                break;
            default:
                break;
        }
    }
}

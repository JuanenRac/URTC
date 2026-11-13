// =============================================================================
// URTC Firmware - CAN command handler: timed weld pulse (Spot Welder
// 0x1C0, Ultrasonic Welder 0x200)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_WELDPULSE_H
#define FIRMWARE_CAN_WELDPULSE_H

void Handle_CAN_WeldPulse(void);

// Called every 20ms from the same cyclic safety loop the thermal
// watchdogs already use (STM32F303CC_main.c) - ends an active pulse
// once its declared duration has elapsed. This is where this
// implementation's default resolution limit lives: with the default build a
// pulse can only end on one of these ~20ms boundaries, not at an arbitrary
// millisecond, since T12_PWM_PIN is plain GPIO. A build with
// URTC_WELD_PULSE_HW_TIMER=1 (see weld_pulse_timing.h) also ends the pulse from a
// hardware timer in one-pulse mode, in its own interrupt; that path is only
// proven on the host so far and its timing is still to be measured on a board.
void WeldPulse_Tick(void);

#endif // FIRMWARE_CAN_WELDPULSE_H

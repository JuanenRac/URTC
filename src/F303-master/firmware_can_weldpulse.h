// =============================================================================
// URTC Firmware - CAN command handler: timed weld pulse (Spot Welder
// 0x1C0, Ultrasonic Welder 0x200)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_CAN_WELDPULSE_H
#define FIRMWARE_CAN_WELDPULSE_H

void Handle_CAN_WeldPulse(void);

// Called every 20ms from the same cyclic safety loop the thermal
// watchdogs already use (STM32F303CC_main.c) - ends an active pulse
// once its declared duration has elapsed. This is where this
// implementation's real resolution limit lives: a pulse can only end on
// one of these ~20ms boundaries, not at an arbitrary millisecond, since
// T12_PWM_PIN is plain GPIO with no hardware timer behind it. Genuine
// single-millisecond precision would need a dedicated hardware timer
// interrupt instead of this polled approach - not implemented here,
// flagged rather than silently claimed.
void WeldPulse_Tick(void);

#endif // FIRMWARE_CAN_WELDPULSE_H

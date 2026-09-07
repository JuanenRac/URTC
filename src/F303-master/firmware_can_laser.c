// =============================================================================
// URTC Firmware - CAN command handler: Laser Engraver (0x160)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_laser.h"

void Handle_CAN_Laser(void) {
                if (rxHeader.StdId == 0x160 && rxHeader.DLC >= 2 && !boot_sequence_active) {
                    laser_power_setpoint = rxData[0];
                    laser_last_kick_tick = HAL_GetTick();

                    // Duty forced to 0 unless the interlock is explicitly
                    // armed (rxData[1]==0x01) - a software floor beneath the
                    // physical interlock pin below, not a replacement for it.
                    // Without this, the PWM itself was generated purely from
                    // the power setpoint, with zero dependency on the
                    // interlock state - a host sending a nonzero power byte
                    // while leaving the interlock at 0x00 (locked) would
                    // still get real PWM out of TIM1, relying entirely on
                    // the external interlock circuit to physically cut the
                    // beam. This way, an interlock left unarmed - or a
                    // stray/malformed frame with rxData[1] anything other
                    // than exactly 0x01 - can't produce laser PWM even if
                    // that external circuit is slow, miswired, or absent.
                    uint32_t laser_duty = (rxData[1] == 0x01)
                        ? (laser_power_setpoint * 3199) / 255 // scaled for TIM1's 20kHz period
                        : 0;
                    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, laser_duty);
                    
                    // rxData[1] is the master's explicit interlock/safety
                    // command, independent of the power setpoint above -
                    // follows CANBUS.TXT literally: 0x01=armed=PB6 HIGH,
                    // 0x00=safe/locked=PB6 LOW.
                    // POLARITY WARNING: this pin is physically shared with
                    // TMC_ENN (active-low ENABLE on the stepper driver, in
                    // other tool modes). If the laser's actual interlock relay
                    // was wired assuming that same active-low-enable sense,
                    // PB6 LOW here would mean "enabled" - the OPPOSITE of what
                    // CANBUS.TXT documents (PB6 LOW = safe/locked). Verify
                    // against the real interlock circuit before relying on
                    // this with the laser powered - getting this backwards
                    // means the laser could fire when you expect it locked.
                    HAL_GPIO_WritePin(TMC_ENN_PORT, LASER_SAFETY_PIN,
                        (rxData[1] == 0x01) ? GPIO_PIN_SET : GPIO_PIN_RESET);
                }

}

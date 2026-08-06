// =============================================================================
// URTC Firmware - CAN command handler: UV Curing Head (0x1D0)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #8 - high-power UV LED driver for instant
// glue/mask curing. TIM1_CH1 (PA8/DRILL_PWM_PORT) is a real hardware PWM
// channel already shared between the drill, laser, and 3D printer's own
// layer fan - only one of those tools (or this one) is ever active at a
// time (selected by the ID jumper), so sharing the channel is safe; same
// 3199-count scaling for TIM1's own 20kHz period already established for
// the laser's own duty calculation.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_uvcuring.h"

void Handle_CAN_UVCuring(void) {
    if (rxHeader.StdId == 0x1D0 && rxHeader.DLC >= 1 && !boot_sequence_active) {
        uint32_t uv_duty = (rxData[0] * 3199) / 255;
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, uv_duty);
    }
}

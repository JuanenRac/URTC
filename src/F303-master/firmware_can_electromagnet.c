// =============================================================================
// URTC Firmware - CAN command handler: Heavy-Duty Electromagnet (0x1B0)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #3 - picks up ferromagnetic parts/scrap.
// CONN_T12 drives this the same way it drives the soldering iron's own
// heater: plain GPIO on/off (T12_PWM_PIN isn't a hardware PWM channel
// despite its name - it's a bang-bang output, modulated in software for
// the soldering iron's own temperature loop; see
// firmware_control_thermal.c). An electromagnet only ever needs full
// on (holding a part) or full off (releasing it), so this stays a
// direct on/off command rather than trying to layer a software-PWM duty
// cycle onto a GPIO this project doesn't otherwise treat as one.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_electromagnet.h"

void Handle_CAN_Electromagnet(void) {
    if (rxHeader.StdId == 0x1B0 && rxHeader.DLC >= 1 && !boot_sequence_active) {
        // Any nonzero byte energizes the coil - not just 0x01 - so a
        // master sending a "power level" byte from old habit (0-255)
        // still gets predictable on/off behavior rather than silently
        // doing nothing for anything other than exactly 0x01.
        HAL_GPIO_WritePin(T12_PWM_PORT, T12_PWM_PIN,
            (rxData[0] != 0x00) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

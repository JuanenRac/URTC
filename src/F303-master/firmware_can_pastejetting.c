// =============================================================================
// URTC Firmware - CAN command handler: Solder Paste Jetting Valve
// (0x230/0x231, thin wrapper over the expansion slave bridge)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// doc #14 - the only one of the 13 new tools
// requiring the ADVANCED expansion board purely for its own local PWM
// (no sensor chip involved) - EXP_TMC_STEP/DIR/EN, in the sense a
// BASIC board would provide, has no PWM capability of its own; the
// slave chip's own TIM1 is what actually generates this. See
// firmware_can_slavebridge.c for the underlying I2C mechanism this
// wraps - this file only exists so a host controlling this specific
// tool doesn't need to also know the raw slave register numbers
// (REG_PWM_CONFIGURE=0x30, REG_PWM_PULSE=0x31 in slave_common.h),
// same convenience every other tool's own dedicated CAN ID already
// provides over needing to speak the underlying bus protocol directly.
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_pastejetting.h"

#define REG_APP_PWM_CONFIGURE 0x30
#define REG_APP_PWM_PULSE     0x31

void Handle_CAN_PasteJetting(void) {
    if (rxHeader.StdId == 0x230 && rxHeader.DLC == 3 && !boot_sequence_active) {
        ExpansionI2C_SlaveWriteRegister(REG_APP_PWM_CONFIGURE, rxData, 3);
    } else if (rxHeader.StdId == 0x231 && rxHeader.DLC == 3 && !boot_sequence_active) {
        ExpansionI2C_SlaveWriteRegister(REG_APP_PWM_PULSE, rxData, 3);
    }
}

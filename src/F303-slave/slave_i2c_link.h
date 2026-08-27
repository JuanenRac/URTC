// =============================================================================
// URTC Expansion Slave Application Firmware - I2C1 link-bus protocol
// (application mode) declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef SLAVE_I2C_LINK_H
#define SLAVE_I2C_LINK_H

void I2CLink_Init(void);

// HAL_GetTick() timestamp of the last I2C1 register transaction of any kind
// from the main board - read by slave_main.c's own main loop to detect a
// dead link (cable unplugged, main board crashed) and stop any
// continuous-mode PWM pulse that would otherwise keep driving its output
// forever with nothing left to command it off.
extern volatile uint32_t i2c_link_last_activity_tick;

#endif // SLAVE_I2C_LINK_H

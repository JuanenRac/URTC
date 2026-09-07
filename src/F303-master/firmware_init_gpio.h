// =============================================================================
// URTC Firmware - GPIO init declarations
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#ifndef FIRMWARE_INIT_GPIO_H
#define FIRMWARE_INIT_GPIO_H

// Configures just the ID-jumper pins (needed before Identify_PhysicalTool
// can run). Called first, before anything else.
void MX_GPIO_Init_Early(void);

// Configures every other GPIO pin, tool-dependent alternate functions,
// and EXTI interrupt priorities - called after Identify_PhysicalTool has
// determined active_tool, since several pins are configured differently
// depending on which tool is active.
void MX_GPIO_Post_Init(void);

#endif // FIRMWARE_INIT_GPIO_H

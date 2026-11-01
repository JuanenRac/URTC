// =============================================================================
// URTC - flash layout of the two boards, written once.
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The address where the application of the master lives is needed by four files that cannot all include each other: the bootloader (to validate an image and to
// jump to it), the application (to point the vector table at itself), the linker script of the application and the one of the bootloader. The two C files take
// the numbers from here; the linker scripts cannot include a header, so tests/test_flash_layout.py holds their ORIGIN and LENGTH values to this file.
#ifndef URTC_FLASH_LAYOUT_H
#define URTC_FLASH_LAYOUT_H

// Master board (STM32F303CC, 256 KB): bootloader, metadata page, application slot and the backup slot an update is received into.
#define URTC_MASTER_METADATA_ADDR    0x08007800UL
#define URTC_MASTER_MAIN_APP_ADDR    0x08008000UL
#define URTC_MASTER_BACKUP_APP_ADDR  0x08024000UL
#define URTC_MASTER_APP_MAX_SIZE     (112UL * 1024UL)

// Expansion slave (STM32F303CB, 128 KB, 2 KB pages).
#define URTC_SLAVE_BOOTLOADER_SIZE   18432UL
#define URTC_SLAVE_METADATA_ADDR     0x08004800UL
#define URTC_SLAVE_MAIN_APP_ADDR     0x08005000UL
#define URTC_SLAVE_BACKUP_APP_ADDR   0x08012800UL
#define URTC_SLAVE_APP_MAX_SIZE      (54UL * 1024UL)

#endif

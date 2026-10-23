// =============================================================================
// URTC - how the bootloader of the master knows that it was entered on purpose.
// Plain C99 with no STM32 HAL dependency (the register is a macro the including
// file resolves), so tests/test_boot_session.c exercises exactly the decision
// the bootloader makes.
//
// The application, when it accepts the bootloader entry (0x7F0 with its magic
// payload), leaves URTC_BOOT_SESSION_MAGIC in a backup register right before the
// reset. The bootloader reads it once at start-up and clears it. Two commands
// are then honoured only in a session that was entered that way:
//   0x7FE  read the installed firmware back over CAN
//   0x7FD  authorise the current update to install an older version
// A frame of either kind that arrives in a session nobody asked for (a power-up,
// a reset of any other cause) is ignored. When no valid application is installed
// there is nothing to protect and no application that could be asked to hand
// over, so both commands stay available for the recovery of such a board.
//
// The restriction is a switch: build the bootloader with
// -DURTC_BOOT_RESTRICT_UNSAFE_COMMANDS=0 to answer both commands in every
// session, as the earlier bootloaders did. The default is on.
// =============================================================================
#ifndef URTC_BOOT_SESSION_H
#define URTC_BOOT_SESSION_H

#include <stdint.h>

#ifndef URTC_BOOT_RESTRICT_UNSAFE_COMMANDS
#define URTC_BOOT_RESTRICT_UNSAFE_COMMANDS 1
#endif

// Left by the application in the first backup register of the RTC block (it
// survives a software reset, and reads 0 after a power-up without a battery).
#define URTC_BOOT_SESSION_MAGIC 0x7F0B5E55u
#define URTC_BOOT_SESSION_REG   (RTC->BKP0R)

// 1 when 0x7FE and 0x7FD are answered in this session.
static inline uint8_t urtc_boot_unsafe_commands_allowed(uint32_t restrict_enabled, uint32_t session_word, uint8_t app_valid)
{
    if (!restrict_enabled) return 1;
    if (!app_valid) return 1;
    return session_word == URTC_BOOT_SESSION_MAGIC ? 1 : 0;
}

#endif // URTC_BOOT_SESSION_H

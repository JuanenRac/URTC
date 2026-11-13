// =============================================================================
// URTC - when a bootloader that is listening goes back to a valid application.
// Plain C99 with no STM32 HAL dependency, so tests/test_boot_return.c exercises exactly the decision the bootloader makes.
//
// After the first listening window the bootloader stays in its loop when an update was requested (or the application was not valid) and it never left: a
// transfer that was aborted (a timeout, a bad signature, a rollback refused) left the head out of service until somebody switched it off and on, although
// the application in the main slot had not been touched and was still valid.
//
// The way back is a switch: build the bootloader with -DURTC_BL_RETURN_TO_APP_S=<seconds> to jump to the application after that many seconds with no frame from
// the bus and no update in progress. The default is 0: the bootloader stays as it always was, and the code of the loop is not even compiled in.
// =============================================================================
#ifndef URTC_BOOT_RETURN_H
#define URTC_BOOT_RETURN_H

#include <stdint.h>

#ifndef URTC_BL_RETURN_TO_APP_S
#define URTC_BL_RETURN_TO_APP_S 0
#endif

// 1 when the bootloader should jump to the application now. `idle_ms` is the time since the last frame of any kind was received; `limit_s` is the switch (0 = off).
static inline uint8_t urtc_boot_return_to_app_due(uint32_t idle_ms, uint32_t limit_s, uint8_t app_valid, uint8_t update_in_progress)
{
    if (limit_s == 0u || !app_valid || update_in_progress) return 0;
    return (idle_ms / 1000u) >= limit_s ? 1 : 0;
}

#endif // URTC_BOOT_RETURN_H

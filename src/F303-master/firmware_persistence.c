// =============================================================================
// URTC Firmware - Saved-state checksum, load, and periodic save
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_persistence.h"
#include "firmware_fram.h"

static uint32_t last_fram_write_tick = 0;

uint8_t SavedState_Checksum(const SavedState_t *s) {
    // CRC-8 (polynomial 0x07, the standard CRC-8/SMBUS variant) over
    // every byte except the checksum field itself. Upgraded from a plain
    // additive checksum - that detected "blank/grossly corrupted" F-RAM
    // fine, but two bytes corrupted in a way that happens to cancel out
    // (one +1, one -1) would pass it undetected. Still not a security
    // boundary the way the OTA update's HMAC-SHA256 is (this data is
    // passive display settings, not anything safety-relevant - see
    // SavedState_Load's own comment on what it does and doesn't act on),
    // just a meaningfully stronger version of the same "was this
    // corrupted" check. Bit-by-bit rather than a 256-entry lookup table:
    // this runs only at F-RAM save/load, never in a timing-critical
    // path, so the table's flash cost isn't worth it for 27 bytes.
    const uint8_t *bytes = (const uint8_t *)s;
    uint8_t crc = 0x00;
    for (size_t i = 0; i < sizeof(SavedState_t) - 1; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

// Called once, early in main()'s boot sequence, after active_tool is known
// but before anything reads recovered_state. Populates recovered_state and
// had_valid_recovered_state - it does NOT apply any of it to a live
// setpoint. Recovering a heater/laser/motor setpoint and quietly
// re-arming it the moment the board boots would mean a power blip (or a
// deliberate power cycle for entirely unrelated reasons) could resume a
// potentially hazardous operation with nobody watching, which defeats the
// point of every communication watchdog elsewhere in this firmware. What
// this DOES provide: recovered_state is readable over CAN (0x190/0x191 -
// see CANBUS.TXT) so a master/diagnostic tool can learn what the board was
// doing before the outage and make an informed decision - including
// deliberately re-sending the same setpoint if that's actually wanted.
// The safe, passive settings (LED colors, OLED mode) ARE applied directly
// here, since there's no hazard in restoring what color an LED was.
void SavedState_Load(void) {
    SavedState_t s;
    had_valid_recovered_state = 0;
    if (!FRAM_ReadBytes(SAVEDSTATE_FRAM_ADDR, (uint8_t *)&s, sizeof(s))) {
        return; // F-RAM not present or not responding - board still boots fine without it
    }
    if (s.magic != SAVEDSTATE_MAGIC || s.struct_version != SAVEDSTATE_VERSION) {
        return; // blank chip, or a layout from a different firmware version - don't trust it
    }
    if (SavedState_Checksum(&s) != s.checksum) {
        return; // corrupted - a partial/interrupted write, most likely
    }
    recovered_state = s;
    had_valid_recovered_state = 1;
    last_saved_state = s; // so the very first change-check in SavedState_MaybeSave() compares against what's genuinely on the chip

    // Passive, hazard-free settings only - restored directly.
    led_state_pixel.R = s.status_r;
    led_state_pixel.G = s.status_g;
    led_state_pixel.B = s.status_b;
    for (int i = 0; i < 8; i++) {
        ring_pixels[i].R = s.ring_r;
        ring_pixels[i].G = s.ring_g;
        ring_pixels[i].B = s.ring_b;
    }
    oled_night_mode = s.oled_night_mode;
    expansion_board_type = s.expansion_board_type; // passive config, not an actuator setpoint - safe to restore directly like the settings above
    free_tool_selection = s.free_tool_selection;    // same reasoning - passive config, not an actuator setpoint
    device_serial_number = s.device_serial_number;  // same reasoning - passive config, not an actuator setpoint
    mlx_sensor_variant = s.mlx_sensor_variant;      // same reasoning - one-time hardware config, not an actuator setpoint
    solder_spool_position = s.solder_spool_position; // real physical state, not a per-tool setpoint - restored directly like the settings above, not gated behind recovered_state
}

// Called every main-loop iteration. Cheap when nothing's changed (just a
// memcmp and a tick check) - the actual write only happens when something
// is both different AND at least FRAM_MIN_SAVE_INTERVAL_MS has passed
// since the last one. Unlike a real EEPROM, this isn't protecting a
// limited write-cycle budget - the FM24CL64B's endurance is rated in the
// trillions of cycles, effectively unlimited for anything this board
// would ever do. The interval is kept anyway for a different, more mundane
// reason: if a setpoint were ever adjusted rapidly (a UI slider being
// dragged, say), there's no reason to write every single intermediate
// value to the bus instead of just the one it settles on.
#define FRAM_MIN_SAVE_INTERVAL_MS 3000
void SavedState_MaybeSave(void) {
    SavedState_t s = {0};
    // Snapshotting every field with interrupts disabled - each individual
    // read below is already atomic on its own (single-byte/word volatile
    // reads), but without this a CAN message arriving mid-copy could
    // still leave a logically inconsistent mix of old/new values spread
    // across different fields (e.g. a new laser_power_setpoint but the
    // interlock pin state from before that same command). Brief enough
    // not to risk any timing-sensitive interrupt (SysTick, CAN RX) - this
    // is a few dozen register/memory reads, not a loop or a blocking call.
    __disable_irq();
    s.magic = SAVEDSTATE_MAGIC;
    s.struct_version = SAVEDSTATE_VERSION;
    s.active_tool_at_save = (uint8_t)active_tool;
    s.solder_setpoint = (active_tool == TOOL_SOLDERING_IRON) ? target_temperature : 0;
    s.printer_nozzle_setpoint = (active_tool == TOOL_3D_PRINTER) ? target_temperature : 0;
    s.drill_speed = (active_tool == TOOL_DRILL) ? drill_speed : 0;
    // Direction and interlock have no dedicated RAM variable - the CAN
    // handlers write these straight to their GPIO pin, so the pin's own
    // current output state (readable even though it's configured as an
    // output - HAL_GPIO_ReadPin reflects what's actually being driven,
    // not just an external voltage) is the only source of truth for
    // "what was it last commanded to" without adding a new variable and
    // touching those existing handlers.
    s.drill_direction = (active_tool == TOOL_DRILL)
        ? (HAL_GPIO_ReadPin(DRILL_FRIN_PORT, DRILL_FRIN_PIN) == GPIO_PIN_SET) : 0;
    s.laser_power = (active_tool == TOOL_LASER_ENGRAVER) ? laser_power_setpoint : 0;
    s.laser_interlock = (active_tool == TOOL_LASER_ENGRAVER)
        ? (HAL_GPIO_ReadPin(TMC_ENN_PORT, LASER_SAFETY_PIN) == GPIO_PIN_SET) : 0;
    s.layer_fan_power = (active_tool == TOOL_3D_PRINTER) ? layer_fan_duty : 0;
    s.hotend_fan_power = (active_tool == TOOL_3D_PRINTER) ? hotend_fan_duty : 0;
    s.status_r = led_state_pixel.R; s.status_g = led_state_pixel.G; s.status_b = led_state_pixel.B;
    s.ring_r = ring_pixels[0].R; s.ring_g = ring_pixels[0].G; s.ring_b = ring_pixels[0].B;
    s.ring_on = (ring_pixels[0].R || ring_pixels[0].G || ring_pixels[0].B) ? 1 : 0;
    s.oled_night_mode = oled_night_mode;
    s.had_critical_error = system_error_flag;
    s.expansion_board_type = expansion_board_type;
    s.free_tool_selection = free_tool_selection;
    s.device_serial_number = device_serial_number;
    s.mlx_sensor_variant = mlx_sensor_variant;
    s.solder_spool_position = solder_spool_position;
    __enable_irq();
    s.checksum = SavedState_Checksum(&s);

    if (memcmp(&s, &last_saved_state, sizeof(s) - 1) == 0) {
        return; // nothing meaningful changed since the last save
    }
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_fram_write_tick) < FRAM_MIN_SAVE_INTERVAL_MS) {
        return; // changed, but rate-limited - will be picked up on a later call once the interval passes
    }
    if (FRAM_WriteBytes(SAVEDSTATE_FRAM_ADDR, (const uint8_t *)&s, sizeof(s))) {
        last_saved_state = s;
        last_fram_write_tick = now;
    }
    // On failure (EEPROM missing/unresponsive), last_saved_state is
    // deliberately left unchanged, so the next call's memcmp still sees a
    // difference and retries - rather than silently giving up after one
    // failed attempt.
}

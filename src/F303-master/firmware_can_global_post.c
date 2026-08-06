// =============================================================================
// URTC Firmware - Global CAN commands, part 2 (blocked during a critical error)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
//
// Covers 0x180/181 (expansion SPI passthrough), 0x182/183 (DIAG0 query),
// 0x192 (F-RAM erase), 0x100 (lighting/OLED mode) - everything between
// the two error-gates in the original dispatcher. Called only when
// system_error_flag is clear (or the ID is specifically 0x100).
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_global.h"
#include "firmware_expansion_spi.h"

void Handle_CAN_GlobalCommands_PostErrorGate(void) {
        // Expansion connector SPI passthrough (0x180/0x181) - generic byte
        // transport for whatever ends up on CONN_EXPANSION's SPI bus (a
        // TMC5160, or any other SPI-configurable chip), rather than this
        // firmware needing to know that chip's specific register protocol.
        // byte[0] = N (1-7, how many of the remaining bytes to clock),
        // byte[1..N] = the bytes to send. Toggles EXP_SPI_CS_PIN low for
        // the whole transfer and back high after, per the TMC5160
        // datasheet's own requirement that CS stay low for the complete
        // transaction. Answers with the same N bytes, replaced with
        // whatever came back on MISO during that same transfer (SPI is
        // inherently full-duplex - every byte sent is also a byte
        // received, even if the caller only cares about one direction for
        // a given transaction).
        if (rxHeader.StdId == 0x180 && rxHeader.DLC >= 2) {
            uint8_t n = rxData[0];
            if (n > 7) n = 7;
            if (n > (rxHeader.DLC - 1)) n = rxHeader.DLC - 1;
            uint8_t response[8];
            response[0] = n;
            HAL_GPIO_WritePin(EXP_SPI_CS_PORT, EXP_SPI_CS_PIN, GPIO_PIN_RESET);
            for (uint8_t i = 0; i < n; i++) {
                response[i + 1] = ExpansionSPI_TransferByte(rxData[i + 1]);
            }
            HAL_GPIO_WritePin(EXP_SPI_CS_PORT, EXP_SPI_CS_PIN, GPIO_PIN_SET);
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                txH.StdId = 0x181;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = n + 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, response, &mb);
            }
        }

        // TMC_DIAG0 level query (0x182/0x183) - simple, read-only,
        // polled rather than interrupt-driven for now. A real hardware
        // EXTI on this pin (matching the scan probe's own pattern on
        // PB3) is a natural future addition once real-world latency
        // requirements are clear - not built speculatively ahead of
        // that. Reads back the raw pin level, no inversion - this line
        // is active-HIGH (see TMC_DIAG0_PIN's own comment for why:
        // diode-OR combining the onboard TMC2209's own DIAG with
        // whatever's on the expansion connector), so txD[0]=1 means a
        // stall/error condition on either driver, txD[0]=0 means both
        // clear.
        if (rxHeader.StdId == 0x182) {
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[1];
                txD[0] = (HAL_GPIO_ReadPin(TMC_DIAG0_PORT, TMC_DIAG0_PIN) == GPIO_PIN_SET) ? 1 : 0;
                txH.StdId = 0x183;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 1;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Erase recovered persisted state (0x192) - a magic 4-byte payload is
        // required, same reasoning as 0x7F0's own magic trigger: a
        // corrupted or malformed frame landing on this ID by chance
        // shouldn't be able to wipe a real saved record. Unlike 0x110/
        // 0x190 above, this is gated by the normal error-block (it's
        // above, so a declared critical error already returned before
        // reaching here) - erasing isn't itself physically hazardous the
        // way a motor/heater/laser command would be, but there's no
        // strong reason to except it either, so it stays consistent with
        // "most things wait until the fault clears".
        if (rxHeader.StdId == 0x192 && rxHeader.DLC == 4 &&
            rxData[0] == 0xE3 && rxData[1] == 0xA5 && rxData[2] == 0xE0 && rxData[3] == 0xFF) {
            uint8_t blank[sizeof(SavedState_t)];
            memset(blank, 0xFF, sizeof(blank)); // an arbitrary but definite pattern (no special hardware meaning for F-RAM the way it has for Flash/EEPROM - chosen purely so the magic/checksum check below is guaranteed to fail on the next boot)
            FRAM_WriteBytes(SAVEDSTATE_FRAM_ADDR, blank, sizeof(blank));
            had_valid_recovered_state = 0;
            memset(&recovered_state, 0, sizeof(recovered_state));
            memset(&last_saved_state, 0xFF, sizeof(last_saved_state)); // forces the next SavedState_MaybeSave() comparison to see a real difference and write a fresh record rather than assuming nothing changed
            if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan) > 0) {
                CAN_TxHeaderTypeDef txH;
                uint8_t txD[8] = {0}; // had_valid_recovered_state=0, everything else zeroed - confirms the erase to whatever's listening for 0x191
                txH.StdId = 0x191;
                txH.IDE = CAN_ID_STD;
                txH.RTR = CAN_RTR_DATA;
                txH.DLC = 8;
                txH.TransmitGlobalTime = DISABLE;
                uint32_t mb;
                HAL_CAN_AddTxMessage(&hcan, &txH, txD, &mb);
            }
        }

        // Global synchronous command 0x100: Lighting and OLED screen.
        // DLC 8 carries both the status LED color (bytes 0-2) and the ring
        // LED color (bytes 4-6, see below) in one frame.
        if (rxHeader.StdId == 0x100 && rxHeader.DLC == 8) {
            led_state_pixel.R = rxData[0];
            led_state_pixel.G = rxData[1];
            led_state_pixel.B = rxData[2];
            // Host explicitly asked for this color - hold it for a while
            // instead of letting the automatic scheme (see
            // Update_StatusLED_AutoColor) silently overwrite it on the very
            // next loop iteration. Every fresh 0x100 command extends this
            // window, so a host that updates the color periodically can
            // keep it held indefinitely; a host that sends it once gets it
            // displayed for a solid 10s before this reverts to automatic.
            led_host_override_active = 1;
            led_host_override_expire_tick = HAL_GetTick() + LED_HOST_OVERRIDE_DURATION_MS;
            oled_night_mode_cmd = rxData[3];
            oled_mode_pending_flag = 1;

            ring_color_r = rxData[4];
            ring_color_g = rxData[5];
            ring_color_b = rxData[6];
            uint8_t ring_on = rxData[7]; // 0x00 = off, 0x01 = on

            // AOI drives the ring itself via 0x150 (off/strobe/continuous) -
            // this command still keeps the stored color current for it (so
            // a color change takes effect immediately in continuous mode),
            // but doesn't directly toggle the ring on/off while AOI owns it.
            if (active_tool != TOOL_AOI_INSPECTION) {
                for (int i = 0; i < 8; i++) {
                    ring_pixels[i].R = ring_on ? ring_color_r : 0;
                    ring_pixels[i].G = ring_on ? ring_color_g : 0;
                    ring_pixels[i].B = ring_on ? ring_color_b : 0;
                }
            } else if (aoi_mode == 0x02) {
                // Atomic capture: reading these three volatiles individually,
                // 8 times each inside the loop below, left a window where an
                // interrupt updating them mid-loop could give different
                // pixels in the same ring a momentary mismatched color.
                // Cosmetic at worst, but free to close.
                __disable_irq();
                uint8_t cr = ring_color_r, cg = ring_color_g, cb = ring_color_b;
                __enable_irq();
                for (int i = 0; i < 8; i++) {
                    ring_pixels[i].R = cr;
                    ring_pixels[i].G = cg;
                    ring_pixels[i].B = cb;
                }
            }
            update_led_ring_flag = 1;
            return;
        }

}

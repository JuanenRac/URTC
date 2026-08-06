// =============================================================================
// URTC Firmware - CAN command handler: AOI Inspection (0x150)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_can_aoi.h"

void Handle_CAN_AOI(void) {
                if (rxHeader.StdId == 0x150 && rxHeader.DLC >= 3) {
                    aoi_mode = rxData[0];
                    // Strobe period, big-endian
                    aoi_strobe_period = (rxData[1] << 8) | rxData[2];
                    if (aoi_mode == 0x01) {
                        // Strobe: handled in the main loop's pulse trigger
                        trigger_aoi_strobe_flag = 1;
                    } else if (aoi_mode == 0x02) {
                        // Continuous: ring solid on at the stored color.
                        // Same atomic-capture reasoning as the 0x150 handler.
                        __disable_irq();
                        uint8_t cr2 = ring_color_r, cg2 = ring_color_g, cb2 = ring_color_b;
                        __enable_irq();
                        for (int i = 0; i < 8; i++) {
                            ring_pixels[i].R = cr2;
                            ring_pixels[i].G = cg2;
                            ring_pixels[i].B = cb2;
                        }
                        update_led_ring_flag = 1;
                    } else {
                        // Off
                        for (int i = 0; i < 8; i++) {
                            ring_pixels[i].R = 0;
                            ring_pixels[i].G = 0;
                            ring_pixels[i].B = 0;
                        }
                        update_led_ring_flag = 1;
                    }
                }

}

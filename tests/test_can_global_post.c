// =============================================================================
// URTC Firmware - Host logic tests for the global commands after the error gate (firmware_can_global_post.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The commands the head still answers once the error gate has closed: the SPI passthrough of the expansion connector (0x180), the
// read of the driver diagnostic line (0x182), the erase of the persisted state (0x192, only with its magic payload) and the LED and
// display override of the host (0x100). The real firmware_can_global_post.c is compiled against the fake HAL; the expansion SPI, the
// multi-head and feature command handlers it calls are replaced by recorders, so what is asserted is what this file decides.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_can_global.h"
#include "firmware_common.h"
#include "test_runner.h"

// ---- recorders for the modules this suite does not link ------------------------------------------------------------------------------
static int spi_bytes, config_calls, features_calls, pid_calls;
static uint8_t spi_sent[8];
static int cs_low_during_transfer;
uint8_t ExpansionSPI_TransferByte(uint8_t tx_byte)
{
    // the chip-select must already be low when the bytes move: the last write to the pin so far was a reset
    int level = -1;
    for (int i = 0; i < fake_gpio_write_count; i++) {
        if (fake_gpio_writes[i].port == EXP_SPI_CS_PORT && fake_gpio_writes[i].pin == EXP_SPI_CS_PIN) {
            level = (int)fake_gpio_writes[i].state;
        }
    }
    cs_low_during_transfer = (level == GPIO_PIN_RESET);
    if (spi_bytes < 8) spi_sent[spi_bytes] = tx_byte;
    spi_bytes++;
    return (uint8_t)(tx_byte ^ 0xFF);   // the "slave" answers with the complement
}
void MX_ExpansionSPI_Init(void) {}
void MultiNode_HandleConfigCommand(void) { config_calls++; }
void Features_HandleCommand(void) { features_calls++; }
void Features_HandlePidCommand(void) { pid_calls++; }

static void world(void)
{
    fake_hal_reset_recording();
    memset(fake_fram, 0x11, sizeof fake_fram);
    fake_tick = 5000;
    fake_tick_step = 0;
    spi_bytes = config_calls = features_calls = pid_calls = 0;
    cs_low_during_transfer = 0;
    active_tool = TOOL_SOLDERING_IRON;
    aoi_mode = 0;
    led_host_override_active = 0;
    led_host_override_expire_tick = 0;
    update_led_ring_flag = 0;
    oled_mode_pending_flag = 0;
    oled_night_mode_cmd = 0;
    memset((void *)&led_state_pixel, 0, sizeof led_state_pixel);
}

static void rx(uint32_t id, uint32_t dlc, const uint8_t *bytes)
{
    memset(&rxHeader, 0, sizeof rxHeader);
    rxHeader.StdId = id;
    rxHeader.DLC = dlc;
    memset(rxData, 0, sizeof rxData);
    if (bytes != NULL) memcpy(rxData, bytes, dlc > 8 ? 8 : dlc);
}

int main(void)
{
    int fails = 0;
    int *failures = &fails;
    printf("URTC host logic tests - the global commands after the error gate\n");

    // ---- 0x180: SPI passthrough --------------------------------------------------------------------------------------------------------
    world();
    const uint8_t spi3[5] = { 0x03, 0xA1, 0xB2, 0xC3, 0xFF };
    rx(0x180, 4, spi3);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(spi_bytes == 3 && spi_sent[0] == 0xA1 && spi_sent[1] == 0xB2 && spi_sent[2] == 0xC3 && cs_low_during_transfer, "0x180: n bytes go out with the chip-select already low");
    TEST_ASSERT(fake_gpio_write_count == 2 && fake_gpio_writes[0].state == GPIO_PIN_RESET && fake_gpio_writes[1].state == GPIO_PIN_SET, "the chip-select is low for the transfer and high after it");
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x181 && fake_tx[0].dlc == 4 && fake_tx[0].data[0] == 3 && fake_tx[0].data[1] == (uint8_t)~0xA1 && fake_tx[0].data[3] == (uint8_t)~0xC3, "0x181 answers n and the bytes read back");

    world();
    const uint8_t spi_big[8] = { 0x20, 1, 2, 3, 4, 5, 6, 7 };
    rx(0x180, 8, spi_big);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(spi_bytes == 7 && fake_tx[0].data[0] == 7 && fake_tx[0].dlc == 8, "n is limited to 7, the most a frame can carry");
    world();
    const uint8_t spi_short[3] = { 0x05, 9, 8 };
    rx(0x180, 3, spi_short);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(spi_bytes == 2 && fake_tx[0].data[0] == 2, "n is limited to the bytes the frame really carries");
    world();
    rx(0x180, 1, spi3);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(spi_bytes == 0 && fake_tx_count == 0 && fake_gpio_write_count == 0, "a frame of one byte transfers nothing and does not touch the chip-select");
    world();
    const uint8_t spi0[2] = { 0x00, 0x55 };
    rx(0x180, 2, spi0);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(spi_bytes == 0 && fake_tx_count == 1 && fake_tx[0].dlc == 1 && fake_tx[0].data[0] == 0, "n = 0 pulses the chip-select and answers an empty result");
    world();
    fake_tx_mailboxes_free = 0;
    rx(0x180, 4, spi3);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(spi_bytes == 3 && fake_tx_count == 0, "the transfer happens even when no mailbox is free for the answer");

    // ---- 0x182: driver diagnostic line -----------------------------------------------------------------------------------------------
    world();
    fake_pin_set(TMC_DIAG0_PORT, TMC_DIAG0_PIN, GPIO_PIN_SET);
    rx(0x182, 0, NULL);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x183 && fake_tx[0].dlc == 1 && fake_tx[0].data[0] == 1, "0x182: the diagnostic line high is answered as 1");
    world();
    fake_pin_set(TMC_DIAG0_PORT, TMC_DIAG0_PIN, GPIO_PIN_RESET);
    rx(0x182, 0, NULL);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].data[0] == 0, "and low as 0");
    world();
    fake_tx_mailboxes_free = 0;
    rx(0x182, 0, NULL);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(fake_tx_count == 0, "no answer when no mailbox is free");

    // ---- 0x192: erase of the persisted state ----------------------------------------------------------------------------------------------
    world();
    had_valid_recovered_state = 1;
    recovered_state.free_tool_selection = 9;
    memset(&last_saved_state, 0, sizeof last_saved_state);
    const uint8_t magic[4] = { 0xE3, 0xA5, 0xE0, 0xFF };
    rx(0x192, 4, magic);
    Handle_CAN_GlobalCommands_PostErrorGate();
    int blanked = 1;
    for (unsigned i = 0; i < sizeof(SavedState_t); i++) blanked = blanked && fake_fram[SAVEDSTATE_FRAM_ADDR + i] == 0xFF;
    TEST_ASSERT(blanked && fake_fram[SAVEDSTATE_FRAM_ADDR + sizeof(SavedState_t)] == 0x11, "0x192 with the magic payload blanks the saved state and nothing after it");
    TEST_ASSERT(had_valid_recovered_state == 0 && recovered_state.free_tool_selection == 0 && last_saved_state.magic == 0xFFFFFFFFu, "the copies in RAM are forgotten and the next save is forced");
    TEST_ASSERT(fake_tx_count == 1 && fake_tx[0].id == 0x191 && fake_tx[0].dlc == 8 && fake_tx[0].data[0] == 0, "0x191 confirms the erase");

    const uint8_t bad_magic[4] = { 0xE3, 0xA5, 0xE0, 0xFE };
    world();
    rx(0x192, 4, bad_magic);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(fake_fram[SAVEDSTATE_FRAM_ADDR] == 0x11 && fake_tx_count == 0, "a wrong magic payload erases nothing");
    world();
    rx(0x192, 3, magic);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(fake_fram[SAVEDSTATE_FRAM_ADDR] == 0x11, "a payload of three bytes erases nothing");
    world();
    fake_tx_mailboxes_free = 0;
    rx(0x192, 4, magic);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(fake_fram[SAVEDSTATE_FRAM_ADDR] == 0xFF && fake_tx_count == 0, "the erase happens even when the confirmation cannot be sent");

    // ---- 0x100: LED and display override --------------------------------------------------------------------------------------------------
    world();
    const uint8_t led[8] = { 10, 20, 30, 0x01, 40, 50, 60, 1 };
    rx(0x100, 8, led);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(led_state_pixel.R == 10 && led_state_pixel.G == 20 && led_state_pixel.B == 30 && led_host_override_active == 1 && led_host_override_expire_tick == 5000 + LED_HOST_OVERRIDE_DURATION_MS, "0x100: the status LED colour and the override window");
    TEST_ASSERT(oled_night_mode_cmd == 1 && oled_mode_pending_flag == 1, "the display mode is passed on");
    int ring_ok = 1;
    for (int i = 0; i < 8; i++) ring_ok = ring_ok && ring_pixels[i].R == 40 && ring_pixels[i].G == 50 && ring_pixels[i].B == 60;
    TEST_ASSERT(ring_ok && update_led_ring_flag == 1 && ring_color_r == 40, "the ring takes the colour when its last byte is on");
    world();
    const uint8_t ring_off[8] = { 0, 0, 0, 0, 40, 50, 60, 0 };
    rx(0x100, 8, ring_off);
    Handle_CAN_GlobalCommands_PostErrorGate();
    ring_ok = 1;
    for (int i = 0; i < 8; i++) ring_ok = ring_ok && ring_pixels[i].R == 0 && ring_pixels[i].G == 0 && ring_pixels[i].B == 0;
    TEST_ASSERT(ring_ok && ring_color_g == 50, "and goes dark when it is off, keeping the colour for later");
    world();
    active_tool = TOOL_AOI_INSPECTION;
    aoi_mode = 0x00;
    for (int i = 0; i < 8; i++) { ring_pixels[i].R = 1; ring_pixels[i].G = 2; ring_pixels[i].B = 3; }
    rx(0x100, 8, led);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(ring_pixels[0].R == 1 && ring_pixels[7].B == 3 && update_led_ring_flag == 1, "with the inspection tool the ring belongs to the tool: the override leaves it alone");
    aoi_mode = 0x02;
    rx(0x100, 8, led);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(ring_pixels[0].R == 40 && ring_pixels[7].B == 60, "unless the tool keeps the ring lit, which takes the new colour");
    world();
    rx(0x100, 7, led);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(led_host_override_active == 0 && update_led_ring_flag == 0 && led_state_pixel.R == 0, "0x100 needs exactly eight bytes");

    // ---- the other handlers hear of every frame ------------------------------------------------------------------------------------------
    world();
    rx(0x7FF, 0, NULL);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(config_calls == 1 && features_calls == 1 && pid_calls == 1, "the multi-head, feature and PID command handlers run for every frame");
    world();
    rx(0x100, 8, led);
    Handle_CAN_GlobalCommands_PostErrorGate();
    TEST_ASSERT(config_calls == 1 && features_calls == 1 && pid_calls == 1, "and before the LED command returns");

    if (fails == 0) {
        printf("  OK   all global command (after the error gate) host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", fails);
    return 1;
}

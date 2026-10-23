// =============================================================================
// URTC Firmware - Host logic tests for the bootloader of the master: start-up, display and main loop (bootloader_main.c, bootloader_oled.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The update protocol itself (bootloader_protocol.c, _flash.c, _crypto.c) has its own suites. This one runs the rest of the bootloader: the hardware set-up,
// the small display (its own driver and its own font, with the SSD1306 modelled from the frames the driver sends) and the main loop with its 600 ms
// listening window, its heartbeat once a second and its watchdog on a stalled update. main() is renamed and the loop is left through the watchdog refresh
// (the fake HAL leaves it with a longjmp after the Nth refresh). JumpToApplication, which never returns, stays out: the flash holds no application here.
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "bootloader_common.h"
#include "bootloader_flash.h"
#include "bootloader_oled.h"
#include "bootloader_protocol.h"
#include "urtc_boot_session.h"
#include "test_runner.h"

#define CAN_RTR_REMOTE_FRAME 2u
#define CAN_ID_EXT_FLAG 4u

int bootloader_main_entry(void);

// ---- the display model ------------------------------------------------------------------------------------------------------------------------
static uint8_t screen[8][128];
static int cur_page, cur_col, frames_used, model_errors;
static uint8_t cmds[64];
static int cmd_count;

static void model_reset(void)
{
    memset(screen, 0, sizeof screen);
    cur_page = cur_col = frames_used = model_errors = cmd_count = 0;
}

static void model_apply(void)
{
    for (; frames_used < fake_i2c_frame_count; frames_used++) {
        const FakeI2cFrame *f = &fake_i2c_frames[frames_used];
        if (f->addr != 0x78 || f->len < 2 || f->len > 129) { model_errors++; continue; }
        if (f->data[0] == 0x40) {
            for (int i = 1; i < f->len; i++) {
                if (cur_page < 8) screen[cur_page][cur_col] = f->data[i];
                cur_col = (cur_col + 1) & 127;                // page addressing mode: the column wraps inside the page
            }
        } else if (f->data[0] == 0x00) {
            for (int i = 1; i < f->len; i++) {
                uint8_t c = f->data[i];
                if (c >= 0xB0 && c <= 0xB7) cur_page = c - 0xB0;
                else if (c < 0x10) cur_col = (cur_col & 0xF0) | c;
                else if (c < 0x20) cur_col = (cur_col & 0x0F) | ((c & 0x0F) << 4);
                if (cmd_count < 64) cmds[cmd_count++] = c;
            }
        } else {
            model_errors++;
        }
    }
}

static int blank_page(int page)
{
    for (int c = 0; c < 128; c++) if (screen[page][c]) return 0;
    return 1;
}

static void world(void)
{
    fake_hal_reset_recording();
    model_reset();
    fake_tick = 100;
    fake_tick_step = 0;
    fake_i2c_device_ready = HAL_OK;
}

static void test_display(int *failures)
{
    world();
    OLED_Init_Boot();
    TEST_ASSERT(fake_gpio_init_count == 1 && fake_gpio_inits[0].init.Mode == GPIO_MODE_AF_OD && fake_gpio_inits[0].init.Pull == GPIO_PULLUP && fake_gpio_inits[0].init.Alternate == GPIO_AF4_I2C2, "the bootloader sets up the display pins itself: open-drain alternate function 4 with a pull-up");
    TEST_ASSERT(fake_i2c_inits == 1 && fake_i2c_init.Timing == 0x0010232A && fake_i2c_init.AddressingMode == I2C_ADDRESSINGMODE_7BIT, "and I2C2 with the timing of the application (100 kHz from the 8 MHz source)");
    model_apply();
    static const uint8_t expected[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    int same = cmd_count == (int)sizeof expected && fake_i2c_probes == 1;
    for (int i = 0; same && i < (int)sizeof expected; i++) if (cmds[i] != expected[i]) same = 0;
    TEST_ASSERT(same && model_errors == 0, "a display that answers the probe gets the same start-up sequence as in the application");
    TEST_ASSERT(fake_tick >= 150, "after a wait for it to power up");

    OLED_ClearAll_Boot();
    model_apply();
    TEST_ASSERT(model_errors == 0 && blank_page(0) && blank_page(7), "a clear sends eight pages of zeros");
    int frames = fake_i2c_frame_count;

    memset(screen, 0xFF, sizeof screen);
    OLED_PrintStr2x_Boot(2, 4, "L");
    model_apply();
    // The text of a line lands at the column asked for (the L asked for at column 4 is at column 4).
    TEST_ASSERT(screen[2][4] == 0xFF && screen[2][5] == 0xFF && screen[3][4] == 0x3F && screen[3][5] == 0x3F, "text is drawn at double size: the tall stroke of the L is two columns wide, 4 pixels up on one page and 3 on the next");
    TEST_ASSERT(screen[2][6] == 0x00 && screen[3][6] == 0x30 && screen[3][7] == 0x30, "and the foot of it is two pixels high, on the lowest rows of the bottom half");
    TEST_ASSERT(screen[2][3] == 0xFF && screen[2][30] == 0 && screen[1][4] == 0xFF, "the line is cleared from the column asked for to the end, the column before it and the page above are untouched");

    OLED_PrintStr2x_Boot(2, 4, "aaaaaaaaaaaaaaaaaaaaaaaa");
    model_apply();
    TEST_ASSERT(model_errors == 0, "a string longer than the line stops at the last column that fits, with no write beyond the buffers");
    OLED_PrintStr2x_Boot(2, 120, "LL");
    model_apply();
    TEST_ASSERT(model_errors == 0 && screen[2][120] == 0xFF && screen[2][127] == 0, "text near the right edge is cut at the edge and does not wrap to the start of the page");
    OLED_PrintStr2x_Boot(0, 0, "@~");
    model_apply();
    TEST_ASSERT(blank_page(0) && blank_page(1), "a character the font does not have is a blank");

    // the progress bar and its number
    OLED_DrawProgressBar_Boot(50);
    model_apply();
    TEST_ASSERT(screen[5][4] == 0x7F && screen[5][4 + 109] == 0x7F, "the bar has a frame at both ends");
    TEST_ASSERT(screen[5][4 + 54] == 0x7F && screen[5][4 + 55] == 0x41, "half of the 108 inner pixels is 54");
    static const uint8_t five[5] = {0x00, 0x27, 0x45, 0x45, 0x39};
    static const uint8_t percent[5] = {0x23, 0x13, 0x08, 0x64, 0x62};
    TEST_ASSERT(memcmp(&screen[3][4], five, 5) == 0 && memcmp(&screen[3][4 + 12], percent, 5) == 0, "the number above the bar is 50% in the small font: '5', then the digit after it, then the percent sign");
    OLED_DrawProgressBar_Boot(0);
    model_apply();
    TEST_ASSERT(screen[5][4 + 1] == 0x41 && screen[5][4] == 0x7F, "zero percent is an empty bar");
    OLED_DrawProgressBar_Boot(100);
    model_apply();
    int all_filled = 1;
    for (int k = 0; k < 110; k++) if (screen[5][4 + k] != 0x7F) all_filled = 0;
    TEST_ASSERT(all_filled, "a hundred percent fills the whole bar, up to the frame");
    OLED_DrawProgressBar_Boot(200);
    model_apply();
    TEST_ASSERT(screen[5][4 + 108] == 0x7F && model_errors == 0, "a percentage over a hundred is a hundred");

    // The small font has a real zero and a one: "10%" is drawn as a one, a zero and the percent sign.
    static const uint8_t zero_as_drawn[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t one_as_drawn[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    OLED_DrawProgressBar_Boot(10);
    model_apply();
    TEST_ASSERT(memcmp(&screen[3][4], one_as_drawn, 5) == 0 && memcmp(&screen[3][4 + 6], zero_as_drawn, 5) == 0, "the small font draws '1' and '0' with their own glyphs");

    // a display that does not answer: nothing is sent to it, so the update is not slowed down by a bus with nobody on it
    world();
    fake_i2c_device_ready = HAL_ERROR;
    OLED_Init_Boot();
    frames = fake_i2c_frame_count;
    OLED_ClearAll_Boot();
    OLED_PrintStr2x_Boot(2, 4, "ERROR");
    OLED_DrawProgressBar_Boot(30);
    TEST_ASSERT(fake_i2c_probes == 1 && fake_i2c_frame_count == frames, "with no display nothing is written to it");
    world();
    OLED_Init_Boot();                                   // leave the driver in the state the other tests expect
}

// ---- the main loop --------------------------------------------------------------------------------------------------------------------------
static void push(uint32_t id, uint32_t dlc, uint32_t rtr, uint32_t ide, const uint8_t *data)
{
    FakeRxFrame *f = &fake_rx_queue[fake_rx_queue_count++];
    memset(f, 0, sizeof *f);
    f->hdr.StdId = id; f->hdr.DLC = dlc; f->hdr.RTR = rtr; f->hdr.IDE = ide;
    if (data) memcpy(f->data, data, 8);
}

static int count_id(uint32_t id)
{
    int n = 0;
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == id) n++;
    return n;
}

static int first_index(uint32_t id)
{
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == id) return i;
    return -1;
}

static int run_boot(int refreshes)
{
    fake_iwdg_exit_after = refreshes;
    fake_reset_jmp_armed = 1;
    int left = setjmp(fake_reset_jmp);
    if (left == 0) bootloader_main_entry();
    fake_reset_jmp_armed = 0;
    return left;
}

static void boot_world(void)
{
    world();
    fake_flash_erase_all();
    fake_rx_queue_count = 0;
    fake_tick = 5000;
    fake_tick_step = 1;                                 // every read of the tick moves it by a millisecond
    update_in_progress = 0; update_failed = 0; update_total_size = 0; update_bytes_received = 0;
}

static void test_main_loop(int *failures)
{
    boot_world();
    int left = run_boot(2500);
    TEST_ASSERT(left == 2 && fake_iwdg_refreshes == 2500, "the bootloader refreshes its watchdog on every pass of both loops");
    TEST_ASSERT(fake_hal_inits == 1 && fake_rcc_calls == 2 && fake_rcc_osc.PLL.PLLMUL == RCC_PLL_MUL16 && fake_rcc_clk.APB1CLKDivider == RCC_HCLK_DIV2, "the HAL is started and the clocks are the ones of the application");
    TEST_ASSERT(fake_iwdg_inits == 1 && fake_iwdg_prescaler == IWDG_PRESCALER_32 && fake_iwdg_reload == 999, "the watchdog is armed for one second");
    double quanta = 1.0 + fake_can_init.TimeSeg1 + fake_can_init.TimeSeg2;
    TEST_ASSERT(32000000.0 / fake_can_init.Prescaler / quanta == 500000.0 && fake_can_init.SyncJumpWidth == CAN_SJW_1TQ && fake_can_init.AutoBusOff == ENABLE && fake_can_init.AutoRetransmission == ENABLE, "CAN runs at 500 kbit/s and recovers from bus-off by itself");
    TEST_ASSERT(fake_can_init.TransmitFifoPriority == DISABLE, "the transmit mailboxes are sent in the order of their identifiers (the application sends them in the order of the requests)");
    TEST_ASSERT(fake_can_filters == 1 && fake_can_filter.FilterMaskIdHigh == 0 && fake_can_filter.FilterMaskIdLow == 0 && fake_can_filter.FilterFIFOAssignment == CAN_FILTER_FIFO0 && fake_can_starts == 1, "one filter that accepts everything into FIFO 0, and the controller is started");
    TEST_ASSERT(fake_i2c_probes == 1, "the display is probed once");
    TEST_ASSERT(count_id(CAN_ID_HEARTBEAT) >= 2 && fake_tx[first_index(CAN_ID_HEARTBEAT)].data[0] == STATUS_LISTENING && fake_tx[first_index(CAN_ID_HEARTBEAT)].data[1] == 0xFF, "a heartbeat every second while it waits: listening, no percentage");
    TEST_ASSERT(count_id(CAN_ID_STATUS) == 1 && fake_tx[first_index(CAN_ID_STATUS)].data[0] == STATUS_LISTENING, "after the listening window with no valid application it announces that it is listening, once");
    TEST_ASSERT(first_index(CAN_ID_HEARTBEAT) < first_index(CAN_ID_STATUS), "the first heartbeat comes before the end of the window");

    boot_world();
    uint8_t zero[8] = {0};
    push(CAN_ID_QUERY_VERSION, 0, 0, 0, zero);
    push(CAN_ID_QUERY_ERROR_COUNTERS, 0, 0, 0, zero);
    push(CAN_ID_QUERY_VERSION, 0, CAN_RTR_REMOTE_FRAME, 0, zero);        // a remote frame is not a command
    push(CAN_ID_QUERY_VERSION, 0, 0, CAN_ID_EXT_FLAG, zero);              // nor is an extended identifier
    push(0x123, 8, 0, 0, zero);                                           // nor is an identifier it does not know
    run_boot(900);
    TEST_ASSERT(count_id(CAN_ID_VERSION_RESPONSE) == 1 && count_id(CAN_ID_BOOTLOADER_VERSION_RESPONSE) == 1, "a version query is answered once, with both frames (the remote and the extended ones are not commands)");
    TEST_ASSERT(count_id(CAN_ID_ERROR_COUNTERS_RESPONSE) == 1, "an error counter query is answered");

    boot_world();
    uint8_t start[8] = {0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCC, 0x01};      // 1000 bytes for THIS_HARDWARE_ID 0x0303CC01
    push(CAN_ID_START_UPDATE, 8, 0, 0, start);
    run_boot(5200);
    TEST_ASSERT(count_id(CAN_ID_STATUS) >= 3, "an update that starts inside the window: erasing, receiving, and then listening again when it stalls");
    int erasing = -1, receiving = -1, last_status = -1;
    for (int i = 0; i < fake_tx_count; i++) {
        if (fake_tx[i].id != CAN_ID_STATUS) continue;
        if (fake_tx[i].data[0] == STATUS_ERASING && erasing < 0) erasing = i;
        if (fake_tx[i].data[0] == STATUS_RECEIVING && receiving < 0) receiving = i;
        last_status = fake_tx[i].data[0];
    }
    TEST_ASSERT(erasing >= 0 && receiving > erasing, "it erases the backup slot, then reports receiving");
    int receiving_heartbeat = 0;
    for (int i = 0; i < fake_tx_count; i++) if (fake_tx[i].id == CAN_ID_HEARTBEAT && fake_tx[i].data[0] == STATUS_RECEIVING && fake_tx[i].data[1] == 0) receiving_heartbeat++;
    TEST_ASSERT(receiving_heartbeat >= 1, "the heartbeat of an update in progress says receiving and the percentage received (0)");
    TEST_ASSERT(update_in_progress == 0 && last_status == STATUS_LISTENING, "an update with no data for ten seconds is abandoned and the bootloader listens again");

    boot_world();
    uint8_t short_start[8] = {0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCC, 0x01};
    push(CAN_ID_START_UPDATE, 4, 0, 0, short_start);                      // a start frame that is not eight bytes long
    run_boot(900);
    TEST_ASSERT(update_in_progress == 0 && count_id(CAN_ID_STATUS) == 1, "a start frame of the wrong length does not start anything");

    boot_world();
    uint8_t wrong_hw[8] = {0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCC, 0x02};
    push(CAN_ID_START_UPDATE, 8, 0, 0, wrong_hw);
    run_boot(900);
    TEST_ASSERT(update_in_progress == 0, "an update for another board is refused");
}

// An application is installed (a plausible stack pointer in the main slot, a reset vector that the jump refuses, so the bootloader falls back to listening).
static void install_application(void)
{
    HAL_FLASH_Unlock();
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, MAIN_APP_ADDR, 0x1000);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, MAIN_APP_ADDR + 2, 0x2000);
    HAL_FLASH_Lock();
}

static void test_unsafe_commands(int *failures)
{
    // the decision itself
    TEST_ASSERT(urtc_boot_unsafe_commands_allowed(1, URTC_BOOT_SESSION_MAGIC, 1) == 1, "a session entered through 0x7F0 answers 0x7FE and 0x7FD");
    TEST_ASSERT(urtc_boot_unsafe_commands_allowed(1, 0, 1) == 0, "a start-up nobody asked for does not, with an application installed");
    TEST_ASSERT(urtc_boot_unsafe_commands_allowed(1, 0xFFFFFFFFu, 1) == 0 && urtc_boot_unsafe_commands_allowed(1, URTC_BOOT_SESSION_MAGIC ^ 1u, 1) == 0, "only the exact mark counts");
    TEST_ASSERT(urtc_boot_unsafe_commands_allowed(1, 0, 0) == 1, "with no valid application they stay available for the recovery of the board");
    TEST_ASSERT(urtc_boot_unsafe_commands_allowed(0, 0, 1) == 1, "with the restriction switched off every session answers them");
    TEST_ASSERT(URTC_BOOT_RESTRICT_UNSAFE_COMMANDS == 1, "the restriction is on by default");

    uint8_t zero[8] = {0};
    uint8_t magic[8] = {0xD0, 0x9E, 0x12, 0xAD, 0, 0, 0, 0};
    uint8_t start[8] = {0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCC, 0x01};

    // a power-up with an application installed: neither command is answered
    boot_world();
    install_application();
    fake_rtc.BKP0R = 0;
    push(CAN_ID_READBACK, 0, 0, 0, zero);
    run_boot(3000);
    TEST_ASSERT(count_id(CAN_ID_READBACK) == 0, "readback (0x7FE) is not answered in a session nobody asked for");
    boot_world();
    install_application();
    fake_rtc.BKP0R = 0;
    push(CAN_ID_START_UPDATE, 8, 0, 0, start);
    push(CAN_ID_AUTHORIZE_DOWNGRADE, 4, 0, 0, magic);
    run_boot(1500);
    TEST_ASSERT(DowngradeIsAuthorized() == 0, "the downgrade authorisation (0x7FD) is ignored in a session nobody asked for");

    // the session that 0x7F0 started: both are answered, and the mark is used up
    boot_world();
    install_application();
    fake_rtc.BKP0R = URTC_BOOT_SESSION_MAGIC;
    push(CAN_ID_READBACK, 0, 0, 0, zero);
    run_boot(3000);
    TEST_ASSERT(count_id(CAN_ID_READBACK) >= 1, "readback is answered in the session that 0x7F0 started");
    TEST_ASSERT(fake_rtc.BKP0R == 0, "the mark is cleared as soon as the bootloader has read it");
    boot_world();
    install_application();
    fake_rtc.BKP0R = URTC_BOOT_SESSION_MAGIC;
    push(CAN_ID_START_UPDATE, 8, 0, 0, start);
    push(CAN_ID_AUTHORIZE_DOWNGRADE, 4, 0, 0, magic);
    run_boot(1500);
    TEST_ASSERT(DowngradeIsAuthorized() == 1, "the downgrade authorisation is honoured in the session that 0x7F0 started");

    // with no application the recovery of the board keeps both
    boot_world();
    fake_rtc.BKP0R = 0;
    push(CAN_ID_READBACK, 0, 0, 0, zero);
    run_boot(3000);
    TEST_ASSERT(count_id(CAN_ID_READBACK) >= 1, "with no application installed readback is answered whatever started the session");
    fake_rtc.BKP0R = 0;
}

void run_bootloader_boot_tests(int *failures)
{
    test_display(failures);
    test_main_loop(failures);
    test_unsafe_commands(failures);
}

#undef main      // the suite renames the main() of bootloader_main.c (-Dmain=bootloader_main_entry); the main of this test is the real one
int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the bootloader of the master: start-up, display and main loop\n");
    void *map = mmap((void *)FLASH_BASE_ADDR, FLASH_SIZE_BYTES, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (map != MAP_FAILED) munmap(map, FLASH_SIZE_BYTES);
    if (map == MAP_FAILED || !fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return 0; }
    run_bootloader_boot_tests(&failures);
    if (failures == 0) {
        printf("  OK   all bootloader start-up host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

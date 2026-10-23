// =============================================================================
// URTC Firmware - Host logic tests for the OLED: driver, font and screens (firmware_oled_driver.c, firmware_font.c, firmware_render.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// The three real sources are compiled against the fake HAL. Every I2C frame the driver sends is applied to a model of the SSD1306 (eight pages of
// 128 columns, a cursor set by the 0xB0 / 0x00 / 0x10 commands and moved by the data), so the tests read the screen back the way a person
// would: as text, cell by cell, with the font of the firmware. The driver is tested for its recovery from a failed transfer and its start-up
// sequence; the renderer for the numbers, the bars, the sparkline, the icons and the screen of every tool.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "firmware_common.h"
#include "firmware_font.h"
#include "firmware_oled_driver.h"
#include "firmware_render.h"
#include "test_runner.h"

uint8_t append_str(char *dest, const char *src);
uint8_t fmt_uint(char *dest, uint32_t value, uint8_t width);

static int mx_i2c2_init_calls;
void MX_I2C2_Init(void) { mx_i2c2_init_calls++; }

// ---- the display model -----------------------------------------------------------------------------------------------------------------
static uint8_t screen[8][128];
static int model_errors, cur_page, cur_col, frames_used;
static uint8_t order_of_cmds[64];
static int cmd_count;

static void model_reset(void)
{
    memset(screen, 0, sizeof screen);
    model_errors = cur_page = cur_col = frames_used = cmd_count = 0;
}

// Applies every frame recorded since the last call.
static void model_apply(void)
{
    for (; frames_used < fake_i2c_frame_count; frames_used++) {
        const FakeI2cFrame *f = &fake_i2c_frames[frames_used];
        if (f->addr != OLED_I2C_ADDR || f->len < 2 || f->len > 129) { model_errors++; continue; }
        if (f->data[0] == 0x40) {
            if (cur_col + (f->len - 1) > 128) model_errors++;
            for (int i = 1; i < f->len; i++) {
                if (cur_page < 8 && cur_col < 128) screen[cur_page][cur_col] = f->data[i];
                cur_col++;
            }
        } else if (f->data[0] == 0x00) {
            for (int i = 1; i < f->len; i++) {
                uint8_t c = f->data[i];
                if (c >= 0xB0 && c <= 0xB7) cur_page = c - 0xB0;
                else if (c < 0x10) cur_col = (cur_col & 0xF0) | c;
                else if (c < 0x20) cur_col = (cur_col & 0x0F) | ((c & 0x0F) << 4);
                if (cmd_count < 64) order_of_cmds[cmd_count++] = c;
            }
        } else {
            model_errors++;
        }
    }
}

// The text on a page: each 6-column cell is matched with the font (letters in upper case).
static void text_at(int page, int col, int cells, char *out)
{
    for (int i = 0; i < cells; i++) {
        char found = '?';
        for (int c = 32; c <= 95; c++) {
            if (memcmp(&screen[page][col + i * 6], &Font5x7[(c - 32) * 5], 5) == 0 && screen[page][col + i * 6 + 5] == 0) { found = (char)c; break; }
        }
        out[i] = found;
    }
    out[cells] = 0;
}

static int page_is_blank(int page)
{
    for (int c = 0; c < 128; c++) if (screen[page][c]) return 0;
    return 1;
}

static void world(void)
{
    fake_hal_reset_recording();
    fake_tick = 50000;
    fake_tick_step = 0;
    model_reset();
    mx_i2c2_init_calls = 0;
    active_tool = TOOL_SOLDERING_IRON;
    animation_frame = 0;
    system_error_flag = 0;
    can_led_tick = 0;
    current_temperature = target_temperature = 0;
    steps_remaining = total_steps_setpoint = 0;
    endstop_triggered = 0;
    aoi_mode = 0;
    laser_power_setpoint = 0;
    drill_speed = 0;
    drill_actual_rpm = 0;
    sensor_digital_reading = 0;
    sensor_analog_reading = 0;
}

static void test_font(int *failures)
{
    static const uint8_t space[5] = {0, 0, 0, 0, 0};
    static const uint8_t zero_slashed[5] = {0x3e, 0x51, 0x49, 0x45, 0x3e};
    static const uint8_t one[5] = {0x00, 0x42, 0x7f, 0x40, 0x00};
    static const uint8_t letter_a[5] = {0x7e, 0x11, 0x11, 0x11, 0x7e};
    static const uint8_t letter_b[5] = {0x7f, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t underscore[5] = {0x40, 0x40, 0x40, 0x40, 0x40};
    TEST_ASSERT(memcmp(&Font5x7[0], space, 5) == 0, "the first glyph (code 32) is the space");
    TEST_ASSERT(memcmp(&Font5x7[('_' - 32) * 5], underscore, 5) == 0, "the last glyph (code 95) is the underscore, a line on the bottom row");
    for (int c = 91; c <= 94; c++) TEST_ASSERT(memcmp(&Font5x7[(c - 32) * 5], space, 5) == 0, "the four characters between Z and the underscore are blank glyphs, as the source says");
    int distinct = 1;
    for (int a = 'A'; a <= 'Z'; a++) for (int b = a + 1; b <= 'Z'; b++) if (memcmp(&Font5x7[(a - 32) * 5], &Font5x7[(b - 32) * 5], 5) == 0) distinct = 0;
    TEST_ASSERT(distinct, "no two capital letters share a glyph");

    // The renderer reads Font5x7[(c - 32) * 5], so every glyph must sit at the place of its own character.
    static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
    TEST_ASSERT(memcmp(&Font5x7[('/' - 32) * 5], slash, 5) == 0, "the glyph looked up for '/' is the slash");
    TEST_ASSERT(memcmp(&Font5x7[('0' - 32) * 5], zero_slashed, 5) == 0 && memcmp(&Font5x7[('1' - 32) * 5], one, 5) == 0, "the glyphs looked up for '0' and '1' are their own");
    TEST_ASSERT(memcmp(&Font5x7[('A' - 32) * 5], letter_a, 5) == 0 && memcmp(&Font5x7[('B' - 32) * 5], letter_b, 5) == 0, "the glyphs looked up for 'A' and 'B' are their own");
    static const uint8_t letter_z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};
    TEST_ASSERT(memcmp(&Font5x7[('Z' - 32) * 5], letter_z, 5) == 0, "the glyph looked up for 'Z' is its own");
    static const uint8_t minus[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t comma[5] = {0x00, 0x50, 0x30, 0x00, 0x00};
    TEST_ASSERT(memcmp(&Font5x7[('-' - 32) * 5], minus, 5) == 0 && memcmp(&Font5x7[(',' - 32) * 5], comma, 5) == 0, "the minus and the comma have glyphs");
}

static void test_driver(int *failures)
{
    world();
    OLED_WriteCmd(0xAF);
    TEST_ASSERT(fake_i2c_frame_count == 1 && fake_i2c_frames[0].addr == OLED_I2C_ADDR && fake_i2c_frames[0].len == 2 && fake_i2c_frames[0].data[0] == 0x00 && fake_i2c_frames[0].data[1] == 0xAF, "a command is the control byte 0 and the command, in one frame");

    uint8_t pixels[3] = {1, 2, 3};
    OLED_WriteData(pixels, 3);
    TEST_ASSERT(fake_i2c_frames[1].len == 4 && fake_i2c_frames[1].data[0] == 0x40 && fake_i2c_frames[1].data[3] == 3, "data is the control byte 0x40 and the pixels in the same frame");
    OLED_WriteData(pixels, 0);
    TEST_ASSERT(fake_i2c_frame_count == 2, "no data, no frame (a bare control byte upsets some controllers)");
    uint8_t big[200];
    memset(big, 0xA5, sizeof big);
    OLED_WriteData(big, 200);
    TEST_ASSERT(fake_i2c_frames[2].len == 129 && fake_i2c_frames[2].data[128] == 0xA5, "more than a page is cut at 128 pixels");

    OLED_SetCursor(3, 0x25);
    const FakeI2cFrame *cursor = &fake_i2c_frames[3];
    TEST_ASSERT(cursor->len == 4 && cursor->data[1] == 0xB3 && cursor->data[2] == 0x05 && cursor->data[3] == 0x12, "the cursor is the page, the low and the high nibble of the column, in one frame");

    // a failed transfer: the bus is reset and the frame sent once more
    fake_i2c_frame_count = 0;
    fake_i2c_fail_transmits = 1;
    OLED_WriteCmd(0xA6);
    TEST_ASSERT(fake_i2c_deinits == 1 && mx_i2c2_init_calls == 1 && fake_i2c_frame_count == 1, "a failed transfer resets the I2C peripheral once and sends the frame again");
    fake_i2c_fail_transmits = 2;
    fake_i2c_deinits = 0;
    OLED_WriteCmd(0xA6);
    TEST_ASSERT(fake_i2c_deinits == 1 && fake_i2c_frame_count == 1, "a second failure is given up, not retried for ever");

    // the early init of the pins and the start-up sequence
    world();
    MX_I2C2_Init_Early();
    TEST_ASSERT(fake_gpio_init_count == 1 && fake_gpio_inits[0].init.Mode == GPIO_MODE_AF_OD && fake_gpio_inits[0].init.Pull == GPIO_PULLUP && fake_gpio_inits[0].init.Alternate == GPIO_AF4_I2C2, "the pins are open-drain alternate function 4 with a pull-up");
    TEST_ASSERT(fake_gpio_inits[0].init.Pin == (OLED_SCL_PIN | OLED_SDA_PIN) && fake_gpio_inits[0].port == OLED_PORT && mx_i2c2_init_calls == 1, "on the two I2C2 pins, and the peripheral is initialised");

    world();
    fake_i2c_device_ready = HAL_ERROR;
    OLED_Init();
    TEST_ASSERT(fake_i2c_probes == 1 && fake_i2c_frame_count == 0, "a display that does not answer the probe gets no start-up sequence");
    OLED_WriteCmd(0xAF);
    uint8_t one[1] = {9};
    OLED_WriteData(one, 1);
    OLED_SetCursor(1, 1);
    TEST_ASSERT(fake_i2c_frame_count == 0, "and nothing is sent to it afterwards, so the bus is not held up by a missing display");

    world();
    fake_i2c_device_ready = HAL_OK;
    OLED_Init();
    model_apply();
    static const uint8_t expected[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    int same = (fake_i2c_frame_count == (int)sizeof expected);
    for (int i = 0; same && i < (int)sizeof expected; i++) if (fake_i2c_frames[i].len != 2 || fake_i2c_frames[i].data[1] != expected[i]) same = 0;
    TEST_ASSERT(same, "the start-up sequence is the documented one: off, clock, 64 rows, charge pump, page mode, flips, contrast, on");
    TEST_ASSERT(fake_tick >= 50050, "the display is given time to power up first");
}

static void test_numbers(int *failures)
{
    char buf[40];
    uint8_t n = append_str(buf, "AB");
    TEST_ASSERT(n == 2 && buf[0] == 'A' && buf[1] == 'B', "append_str copies and counts");
    n = fmt_uint(buf, 0, 3);   buf[n] = 0;
    TEST_ASSERT(n == 3 && strcmp(buf, "  0") == 0, "zero is written as one digit, padded to the width");
    n = fmt_uint(buf, 123, 5); buf[n] = 0;
    TEST_ASSERT(n == 5 && strcmp(buf, "  123") == 0, "a number is right-aligned");
    n = fmt_uint(buf, 12345, 3); buf[n] = 0;
    TEST_ASSERT(n == 5 && strcmp(buf, "12345") == 0, "a number wider than the field is never cut");
    n = fmt_uint(buf, 4294967295u, 3); buf[n] = 0;
    TEST_ASSERT(n == 10 && strcmp(buf, "4294967295") == 0, "the largest value has ten digits");
    n = fmt_uint(buf, 7, 50); buf[n] = 0;
    TEST_ASSERT(n == 21 && buf[20] == '7' && buf[0] == ' ', "a width beyond one line of text is capped at 21");
}

static void test_text_and_bars(int *failures)
{
    char out[32];
    world();
    OLED_PrintStr(2, 10, "Ab_9");
    model_apply();
    text_at(2, 10, 4, out);
    TEST_ASSERT(strcmp(out, "AB_9") == 0, "lower case is drawn with the capital glyphs");
    TEST_ASSERT(screen[2][10 + 5] == 0 && screen[2][10 + 23] == 0, "one empty column after each character");
    TEST_ASSERT(screen[2][9] == 0 && screen[2][34] == 0, "and nothing outside the text");

    world();
    OLED_PrintStr(0, 0, "THIS TEXT IS WAY LONGER THAN A LINE");
    model_apply();
    TEST_ASSERT(model_errors == 0 && screen[0][125] == 0 && screen[0][126] == 0, "text is cut at 21 characters (126 columns)");
    text_at(0, 0, 21, out);
    TEST_ASSERT(strcmp(out, "THIS TEXT IS WAY LONG") == 0, "the first 21 characters are the ones shown");

    world();
    OLED_PrintStr(1, 0, "A~\x7f" "B");
    model_apply();
    TEST_ASSERT(memcmp(&screen[1][6], "\0\0\0\0\0", 5) == 0 && memcmp(&screen[1][12], "\0\0\0\0\0", 5) == 0, "a character outside the font is a blank cell");
    TEST_ASSERT(memcmp(&screen[1][18], &Font5x7[('B' - 32) * 5], 5) == 0, "the next character is still where it belongs");

    world();
    memset(screen, 0xFF, sizeof screen);
    OLED_ClearPage(4);
    model_apply();
    // the model starts from the screen filled with ones to see the clear
    TEST_ASSERT(model_errors == 0 && page_is_blank(4) && screen[3][0] == 0xFF && screen[5][127] == 0xFF, "a page is cleared with one frame and its neighbours are untouched");

    world();
    OLED_DrawHorizontalBar(7, 5, 85, 50);
    model_apply();
    int filled = 0, empty = 0;
    for (int i = 1; i < 84; i++) { if (screen[7][5 + i] == 0x5E) filled++; else if (screen[7][5 + i] == 0x42) empty++; }
    TEST_ASSERT(screen[7][5] == 0x7E && screen[7][5 + 84] == 0x7E, "the bar has a frame at both ends");
    TEST_ASSERT(filled == 42 && empty == 41, "half of 85 pixels is filled (42), the rest is empty");
    TEST_ASSERT(screen[7][4] == 0 && screen[7][5 + 85] == 0, "and the bar stays inside its width");

    world();
    OLED_DrawHorizontalBar(7, 5, 85, 250);
    model_apply();
    TEST_ASSERT(screen[7][5 + 83] == 0x5E, "a percentage over 100 is a full bar");
    world();
    OLED_DrawHorizontalBar(7, 5, 85, 0);
    model_apply();
    TEST_ASSERT(screen[7][5 + 1] == 0x42 && screen[7][5 + 2] == 0x42, "zero percent is an empty bar");
    world();
    OLED_DrawHorizontalBar(7, 100, 85, 100);
    model_apply();
    TEST_ASSERT(model_errors == 0 && screen[7][127] == 0x7E, "a bar that would run off the display is cut at the last column");
    world();
    OLED_DrawHorizontalBar(7, 0, 250, 100);
    model_apply();
    TEST_ASSERT(model_errors == 0 && screen[7][127] == 0x7E, "a width over 128 is cut to 128");
}

static void test_sparkline(int *failures)
{
    world();
    OLED_DrawTempSparkline(4, 5, 0, 100, 0, 450);
    OLED_DrawTempSparkline(4, 5, 85, 100, 450, 450);
    model_apply();
    TEST_ASSERT(page_is_blank(4), "no width, or a range with no height, draws nothing");

    world();
    for (int i = 0; i < 40; i++) { fake_tick += 2000; OLED_DrawTempSparkline(4, 5, 40, 450, 0, 450); }
    model_apply();
    int full = 1;
    for (int i = 0; i < 40; i++) if (screen[4][5 + i] != 0xFF) full = 0;
    TEST_ASSERT(full, "a history all at the top of the range fills every column");

    world();
    for (int i = 0; i < 40; i++) { fake_tick += 2000; OLED_DrawTempSparkline(4, 5, 32, (uint16_t)(i * 11), 0, 450); }
    model_apply();
    int monotonic = 1;
    for (int i = 1; i < 32; i++) if (screen[4][5 + i] < screen[4][5 + i - 1]) monotonic = 0;
    TEST_ASSERT(model_errors == 0 && monotonic && screen[4][5 + 31] > screen[4][5], "a rising temperature is drawn rising, oldest sample on the left (the history wrapped)");

    world();
    OLED_DrawTempSparkline(4, 5, 10, 100, 0, 450);
    OLED_DrawTempSparkline(4, 5, 10, 400, 0, 450);
    fake_tick += 1000;
    OLED_DrawTempSparkline(4, 5, 10, 440, 0, 450);
    model_apply();
    TEST_ASSERT(model_errors == 0 && fake_i2c_frame_count == 6, "a call inside the sample period draws again but keeps the history (two frames per call)");

    world();
    OLED_DrawTempSparkline(4, 100, 85, 0, 0, 450);
    model_apply();
    TEST_ASSERT(model_errors == 0, "a sparkline that would run off the display is cut");
    world();
    fake_tick += 5000;
    OLED_DrawTempSparkline(4, 5, 10, 900, 100, 450);   // above the range: clamped to the top
    OLED_DrawTempSparkline(4, 5, 10, 0, 100, 450);     // below the range: clamped to the bottom
    model_apply();
    TEST_ASSERT(model_errors == 0, "values outside the range are held to its ends");
}

static void test_screens(int *failures)
{
    char out[40];
    world();
    OLED_DrawToolIcon(TOOL_DRILL);
    model_apply();
    TEST_ASSERT(model_errors == 0 && !page_is_blank(5) && !page_is_blank(6) && screen[5][107] == 0 && screen[5][124] == 0, "an icon is two 16-column strips on pages 5 and 6 at column 108");
    uint8_t first[16];
    memcpy(first, &screen[5][108], 16);
    world();
    OLED_DrawToolIcon(200);
    model_apply();
    world();
    OLED_DrawToolIcon(TOOL_SOLDERING_IRON);
    uint8_t soldering[16];
    model_apply();
    memcpy(soldering, &screen[5][108], 16);
    world();
    OLED_DrawToolIcon(200);
    model_apply();
    TEST_ASSERT(memcmp(&screen[5][108], soldering, 16) == 0, "an unknown tool number draws the soldering iron");
    world();
    animation_frame = 0;
    OLED_DrawToolIcon(TOOL_DRILL);
    model_apply();
    uint8_t frame0[16];
    memcpy(frame0, &screen[5][108], 16);
    world();
    animation_frame = 1;
    OLED_DrawToolIcon(TOOL_DRILL);
    model_apply();
    TEST_ASSERT(memcmp(frame0, &screen[5][108], 16) != 0 || memcmp(first, frame0, 16) == 0, "the next animation frame is another picture");

    world();
    Render_SplashScreen(0);
    model_apply();
    TEST_ASSERT(model_errors == 0 && !page_is_blank(0) && !page_is_blank(1) && !page_is_blank(2) && !page_is_blank(6), "the splash screen draws the title and the face");
    TEST_ASSERT(page_is_blank(7), "the last page is left blank");
    world();
    Render_SplashScreen(9);
    model_apply();
    uint8_t face_after_range[128];
    memcpy(face_after_range, screen[3], 128);
    world();
    Render_SplashScreen(0);
    model_apply();
    TEST_ASSERT(memcmp(face_after_range, screen[3], 128) == 0, "a face frame beyond the last one is the first");

    world();
    oled_night_mode_cmd = 0x01; oled_mode_pending_flag = 1;
    Process_OLED_NightModeChange();
    model_apply();
    TEST_ASSERT(oled_mode_pending_flag == 0 && oled_night_mode == 0x01 && cmd_count == 3 && order_of_cmds[0] == 0xAF && order_of_cmds[1] == 0x81 && order_of_cmds[2] == 0x0F, "night mode: display on at the lowest contrast");
    world();
    oled_night_mode_cmd = 0x0F;
    Process_OLED_NightModeChange();
    model_apply();
    TEST_ASSERT(oled_night_mode == 0x0F && cmd_count == 1 && order_of_cmds[0] == 0xAE, "off mode: display off");
    world();
    oled_night_mode_cmd = 0x00;
    Process_OLED_NightModeChange();
    model_apply();
    TEST_ASSERT(cmd_count == 4 && order_of_cmds[0] == 0xAE && order_of_cmds[1] == 0x81 && order_of_cmds[2] == 0xCF && order_of_cmds[3] == 0xAF, "day mode: normal contrast, display on");

    world();
    active_tool = TOOL_INVALID;
    animation_frame = 0;
    Render_ToolScreen();
    model_apply();
    text_at(7, 4, 16, out);
    TEST_ASSERT(model_errors == 0 && strcmp(out, "CHECK ID JUMPERS") == 0 && !page_is_blank(2), "no valid tool: the jumper warning with its picture");
    world();
    active_tool = TOOL_INVALID;
    animation_frame = 1;
    Render_ToolScreen();
    model_apply();
    TEST_ASSERT(page_is_blank(2) && page_is_blank(3), "the picture of the warning blinks with the animation frame");

    world();
    system_error_flag = 1;
    Render_ToolScreen();
    model_apply();
    text_at(2, 10, 18, out);
    TEST_ASSERT(strcmp(out, "  CRITICAL ERROR  ") == 0, "a latched error replaces the tool screen (the brackets are blank glyphs, so only the words show)");
    TEST_ASSERT(page_is_blank(5), "and nothing of the tool is drawn under it");

    world();
    active_tool = TOOL_SOLDERING_IRON;
    current_temperature = 250; target_temperature = 300;
    Render_ToolScreen();
    model_apply();
    text_at(2, 16, 15, out);
    TEST_ASSERT(strcmp(out, "T12 SOLDER IRON") == 0, "the name of the tool on page 2");
    text_at(5, 5, 15, out);
    TEST_ASSERT(strcmp(out, "TEMP: 250/300 C") == 0, "the temperature and its target");
    text_at(1, 0, 11, out);
    TEST_ASSERT(strcmp(out, "TEMP: 250 C") == 0, "the temperature in the header line");
    text_at(0, 10, 13, out);
    TEST_ASSERT(strcmp(out, "HEATING...   ") == 0, "heating while below the target");
    current_temperature = 300;
    world();
    current_temperature = 300; target_temperature = 300;
    Render_ToolScreen();
    model_apply();
    text_at(0, 10, 13, out);
    TEST_ASSERT(strcmp(out, "READY        ") == 0, "ready at the target");
    world();
    current_temperature = 100; target_temperature = 5000;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 15, out);
    TEST_ASSERT(strcmp(out, "TEMP: 100/999 C") == 0, "a target over 999 is shown as 999");

    world();
    active_tool = TOOL_DRILL;
    drill_speed = 40; drill_actual_rpm = 12000; endstop_triggered = 1;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 15, out);
    TEST_ASSERT(strcmp(out, "REAL RPM: 12000") == 0, "the drill shows its measured speed");
    text_at(7, 5, 13, out);
    TEST_ASSERT(strcmp(out, "SETPOINT:  40") == 0, "and the setpoint");
    text_at(6, 5, 12, out);
    TEST_ASSERT(strcmp(out, "ENDSTOP: HIT") == 0, "and the end stop");
    text_at(0, 10, 13, out);
    TEST_ASSERT(strcmp(out, "DRILLING     ") == 0, "the strip says DRILLING while it turns");

    world();
    active_tool = TOOL_PASTE_DISPENSER;
    steps_remaining = 250; total_steps_setpoint = 1000;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 16, out);
    TEST_ASSERT(strcmp(out, "STEPS REM:   250") == 0, "a motion tool shows the steps that are left");
    TEST_ASSERT(screen[7][5 + 60] == 0x5E && screen[7][5 + 70] == 0x42, "and the bar shows the 75 percent done");
    world();
    active_tool = TOOL_SCREWDRIVER;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 13, out);
    TEST_ASSERT(strcmp(out, "STATUS: READY") == 0 && screen[7][5 + 83] == 0x5E, "with nothing to do it is ready, with a full bar");
    world();
    active_tool = TOOL_PASTE_DISPENSER;
    steps_remaining = 10; total_steps_setpoint = 0;
    Render_ToolScreen();
    model_apply();
    TEST_ASSERT(screen[7][5 + 1] == 0x42, "with an unknown total the bar is empty, not a division by zero");
    world();
    active_tool = TOOL_PASTE_DISPENSER;
    steps_remaining = 1000000; total_steps_setpoint = 10;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 16, out);
    TEST_ASSERT(strcmp(out, "STEPS REM: 99999") == 0 && model_errors == 0, "a count over 99999 is shown as 99999");

    world();
    active_tool = TOOL_3D_PRINTER;
    current_temperature = 200; target_temperature = 210; layer_fan_duty = 255; hotend_fan_duty = 128;
    layer_fan_actual_rpm = 4200; hotend_fan_actual_rpm = 3100;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 14, out);
    TEST_ASSERT(strcmp(out, "NOZZLE:200/210") == 0, "the printer shows the nozzle");
    text_at(6, 5, 16, out);
    TEST_ASSERT(strncmp(out, "LFAN:100%4200RPM", 16) == 0, "and the layer fan as a percentage and in revolutions");
    text_at(7, 5, 16, out);
    TEST_ASSERT(strcmp(out, "HFAN: 50%3100RPM") == 0, "and the hotend fan");

    world();
    active_tool = TOOL_LASER_ENGRAVER;
    laser_power_setpoint = 255;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 14, out);
    TEST_ASSERT(strcmp(out, "POWER:    255%") == 0 && screen[7][5 + 83] == 0x5E, "the laser power setpoint, and its bar in percent of the full scale");
    world();
    active_tool = TOOL_VACUUM_PICKUP;
    sensor_analog_reading = 512; sensor_digital_reading = 1;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 15, out);
    TEST_ASSERT(strcmp(out, "VACUUM ADC: 512") == 0, "the vacuum pickup shows its sensor");
    text_at(7, 5, 7, out);
    TEST_ASSERT(strcmp(out, "OBJ: OK") == 0, "and whether it holds something");
    world();
    active_tool = TOOL_AOI_INSPECTION;
    aoi_mode = 2; aoi_strobe_period = 250;
    Render_ToolScreen();
    model_apply();
    text_at(5, 5, 14, out);
    TEST_ASSERT(strcmp(out, "LIGHT: STROBE ") == 0, "the inspection head shows its light mode");
    text_at(7, 5, 12, out);
    TEST_ASSERT(strcmp(out, "PERIOD:  250") == 0, "and the strobe period");
    world();
    active_tool = TOOL_SCAN_PROBE;
    probe_impact_counter = 123456;
    Render_ToolScreen();
    model_apply();
    text_at(7, 5, 15, out);
    TEST_ASSERT(strcmp(out, "IMPACTS:  99999") == 0, "the scan probe shows the hits, capped at 99999");
    world();
    active_tool = TOOL_UV_CURING;
    Render_ToolScreen();
    model_apply();
    text_at(5, 10, 14, out);
    TEST_ASSERT(strcmp(out, "BUS CAN ONLINE") == 0, "a tool without a display of its own says the bus is online");
}

static void test_every_tool_draws_inside_the_display(int *failures)
{
    int bad = 0, blank_name = 0;
    for (int tool = 0; tool <= TOOL_INVALID; tool++) {
        for (int frame = 0; frame < 2; frame++) {
            for (int error = 0; error < 2; error++) {
                world();
                active_tool = (ToolMode_t)tool;
                animation_frame = (uint8_t)frame;
                system_error_flag = (uint8_t)error;
                current_temperature = 420; target_temperature = 450;
                steps_remaining = 77; total_steps_setpoint = 100;
                drill_speed = 90; drill_actual_rpm = 99999; layer_fan_duty = 200; hotend_fan_duty = 90;
                laser_power_setpoint = 77; aoi_mode = 1; sensor_digital_reading = 1;
                Render_ToolScreen();
                model_apply();
                if (model_errors) bad++;
                if (tool != TOOL_INVALID && !error && page_is_blank(2)) blank_name++;
            }
        }
    }
    TEST_ASSERT(bad == 0, "no tool screen sends a frame outside the display or longer than a page");
    TEST_ASSERT(blank_name == 0, "every tool screen has a name on page 2");
}

void run_oled_render_tests(int *failures)
{
    test_font(failures);
    test_driver(failures);
    test_numbers(failures);
    test_text_and_bars(failures);
    test_sparkline(failures);
    test_screens(failures);
    test_every_tool_draws_inside_the_display(failures);
}

int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the OLED: driver, font and screens\n");
    run_oled_render_tests(&failures);
    if (failures == 0) {
        printf("  OK   all OLED host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

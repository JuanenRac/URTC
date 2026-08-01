// =============================================================================
// URTC Bootloader - OLED status display (SSD1306/SSD1315, I2C1)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "stm32f3xx_hal.h"
#include "bootloader_common.h"
#include "bootloader_oled.h"

#define OLED_SCL_PIN    GPIO_PIN_9
#define OLED_SDA_PIN    GPIO_PIN_10
#define OLED_PORT       GPIOA

#define OLED_I2C_ADDR   0x78

// Compact 27-character font (space, %, ., 0-9, and just the letters this
// bootloader's own status messages need) - most extracted from the
// application's Font5x7 table, 'P' and 'G' hand-designed and verified by
// rendering instead: Font5x7 has a known character-extraction-offset bug
// (documented where it's defined in the application source), and pulling
// 'G' from it directly returned bytes identical to this font's own
// already-verified 'H' - a live example of exactly that bug, not
// something to build on for two brand new letters.
static const uint8_t BootFont[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 0: space
    {0x23,0x13,0x08,0x64,0x62}, // 1: '%'
    {0x00,0x60,0x60,0x00,0x00}, // 2: '.'
    {0x00,0x42,0x7F,0x40,0x00}, // 3: '0'
    {0x00,0x44,0x7E,0x40,0x00}, // 4: '1'
    {0x00,0x62,0x51,0x49,0x46}, // 5: '2'
    {0x00,0x22,0x49,0x49,0x36}, // 6: '3'
    {0x18,0x14,0x12,0x7F,0x10}, // 7: '4'
    {0x00,0x27,0x45,0x45,0x39}, // 8: '5'
    {0x00,0x3E,0x49,0x49,0x32}, // 9: '6'
    {0x00,0x61,0x11,0x09,0x07}, // 10: '7'
    {0x00,0x36,0x49,0x49,0x36}, // 11: '8'
    {0x00,0x26,0x49,0x49,0x3E}, // 12: '9'
    {0x7E,0x11,0x11,0x11,0x7E}, // 13: 'A'
    {0x7F,0x41,0x41,0x22,0x1C}, // 14: 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 15: 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 16: 'F'
    {0x3E,0x41,0x41,0x49,0x3A}, // 17: 'G' (hand-designed, see comment above)
    {0x7F,0x08,0x08,0x08,0x7F}, // 18: 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 19: 'I'
    {0x7F,0x08,0x14,0x22,0x41}, // 20: 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 21: 'L'
    {0x7F,0x04,0x08,0x10,0x7F}, // 22: 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 23: 'O'
    {0x7F,0x09,0x09,0x0F,0x00}, // 24: 'P' (hand-designed, see comment above)
    {0x7F,0x09,0x19,0x29,0x46}, // 25: 'R'
    {0x00,0x26,0x49,0x49,0x32}, // 26: 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 27: 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 28: 'U'
};

static uint8_t BootFont_Lookup(char c) {
    if (c >= 'a' && c <= 'z') c -= 32; // not reachable today (every current
    // status message is already uppercase), but matches the application
    // firmware's own font lookup convention and costs nothing.
    switch (c) {
        case ' ': return 0;  case '%': return 1;  case '.': return 2;
        case '0': return 3;  case '1': return 4;  case '2': return 5;
        case '3': return 6;  case '4': return 7;  case '5': return 8;
        case '6': return 9;  case '7': return 10; case '8': return 11;
        case '9': return 12; case 'A': return 13; case 'D': return 14;
        case 'E': return 15; case 'F': return 16; case 'G': return 17;
        case 'H': return 18; case 'I': return 19; case 'K': return 20;
        case 'L': return 21; case 'N': return 22; case 'O': return 23;
        case 'P': return 24; case 'R': return 25; case 'S': return 26;
        case 'T': return 27; case 'U': return 28;
        default: return 0; // unsupported char -> blank space
    }
}

static void MX_I2C2_Init_Boot(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF4_I2C2; // PA9/PA10 with AF4 is I2C2, not I2C1 -
    // confirmed against DS9118 Table 14. This bootloader was making the
    // exact same mistake already found and fixed in the application
    // firmware's own OLED init.
    HAL_GPIO_Init(OLED_PORT, &gpio);

    __HAL_RCC_I2C1_CLK_ENABLE();
    hi2c2.Instance = I2C2;
    hi2c2.Init.Timing = 0x0010232A; // ~100kHz, same derivation as the application's
    hi2c2.Init.OwnAddress1 = 0;
    hi2c2.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c2.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c2.Init.OwnAddress2 = 0;
    hi2c2.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c2.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c2);
}

// Set once by OLED_Init_Boot(); every OLED write function checks this
// first and returns immediately if the display never responded, rather
// than paying a real I2C timeout on every single command. A bootloader's
// actual job is the CAN update itself - the display is a convenience,
// and a missing/disconnected one should never be able to affect update
// timing or, worse, chain into an IWDG reset loop before CAN is ever
// reached. The 50ms->5ms timeout reduction below is a second, independent
// layer for the case this flag can't catch: a display that responded
// fine at init but fails mid-session (a damaged cable, a connector
// working loose) - even every single write stacking a full timeout in
// that scenario stays well inside the IWDG's own budget.
static uint8_t oled_present_boot = 0;

static void OLED_WriteCmd_Boot(uint8_t cmd) {
    if (!oled_present_boot) return;
    uint8_t frame[2] = {0x00, cmd};
    HAL_I2C_Master_Transmit(&hi2c2, OLED_I2C_ADDR, frame, 2, 20);
}

static void OLED_WriteData_Boot(uint8_t *buf, uint16_t len) {
    if (!oled_present_boot) return;
    if (len > 128) len = 128;
    uint8_t frame[129];
    frame[0] = 0x40;
    for (uint16_t i = 0; i < len; i++) frame[i+1] = buf[i];
    HAL_I2C_Master_Transmit(&hi2c2, OLED_I2C_ADDR, frame, len + 1, 20);
}

static void OLED_SetCursor_Boot(uint8_t page, uint8_t col) {
    OLED_WriteCmd_Boot(0xB0 + page);
    OLED_WriteCmd_Boot(0x00 + (col & 0x0F));
    OLED_WriteCmd_Boot(0x10 + (col >> 4));
}

void OLED_Init_Boot(void) {
    MX_I2C2_Init_Boot();
    HAL_Delay(50);
    // A single, cheap probe decides whether this session touches the
    // display at all - one bounded transaction instead of every
    // subsequent command individually risking a timeout against a
    // display that was never going to answer.
    oled_present_boot = (HAL_I2C_IsDeviceReady(&hi2c2, OLED_I2C_ADDR, 2, 20) == HAL_OK) ? 1 : 0;
    if (!oled_present_boot) return;
    // Same sequence as the application's OLED_Init, verified compatible
    // with both SSD1306 and SSD1315 - see that function's own comment.
    OLED_WriteCmd_Boot(0xAE);
    OLED_WriteCmd_Boot(0xD5); OLED_WriteCmd_Boot(0x80);
    OLED_WriteCmd_Boot(0xA8); OLED_WriteCmd_Boot(0x3F);
    OLED_WriteCmd_Boot(0xD3); OLED_WriteCmd_Boot(0x00);
    OLED_WriteCmd_Boot(0x40);
    OLED_WriteCmd_Boot(0x8D); OLED_WriteCmd_Boot(0x14);
    OLED_WriteCmd_Boot(0x20); OLED_WriteCmd_Boot(0x02);
    OLED_WriteCmd_Boot(0xA1);
    OLED_WriteCmd_Boot(0xC8);
    OLED_WriteCmd_Boot(0xDA); OLED_WriteCmd_Boot(0x12);
    OLED_WriteCmd_Boot(0x81); OLED_WriteCmd_Boot(0xCF);
    OLED_WriteCmd_Boot(0xD9); OLED_WriteCmd_Boot(0xF1);
    OLED_WriteCmd_Boot(0xDB); OLED_WriteCmd_Boot(0x40);
    OLED_WriteCmd_Boot(0xA4); OLED_WriteCmd_Boot(0xA6);
    OLED_WriteCmd_Boot(0xAF);
}

void OLED_ClearAll_Boot(void) {
    uint8_t blank[128] = {0};
    for (uint8_t p = 0; p < 8; p++) {
        OLED_SetCursor_Boot(p, 0);
        OLED_WriteData_Boot(blank, 128);
    }
}

// Prints text scaled 2x (using BootFont at native size doubled both ways)
// so status messages are legible - this display only ever shows a handful
// of short words during an update, not dense telemetry, so legibility at
// a glance matters more than fitting lots of text.
void OLED_PrintStr2x_Boot(uint8_t page, uint8_t col, const char *str) {
    uint8_t buf_top[128] = {0};
    uint8_t buf_bot[128] = {0};
    // Entering the loop with c > 118 would write buf_top[128]/buf_bot[128]
    // partway through the character (each of the 5 sub-columns advances c
    // by 2, so a full character needs c+9 to stay within the last valid
    // index, 127) - one byte past these 128-byte arrays.
    uint16_t c = col;
    while (*str && c <= 118) {
        uint8_t idx = BootFont_Lookup(*str++);
        for (uint8_t gc = 0; gc < 5; gc++) {
            uint8_t byte = BootFont[idx][gc];
            uint8_t top = 0, bot = 0;
            for (uint8_t bit = 0; bit < 4; bit++) {
                if (byte & (1 << bit)) top |= (0x03 << (bit*2));
            }
            for (uint8_t bit = 4; bit < 7; bit++) {
                if (byte & (1 << bit)) bot |= (0x03 << ((bit-4)*2));
            }
            buf_top[c] = top; buf_top[c+1] = top;
            buf_bot[c] = bot; buf_bot[c+1] = bot;
            c += 2;
        }
        c += 1; // 1px inter-character spacing - without this, each 10px
                 // character ran directly into the next with no gap,
                 // rendering status text as a hard-to-read solid block
    }
    OLED_SetCursor_Boot(page, col);
    OLED_WriteData_Boot(buf_top, 128);
    OLED_SetCursor_Boot(page + 1, col);
    OLED_WriteData_Boot(buf_bot, 128);
}

// Progress bar: outlined box with a filled portion proportional to percent,
// plus the percentage as text alongside it.
void OLED_DrawProgressBar_Boot(uint8_t percent) {
    if (percent > 100) percent = 100;
    uint8_t bar[110];
    uint8_t filled = (uint8_t)((uint16_t)percent * 106 / 100);
    for (uint8_t i = 0; i < 110; i++) {
        if (i == 0 || i == 109) bar[i] = 0x7F;
        else if (i <= filled) bar[i] = 0x7F;
        else bar[i] = 0x41;
    }
    OLED_SetCursor_Boot(5, 4);
    OLED_WriteData_Boot(bar, 110);
    // Percentage text, small (1x), to the right of the bar - reuses the
    // same font table without the 2x scaling used for word messages.
    char txt[5];
    uint8_t tp = 0;
    if (percent >= 100) { txt[tp++] = '1'; txt[tp++] = '0'; txt[tp++] = '0'; }
    else if (percent >= 10) { txt[tp++] = '0' + (percent/10); txt[tp++] = '0' + (percent%10); }
    else { txt[tp++] = '0' + percent; }
    txt[tp++] = '%';
    txt[tp] = 0;
    uint8_t small_buf[30] = {0};
    uint8_t sc = 0;
    for (uint8_t i = 0; txt[i]; i++) {
        uint8_t idx = BootFont_Lookup(txt[i]);
        for (uint8_t gc = 0; gc < 5; gc++) small_buf[sc++] = BootFont[idx][gc];
        sc++;
    }
    OLED_SetCursor_Boot(3, 4);
    OLED_WriteData_Boot(small_buf, sc);
}

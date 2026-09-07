// =============================================================================
// URTC Firmware - OLED low-level I2C driver (SSD1306/SSD1315)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0 - see LICENSE
// =============================================================================
#include "firmware_common.h"
#include "firmware_oled_driver.h"

static uint8_t oled_present = 1;

// Wraps every I2C2 transmit on this bus (OLED + F-RAM both use it) with
// error detection: if a transmission times out or errors, hi2c2 can be
// left in HAL_I2C_STATE_BUSY/ERROR with nothing else in this firmware to
// detect or recover it, silently breaking every subsequent transaction
// on this bus (both the display AND the F-RAM, since they share it) for
// the rest of runtime. One HAL_I2C_DeInit()+MX_I2C2_Init() re-init
// attempt on failure is cheap insurance against a transient glitch
// (electrical noise, a momentary bus contention) leaving the peripheral
// stuck rather than genuinely disconnected hardware, which would just
// fail again next time and get caught by oled_present's own probe.
static uint8_t I2C2_TransmitWithRecovery(uint16_t addr, uint8_t *data, uint16_t len) {
    if (HAL_I2C_Master_Transmit(&hi2c2, addr, data, len, 50) == HAL_OK) return 1;
    HAL_I2C_DeInit(&hi2c2);
    MX_I2C2_Init();
    return (HAL_I2C_Master_Transmit(&hi2c2, addr, data, len, 50) == HAL_OK) ? 1 : 0;
}

void OLED_WriteCmd(uint8_t cmd) {
    if (!oled_present) return;
    uint8_t frame[2] = {0x00, cmd}; // control byte 0x00 = command stream
    I2C2_TransmitWithRecovery(OLED_I2C_ADDR, frame, 2);
}

void OLED_WriteData(uint8_t *buf, uint16_t len) {
    if (!oled_present) return;
    if (len == 0) return; // a control-byte-only transaction with no data
                           // bytes following desyncs some SSD1306 clone
                           // controllers - nothing to actually write anyway.
    // Control byte and pixel data must go out in a single I2C transaction -
    // per the SSD1306 protocol, every transaction needs its own control
    // byte, so sending them as two separate transactions would make the
    // display read the data's first byte as a malformed control byte.
    if (len > 128) len = 128; // defensive clamp - every real call site today
                              // is verified within this bound, but this
                              // costs nothing and protects against a future
                              // caller passing an oversized len.
    uint8_t frame[129];
    frame[0] = 0x40;
    memcpy(&frame[1], buf, len);
    I2C2_TransmitWithRecovery(OLED_I2C_ADDR, frame, len + 1);
}

void OLED_SetCursor(uint8_t page, uint8_t col) {
    if (!oled_present) return;
    // Combined into one transaction rather than 3 separate OLED_WriteCmd
    // calls - the SSD1306 protocol allows any number of command bytes to
    // follow a single 0x00 control byte, so there's no need to repeat the
    // START/address/control/STOP overhead for each of the 3 commands here.
    uint8_t frame[4] = {0x00, (uint8_t)(0xB0 + page), (uint8_t)(0x00 + (col & 0x0F)), (uint8_t)(0x10 + ((col >> 4) & 0x0F))};
    I2C2_TransmitWithRecovery(OLED_I2C_ADDR, frame, 4);
}

// Split out from OLED_Init() below - just the I2C2 peripheral+pin setup,
// with none of the OLED-specific probing/commands that follow it there.
// Called early in main(), before Identify_PhysicalTool(), specifically so
// that function can read the F-RAM directly when the ID-jumper reading is
// 0x1F (11111b, "free configuration" - see EEPROM.TXT) - the F-RAM shares
// this same I2C2 bus with the OLED (see FRAM_I2C_ADDR's own comment), and
// without this split, I2C2 wouldn't be usable until OLED_Init() runs,
// which happens much later in boot (after MX_GPIO_Post_Init() has already
// configured tool-dependent pins/timers/ADC based on whatever
// active_tool was at that point). OLED_Init() below also calls this
// function rather than keeping its own copy of this setup, same as
// everything else that needs I2C2 up before using it.
void MX_I2C2_Init_Early(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin = OLED_SCL_PIN | OLED_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD; // Open-drain: I2C is wired-AND, matches R33/R34's external pull-ups
    GPIO_InitStruct.Pull = GPIO_PULLUP;     // Belt-and-suspenders alongside R33/R34
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C2;
    HAL_GPIO_Init(OLED_PORT, &GPIO_InitStruct);

    MX_I2C2_Init();
}

void OLED_Init(void) {
    MX_I2C2_Init_Early();
    
    HAL_Delay(50);
    // A single, cheap probe decides whether this session touches the
    // display at all - one bounded transaction instead of every
    // subsequent command individually risking a timeout against a
    // display that was never going to answer (never populated, a
    // damaged cable, etc. - all indistinguishable from here, and all
    // handled the same way: skip the rest of this function, and every
    // OLED_WriteCmd/WriteData call for the rest of the session).
    oled_present = (HAL_I2C_IsDeviceReady(&hi2c2, OLED_I2C_ADDR, 2, 5) == HAL_OK) ? 1 : 0;
    if (!oled_present) return;
    // Verified compatible with both SSD1306 and SSD1315 (a newer, drop-in
    // replacement controller that many "SSD1306" modules actually ship
    // with today without the silkscreen/listing changing) - Solomon
    // Systech's own SSD1315 datasheet documents an identical command
    // register set to the SSD1306, and this full sequence (as opposed to a
    // truncated one) is what community reports confirm as most reliable
    // across both. One SSD1315-specific quirk exists in the wild: some
    // units emit an audible high-frequency squeak with the standard 0xD5
    // 0x80 oscillator setting, fixed by using 0xD5 0xF0 instead - cosmetic
    // only (doesn't affect what's shown), so left at the SSD1306-standard
    // 0x80 here rather than changing a working parameter on unverified
    // hardware; if a squeak shows up on the actual board, that's the
    // specific value to try.
    OLED_WriteCmd(0xAE); 
    OLED_WriteCmd(0xD5); OLED_WriteCmd(0x80);
    OLED_WriteCmd(0xA8); OLED_WriteCmd(0x3F); 
    OLED_WriteCmd(0xD3); OLED_WriteCmd(0x00);
    OLED_WriteCmd(0x40); 
    OLED_WriteCmd(0x8D); OLED_WriteCmd(0x14); 
    OLED_WriteCmd(0x20); OLED_WriteCmd(0x02); 
    OLED_WriteCmd(0xA1); 
    OLED_WriteCmd(0xC8); 
    OLED_WriteCmd(0xDA); OLED_WriteCmd(0x12);
    OLED_WriteCmd(0x81); OLED_WriteCmd(0xCF); 
    OLED_WriteCmd(0xD9); OLED_WriteCmd(0xF1);
    OLED_WriteCmd(0xDB); OLED_WriteCmd(0x40);
    OLED_WriteCmd(0xA4); OLED_WriteCmd(0xA6); 
    OLED_WriteCmd(0xAF); 
}

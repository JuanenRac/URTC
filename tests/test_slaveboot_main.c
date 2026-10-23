// =============================================================================
// URTC Firmware - Host logic tests for the bootloader of the expansion slave: start-up, I2C link and main loop (slaveboot_main.c)
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// tests/test_slaveboot_update.c covers the update protocol (size, hardware identifier, CRC, signature, rollback). This one runs the rest of the file: the
// hardware set-up, the address and receive callbacks the master reaches the registers through, and the main loop that gives up a stalled update after ten
// seconds. main() is renamed and the loop is left through the watchdog refresh. JumpToApplication, which never returns, stays out: the flash holds no application.
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#include "slaveboot_common.h"
#include "slaveboot_flash.h"
#include "slaveboot_protocol.h"
#include "urtc_version_wire.h"
#include "test_runner.h"

extern uint32_t update_total_size, update_bytes_received;      // defined in slaveboot_protocol.c, not in its header
int slaveboot_main_entry(void);
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, uint8_t TransferDirection, uint16_t AddrMatchCode);
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef *hi2c);
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c);
void I2C1_EV_IRQHandler(void);
void I2C1_ER_IRQHandler(void);

static void world(void)
{
    fake_hal_reset_recording();
    fake_flash_erase_all();
    fake_tick = 1000;
    fake_tick_step = 0;
    update_in_progress = 0; update_failed = 0; update_total_size = 0; update_bytes_received = 0;
    current_status = STATUS_LISTENING;
    memset(&hi2c1, 0, sizeof hi2c1);
}

static void master_writes(const uint8_t *bytes, uint32_t count)
{
    // the call the firmware reads as "the master writes" (see the note in tests/test_slave.c about the direction); the bytes go into its buffer
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, I2C_SLAVE_ADDRESS << 1);
    for (uint32_t i = 0; i < count && i < fake_i2c_slave_rx_len; i++) fake_i2c_slave_rx_buffer[i] = bytes[i];
    hi2c1.XferCount = fake_i2c_slave_rx_len - count;
    HAL_I2C_SlaveRxCpltCallback(&hi2c1);
}

static uint8_t read_register(uint8_t reg)
{
    master_writes(&reg, 1);
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, I2C_SLAVE_ADDRESS << 1);
    return fake_i2c_slave_tx[0];
}

static void test_link(int *failures)
{
    world();
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_RECEIVE, 0x84);
    TEST_ASSERT(fake_i2c_slave_rx_count == 1 && fake_i2c_slave_rx_len == 33, "KNOWN: the direction RECEIVE starts a receive of up to 33 bytes (the register and 32 bytes of data); see the note on the direction in test_slave.c");
    HAL_I2C_AddrCallback(&hi2c1, I2C_DIRECTION_TRANSMIT, 0x84);
    TEST_ASSERT(fake_i2c_slave_tx_count == 1 && fake_i2c_slave_tx_len == 1 && fake_i2c_slave_tx[0] == STATUS_LISTENING, "with no register chosen the answer is the status");

    TEST_ASSERT(read_register(REG_STATUS) == STATUS_LISTENING && fake_i2c_slave_tx_len == 1, "register 0: the status");
    update_progress_percent = 42;
    TEST_ASSERT(read_register(REG_PROGRESS) == 42 && fake_i2c_slave_tx_len == 1, "register 5: the progress");
    verify_fail_reason = VERIFY_FAIL_REASON_CRC32;
    TEST_ASSERT(read_register(REG_VERIFY_FAIL_REASON) == VERIFY_FAIL_REASON_CRC32 && fake_i2c_slave_tx_len == 1, "register 7: why the last verification failed");
    read_register(REG_QUERY_VERSION);
    TEST_ASSERT(fake_i2c_slave_tx_len == 10 && fake_i2c_slave_tx[0] == 1, "register 6: ten bytes, the first of which says the bootloader is running");
    TEST_ASSERT(fake_i2c_slave_tx[1] == 0 && fake_i2c_slave_tx[2] == 0 && fake_i2c_slave_tx[3] == 0 && fake_i2c_slave_tx[4] == 0 && fake_i2c_slave_tx[9] == BOOTLOADER_VERSION_MAJOR, "followed by the hardware identifier and version of the installed application (zeros with none installed) and the version of the bootloader");
    read_register(REG_QUERY_VERSION_EXT);
    TEST_ASSERT(fake_i2c_slave_tx_len == URTC_SLAVE_VERSION_EXT_LEN && fake_i2c_slave_tx[0] == URTC_SLAVE_VERSION_EXT_MARKER, "register 8: the extended answer starts with its marker, a value no status code has");
    TEST_ASSERT(read_register(0x55) == current_status && fake_i2c_slave_tx_len == 1, "an unknown register answers the status");

    // the writes
    uint8_t start[9] = {REG_START_UPDATE, 0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCB, 0x01};      // 1000 bytes for this board
    master_writes(start, 9);
    TEST_ASSERT(update_in_progress == 1 && update_total_size == 1000 && current_status == STATUS_RECEIVING, "a start command of nine bytes begins an update");
    uint8_t data[9] = {REG_DATA, 1, 2, 3, 4, 5, 6, 7, 8};
    master_writes(data, 9);
    TEST_ASSERT(update_bytes_received == 8, "a data command hands its eight bytes to the update");
    uint8_t data3[4] = {REG_DATA, 9, 9, 9};
    master_writes(data3, 4);
    TEST_ASSERT(update_bytes_received == 11, "and a shorter one its own length");
    uint8_t short_start[8] = {REG_START_UPDATE, 0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCB};
    update_in_progress = 0; update_total_size = 0;
    master_writes(short_start, 8);
    TEST_ASSERT(update_in_progress == 0 && update_total_size == 0, "a start command that is not nine bytes long does nothing");
    uint8_t short_hmac[10] = {REG_HMAC_EXPECTED, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    master_writes(short_hmac, 10);
    uint8_t short_end[5] = {REG_END_UPDATE, 1, 2, 3, 4};
    master_writes(short_end, 5);
    uint8_t unknown[3] = {0x77, 1, 2};
    master_writes(unknown, 3);
    TEST_ASSERT(update_in_progress == 0, "short HMAC and end commands and an unknown register are ignored");
    uint8_t before = current_status;
    hi2c1.XferCount = 33;
    HAL_I2C_SlaveRxCpltCallback(&hi2c1);
    TEST_ASSERT(current_status == before, "a receive that ended with no byte does nothing");

    HAL_I2C_SlaveTxCpltCallback(&hi2c1);
    int listens = fake_i2c_listen_count;
    HAL_I2C_ListenCpltCallback(&hi2c1);
    HAL_I2C_ErrorCallback(&hi2c1);
    TEST_ASSERT(fake_i2c_listen_count == listens + 2, "after the end of a transfer or an error it listens again");
    I2C1_EV_IRQHandler();
    I2C1_ER_IRQHandler();
    TEST_ASSERT(fake_i2c_ev_irq_count == 1 && fake_i2c_er_irq_count == 1, "the two I2C1 vectors are handed to the HAL");
}

static void test_main(int *failures)
{
    world();
    fake_iwdg_exit_after = 300;
    fake_reset_jmp_armed = 1;
    uint8_t start[9] = {REG_START_UPDATE, 0, 0, 0x03, 0xE8, 0x03, 0x03, 0xCB, 0x01};
    volatile int stage = 0;
    int left = setjmp(fake_reset_jmp);
    if (left == 0 && stage == 0) {
        stage = 1;
        // an update is under way when the loop starts to run: the last activity was now
        master_writes(start, 9);
        update_last_activity_tick = fake_tick;
        slaveboot_main_entry();
    }
    fake_reset_jmp_armed = 0;
    TEST_ASSERT(left == 2 && fake_iwdg_refreshes == 300, "the main loop refreshes the watchdog on every pass (the test leaves it after 300)");
    TEST_ASSERT(fake_hal_inits == 1 && fake_rcc_osc.PLL.PLLMUL == RCC_PLL_MUL16 && fake_rcc_clk.APB1CLKDivider == RCC_HCLK_DIV2, "the HAL is started and the clocks are the ones of the application");
    TEST_ASSERT(fake_iwdg_inits == 1 && fake_iwdg_prescaler == IWDG_PRESCALER_32 && fake_iwdg_reload == 999, "the watchdog is armed for one second");
    TEST_ASSERT(fake_i2c_inits == 1 && hi2c1.Init.OwnAddress1 == (I2C_SLAVE_ADDRESS << 1) && fake_i2c_init.NoStretchMode == I2C_NOSTRETCH_DISABLE, "I2C1 answers at 0x42 and may stretch the clock");
    TEST_ASSERT(fake_nvic[I2C1_EV_IRQn].enabled && fake_nvic[I2C1_ER_IRQn].enabled && fake_nvic[I2C1_ER_IRQn].preempt == 1, "its two interrupts are enabled at priority 1");
    int sda_scl = -1;
    for (int i = 0; i < fake_gpio_init_count; i++) if (fake_gpio_inits[i].init.Pin == (GPIO_PIN_6 | GPIO_PIN_7)) sda_scl = i;
    TEST_ASSERT(sda_scl >= 0 && fake_gpio_inits[sda_scl].init.Mode == GPIO_MODE_AF_OD && fake_gpio_inits[sda_scl].init.Alternate == GPIO_AF4_I2C1, "PB6 and PB7 are the I2C1 lines, open drain, alternate function 4");
    TEST_ASSERT(fake_i2c_listen_count >= 1, "it listens for its address");
    TEST_ASSERT(update_in_progress == 0 && current_status == STATUS_LISTENING, "an update with no data for ten seconds (200 passes of 50 ms) is abandoned and the bootloader listens again");
    TEST_ASSERT(fake_tick >= 1000 + 290 * 50 && fake_tick <= 1000 + 300 * 50, "every pass waits 50 ms");
}

void run_slaveboot_main_tests(int *failures)
{
    test_link(failures);
    test_main(failures);
}

#undef main      // the suite renames the main() of slaveboot_main.c (-Dmain=slaveboot_main_entry); the main of this test is the real one
int main(void)
{
    int failures = 0;
    printf("URTC host logic tests - the bootloader of the expansion slave: start-up, I2C link and main loop\n");
    void *map = mmap((void *)FLASH_BASE_ADDR, FLASH_SIZE_BYTES, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (map != MAP_FAILED) munmap(map, FLASH_SIZE_BYTES);
    if (map == MAP_FAILED || !fake_flash_init()) { printf("  SKIP: cannot map the fake flash on this host\n"); return 0; }
    run_slaveboot_main_tests(&failures);
    if (failures == 0) {
        printf("  OK   all expansion slave bootloader start-up host tests passed\n");
        return 0;
    }
    printf("  %d assertion(s) failed\n", failures);
    return 1;
}

// =============================================================================
// URTC Firmware - Fake STM32F3 HAL for host-side tests: fake_hal.c
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
#define _GNU_SOURCE
#include <string.h>
#include <sys/mman.h>
#include "stm32f3xx_hal.h"

GPIO_TypeDef fake_gpio_a, fake_gpio_b, fake_gpio_c, fake_gpio_f;
FakeTxFrame fake_tx[FAKE_MAX_TX];
int fake_tx_count;
int fake_tx_mailboxes_free = 3;
FakeGpioWrite fake_gpio_writes[FAKE_MAX_GPIO];
int fake_gpio_write_count;
uint32_t fake_tim_compare[4];
int fake_iwdg_refreshes;
int fake_reset_count;
jmp_buf fake_reset_jmp;
int fake_reset_jmp_armed;

void fake_hal_reset_recording(void)
{
    fake_tx_count = 0;
    fake_tx_mailboxes_free = 3;
    fake_gpio_write_count = 0;
    memset(fake_tim_compare, 0xFF, sizeof fake_tim_compare);
    fake_iwdg_refreshes = 0;
    fake_reset_count = 0;
    fake_reset_jmp_armed = 0;
}

HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *hdr, uint8_t *data, uint32_t *mb)
{
    (void)h; (void)mb;
    if (fake_tx_count < FAKE_MAX_TX) {
        FakeTxFrame *f = &fake_tx[fake_tx_count++];
        memset(f, 0, sizeof *f);
        f->id = hdr->StdId;
        f->dlc = hdr->DLC;
        memcpy(f->data, data, hdr->DLC > 8 ? 8 : hdr->DLC);
    }
    return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (fake_gpio_write_count < FAKE_MAX_GPIO) {
        fake_gpio_writes[fake_gpio_write_count].port = port;
        fake_gpio_writes[fake_gpio_write_count].pin = pin;
        fake_gpio_writes[fake_gpio_write_count].state = state;
        fake_gpio_write_count++;
    }
}

HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h) { (void)h; fake_iwdg_refreshes++; return HAL_OK; }

void NVIC_SystemReset(void)
{
    fake_reset_count++;
    if (fake_reset_jmp_armed) longjmp(fake_reset_jmp, 1);
}


SysTick_Type fake_systick;
SCB_Type fake_scb;
int fake_flash_fail_erase_nth;
int fake_flash_fail_program_nth;
uint32_t fake_tick;
uint32_t fake_tick_step;
uint32_t fake_rx_queue_count;
FakeRxFrame fake_rx_queue[8];
static uint8_t *flash_mem;
static int erase_calls, program_calls;

uint32_t HAL_GetTick(void) { uint32_t t = fake_tick; fake_tick += fake_tick_step; return t; }
void HAL_Delay(uint32_t ms) { fake_tick += ms; }

int fake_flash_init(void)
{
    if (!flash_mem) {
        void *p = mmap((void *)FLASH_BASE_ADDR, FLASH_SIZE_BYTES, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p == MAP_FAILED || p != (void *)FLASH_BASE_ADDR) return 0;
        flash_mem = p;
    }
    fake_flash_erase_all();
    return 1;
}

void fake_flash_erase_all(void)
{
    memset(flash_mem, 0xFF, FLASH_SIZE_BYTES);
    erase_calls = program_calls = 0;
    fake_flash_fail_erase_nth = fake_flash_fail_program_nth = 0;
    fake_rx_queue_count = 0;
}

HAL_StatusTypeDef HAL_FLASH_Unlock(void) { return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { return HAL_OK; }

HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *init, uint32_t *page_error)
{
    *page_error = 0xFFFFFFFFu;
    erase_calls++;
    if (fake_flash_fail_erase_nth && erase_calls == fake_flash_fail_erase_nth) { *page_error = init->PageAddress; return HAL_ERROR; }
    for (uint32_t i = 0; i < init->NbPages; i++) {
        uint32_t a = init->PageAddress + i * FLASH_PAGE_SIZE;
        if (a < FLASH_BASE_ADDR || a + FLASH_PAGE_SIZE > FLASH_BASE_ADDR + FLASH_SIZE_BYTES || (a & (FLASH_PAGE_SIZE - 1))) {
            *page_error = a; return HAL_ERROR;
        }
        memset(flash_mem + (a - FLASH_BASE_ADDR), 0xFF, FLASH_PAGE_SIZE);
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data)
{
    (void)type;
    program_calls++;
    if (fake_flash_fail_program_nth && program_calls == fake_flash_fail_program_nth) return HAL_ERROR;
    if (address < FLASH_BASE_ADDR || address + 2 > FLASH_BASE_ADDR + FLASH_SIZE_BYTES || (address & 1)) return HAL_ERROR;
    uint16_t old = (uint16_t)(flash_mem[address - FLASH_BASE_ADDR] | (flash_mem[address - FLASH_BASE_ADDR + 1] << 8));
    uint16_t want = (uint16_t)data;
    if (want & (uint16_t)~old) return HAL_ERROR; // would need a 0 -> 1 transition: not erased
    flash_mem[address - FLASH_BASE_ADDR] = (uint8_t)(want & 0xFF);
    flash_mem[address - FLASH_BASE_ADDR + 1] = (uint8_t)(want >> 8);
    return HAL_OK;
}

uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo) { (void)h; (void)fifo; return fake_rx_queue_count; }

HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hdr, uint8_t *data)
{
    (void)h; (void)fifo;
    if (fake_rx_queue_count == 0) return HAL_ERROR;
    *hdr = fake_rx_queue[0].hdr;
    memcpy(data, fake_rx_queue[0].data, 8);
    for (uint32_t i = 1; i < fake_rx_queue_count; i++) fake_rx_queue[i - 1] = fake_rx_queue[i];
    fake_rx_queue_count--;
    return HAL_OK;
}

void HAL_NVIC_DisableIRQ(int irq) { (void)irq; }
void HAL_CAN_DeInit(CAN_HandleTypeDef *h) { (void)h; }
void HAL_I2C_DeInit(I2C_HandleTypeDef *h) { (void)h; }
void HAL_RCC_DeInit(void) {}
void HAL_DeInit(void) {}

uint8_t fake_fram[FAKE_FRAM_SIZE];
int fake_fram_fail_reads, fake_fram_fail_writes;
int fake_fram_write_calls;
uint16_t fake_fram_write_sizes[64];
GPIO_PinState fake_pin_state[8];
static struct { GPIO_TypeDef *port; uint16_t pin; } pin_keys[8];
static int pin_key_count;

void fake_pin_set(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    for (int i = 0; i < pin_key_count; i++) {
        if (pin_keys[i].port == port && pin_keys[i].pin == pin) { fake_pin_state[i] = state; return; }
    }
    if (pin_key_count < 8) {
        pin_keys[pin_key_count].port = port;
        pin_keys[pin_key_count].pin = pin;
        fake_pin_state[pin_key_count++] = state;
    }
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    for (int i = 0; i < pin_key_count; i++) {
        if (pin_keys[i].port == port && pin_keys[i].pin == pin) return fake_pin_state[i];
    }
    return GPIO_PIN_RESET;
}

HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t mem, uint16_t memsize, uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)h; (void)dev; (void)memsize; (void)timeout;
    if (fake_fram_fail_writes) return HAL_ERROR;
    if ((uint32_t)mem + size > FAKE_FRAM_SIZE) return HAL_ERROR;
    if (fake_fram_write_calls < 64) fake_fram_write_sizes[fake_fram_write_calls] = size;
    fake_fram_write_calls++;
    memcpy(fake_fram + mem, data, size);
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t mem, uint16_t memsize, uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)h; (void)dev; (void)memsize; (void)timeout;
    if (fake_fram_fail_reads) return HAL_ERROR;
    if ((uint32_t)mem + size > FAKE_FRAM_SIZE) return HAL_ERROR;
    memcpy(data, fake_fram + mem, size);
    return HAL_OK;
}

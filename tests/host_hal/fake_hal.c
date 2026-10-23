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

uint32_t fake_adc_value;
HAL_StatusTypeDef fake_adc_status = HAL_OK;
int fake_adc_starts, fake_adc_stops;

void fake_hal_reset_recording(void)
{
    fake_adc_starts = fake_adc_stops = 0;
    fake_tx_count = 0;
    fake_tx_mailboxes_free = 3;
    fake_gpio_write_count = 0;
    memset(fake_tim_compare, 0xFF, sizeof fake_tim_compare);
    fake_iwdg_refreshes = 0;
    fake_reset_count = 0;
    fake_reset_jmp_armed = 0;
    fake_gpio_init_count = 0;
    fake_gpio_read_hook = 0;
    fake_gpio_write_hook = 0;
    fake_i2c_frame_count = fake_i2c_fail_transmits = fake_i2c_deinits = fake_i2c_probes = 0;
    fake_i2c_device_ready = HAL_OK;
    fake_spi_state = HAL_SPI_STATE_READY;
    fake_spi_dma_count = fake_dma_irq_count = 0;
    fake_spi_dma_len = 0;
    fake_can_irq_count = fake_can_abort_count = fake_exti_irq_count = fake_tim_irq_count = fake_inc_tick_count = 0;
    fake_exti_last_pin = 0;
    fake_i2c_tx_hook = 0;
    fake_i2c_rx_hook = 0;
    fake_i2c_listen_count = fake_i2c_slave_rx_count = fake_i2c_slave_tx_count = 0;
    fake_i2c_slave_rx_buffer = 0;
    fake_i2c_slave_rx_len = fake_i2c_slave_tx_len = 0;
    fake_tim_pwm_stops = 0;
    fake_iwdg_exit_after = 0;
    fake_iwdg_hook = 0;
    fake_gpio_toggle_count = fake_rcc_flag_clears = 0;
    fake_hal_inits = fake_i2c_ev_irq_count = fake_i2c_er_irq_count = fake_iwdg_inits = 0;
    memset(&fake_rcc_osc, 0, sizeof fake_rcc_osc);
    memset(&fake_rcc_clk, 0, sizeof fake_rcc_clk);
    memset(&fake_rcc_periph, 0, sizeof fake_rcc_periph);
    fake_rcc_calls = 0;
    fake_can_inits = fake_can_filters = fake_can_starts = 0;
    fake_can_notifications = 0;
    memset(fake_nvic, 0, sizeof fake_nvic);
    fake_i2c_inits = fake_dma_inits = fake_spi_inits = 0;
    fake_tim_base_inits = fake_tim_pwm_inits = fake_tim_oc_configs = fake_tim_pwm_starts = fake_tim_base_starts_it = fake_tim_moe = 0;
    memset(fake_tim_init, 0, sizeof fake_tim_init);
    memset(fake_tim_oc, 0, sizeof fake_tim_oc);
    fake_adc_inits = fake_adc_channel_configs = fake_adc_calibrations = 0;
    fake_bkup_access_calls = 0;
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

int (*fake_gpio_read_hook)(GPIO_TypeDef *port, uint16_t pin);
void (*fake_gpio_write_hook)(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (fake_gpio_write_count < FAKE_MAX_GPIO) {
        fake_gpio_writes[fake_gpio_write_count].port = port;
        fake_gpio_writes[fake_gpio_write_count].pin = pin;
        fake_gpio_writes[fake_gpio_write_count].state = state;
        fake_gpio_write_count++;
    }
    if (fake_gpio_write_hook) fake_gpio_write_hook(port, pin, state);
}

int fake_iwdg_exit_after;
void (*fake_iwdg_hook)(int refreshes);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h)
{
    (void)h;
    fake_iwdg_refreshes++;
    if (fake_iwdg_hook) fake_iwdg_hook(fake_iwdg_refreshes);
    if (fake_iwdg_exit_after && fake_iwdg_refreshes >= fake_iwdg_exit_after && fake_reset_jmp_armed) longjmp(fake_reset_jmp, 2);
    return HAL_OK;
}

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
void HAL_I2C_DeInit(I2C_HandleTypeDef *h) { (void)h; fake_i2c_deinits++; }
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
    if (fake_gpio_read_hook) {
        int answer = fake_gpio_read_hook(port, pin);
        if (answer >= 0) return answer ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
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

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h) { (void)h; fake_adc_starts++; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout) { (void)h; (void)timeout; return fake_adc_status; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h) { (void)h; return fake_adc_value; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h) { (void)h; fake_adc_stops++; return HAL_OK; }

// ---- the pins, the buses and the interrupt entry points of the peripheral code -----------------------------------------------------
FakeGpioInit fake_gpio_inits[FAKE_MAX_GPIO_INIT];
int fake_gpio_init_count;

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    if (fake_gpio_init_count < FAKE_MAX_GPIO_INIT) {
        fake_gpio_inits[fake_gpio_init_count].port = port;
        fake_gpio_inits[fake_gpio_init_count].init = *init;
        fake_gpio_init_count++;
    }
}

FakeI2cFrame fake_i2c_frames[FAKE_MAX_I2C_FRAMES];
int fake_i2c_frame_count, fake_i2c_fail_transmits, fake_i2c_deinits, fake_i2c_probes;
HAL_StatusTypeDef fake_i2c_device_ready = HAL_OK;

HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t addr, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)h; (void)timeout;
    if (fake_i2c_tx_hook) return fake_i2c_tx_hook(addr, data, len);
    if (fake_i2c_fail_transmits > 0) { fake_i2c_fail_transmits--; return HAL_ERROR; }
    if (fake_i2c_frame_count < FAKE_MAX_I2C_FRAMES) {
        FakeI2cFrame *f = &fake_i2c_frames[fake_i2c_frame_count++];
        f->addr = addr;
        f->len = len;
        memcpy(f->data, data, len > sizeof f->data ? sizeof f->data : len);
    }
    return HAL_OK;
}

HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr, uint32_t trials, uint32_t timeout)
{
    (void)h; (void)addr; (void)trials; (void)timeout;
    fake_i2c_probes++;
    return fake_i2c_device_ready;
}

uint32_t fake_spi_state = HAL_SPI_STATE_READY;
int fake_spi_dma_count, fake_dma_irq_count;
uint16_t fake_spi_dma_len;
uint8_t fake_spi_dma_data[256];

uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *h) { (void)h; return fake_spi_state; }

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *h, uint8_t *data, uint16_t len)
{
    (void)h;
    fake_spi_dma_count++;
    fake_spi_dma_len = len;
    memcpy(fake_spi_dma_data, data, len > sizeof fake_spi_dma_data ? sizeof fake_spi_dma_data : len);
    return HAL_OK;
}

void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h) { (void)h; fake_dma_irq_count++; }

int fake_can_irq_count, fake_can_abort_count, fake_exti_irq_count, fake_tim_irq_count, fake_inc_tick_count;
uint16_t fake_exti_last_pin;
static int tim3_instance;
void *fake_tim3_instance_ptr = &tim3_instance;
uint32_t fake_uid[3];

void HAL_CAN_IRQHandler(CAN_HandleTypeDef *h) { (void)h; fake_can_irq_count++; }
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailboxes) { (void)h; (void)mailboxes; fake_can_abort_count++; fake_tx_mailboxes_free = 3; return HAL_OK; }
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin) { fake_exti_irq_count++; fake_exti_last_pin = pin; }
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *h) { (void)h; fake_tim_irq_count++; }
void HAL_IncTick(void) { fake_inc_tick_count++; }
uint32_t HAL_GetUIDw0(void) { return fake_uid[0]; }
uint32_t HAL_GetUIDw1(void) { return fake_uid[1]; }
uint32_t HAL_GetUIDw2(void) { return fake_uid[2]; }

// ---- the start-up configuration of the peripherals ---------------------------------------------------------------------------------------
RCC_OscInitTypeDef fake_rcc_osc;
RCC_ClkInitTypeDef fake_rcc_clk;
RCC_PeriphCLKInitTypeDef fake_rcc_periph;
uint32_t fake_rcc_flash_latency;
int fake_rcc_calls;
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *init) { fake_rcc_osc = *init; fake_rcc_calls++; return HAL_OK; }
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *init, uint32_t latency) { fake_rcc_clk = *init; fake_rcc_flash_latency = latency; fake_rcc_calls++; return HAL_OK; }
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *init) { fake_rcc_periph = *init; fake_rcc_calls++; return HAL_OK; }

CAN_TypeDef fake_can_regs;
CAN_InitTypeDef fake_can_init;
CAN_FilterTypeDef fake_can_filter;
int fake_can_inits, fake_can_filters, fake_can_starts;
uint32_t fake_can_notifications;
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h) { fake_can_init = h->Init; fake_can_inits++; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f) { (void)h; fake_can_filter = *f; fake_can_filters++; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h) { (void)h; fake_can_starts++; return HAL_OK; }
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t its) { (void)h; fake_can_notifications |= its; return HAL_OK; }

FakeNvic fake_nvic[64];
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub) { if (irq >= 0 && irq < 64) { fake_nvic[irq].preempt = preempt; fake_nvic[irq].sub = sub; } }
void HAL_NVIC_EnableIRQ(int irq) { if (irq >= 0 && irq < 64) fake_nvic[irq].enabled = 1; }

int fake_i2c_inits;
I2C_InitTypeDef fake_i2c_init;
static int i2c2_instance;
void *fake_i2c2_instance_ptr = &i2c2_instance;
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h) { fake_i2c_init = h->Init; fake_i2c_inits++; return HAL_OK; }

int fake_dma_inits;
DMA_InitTypeDef fake_dma_init;
static int dma1_ch3;
void *fake_dma1_ch3_ptr = &dma1_ch3;
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h) { fake_dma_init = h->Init; fake_dma_inits++; return HAL_OK; }

int fake_spi_inits;
SPI_InitTypeDef fake_spi_init;
static int spi1_instance;
void *fake_spi1_ptr = &spi1_instance;
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h) { fake_spi_init = h->Init; fake_spi_inits++; return HAL_OK; }

static int tim1_instance, tim2_instance;
void *fake_tim1_ptr = &tim1_instance, *fake_tim2_ptr = &tim2_instance;
TIM_Base_InitTypeDef fake_tim_init[3];
TIM_OC_InitTypeDef fake_tim_oc[3];
int fake_tim_base_inits, fake_tim_pwm_inits, fake_tim_oc_configs, fake_tim_pwm_starts, fake_tim_base_starts_it, fake_tim_moe;
uint32_t fake_tim_oc_channel;
static int tim_slot(TIM_HandleTypeDef *h) { return h->Instance == fake_tim1_ptr ? 0 : h->Instance == fake_tim2_ptr ? 1 : 2; }
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h) { fake_tim_init[tim_slot(h)] = h->Init; fake_tim_base_inits++; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *h) { fake_tim_init[tim_slot(h)] = h->Init; fake_tim_pwm_inits++; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *h, TIM_OC_InitTypeDef *oc, uint32_t channel) { fake_tim_oc[tim_slot(h)] = *oc; fake_tim_oc_channel = channel; fake_tim_oc_configs++; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t channel) { (void)h; (void)channel; fake_tim_pwm_starts++; return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h) { (void)h; fake_tim_base_starts_it++; return HAL_OK; }

static int adc1_instance;
void *fake_adc1_ptr = &adc1_instance;
int fake_adc_inits, fake_adc_channel_configs, fake_adc_calibrations;
ADC_InitTypeDef fake_adc_init;
ADC_ChannelConfTypeDef fake_adc_channel;
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h) { fake_adc_init = h->Init; fake_adc_inits++; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h, ADC_ChannelConfTypeDef *c) { (void)h; fake_adc_channel = *c; fake_adc_channel_configs++; return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t single_diff) { (void)h; (void)single_diff; fake_adc_calibrations++; return HAL_OK; }

int fake_bkup_access_calls;
void HAL_PWR_EnableBkUpAccess(void) { fake_bkup_access_calls++; }

// ---- the hardware I2C of the expansion slave and its PWM timer ------------------------------------------------------------------------------
HAL_StatusTypeDef (*fake_i2c_tx_hook)(uint16_t addr, uint8_t *data, uint16_t len);
HAL_StatusTypeDef (*fake_i2c_rx_hook)(uint16_t addr, uint8_t *data, uint16_t len);
int fake_i2c_listen_count, fake_i2c_slave_rx_count, fake_i2c_slave_tx_count;
uint8_t *fake_i2c_slave_rx_buffer;
uint16_t fake_i2c_slave_rx_len, fake_i2c_slave_tx_len;
uint8_t fake_i2c_slave_tx[64];
uint32_t SystemCoreClock = 72000000u;
int fake_tim_pwm_stops;
uint32_t fake_tim_pwm_last_channel;

HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h, uint16_t addr, uint8_t *data, uint16_t len, uint32_t timeout)
{
    (void)h; (void)timeout;
    if (fake_i2c_rx_hook) return fake_i2c_rx_hook(addr, data, len);
    memset(data, 0, len);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_EnableListen_IT(I2C_HandleTypeDef *h) { (void)h; fake_i2c_listen_count++; return HAL_OK; }
HAL_StatusTypeDef HAL_I2C_Slave_Seq_Receive_IT(I2C_HandleTypeDef *h, uint8_t *data, uint16_t len, uint32_t option)
{
    (void)h; (void)option;
    fake_i2c_slave_rx_count++;
    fake_i2c_slave_rx_buffer = data;
    fake_i2c_slave_rx_len = len;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_Slave_Seq_Transmit_IT(I2C_HandleTypeDef *h, uint8_t *data, uint16_t len, uint32_t option)
{
    (void)h; (void)option;
    fake_i2c_slave_tx_count++;
    fake_i2c_slave_tx_len = len;
    memcpy(fake_i2c_slave_tx, data, len > sizeof fake_i2c_slave_tx ? sizeof fake_i2c_slave_tx : len);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *h, uint32_t channel) { (void)h; fake_tim_pwm_stops++; fake_tim_pwm_last_channel = channel; return HAL_OK; }

int fake_hal_inits, fake_i2c_ev_irq_count, fake_i2c_er_irq_count, fake_iwdg_inits;
uint32_t fake_iwdg_prescaler, fake_iwdg_reload;
HAL_StatusTypeDef HAL_Init(void) { fake_hal_inits++; return HAL_OK; }
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *h) { fake_iwdg_prescaler = h->Init.Prescaler; fake_iwdg_reload = h->Init.Reload; fake_iwdg_inits++; return HAL_OK; }
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef *h) { (void)h; fake_i2c_ev_irq_count++; }
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef *h) { (void)h; fake_i2c_er_irq_count++; }

uint32_t fake_rcc_flags;
int fake_rcc_flag_clears, fake_gpio_toggle_count;
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin) { (void)port; (void)pin; fake_gpio_toggle_count++; }

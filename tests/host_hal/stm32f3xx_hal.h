// =============================================================================
// URTC Firmware - Fake STM32F3 HAL for host-side tests: stm32f3xx_hal.h
// Copyright (C) 2026 JuanenRac (Electro Hobby 3D) <electrohobby3d@gmail.com>
// GPL-3.0-or-later - see LICENSE
// =============================================================================
// Just enough of the ST HAL surface for the firmware modules that the host
// tests compile unchanged (bootloader_crypto.c, firmware_can_global_pre.c).
// Every actuator write and every CAN transmit is recorded so a test can
// assert on it; NVIC_SystemReset() longjmps back into the test instead of
// resetting. Nothing here talks to hardware.
#ifndef HOST_FAKE_STM32F3XX_HAL_H
#define HOST_FAKE_STM32F3XX_HAL_H

#include <stdint.h>
#include <setjmp.h>

typedef enum { RESET = 0, SET = 1 } FlagStatus;
typedef enum { DISABLE = 0, ENABLE = 1 } FunctionalState;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

#define GPIO_PIN_0  0x0001u
#define GPIO_PIN_1  0x0002u
#define GPIO_PIN_2  0x0004u
#define GPIO_PIN_3  0x0008u
#define GPIO_PIN_4  0x0010u
#define GPIO_PIN_5  0x0020u
#define GPIO_PIN_6  0x0040u
#define GPIO_PIN_7  0x0080u
#define GPIO_PIN_8  0x0100u
#define GPIO_PIN_9  0x0200u
#define GPIO_PIN_10 0x0400u
#define GPIO_PIN_11 0x0800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u

typedef struct { int id; uint32_t MODER; } GPIO_TypeDef;
extern GPIO_TypeDef fake_gpio_a, fake_gpio_b, fake_gpio_c, fake_gpio_f;
#define GPIOA (&fake_gpio_a)
#define GPIOB (&fake_gpio_b)
#define GPIOC (&fake_gpio_c)
#define GPIOF (&fake_gpio_f)

typedef struct { uint32_t ESR; } CAN_TypeDef;
typedef struct { CAN_TypeDef *Instance; } CAN_HandleTypeDef;
typedef struct { uint32_t Dummy; } TIM_HandleTypeDef;
typedef struct { uint32_t Dummy; } SPI_HandleTypeDef;
typedef struct { uint32_t Dummy; } DMA_HandleTypeDef;
typedef struct { uint32_t Dummy; } I2C_HandleTypeDef;
typedef struct { uint32_t Dummy; } ADC_HandleTypeDef;
typedef struct { uint32_t Dummy; } IWDG_HandleTypeDef;

#define TIM_CHANNEL_1 0u
#define CAN_ID_STD    0u
#define CAN_RTR_DATA  0u

typedef struct {
    uint32_t StdId, ExtId, IDE, RTR, DLC;
    FunctionalState TransmitGlobalTime;
} CAN_TxHeaderTypeDef;

typedef struct {
    uint32_t StdId, ExtId, IDE, RTR, DLC, Timestamp, FilterMatchIndex;
} CAN_RxHeaderTypeDef;

// ---- recording --------------------------------------------------------------
#define FAKE_MAX_TX 512
#define FAKE_MAX_GPIO 32
typedef struct { uint32_t id, dlc; uint8_t data[8]; } FakeTxFrame;
typedef struct { GPIO_TypeDef *port; uint16_t pin; GPIO_PinState state; } FakeGpioWrite;

extern FakeTxFrame fake_tx[FAKE_MAX_TX];
extern int fake_tx_count;
extern int fake_tx_mailboxes_free;
extern FakeGpioWrite fake_gpio_writes[FAKE_MAX_GPIO];
extern int fake_gpio_write_count;
extern uint32_t fake_tim_compare[4];
extern int fake_iwdg_refreshes;
extern int fake_reset_count;
extern jmp_buf fake_reset_jmp;
extern int fake_reset_jmp_armed;

void fake_hal_reset_recording(void);

static inline uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *h) { (void)h; return (uint32_t)fake_tx_mailboxes_free; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(CAN_HandleTypeDef *h, CAN_TxHeaderTypeDef *hdr, uint8_t *data, uint32_t *mb);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *h);
void NVIC_SystemReset(void);
uint32_t HAL_GetTick(void);

#define __HAL_TIM_SET_COMPARE(h, ch, v) (fake_tim_compare[(ch) & 3u] = (uint32_t)(v), (void)(h))

// ---- extras used by the bootloader sources ------------------------------------
#define SRAM_BASE        0x20000000UL
#define CCMDATARAM_BASE  0x10000000UL
#define CAN_RX_FIFO0     0u
#define CAN_RX0_IRQn     20
#define FLASH_BASE_ADDR  0x08000000UL
#define FLASH_SIZE_BYTES 0x40000UL
#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE  0x800UL
#endif
#define FLASH_TYPEERASE_PAGES        0u
#define FLASH_TYPEPROGRAM_HALFWORD   1u
#define FLASH_FLAG_EOP    1u
#define FLASH_FLAG_WRPERR 2u
#define FLASH_FLAG_PGERR  4u
#define __HAL_FLASH_CLEAR_FLAG(f) ((void)(f))
#define __DSB() ((void)0)
#define __ISB() ((void)0)
#define __disable_irq() ((void)0)
#define __enable_irq() ((void)0)
#define __set_MSP(v) ((void)(v))
#define SCB_ICSR_PENDSTCLR_Msk 1u

typedef struct { uint32_t TypeErase, PageAddress, NbPages; } FLASH_EraseInitTypeDef;
typedef struct { uint32_t CTRL, LOAD, VAL; } SysTick_Type;
typedef struct { uint32_t ICSR, VTOR; } SCB_Type;
extern SysTick_Type fake_systick;
extern SCB_Type fake_scb;
#define SysTick (&fake_systick)
#define SCB (&fake_scb)

// Flash model: 256 KiB mapped at 0x08000000, erased = 0xFF, programming can only
// clear bits (a 0->1 request fails like a real PGERR). Fault injection counters
// count down to the Nth operation (0 = disabled).
extern int fake_flash_fail_erase_nth;
extern int fake_flash_fail_program_nth;
extern uint32_t fake_tick;
extern uint32_t fake_tick_step;
int fake_flash_init(void);                    // maps the flash, returns 1 on success
void fake_flash_erase_all(void);
extern uint32_t fake_rx_queue_count;
typedef struct { CAN_RxHeaderTypeDef hdr; uint8_t data[8]; } FakeRxFrame;
extern FakeRxFrame fake_rx_queue[8];

HAL_StatusTypeDef HAL_FLASH_Unlock(void);
HAL_StatusTypeDef HAL_FLASH_Lock(void);
HAL_StatusTypeDef HAL_FLASHEx_Erase(FLASH_EraseInitTypeDef *init, uint32_t *page_error);
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type, uint32_t address, uint64_t data);
void HAL_Delay(uint32_t ms);
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *h, uint32_t fifo);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *h, uint32_t fifo, CAN_RxHeaderTypeDef *hdr, uint8_t *data);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_CAN_DeInit(CAN_HandleTypeDef *h);
void HAL_I2C_DeInit(I2C_HandleTypeDef *h);
void HAL_RCC_DeInit(void);
void HAL_DeInit(void);

// ---- FRAM on I2C2 and pin reads (firmware_fram.c, firmware_persistence.c) -----------------
#define I2C_MEMADD_SIZE_16BIT 2u
#define FAKE_FRAM_SIZE 8192
extern uint8_t fake_fram[FAKE_FRAM_SIZE];
extern int fake_fram_fail_reads, fake_fram_fail_writes;
extern int fake_fram_write_calls;
extern uint16_t fake_fram_write_sizes[64];
extern GPIO_PinState fake_pin_state[8];     // value HAL_GPIO_ReadPin returns; keyed by fake_pin_set()
void fake_pin_set(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *h, uint16_t dev, uint16_t mem, uint16_t memsize, uint8_t *data, uint16_t size, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *h, uint16_t dev, uint16_t mem, uint16_t memsize, uint8_t *data, uint16_t size, uint32_t timeout);


// ---- the ADC of the thermal control loops (firmware_control_thermal.c, firmware_control_sensors.c) ------------------------------------
// One conversion: HAL_ADC_PollForConversion returns fake_adc_status and HAL_ADC_GetValue fake_adc_value.
extern uint32_t fake_adc_value;
extern HAL_StatusTypeDef fake_adc_status;
extern int fake_adc_starts, fake_adc_stops;
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *h, uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *h);

#endif // HOST_FAKE_STM32F3XX_HAL_H

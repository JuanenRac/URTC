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

typedef struct { int id; uint32_t MODER; volatile uint32_t BSRR, BRR; } GPIO_TypeDef;
extern GPIO_TypeDef fake_gpio_a, fake_gpio_b, fake_gpio_c, fake_gpio_f;
#define GPIOA (&fake_gpio_a)
#define GPIOB (&fake_gpio_b)
#define GPIOC (&fake_gpio_c)
#define GPIOF (&fake_gpio_f)

typedef struct { uint32_t ESR; } CAN_TypeDef;
typedef struct { uint32_t Prescaler, Mode, SyncJumpWidth, TimeSeg1, TimeSeg2; FunctionalState TimeTriggeredMode, AutoBusOff, AutoWakeUp, AutoRetransmission, ReceiveFifoLocked, TransmitFifoPriority; } CAN_InitTypeDef;
typedef struct { uint32_t Prescaler, CounterMode, Period, ClockDivision, RepetitionCounter, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS, BaudRatePrescaler, FirstBit, TIMode, CRCCalculation, CRCPolynomial; } SPI_InitTypeDef;
typedef struct { uint32_t Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority; } DMA_InitTypeDef;
typedef struct { uint32_t Timing, OwnAddress1, AddressingMode, DualAddressMode, OwnAddress2, OwnAddress2Masks, GeneralCallMode, NoStretchMode; } I2C_InitTypeDef;
typedef struct { uint32_t ClockPrescaler, Resolution, DataAlign, ScanConvMode, EOCSelection; FunctionalState LowPowerAutoWait, ContinuousConvMode; uint32_t NbrOfConversion; FunctionalState DiscontinuousConvMode; uint32_t ExternalTrigConv; FunctionalState DMAContinuousRequests; uint32_t Overrun; } ADC_InitTypeDef;
typedef struct { CAN_TypeDef *Instance; CAN_InitTypeDef Init; } CAN_HandleTypeDef;
typedef struct { void *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { void *Instance; DMA_InitTypeDef Init; } DMA_HandleTypeDef;
typedef struct { void *Instance; SPI_InitTypeDef Init; DMA_HandleTypeDef *hdmatx; } SPI_HandleTypeDef;
typedef struct { void *Instance; I2C_InitTypeDef Init; uint32_t XferCount; } I2C_HandleTypeDef;
typedef struct { void *Instance; ADC_InitTypeDef Init; } ADC_HandleTypeDef;
typedef struct { void *Instance; struct { uint32_t Prescaler, Reload; } Init; } IWDG_HandleTypeDef;

#define TIM_CHANNEL_1 0u
#define CAN_ID_STD    0u
#define CAN_RTR_DATA  0u
#define CAN_ID_EXT    4u
#define CAN_RTR_REMOTE 2u

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
#define I2C1_EV_IRQn     31  // the expansion slave bootloader disables them before it jumps
#define I2C1_ER_IRQn     32
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

// ---- the pins, the buses and the interrupt entry points of the peripheral code (oled, led, expansion buses, interrupts) -------------------
// Every pin write goes through HAL_GPIO_WritePin; a test that plays a device on a bit-banged bus sets fake_gpio_write_hook (called on every write,
// after it is recorded) and fake_gpio_read_hook (its answer wins when it is not negative). HAL_GPIO_Init records what it is asked to configure.
#define GPIO_MODE_INPUT      0u
#define GPIO_MODE_OUTPUT_PP  1u
#define GPIO_MODE_OUTPUT_OD  2u
#define GPIO_MODE_AF_PP      3u
#define GPIO_MODE_AF_OD      4u
#define GPIO_MODE_IT_RISING  5u
#define GPIO_MODE_IT_FALLING 6u
#define GPIO_MODE_IT_RISING_FALLING 7u
#define GPIO_MODE_ANALOG     8u
#define GPIO_NOPULL   0u
#define GPIO_PULLUP   1u
#define GPIO_PULLDOWN 2u
#define GPIO_SPEED_FREQ_LOW    0u
#define GPIO_SPEED_FREQ_MEDIUM 1u
#define GPIO_SPEED_FREQ_HIGH   2u
#define GPIO_AF4_I2C2 4u
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
#define FAKE_MAX_GPIO_INIT 64
typedef struct { GPIO_TypeDef *port; GPIO_InitTypeDef init; } FakeGpioInit;
extern FakeGpioInit fake_gpio_inits[FAKE_MAX_GPIO_INIT];
extern int fake_gpio_init_count;
extern int (*fake_gpio_read_hook)(GPIO_TypeDef *port, uint16_t pin);
extern void (*fake_gpio_write_hook)(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init);

#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)

// I2C master: every transmit is recorded; fake_i2c_fail_transmits makes the next N of them fail, fake_i2c_device_ready is the answer of the probe.
#define HAL_I2C_STATE_BUSY 2u
#define FAKE_MAX_I2C_FRAMES 256
typedef struct { uint16_t addr; uint16_t len; uint8_t data[140]; } FakeI2cFrame;
extern FakeI2cFrame fake_i2c_frames[FAKE_MAX_I2C_FRAMES];
extern int fake_i2c_frame_count, fake_i2c_fail_transmits, fake_i2c_deinits, fake_i2c_probes;
extern HAL_StatusTypeDef fake_i2c_device_ready;
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t addr, uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr, uint32_t trials, uint32_t timeout);

// SPI with DMA (the status LED): the state the program sees and the transfers it starts.
#define HAL_SPI_STATE_READY 1u
#define HAL_SPI_STATE_BUSY  2u
extern uint32_t fake_spi_state;
extern int fake_spi_dma_count, fake_dma_irq_count;
extern uint16_t fake_spi_dma_len;
extern uint8_t fake_spi_dma_data[256];
uint32_t HAL_SPI_GetState(SPI_HandleTypeDef *h);
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *h, uint8_t *data, uint16_t len);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *h);

// Interrupt entry points: the ones the firmware calls are counted with what they were given.
#define CAN_TX_MAILBOX0 1u
#define CAN_TX_MAILBOX1 2u
#define CAN_TX_MAILBOX2 4u
extern int fake_can_irq_count, fake_can_abort_count, fake_exti_irq_count, fake_tim_irq_count, fake_inc_tick_count;
extern uint16_t fake_exti_last_pin;
extern void *fake_tim3_instance_ptr;
#define TIM3 (fake_tim3_instance_ptr)
void HAL_CAN_IRQHandler(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_AbortTxRequest(CAN_HandleTypeDef *h, uint32_t mailboxes);
void HAL_GPIO_EXTI_IRQHandler(uint16_t pin);
void HAL_TIM_IRQHandler(TIM_HandleTypeDef *h);
void HAL_IncTick(void);
extern uint32_t fake_uid[3];
uint32_t HAL_GetUIDw0(void);
uint32_t HAL_GetUIDw1(void);
uint32_t HAL_GetUIDw2(void);

// ---- the start-up configuration of the peripherals (firmware_init_*.c) --------------------------------------------------------------------
// The handles carry their Init structures and every HAL_*_Init call records a copy of what it was given, so a test reads the configuration back.
// The values of the constants are the ones the firmware's arithmetic needs, not ST's register values: a divider is its divisor (RCC_HCLK_DIV2 is 2),
// a time-quanta setting is the number of quanta (CAN_BS1_13TQ is 13), a sampling time is in tenths of a cycle. The firmware only ever passes the names.
typedef struct { uint32_t OscillatorType, HSIState, HSICalibrationValue; struct { uint32_t PLLState, PLLSource, PLLMUL; } PLL; } RCC_OscInitTypeDef;
typedef struct { uint32_t ClockType, SYSCLKSource, AHBCLKDivider, APB1CLKDivider, APB2CLKDivider; } RCC_ClkInitTypeDef;
typedef struct { uint32_t PeriphClockSelection, I2c1ClockSelection, I2c2ClockSelection; } RCC_PeriphCLKInitTypeDef;
#define RCC_OSCILLATORTYPE_HSI 1u
#define RCC_HSI_ON 1u
#define RCC_HSICALIBRATION_DEFAULT 16u
#define RCC_PLL_ON 1u
#define RCC_PLLSOURCE_HSI 2u        // the PLL takes the HSI divided by 2
#define RCC_PLL_MUL16 16u
#define RCC_CLOCKTYPE_HCLK 1u
#define RCC_CLOCKTYPE_SYSCLK 2u
#define RCC_CLOCKTYPE_PCLK1 4u
#define RCC_CLOCKTYPE_PCLK2 8u
#define RCC_SYSCLKSOURCE_PLLCLK 2u
#define RCC_SYSCLK_DIV1 1u
#define RCC_HCLK_DIV1 1u
#define RCC_HCLK_DIV2 2u
#define FLASH_LATENCY_2 2u
#define RCC_PERIPHCLK_I2C1 1u
#define RCC_PERIPHCLK_I2C2 2u
#define RCC_I2C1CLKSOURCE_HSI 1u
#define RCC_I2C2CLKSOURCE_HSI 1u
extern RCC_OscInitTypeDef fake_rcc_osc;
extern RCC_ClkInitTypeDef fake_rcc_clk;
extern RCC_PeriphCLKInitTypeDef fake_rcc_periph;
extern uint32_t fake_rcc_flash_latency;
extern int fake_rcc_calls;
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *init);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *init, uint32_t latency);
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *init);

typedef struct { uint32_t FilterIdHigh, FilterIdLow, FilterMaskIdHigh, FilterMaskIdLow, FilterFIFOAssignment, FilterBank, FilterMode, FilterScale; FunctionalState FilterActivation; uint32_t SlaveStartFilterBank; } CAN_FilterTypeDef;
extern CAN_TypeDef fake_can_regs;
#define CAN (&fake_can_regs)
#define CAN_MODE_NORMAL 0u
#define CAN_SJW_1TQ 1u
#define CAN_SJW_2TQ 2u
#define CAN_FILTER_FIFO0 0u
#define CAN_BS1_13TQ 13u
#define CAN_BS2_2TQ 2u
#define CAN_FILTERMODE_IDMASK 0u
#define CAN_FILTERSCALE_32BIT 1u
#define CAN_IT_RX_FIFO0_MSG_PENDING 0x02u
#define CAN_IT_ERROR 0x8000u
#define CAN_IT_BUSOFF 0x400u
#define CAN_SCE_IRQn 22
#define GPIO_AF9_CAN 9u
#define GPIO_AF5_SPI1 5u
#define GPIO_AF6_TIM1 6u
extern CAN_InitTypeDef fake_can_init;
extern CAN_FilterTypeDef fake_can_filter;
extern int fake_can_inits, fake_can_filters, fake_can_starts;
extern uint32_t fake_can_notifications;
HAL_StatusTypeDef HAL_CAN_Init(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *h, CAN_FilterTypeDef *f);
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_CAN_ActivateNotification(CAN_HandleTypeDef *h, uint32_t its);

#define DMA1_Channel3_IRQn 13
#define TIM3_IRQn 29
#define EXTI3_IRQn 9
#define EXTI9_5_IRQn 23
typedef struct { int enabled; uint32_t preempt, sub; } FakeNvic;
extern FakeNvic fake_nvic[64];
void HAL_NVIC_SetPriority(int irq, uint32_t preempt, uint32_t sub);
void HAL_NVIC_EnableIRQ(int irq);

#define I2C_ADDRESSINGMODE_7BIT 1u
#define I2C_DUALADDRESS_DISABLE 0u
#define I2C_OA2_NOMASK 0u
#define I2C_GENERALCALL_DISABLE 0u
#define I2C_NOSTRETCH_DISABLE 0u
extern int fake_i2c_inits;
extern I2C_InitTypeDef fake_i2c_init;
extern void *fake_i2c2_instance_ptr;
#define I2C2 (fake_i2c2_instance_ptr)
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h);

#define DMA_MEMORY_TO_PERIPH 1u
#define DMA_PINC_DISABLE 0u
#define DMA_MINC_ENABLE 1u
#define DMA_PDATAALIGN_BYTE 0u
#define DMA_MDATAALIGN_BYTE 0u
#define DMA_NORMAL 0u
#define DMA_PRIORITY_LOW 0u
extern int fake_dma_inits;
extern DMA_InitTypeDef fake_dma_init;
extern void *fake_dma1_ch3_ptr;
#define DMA1_Channel3 (fake_dma1_ch3_ptr)
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *h);
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define __HAL_LINKDMA(handle, field, dma) ((handle)->field = &(dma))

#define SPI_MODE_MASTER 1u
#define SPI_DIRECTION_2LINES 0u
#define SPI_DATASIZE_8BIT 8u
#define SPI_POLARITY_LOW 0u
#define SPI_PHASE_1EDGE 0u
#define SPI_NSS_SOFT 1u
#define SPI_BAUDRATEPRESCALER_32 32u        // a divisor
#define SPI_FIRSTBIT_MSB 0u
#define SPI_TIMODE_DISABLE 0u
#define SPI_CRCCALCULATION_DISABLE 0u
extern int fake_spi_inits;
extern SPI_InitTypeDef fake_spi_init;
extern void *fake_spi1_ptr;
#define SPI1 (fake_spi1_ptr)
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h);

typedef struct { uint32_t OCMode, Pulse, OCPolarity, OCFastMode, OCNPolarity, OCIdleState, OCNIdleState; } TIM_OC_InitTypeDef;
#define TIM_COUNTERMODE_UP 0u
#define TIM_CLOCKDIVISION_DIV1 0u
#define TIM_OCMODE_PWM1 6u
#define TIM_OCPOLARITY_HIGH 0u
#define TIM_OCFAST_DISABLE 0u
#define TIM_OCNPOLARITY_HIGH 0u
#define TIM_OCIDLESTATE_RESET 0u
#define TIM_OCNIDLESTATE_RESET 0u
#define __HAL_RCC_TIM1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM3_CLK_ENABLE() ((void)0)
#define __HAL_RCC_ADC12_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_I2C1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SPI1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_CAN1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_RCC_SYSCFG_CLK_ENABLE() ((void)0)
extern void *fake_tim1_ptr, *fake_tim2_ptr;
#define TIM1 (fake_tim1_ptr)
#define TIM2 (fake_tim2_ptr)
extern TIM_Base_InitTypeDef fake_tim_init[3];       // as last given for TIM1, TIM2, TIM3
extern TIM_OC_InitTypeDef fake_tim_oc[3];
extern int fake_tim_base_inits, fake_tim_pwm_inits, fake_tim_oc_configs, fake_tim_pwm_starts, fake_tim_base_starts_it, fake_tim_moe;
extern uint32_t fake_tim_oc_channel;
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *h);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *h, TIM_OC_InitTypeDef *oc, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *h, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *h);
#define __HAL_TIM_MOE_ENABLE(h) (fake_tim_moe++, (void)(h))

typedef struct { uint32_t Channel, Rank, SamplingTime, SingleDiff, OffsetNumber, Offset; } ADC_ChannelConfTypeDef;
#define ADC_CLOCK_SYNC_PCLK_DIV2 2u
#define ADC_RESOLUTION_12B 12u
#define ADC_DATAALIGN_RIGHT 0u
#define ADC_SCAN_DISABLE 0u
#define ADC_EOC_SINGLE_CONV 4u
#define ADC_SOFTWARE_START 0u
#define ADC_OVR_DATA_OVERWRITTEN 1u
#define ADC_REGULAR_RANK_1 1u
#define ADC_SAMPLETIME_61CYCLES_5 615u        // tenths of a cycle
#define ADC_SINGLE_ENDED 0u
#define ADC_OFFSET_NONE 0u
extern void *fake_adc1_ptr;
#define ADC1 (fake_adc1_ptr)
extern int fake_adc_inits, fake_adc_channel_configs, fake_adc_calibrations;
extern ADC_InitTypeDef fake_adc_init;
extern ADC_ChannelConfTypeDef fake_adc_channel;
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *h);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *h, ADC_ChannelConfTypeDef *c);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *h, uint32_t single_diff);

extern int fake_bkup_access_calls;
void HAL_PWR_EnableBkUpAccess(void);
// The first backup register of the RTC block: it survives NVIC_SystemReset(), so the fake keeps it across the reset the tests play.
typedef struct { uint32_t BKP0R; } RTC_TypeDef;
extern RTC_TypeDef fake_rtc;
#define RTC (&fake_rtc)

// ---- the hardware I2C of the expansion slave (slave_i2c_link.c, slave_i2c_sensors.c) and its PWM timer ---------------------------------------------------
// The slave talks to the sensors through HAL_I2C_Master_Transmit / Receive on I2C2: a test that plays a device sets fake_i2c_tx_hook and fake_i2c_rx_hook
// (they return a HAL status and win over the recording); tests/host_hal/fake_i2c_bus.c installs them with the same devices as the bit-banged bus.
// As a slave on I2C1 the firmware is handed transfers by the HAL: the calls it makes to start them are recorded, and a test plays the interrupt.
#define I2C_DIRECTION_TRANSMIT 0u      // the direction of the master: it transmits, the slave receives
#define I2C_DIRECTION_RECEIVE  1u      // the master receives, the slave transmits
#define I2C_LAST_FRAME 0x20u
extern HAL_StatusTypeDef (*fake_i2c_tx_hook)(uint16_t addr, uint8_t *data, uint16_t len);
extern HAL_StatusTypeDef (*fake_i2c_rx_hook)(uint16_t addr, uint8_t *data, uint16_t len);
extern int fake_i2c_listen_count, fake_i2c_slave_rx_count, fake_i2c_slave_tx_count;
extern uint8_t *fake_i2c_slave_rx_buffer;
extern uint16_t fake_i2c_slave_rx_len, fake_i2c_slave_tx_len;
extern uint8_t fake_i2c_slave_tx[64];
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *h, uint16_t addr, uint8_t *data, uint16_t len, uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_EnableListen_IT(I2C_HandleTypeDef *h);
HAL_StatusTypeDef HAL_I2C_Slave_Seq_Receive_IT(I2C_HandleTypeDef *h, uint8_t *data, uint16_t len, uint32_t option);
HAL_StatusTypeDef HAL_I2C_Slave_Seq_Transmit_IT(I2C_HandleTypeDef *h, uint8_t *data, uint16_t len, uint32_t option);

#define TIM_CHANNEL_2 1u               // the fake indexes its compare registers by channel & 3
#define TIM_CHANNEL_3 2u
#define TIM_CHANNEL_4 3u
#define TIM_AUTORELOAD_PRELOAD_ENABLE 1u
#define __HAL_TIM_SET_AUTORELOAD(h, v) ((h)->Init.Period = (uint32_t)(v))
#define __HAL_TIM_GET_AUTORELOAD(h) ((h)->Init.Period)
extern uint32_t SystemCoreClock;
extern int fake_tim_pwm_stops;
extern uint32_t fake_tim_pwm_last_channel;
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *h, uint32_t channel);

// ---- the start-up of the expansion slave (slave_main.c, compiled with main renamed) --------------------------------------------------------
// The main loop of the slave refreshes the watchdog on every pass: fake_iwdg_exit_after makes the Nth refresh leave it with a longjmp (fake_reset_jmp).
#define IWDG ((void *)0)
#define IWDG_PRESCALER_32 3u
#define I2C1 (fake_i2c2_instance_ptr)
#define GPIO_AF4_I2C1 4u
extern int fake_iwdg_exit_after, fake_hal_inits;
HAL_StatusTypeDef HAL_Init(void);
HAL_StatusTypeDef HAL_IWDG_Init(IWDG_HandleTypeDef *h);
void HAL_I2C_EV_IRQHandler(I2C_HandleTypeDef *h);
void HAL_I2C_ER_IRQHandler(I2C_HandleTypeDef *h);
extern int fake_i2c_ev_irq_count, fake_i2c_er_irq_count, fake_iwdg_inits;
extern uint32_t fake_iwdg_prescaler, fake_iwdg_reload;

// ---- the start-up of the application (STM32F303CC_main.c, compiled with main renamed) ---------------------------------------------------------------
// The reset cause flags the application reads and clears, the diagnostic LED it toggles, the ADC channel numbers and the hook that lets a test act on the
// state of the firmware in the middle of its main loop (called on every watchdog refresh with the number of refreshes so far).
#define RCC_FLAG_IWDGRST 0x01u
#define RCC_FLAG_WWDGRST 0x02u
#define RCC_FLAG_SFTRST  0x04u
#define RCC_FLAG_PORRST  0x08u
#define RCC_FLAG_PINRST  0x10u
#define RCC_FLAG_LPWRRST 0x20u
#define RCC_FLAG_OBLRST  0x40u
extern uint32_t fake_rcc_flags;
extern int fake_rcc_flag_clears, fake_gpio_toggle_count;
#define __HAL_RCC_GET_FLAG(f) ((fake_rcc_flags & (f)) != 0u)
#define __HAL_RCC_CLEAR_RESET_FLAGS() (fake_rcc_flags = 0u, fake_rcc_flag_clears++)
#define ADC_CHANNEL_1  1u
#define ADC_CHANNEL_11 11u
#define GPIO_AF1_TIM2 1u
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);
extern void (*fake_iwdg_hook)(int refreshes);

#endif // HOST_FAKE_STM32F3XX_HAL_H

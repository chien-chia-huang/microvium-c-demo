/**
 * stm32l4xx_hal_conf.h
 *
 * Trimmed down from ST's NUCLEO-L433RC-P template: this project only uses
 * GPIO + the clock/tick/flash-latency plumbing HAL_Init() needs, so only
 * those HAL modules are enabled (and only their .c files are compiled --
 * see the Makefile). Re-enable more modules here (and add the matching .c
 * file to the Makefile) if you extend the firmware, e.g. to add a UART.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* ########################## Module Selection ############################## */
#define HAL_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED
/* stm32l4xx_hal_uart.h's handle struct references DMA_HandleTypeDef even
 * though this project only uses UART in blocking/polling mode -- DMA must
 * be enabled+compiled for the header to parse, but stm32l4xx_hal_dma.c is
 * never actually exercised at runtime. */
#define HAL_DMA_MODULE_ENABLED

/* ########################## Oscillator Values adaptation ####################*/
#if !defined (HSE_VALUE)
  #define HSE_VALUE    8000000U
#endif
#if !defined (HSE_STARTUP_TIMEOUT)
  #define HSE_STARTUP_TIMEOUT    100U
#endif
#if !defined (MSI_VALUE)
  #define MSI_VALUE    4000000U
#endif
#if !defined (HSI_VALUE)
  #define HSI_VALUE    16000000U
#endif
#if !defined (HSI48_VALUE)
  #define HSI48_VALUE   48000000U
#endif
#if !defined (LSI_VALUE)
  #define LSI_VALUE  32000U
#endif
#if !defined (LSE_VALUE)
  #define LSE_VALUE    32768U
#endif
#if !defined (LSE_STARTUP_TIMEOUT)
  #define LSE_STARTUP_TIMEOUT    5000U
#endif
/* Referenced by stm32l4xx_hal_rcc_ex.c's SAI clock frequency helper even
 * though this project doesn't use SAI; harmless placeholder values. */
#if !defined (EXTERNAL_SAI1_CLOCK_VALUE)
  #define EXTERNAL_SAI1_CLOCK_VALUE    48000U
#endif
#if !defined (EXTERNAL_SAI2_CLOCK_VALUE)
  #define EXTERNAL_SAI2_CLOCK_VALUE    48000U
#endif

/* ########################### System Configuration ######################### */
#define  VDD_VALUE                    3300U
#define  TICK_INT_PRIORITY            0x0FU
#define  USE_RTOS                     0U
#define  PREFETCH_ENABLE              0U
#define  INSTRUCTION_CACHE_ENABLE     1U
#define  DATA_CACHE_ENABLE            1U

/* ########################## Assert Selection ############################## */
/* #define USE_FULL_ASSERT               1U */

/* Includes ------------------------------------------------------------------*/
#ifdef HAL_RCC_MODULE_ENABLED
  #include "stm32l4xx_hal_rcc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
  #include "stm32l4xx_hal_gpio.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
  #include "stm32l4xx_hal_cortex.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
  #include "stm32l4xx_hal_pwr.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
  #include "stm32l4xx_hal_flash.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
  #include "stm32l4xx_hal_dma.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
  #include "stm32l4xx_hal_uart.h"
#endif

/* Exported macro ------------------------------------------------------------*/
#ifdef  USE_FULL_ASSERT
  #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
  void assert_failed(uint8_t *file, uint32_t line);
#else
  #define assert_param(expr) ((void)0U)
#endif

#ifdef __cplusplus
}
#endif

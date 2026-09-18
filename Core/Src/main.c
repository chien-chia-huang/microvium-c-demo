/**
 * main.c
 *
 * C-side responsibilities only (per project scope): bring up the clock,
 * configure the B1/LED GPIOs, start the Microvium VM, and drive it with a
 * fixed-period tick. All "how long should the LED stay lit" decision logic
 * lives in js/agent.mvm.js -- see mvm_host.c for the C<->JS boundary.
 */
#include "main.h"
#include "mvm_host.h"
#include "debug_uart.h"
#include <stdio.h>

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void LedSelfTest(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  Debug_UART_Init();

  printf("\r\n\r\n=== STM32L433RC + Microvium button/LED demo booting ===\r\n");
  printf("SystemCoreClock = %lu Hz\r\n", (unsigned long)SystemCoreClock);

  LedSelfTest();

  mvm_host_init();
  printf("Entering main loop (tick every %ums)\r\n", (unsigned)APP_TICK_INTERVAL_MS);

  uint32_t lastTick = HAL_GetTick();
  for (;;)
  {
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - lastTick) >= APP_TICK_INTERVAL_MS)
    {
      lastTick += APP_TICK_INTERVAL_MS;
      mvm_host_tick(now);
    }
  }
}

/**
 * Blinks LD4 three times before the VM starts, independent of any JS/VM
 * logic. If you see this blink but the LED never reacts to B1 afterwards,
 * the bug is in the VM/JS path; if you *don't* see this blink, the bug is
 * in the GPIO pin/wiring itself (wrong pin, board revision, etc).
 */
static void LedSelfTest(void)
{
  printf("LED self-test: blinking LD4 (PB13) 3x...\r\n");
  for (int i = 0; i < 3; i++)
  {
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(150);
  }
  printf("LED self-test done.\r\n");
}

/**
 * System Clock Configuration
 *   System Clock source = PLL (MSI)
 *   SYSCLK = 80 MHz, HCLK = 80 MHz, PCLK1 = PCLK2 = 80 MHz
 *
 * Copied from ST's NUCLEO-L433RC-P "Templates" example (main.c), which is
 * the vendor-recommended max-speed clock tree for this exact board/MCU.
 */
static void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
  RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLP = 7;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
 * Configures B1 (PC13) as an input and LD4 (PB13) as a push-pull output,
 * initially low (LED off). See app_config.h for the pin mapping.
 */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = { 0 };

  APP_BUTTON_CLK_ENABLE();
  APP_LED_CLK_ENABLE();

  HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_RESET);

  GPIO_InitStruct.Pin = APP_BUTTON_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  /* The board has an external pull-up on B1, so no internal pull is needed. */
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(APP_BUTTON_GPIO_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = APP_LED_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(APP_LED_GPIO_PORT, &GPIO_InitStruct);
}

void Error_Handler(void)
{
  printf("*** Error_Handler() called -- firmware halted. ***\r\n");
  __disable_irq();
  while (1)
  {
  }
}

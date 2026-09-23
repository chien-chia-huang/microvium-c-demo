/**
 * main.c
 *
 * C-side responsibilities only (per project scope): bring up the clock,
 * configure the B1/LED GPIOs, and hand off to FreeRTOS. The Microvium VM
 * itself is created lazily on the first button press and freed again once
 * its countdown ends -- see mvm_host_process() in mvm_host.c. All "how long
 * should the LED stay lit" decision logic lives in js/agent.mvm.js -- see
 * mvm_host.c for the C<->JS boundary, and app_tasks.c for the two tasks
 * that drive it.
 */
#include "main.h"
#include "mvm_host.h"
#include "app_tasks.h"
#include "debug_uart.h"

#include "FreeRTOS.h"
#include "task.h"

static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void LedSelfTest(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  Debug_UART_Init();

  Debug_Printf("\r\n\r\n=== STM32L433RC + Microvium button/LED demo booting ===\r\n");
  Debug_Printf("SystemCoreClock = %lu Hz\r\n", (unsigned long)SystemCoreClock);

  /*
   * LedSelfTest() must run before any FreeRTOS object (mutex/queue/task) is
   * created -- it's the last code that depends on HAL_Delay(), which needs
   * the TIM6 tick interrupt to still be able to fire. Creating a FreeRTOS
   * object before vTaskStartScheduler() has a real side effect on this
   * port: it permanently masks interrupts until the scheduler actually
   * starts (see the comment on Debug_UART_EnableThreadSafety() in
   * debug_uart.c for the full mechanism). That's invisible in the usual
   * FreeRTOS pattern -- create everything, immediately start the scheduler
   * -- but not if anything in between still needs an interrupt to make
   * progress, like this does.
   */
  LedSelfTest();

  Debug_UART_EnableThreadSafety();
  /* No mvm_host_init() here -- the VM task creates the VM lazily on the
   * first button press instead of at boot; see mvm_host_process() in
   * Core/Src/mvm_host.c. */
  App_StartTasks();

  Debug_Printf("Starting FreeRTOS scheduler...\r\n");
  vTaskStartScheduler();

  /* vTaskStartScheduler() only returns if it couldn't start (e.g. out of
   * heap for the idle/timer task) -- should never happen with the RAM
   * budget in FreeRTOSConfig.h, but fail loudly if it ever does. */
  Debug_Printf("vTaskStartScheduler() returned unexpectedly!\r\n");
  Error_Handler();
  for (;;)
  {
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
  Debug_Printf("LED self-test: blinking LD4 (PB13) 3x...\r\n");
  for (int i = 0; i < 3; i++)
  {
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_SET);
    HAL_Delay(150);
    HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, GPIO_PIN_RESET);
    HAL_Delay(150);
  }
  Debug_Printf("LED self-test done.\r\n");
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
  /* GPIO_NOPULL: verified empirically to be the setting that actually
   * detects real presses on this board (see app_config.h). GPIO_PULLUP
   * was also tried -- it stopped B1 from registering presses at all, which
   * means the press path has enough series resistance that stacking the
   * MCU's internal pull-up on top keeps the pin above the input-low
   * threshold even while pressed. Don't add a pull here without re-testing
   * an actual press/release on hardware afterward. */
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
  Debug_Printf("*** Error_Handler() called -- firmware halted. ***\r\n");
  __disable_irq();
  while (1)
  {
  }
}

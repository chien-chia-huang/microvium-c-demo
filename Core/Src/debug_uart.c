/**
 * debug_uart.c -- see debug_uart.h for the pin/terminal-settings summary.
 */
#include "debug_uart.h"
#include "stm32l4xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>
#include <stdarg.h>

static UART_HandleTypeDef s_huart2;
static SemaphoreHandle_t s_printfMutex;

/*
 * Received bytes are captured here by USART2_IRQHandler() below, not read
 * directly off the peripheral by Debug_UART_TryReadByte() -- see that
 * function's comment for why polling alone isn't safe. Must be a power of
 * 2 so the index-wrap below can use a mask instead of modulo.
 *
 * Sized to hold a whole upload frame (see js_upload.h's JS_UPLOAD_MAX_SIZE
 * plus its framing overhead) in one go, not just "a handful of bytes" -- at
 * 115200 baud, ~114 bytes can arrive within one APP_TICK_INTERVAL_MS (10ms)
 * polling period (app_tasks.c's JsUpload_Poll() call), and a whole upload
 * frame typically arrives as one continuous burst. A ring too small to
 * hold it all can silently drop bytes mid-frame if the GPIO task doesn't
 * get scheduled promptly enough during that burst, truncating the frame
 * with no error -- the parser just stalls, waiting on bytes that already
 * got lost. uint16_t indices (not uint8_t) because 4096 doesn't fit in 8 bits.
 */
#define RX_RING_SIZE 4096u
static volatile uint8_t s_rxRing[RX_RING_SIZE];
static volatile uint16_t s_rxHead = 0; /* next slot the ISR will write */
static volatile uint16_t s_rxTail = 0; /* next slot Debug_UART_TryReadByte() will read */

void Debug_UART_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART2_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = { 0 };
  GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3; /* PA2 = USART2_TX, PA3 = USART2_RX */
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  s_huart2.Instance = USART2;
  s_huart2.Init.BaudRate = 115200;
  s_huart2.Init.WordLength = UART_WORDLENGTH_8B;
  s_huart2.Init.StopBits = UART_STOPBITS_1;
  s_huart2.Init.Parity = UART_PARITY_NONE;
  s_huart2.Init.Mode = UART_MODE_TX_RX;
  s_huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  s_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  HAL_UART_Init(&s_huart2);

  /*
   * RX is interrupt-driven (not polled) precisely so it can't be starved by
   * __io_putchar()'s blocking transmit below: that call busy-waits for
   * several ms per debug line (e.g. the once-a-second heartbeat), and the
   * UART hardware has no RX FIFO -- just one byte of buffering -- so any
   * byte arriving while the CPU is stuck in that TX loop would otherwise be
   * silently lost to an overrun the instant a second byte follows it.
   */
  __HAL_UART_ENABLE_IT(&s_huart2, UART_IT_RXNE);
  HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);

  /* Deliberately NOT creating the print mutex here -- see
   * Debug_UART_EnableThreadSafety() below for why. */
}

/*
 * Pushes one received byte into the ring buffer, or drops it if the buffer
 * is full (better than overwriting a byte nothing has read yet, which
 * would corrupt the upload framing in a different way). Also clears a
 * hardware overrun (ORE) if one occurs, since that flag otherwise blocks
 * RXNE from ever setting again. No FreeRTOS API calls here -- this only
 * touches a plain volatile ring buffer, so it needs no special interrupt
 * priority relative to configMAX_SYSCALL_INTERRUPT_PRIORITY.
 */
void USART2_IRQHandler(void)
{
  if (__HAL_UART_GET_FLAG(&s_huart2, UART_FLAG_RXNE))
  {
    uint8_t b = (uint8_t)(s_huart2.Instance->RDR & 0xFF);
    uint16_t nextHead = (uint16_t)((s_rxHead + 1u) & (RX_RING_SIZE - 1u));
    if (nextHead != s_rxTail)
    {
      s_rxRing[s_rxHead] = b;
      s_rxHead = nextHead;
    }
  }

  if (__HAL_UART_GET_FLAG(&s_huart2, UART_FLAG_ORE))
  {
    __HAL_UART_CLEAR_OREFLAG(&s_huart2);
  }
}

/*
 * Creates the mutex Debug_Printf() uses to keep log lines from interleaving
 * once two tasks are both logging. This is a SEPARATE step from
 * Debug_UART_Init() -- and must be called only after any pre-scheduler code
 * that depends on hardware interrupts still firing (e.g. HAL_Delay(), which
 * needs the TIM6 tick interrupt -- see stm32l4xx_hal_timebase_tim.c) has
 * already finished running. Concretely: call this right before
 * App_StartTasks(), never before LedSelfTest() in main.c.
 *
 * Why: xSemaphoreCreateMutex() ends up inside queue.c's xQueueGenericSend(),
 * which uses taskENTER_CRITICAL()/taskEXIT_CRITICAL(). This port's critical-
 * section nesting counter (uxCriticalNesting in port.c) starts at a sentinel
 * value, not 0, until vTaskStartScheduler() resets it -- so the first
 * taskEXIT_CRITICAL() called before the scheduler starts decrements back to
 * that same sentinel, never to 0, and the "re-enable interrupts" branch
 * never runs. The net effect: creating a mutex (or queue, or task) before
 * vTaskStartScheduler() leaves BASEPRI permanently raised -- silently
 * masking ALL interrupts, including TIM6's -- until the scheduler actually
 * starts. Harmless in the universal FreeRTOS pattern (create everything,
 * immediately start the scheduler, nothing in between needs interrupts),
 * but fatal here if it happens before LedSelfTest()'s HAL_Delay() calls,
 * which silently hang forever waiting for a TIM6 interrupt that can no
 * longer fire.
 */
void Debug_UART_EnableThreadSafety(void)
{
  s_printfMutex = xSemaphoreCreateMutex();
  configASSERT(s_printfMutex != NULL);
}

void Debug_Printf(const char *fmt, ...)
{
  /* Before Debug_UART_EnableThreadSafety() has run, there's only ever one
   * execution context anyway (no tasks exist yet), so skipping the lock is
   * safe -- see that function's comment for why it can't be created any
   * earlier than it is. */
  if (s_printfMutex != NULL)
  {
    xSemaphoreTake(s_printfMutex, portMAX_DELAY);
  }

  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);

  if (s_printfMutex != NULL)
  {
    xSemaphoreGive(s_printfMutex);
  }
}

/*
 * Newlib's _write() (Core/Src/syscalls.c) calls this per character, which is
 * what makes printf()/puts() route out over USART2. Blocking transmit is
 * fine here -- this is a debug console, not a hot path, and the tick loop
 * only prints on state transitions (see mvm_host.c), not every 10ms.
 */
int __io_putchar(int ch)
{
  uint8_t c = (uint8_t)ch;
  HAL_UART_Transmit(&s_huart2, &c, 1, HAL_MAX_DELAY);
  return ch;
}

int Debug_UART_TryReadByte(uint8_t *out)
{
  /* Reads out of the ring buffer USART2_IRQHandler() fills above, not the
   * peripheral directly -- the byte is already captured by the time this
   * runs, regardless of what the caller's task was busy doing when it
   * arrived. s_rxTail is only ever written here (single reader), so this
   * needs no lock against the ISR (single writer) beyond the ordering
   * NVIC already gives a plain load/store on Cortex-M. */
  if (s_rxTail == s_rxHead)
  {
    return 0;
  }
  *out = s_rxRing[s_rxTail];
  s_rxTail = (uint16_t)((s_rxTail + 1u) & (RX_RING_SIZE - 1u));
  return 1;
}

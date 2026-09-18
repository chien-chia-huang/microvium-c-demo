/**
 * debug_uart.c -- see debug_uart.h for the pin/terminal-settings summary.
 */
#include "debug_uart.h"
#include "stm32l4xx_hal.h"

static UART_HandleTypeDef s_huart2;

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

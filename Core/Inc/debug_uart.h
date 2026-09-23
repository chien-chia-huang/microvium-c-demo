/**
 * debug_uart.h
 *
 * Serial console output over the Nucleo's onboard ST-LINK Virtual COM Port
 * (VCP), so you can watch what the firmware is actually doing instead of
 * only inferring it from the LED. USART2 on PA2 (TX) / PA3 (RX) is the
 * standard VCP UART on essentially every ST Nucleo-64 board (confirmed here
 * against ST's own UART example for the sibling NUCLEO-L476RG board, which
 * shares the same Nucleo-64 PCB/ST-LINK wiring convention).
 *
 * Open it from your host at 115200 8N1, e.g.:
 *   macOS:   screen /dev/tty.usbmodemXXXX 115200   (find the name with `ls /dev/tty.usbmodem*`)
 *   Linux:   screen /dev/ttyACM0 115200
 *   Windows: PuTTY / Tera Term on the matching COM port, 115200 8N1
 */
#pragma once

#include <stdint.h>

/* Configures PA2/PA3 + USART2 and hooks up printf() (via __io_putchar in
 * debug_uart.c) to it. Call once at startup, early -- this does NOT create
 * the Debug_Printf() mutex; see Debug_UART_EnableThreadSafety() for why
 * that's a deliberately separate, later step. */
void Debug_UART_Init(void);

/*
 * Creates the mutex Debug_Printf() uses. Call this once, later than
 * Debug_UART_Init() -- specifically, after any pre-scheduler code that
 * calls HAL_Delay() (or otherwise depends on hardware interrupts still
 * firing) has already finished, and before either FreeRTOS task is
 * created. See the comment on this function in debug_uart.c for the full
 * explanation: creating a mutex before vTaskStartScheduler() has a real
 * side effect on this port (permanently masking interrupts until the
 * scheduler starts), which is invisible in most FreeRTOS programs but not
 * if anything in between needs an interrupt to make progress.
 */
void Debug_UART_EnableThreadSafety(void);

/*
 * printf(), but holds a mutex for the whole call so a full log line from
 * one task can't get interleaved with characters from another task's line.
 * Now that the GPIO task and the VM task both log independently (see
 * Core/Src/app_tasks.c), use this instead of printf() directly everywhere
 * a log line is emitted. Safe to call before Debug_UART_EnableThreadSafety()
 * too (it just skips the lock, since there's only one execution context
 * that early anyway).
 */
void Debug_Printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
 * Non-blocking: if a byte has arrived on USART2's receiver, stores it in
 * *out and returns 1; otherwise returns 0 immediately. Safe to call every
 * tick from a polling loop -- see js_upload.c, the only current reader.
 */
int Debug_UART_TryReadByte(uint8_t *out);

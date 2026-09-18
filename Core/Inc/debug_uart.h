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

/* Configures PA2/PA3 + USART2 and hooks up printf() (via __io_putchar in
 * debug_uart.c) to it. Call once at startup, before any printf(). */
void Debug_UART_Init(void);

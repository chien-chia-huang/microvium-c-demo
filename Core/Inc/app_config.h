/**
 * app_config.h
 *
 * Board-specific pin mapping and app-level constants shared by main.c and
 * mvm_host.c. Keeping these in one place makes it obvious what to change if
 * you port this to a different board.
 *
 * ---------------------------------------------------------------------------
 * Board identity (see README.md "Board identification" section):
 *
 *   Board:    NUCLEO-L433RC-P  (Nucleo-64-P, MB1319)
 *   MCU:      STM32L433RCT6P   (LQFP64, 256KB flash, 64KB SRAM)
 *
 *   B1 (user button): PC13, active LOW (pressed = electrical low), with an
 *                      external pull-up already on the board.
 *
 *   LD4 (user LED):   PB13. Labelled LD4 on this board's silkscreen (LD3 is
 *                      the separate power/overcurrent indicator).
 * ---------------------------------------------------------------------------
 */
#pragma once

#include "stm32l4xx_hal.h"

/* ---- User button (B1) ---- */
#define APP_BUTTON_GPIO_PORT        GPIOC
#define APP_BUTTON_GPIO_PIN         GPIO_PIN_13
#define APP_BUTTON_CLK_ENABLE()     __HAL_RCC_GPIOC_CLK_ENABLE()
/* Electrical level read on the pin while B1 is held down. */
#define APP_BUTTON_PRESSED_LEVEL    GPIO_PIN_RESET

/* ---- User LED (LD4) ---- */
#define APP_LED_GPIO_PORT           GPIOB
#define APP_LED_GPIO_PIN            GPIO_PIN_13
#define APP_LED_CLK_ENABLE()        __HAL_RCC_GPIOB_CLK_ENABLE()

/* How often the main loop samples the button and calls the JS onTick(). */
#define APP_TICK_INTERVAL_MS        10u

/*
 * Number of consecutive stable raw samples (each APP_TICK_INTERVAL_MS apart)
 * required before a button edge is accepted. 2 samples * 10ms = 20ms of
 * debounce, which is comfortably more than typical mechanical bounce time.
 */
#define APP_BUTTON_DEBOUNCE_SAMPLES  2u

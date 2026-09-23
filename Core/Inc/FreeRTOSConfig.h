/**
 * FreeRTOSConfig.h
 *
 * Sized for this project: two small tasks (GPIO/IO task + Microvium VM
 * task), no software timers, no MPU. Budget check against the 64KB SRAM
 * total: configTOTAL_HEAP_SIZE (8KB) + both task stacks (2 * 1KB) + idle
 * task stack (~0.5KB) + a handful of small TCBs/queue storage is well
 * under 12KB, leaving the rest for the C .data/.bss, the 1KB main stack,
 * and Microvium's own heap (which uses newlib malloc via sbrk, a
 * completely separate allocator from FreeRTOS's heap_4 -- see the sizing
 * note in third_party/microvium/microvium_port.h for that side of the
 * budget). If you add more tasks or bump stack sizes, re-check with
 * `arm-none-eabi-size build/firmware.elf` same as before.
 */
#pragma once

#include <stdint.h>

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      ( SystemCoreClock )
#define configTICK_RATE_HZ                      ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES                    ( 5 )
#define configMINIMAL_STACK_SIZE                ( ( uint16_t ) 128 )  /* words, not bytes: 512 bytes */
#define configTOTAL_HEAP_SIZE                   ( ( size_t ) ( 8 * 1024 ) )
#define configMAX_TASK_NAME_LEN                 ( 12 )
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configQUEUE_REGISTRY_SIZE               4
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

/* Hooks */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW           2  /* catches overflow bugs during bring-up; cheap to leave on */

/* No software timers in this project. */
#define configUSE_TIMERS                        0
#define configTIMER_TASK_PRIORITY               ( configMAX_PRIORITIES - 1 )
#define configTIMER_QUEUE_LENGTH                4
#define configTIMER_TASK_STACK_DEPTH            ( configMINIMAL_STACK_SIZE * 2 )

/* No MPU in this project (plain ARM_CM4F port, not ARM_CM4_MPU). */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1

/* Optional API sets -- keep the ones actually used to save flash. */
#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               0
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState           0
#define INCLUDE_xTaskGetCurrentTaskHandle        0
#define INCLUDE_uxTaskGetStackHighWaterMark      1  /* handy for tuning stack sizes later */
#define INCLUDE_xTaskGetIdleTaskHandle            0
#define INCLUDE_eTaskGetState                    0
#define INCLUDE_xTimerPendFunctionCall            0
#define INCLUDE_xTaskAbortDelay                   0
#define INCLUDE_xTaskGetHandle                    0

/* Cortex-M4F port config */
#define configPRIO_BITS                         4   /* STM32L4's NVIC has 4 priority bits */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0x0f
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
  ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
  ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* SysTick_Handler is intentionally NOT renamed here -- Core/Src/stm32l4xx_it.c
 * defines it explicitly and calls xPortSysTickHandler() from inside, since
 * HAL's own 1ms tick comes from TIM6 instead (see
 * Core/Src/stm32l4xx_hal_timebase_tim.c), not from SysTick. */

#define configASSERT( x ) if ( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

extern uint32_t SystemCoreClock;

/**
 * app_tasks.c -- see app_tasks.h for the two-task design.
 */
#include "app_tasks.h"
#include "mvm_host.h"
#include "app_config.h"
#include "js_upload.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"
#include "debug_uart.h"

/* Handle for the VM task, so the GPIO task can notify it directly --
 * no queue object needed for a single producer/single consumer handoff. */
static TaskHandle_t s_mvmTaskHandle = NULL;

static void vGpioTask(void *pvParameters)
{
  (void)pvParameters;

  TickType_t lastWakeTime = xTaskGetTickCount();
  for (;;)
  {
    uint32_t nowMs = HAL_GetTick();
    int buttonState = mvm_host_poll_button(nowMs);

    /* Drains any bytes waiting on the same serial console for a new
     * agent.mvm.js image -- see js_upload.h. Cheap no-op unless something's
     * actually being sent. */
    JsUpload_Poll();

    /* Wakes the VM task and hands it the button state as the notification's
     * value. eSetValueWithOverwrite always replaces whatever value (if any)
     * is still pending -- the VM task only ever needs the *latest* reading,
     * not a backlog of every one posted, same as our old length-1 queue. */
    xTaskNotify(s_mvmTaskHandle, (uint32_t)buttonState, eSetValueWithOverwrite);

    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(APP_TICK_INTERVAL_MS));
  }
}

static void vMvmTask(void *pvParameters)
{
  (void)pvParameters;

  for (;;)
  {
    uint32_t notifiedButtonState = 0;
    /* Blocks here until the GPIO task notifies -- this task does no
     * polling of its own and uses no CPU while idle. */
    xTaskNotifyWait(0, 0, &notifiedButtonState, portMAX_DELAY);

    /* HAL_GetTick() read fresh here rather than reusing the GPIO task's
     * timestamp -- this task wakes essentially immediately after being
     * notified, so the difference is a fraction of a millisecond, well
     * under the 10ms tick period. */
    mvm_host_process(HAL_GetTick(), (int)notifiedButtonState);
  }
}

void App_StartTasks(void)
{
  BaseType_t ok;

  /* Create the VM task first so s_mvmTaskHandle is valid before the GPIO
   * task could possibly notify it (neither task actually runs until
   * vTaskStartScheduler(), but this keeps the ordering obviously safe). */
  ok = xTaskCreate(vMvmTask, "mvm", 256, NULL, 1, &s_mvmTaskHandle);
  configASSERT(ok == pdPASS);

  /* GPIO task runs slightly higher priority than the VM task so B1 sampling
   * stays on schedule even if the VM task is doing something. Both stacks
   * are 1KB (256 words); see the RAM budget note in Core/Inc/FreeRTOSConfig.h. */
  ok = xTaskCreate(vGpioTask, "gpio", 256, NULL, 2, NULL);
  configASSERT(ok == pdPASS);
}

/* Required by configCHECK_FOR_STACK_OVERFLOW (FreeRTOSConfig.h). */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;
  Debug_Printf("*** STACK OVERFLOW in task '%s' ***\r\n", pcTaskName);
  Error_Handler();
}

/* Required by configUSE_MALLOC_FAILED_HOOK (FreeRTOSConfig.h) -- fires if
 * the FreeRTOS heap (configTOTAL_HEAP_SIZE) runs out, e.g. from creating
 * too many tasks. Distinct from Microvium's own MVM_E_OUT_OF_MEMORY, which
 * is a separate allocator (see FreeRTOSConfig.h's sizing note). */
void vApplicationMallocFailedHook(void)
{
  Debug_Printf("*** FreeRTOS heap allocation failed (out of configTOTAL_HEAP_SIZE) ***\r\n");
  Error_Handler();
}

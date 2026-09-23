/**
 * app_tasks.h
 *
 * The two FreeRTOS tasks this project runs:
 *
 *   - the GPIO task ("the normal C" side): every APP_TICK_INTERVAL_MS,
 *     debounces + reads B1 (mvm_host_poll_button()) and notifies the VM
 *     task with the result (xTaskNotify -- see app_tasks.c). Nothing here
 *     knows anything about Microvium.
 *
 *   - the VM task ("the mvm.js" side): blocks on that notification
 *     (xTaskNotifyWait), and for each one it gets, calls
 *     mvm_host_process(). That function creates the Microvium VM the
 *     first time it sees a fresh button press, calls into the JS onTick()
 *     export, applies whatever LED action it returns, and frees the VM
 *     again once onTick() reports the countdown has ended -- so the VM
 *     only exists while a press is being timed, not continuously. See
 *     mvm_host.c for that lifecycle.
 *
 * They're deliberately separate tasks (rather than one task doing both, or
 * calling mvm_host_process() directly from the GPIO task) so that the
 * VM call is never on the same execution context as the raw hardware
 * polling -- e.g. if a future onTick() implementation ever did something
 * slower, it wouldn't affect how promptly B1 gets sampled/debounced.
 */
#pragma once

/* Creates both tasks and starts them. Call once from main(), before
 * vTaskStartScheduler(). */
void App_StartTasks(void);

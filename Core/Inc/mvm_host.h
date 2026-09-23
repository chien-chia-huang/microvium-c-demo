/**
 * mvm_host.h
 *
 * Glue between the C firmware and the Microvium JS VM running agent.mvm.js.
 *
 * The IDs below are the shared contract between C (this file) and JS
 * (js/agent.mvm.js). If you renumber something here, update agent.mvm.js
 * to match, and vice versa.
 */
#pragma once

#include <stdint.h>

/* Host function IDs, called from JS via vmImport(id). */
typedef enum
{
  MVM_HOST_FN_READ_BUTTON     = 1, /* readButton() -> 0/1 */
  MVM_HOST_FN_SET_LED         = 2, /* setLed(state) -> undefined */
  MVM_HOST_FN_GET_TIME        = 3, /* getTime() -> milliseconds since boot */
  MVM_HOST_FN_GET_PRESS_COUNT = 4, /* getPressCount() -> number of presses since boot */
} mvm_host_function_id_t;

/* Export IDs, resolved from C via mvm_resolveExports() to call into JS. */
typedef enum
{
  MVM_EXPORT_ON_TICK = 100, /* onTick(nowMs, buttonState) -> action code */
} mvm_export_id_t;

/*
 * The LED itself is controlled by JS calling setLed() directly (a genuine
 * host callback, not C decoding a return value) -- see host_setLed() in
 * mvm_host.c. The only thing onTick()'s return value communicates back to
 * C is the one decision JS *can't* make for itself: whether to keep the VM
 * alive or free it. JS can't call mvm_free() on itself mid-call, so that
 * has to come back out as a return value for C to act on afterward.
 *
 *   MVM_ACTION_DONE            -- countdown finished (JS already called
 *                                  setLed(false)); C should free the VM now.
 *   MVM_ACTION_COUNTDOWN_BASE + N -- still counting down, N seconds
 *                                  remaining. Purely informational: C only
 *                                  uses it to print progress, never to
 *                                  touch hardware. agent.mvm.js computes N
 *                                  itself (it's the one that knows
 *                                  LIGHT_DURATION_MS); C just decodes and
 *                                  displays whatever number it's given.
 */
typedef enum
{
  MVM_ACTION_DONE = 0,
} mvm_action_t;

#define MVM_ACTION_COUNTDOWN_BASE 100

/*
 * Debounces + reads B1 (logging any raw or debounced transition, plus a
 * once-a-second heartbeat), and returns the debounced state (0/1). This is
 * the "GPIO task" side of the split -- see Core/Src/app_tasks.c. Call once
 * per APP_TICK_INTERVAL_MS.
 */
int mvm_host_poll_button(uint32_t nowMs);

/*
 * The "VM task" side of the split -- see Core/Src/app_tasks.c. Call once
 * per new (nowMs, buttonState) reading from mvm_host_poll_button().
 *
 * The Microvium VM is created lazily here, on the first reading that shows
 * a fresh button press with no VM currently running, and freed again once
 * onTick() reports the countdown has ended -- so the VM only exists while
 * a press is actively being timed, not continuously from boot. See the
 * comment on this function in mvm_host.c for the full lifecycle.
 */
void mvm_host_process(uint32_t nowMs, int buttonState);

/*
 * True while a VM is alive (mid-countdown). Checked by js_upload.c before
 * committing a freshly received bytecode image, so an upload arriving
 * mid-countdown can't overwrite the buffer a running VM is still reading
 * bytecode out of (Microvium keeps referencing the original buffer for the
 * VM's whole lifetime, rather than copying all of it up front).
 */
int mvm_host_is_vm_active(void);

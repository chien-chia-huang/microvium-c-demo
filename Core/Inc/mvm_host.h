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
  MVM_HOST_FN_READ_BUTTON = 1, /* readButton() -> 0/1 */
  MVM_HOST_FN_SET_LED     = 2, /* setLed(state) -> undefined */
  MVM_HOST_FN_GET_TIME    = 3, /* getTime() -> milliseconds since boot */
} mvm_host_function_id_t;

/* Export IDs, resolved from C via mvm_resolveExports() to call into JS. */
typedef enum
{
  MVM_EXPORT_ON_TICK = 100, /* onTick(nowMs, buttonState) -> action code */
} mvm_export_id_t;

/*
 * Action codes returned by the JS onTick() export. The C main loop uses
 * these to decide whether (and how) to call setLed() -- see mvm_host_tick().
 */
typedef enum
{
  MVM_ACTION_NONE    = 0,
  MVM_ACTION_LED_ON  = 1,
  MVM_ACTION_LED_OFF = 2,
} mvm_action_t;

/* Restores the VM from the embedded agent bytecode. Call once at startup. */
void mvm_host_init(void);

/*
 * Debounces + reads B1, then calls the JS onTick(nowMs, buttonState) export
 * and applies whatever action it returns to the LED. Call this once per
 * APP_TICK_INTERVAL_MS from the main loop.
 */
void mvm_host_tick(uint32_t nowMs);

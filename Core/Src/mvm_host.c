/**
 * mvm_host.c
 *
 * Everything Microvium-specific lives here, away from main.c:
 *   - the three host functions (readButton/setLed/getTime) that JS can call
 *   - the raw-GPIO button debounce (a hardware concern, so it lives in C)
 *   - restoring the VM from the bytecode baked in by tools/gen_bytecode_header.sh
 *   - the per-tick call into the JS onTick() export
 *
 * All "how long should the LED stay on" logic is deliberately NOT here --
 * that lives in js/agent.mvm.js. This file only ever reads/writes GPIOs and
 * hands raw numbers across the VM boundary.
 */
#include "mvm_host.h"
#include "app_config.h"
#include "microvium.h"
#include "agent_bytecode.h"

#include <stddef.h>
#include <stdio.h>

static mvm_VM *s_vm = NULL;
static mvm_Value s_onTick;

/* ---- Raw GPIO access (the "C only reads/writes raw pins" boundary) ---- */

/* Returns the *electrical* level, unfiltered: GPIO_PIN_SET or GPIO_PIN_RESET.
 * Exposed (not just the debounced/inverted version below) so it can be
 * logged directly -- see the raw-transition print in mvm_host_tick(). */
static inline GPIO_PinState button_read_electrical(void)
{
  return HAL_GPIO_ReadPin(APP_BUTTON_GPIO_PORT, APP_BUTTON_GPIO_PIN);
}

static inline int button_read_raw(void)
{
  return (button_read_electrical() == APP_BUTTON_PRESSED_LEVEL) ? 1 : 0;
}

static void led_write_raw(int on)
{
  HAL_GPIO_WritePin(APP_LED_GPIO_PORT, APP_LED_GPIO_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/*
 * Simple debounce: only accept a new button state once we've seen
 * APP_BUTTON_DEBOUNCE_SAMPLES consecutive identical raw reads, each one
 * APP_TICK_INTERVAL_MS apart. This is a hardware signal-conditioning
 * concern, not app logic, so it stays in C.
 */
static int debounced_button_read(void)
{
  static int lastRaw = 0;
  static int stableState = 0;
  static uint32_t sameCount = 0;

  int raw = button_read_raw();
  if (raw == lastRaw)
  {
    if (sameCount < APP_BUTTON_DEBOUNCE_SAMPLES)
    {
      sameCount++;
    }
  }
  else
  {
    lastRaw = raw;
    sameCount = 1;
  }

  if (sameCount >= APP_BUTTON_DEBOUNCE_SAMPLES)
  {
    stableState = raw;
  }

  return stableState;
}

/* ---- Host functions exposed to JS via vmImport() ---- */

static mvm_TeError host_readButton(mvm_VM *vm, mvm_HostFunctionID id, mvm_Value *result, mvm_Value *args, uint8_t argCount)
{
  (void)vm;
  (void)id;
  (void)args;
  (void)argCount;
  *result = mvm_newBoolean(debounced_button_read() != 0);
  return MVM_E_SUCCESS;
}

static mvm_TeError host_setLed(mvm_VM *vm, mvm_HostFunctionID id, mvm_Value *result, mvm_Value *args, uint8_t argCount)
{
  (void)id;
  if (argCount >= 1)
  {
    led_write_raw(mvm_toBool(vm, args[0]) ? 1 : 0);
  }
  *result = mvm_undefined;
  return MVM_E_SUCCESS;
}

static mvm_TeError host_getTime(mvm_VM *vm, mvm_HostFunctionID id, mvm_Value *result, mvm_Value *args, uint8_t argCount)
{
  (void)id;
  (void)args;
  (void)argCount;
  *result = mvm_newNumber(vm, (MVM_FLOAT64)HAL_GetTick());
  return MVM_E_SUCCESS;
}

static mvm_TeError resolveImport(mvm_HostFunctionID hostFunctionID, void *context, mvm_TfHostFunction *out_hostFunction)
{
  (void)context;
  switch (hostFunctionID)
  {
    case MVM_HOST_FN_READ_BUTTON: *out_hostFunction = host_readButton; return MVM_E_SUCCESS;
    case MVM_HOST_FN_SET_LED:     *out_hostFunction = host_setLed;     return MVM_E_SUCCESS;
    case MVM_HOST_FN_GET_TIME:    *out_hostFunction = host_getTime;    return MVM_E_SUCCESS;
    default: return MVM_E_UNRESOLVED_IMPORT;
  }
}

void mvm_host_init(void)
{
  printf("mvm_host_init: restoring %u bytes of bytecode...\r\n", (unsigned)sizeof(agent_bytecode));

  mvm_TeError err = mvm_restore(&s_vm, (void *)agent_bytecode, sizeof(agent_bytecode), NULL, resolveImport);
  if (err != MVM_E_SUCCESS)
  {
    printf("mvm_restore failed: MVM_E_* = %d\r\n", (int)err);
    Error_Handler();
  }

  const mvm_VMExportID exportIds[] = { MVM_EXPORT_ON_TICK };
  mvm_Value exportValues[1];
  err = mvm_resolveExports(s_vm, exportIds, exportValues, 1);
  if (err != MVM_E_SUCCESS)
  {
    printf("mvm_resolveExports failed: MVM_E_* = %d (does agent.mvm.js still call vmExport(%d, onTick)?)\r\n",
           (int)err, (int)MVM_EXPORT_ON_TICK);
    Error_Handler();
  }
  s_onTick = exportValues[0];

  printf("mvm_host_init: VM ready, onTick exported OK.\r\n");
}

void mvm_host_tick(uint32_t nowMs)
{
  static int prevButtonState = -1; /* -1 = "not yet observed", forces one log line at boot */
  static GPIO_PinState prevRawLevel = (GPIO_PinState)-1; /* forces one log line at boot */
  static uint32_t lastHeartbeatMs = 0;

  /*
   * Logged BEFORE debounce and BEFORE the RESET/pressed-level inversion, so
   * this line reflects only what the pin itself is doing electrically. If
   * you press B1 and never see this line change at all, the problem is
   * upstream of all our code -- wrong pin, wrong port, or a hardware/wiring
   * issue -- since this is as close to the bare metal as we can observe.
   */
  GPIO_PinState rawLevel = button_read_electrical();
  if (rawLevel != prevRawLevel)
  {
    printf("[t=%lu] PC13 electrical level -> %s\r\n", (unsigned long)nowMs, rawLevel == GPIO_PIN_SET ? "HIGH" : "LOW");
    prevRawLevel = rawLevel;
  }

  int buttonState = debounced_button_read();
  if (buttonState != prevButtonState)
  {
    printf("[t=%lu] button %s\r\n", (unsigned long)nowMs, buttonState ? "PRESSED" : "released");
    prevButtonState = buttonState;
  }

  mvm_Value args[2];
  args[0] = mvm_newNumber(s_vm, (MVM_FLOAT64)nowMs);
  args[1] = mvm_newBoolean(buttonState != 0);

  mvm_Value result;
  mvm_TeError err = mvm_call(s_vm, s_onTick, &result, args, 2);
  if (err != MVM_E_SUCCESS)
  {
    printf("[t=%lu] mvm_call(onTick) failed: MVM_E_* = %d\r\n", (unsigned long)nowMs, (int)err);
    Error_Handler();
  }

  switch (mvm_toInt32(s_vm, result))
  {
    case MVM_ACTION_LED_ON:
      printf("[t=%lu] onTick -> ACTION_LED_ON, driving PB13 high\r\n", (unsigned long)nowMs);
      led_write_raw(1);
      break;
    case MVM_ACTION_LED_OFF:
      printf("[t=%lu] onTick -> ACTION_LED_OFF, driving PB13 low\r\n", (unsigned long)nowMs);
      led_write_raw(0);
      break;
    case MVM_ACTION_NONE:
    default:
      break;
  }

  /* Heartbeat so you can tell the loop is alive even when nothing else logs. */
  if ((uint32_t)(nowMs - lastHeartbeatMs) >= 1000u)
  {
    lastHeartbeatMs = nowMs;
    printf("[t=%lu] heartbeat: PC13=%s button=%d\r\n", (unsigned long)nowMs,
           rawLevel == GPIO_PIN_SET ? "HIGH" : "LOW", buttonState);
  }
}

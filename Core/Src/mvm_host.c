/**
 * mvm_host.c
 *
 * Everything Microvium-specific lives here, away from main.c:
 *   - the three host functions (readButton/setLed/getTime) that JS can call
 *   - the raw-GPIO button debounce (a hardware concern, so it lives in C)
 *   - the VM's whole lifecycle: created lazily on the first button press
 *     (mvm_restore from the bytecode baked in by tools/gen_bytecode_header.sh),
 *     driven one tick at a time while a press is being timed, and freed
 *     again (mvm_free) once onTick() reports the countdown has ended
 *   - the call into the JS onTick() export
 *
 * All "how long should the LED stay on" logic is deliberately NOT here --
 * that lives in js/agent.mvm.js. This file only ever reads/writes GPIOs and
 * hands raw numbers across the VM boundary.
 *
 * Two different directions of control cross that boundary here:
 *   - C calls JS: mvm_host_process() calls mvm_call(onTick) every tick.
 *   - JS calls C: onTick() calls setLed() directly (host_setLed() below)
 *     to actively drive the LED, rather than returning an on/off code for
 *     C to act on afterward. onTick()'s return value is used for exactly
 *     one thing C can't decide from inside JS's own call: whether to keep
 *     the VM alive or free it (see MVM_ACTION_DONE in mvm_host.h).
 *
 * Split into two entry points (mvm_host_poll_button / mvm_host_process) so
 * they can run in separate FreeRTOS tasks -- see Core/Src/app_tasks.c.
 */
#include "mvm_host.h"
#include "app_config.h"
#include "microvium.h"
#include "agent_bytecode.h"
#include "debug_uart.h"
#include "js_upload.h"

#include <stddef.h>

static mvm_VM *s_vm = NULL;
static mvm_Value s_onTick;

/*
 * Last debounced button state, as computed by mvm_host_poll_button() (the
 * GPIO task -- see Core/Src/app_tasks.c). host_readButton() below just
 * reads this rather than running its own debounce pass, so there's a
 * single owner of the debounce state machine even though this value is
 * read from a different task (the VM task, if/when JS calls readButton()).
 * A plain int read/write is atomic on Cortex-M, so no mutex is needed for
 * this single word.
 */
static volatile int s_lastButtonState = 0;

/*
 * Counts rising edges (release -> press), incremented once per press in
 * mvm_host_process() -- see the isRisingEdge check there. Read-only from
 * JS's side (host_getPressCount() below); this is the "C -> JS read"
 * counterpart to setLed()'s "JS -> C write".
 */
static uint32_t s_pressCount = 0;

/* ---- Raw GPIO access (the "C only reads/writes raw pins" boundary) ---- */

/* Returns the *electrical* level, unfiltered: GPIO_PIN_SET or GPIO_PIN_RESET.
 * Exposed (not just the debounced/inverted version below) so it can be
 * logged directly -- see the raw-transition print in mvm_host_poll_button(). */
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
  *result = mvm_newBoolean(s_lastButtonState != 0);
  return MVM_E_SUCCESS;
}

/*
 * JS calls this directly to turn the LED on/off -- see onTick() in
 * js/agent.mvm.js. This is the "reverse direction" from
 * mvm_host_process()'s own C->JS call: here JS is the one calling back
 * into C mid-onTick() to actively drive hardware, rather than C decoding
 * onTick()'s return value afterward.
 */
static mvm_TeError host_setLed(mvm_VM *vm, mvm_HostFunctionID id, mvm_Value *result, mvm_Value *args, uint8_t argCount)
{
  (void)id;
  if (argCount >= 1)
  {
    int on = mvm_toBool(vm, args[0]) ? 1 : 0;
    Debug_Printf("[JS task] setLed(%d) -- driving PB13 %s\r\n", on, on ? "high" : "low");
    led_write_raw(on);
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

/*
 * JS calls this to read a value C owns (s_pressCount) -- the reverse of
 * setLed()'s "JS writes into C". Logged here (not where s_pressCount is
 * incremented) so the log line reflects the moment JS actually asked for
 * it, not the moment the edge was detected.
 */
static mvm_TeError host_getPressCount(mvm_VM *vm, mvm_HostFunctionID id, mvm_Value *result, mvm_Value *args, uint8_t argCount)
{
  (void)id;
  (void)args;
  (void)argCount;
  Debug_Printf("[JS task] getPressCount() -> %lu\r\n", (unsigned long)s_pressCount);
  *result = mvm_newNumber(vm, (MVM_FLOAT64)s_pressCount);
  return MVM_E_SUCCESS;
}

static mvm_TeError resolveImport(mvm_HostFunctionID hostFunctionID, void *context, mvm_TfHostFunction *out_hostFunction)
{
  (void)context;
  switch (hostFunctionID)
  {
    case MVM_HOST_FN_READ_BUTTON:     *out_hostFunction = host_readButton;     return MVM_E_SUCCESS;
    case MVM_HOST_FN_SET_LED:         *out_hostFunction = host_setLed;         return MVM_E_SUCCESS;
    case MVM_HOST_FN_GET_TIME:        *out_hostFunction = host_getTime;        return MVM_E_SUCCESS;
    case MVM_HOST_FN_GET_PRESS_COUNT: *out_hostFunction = host_getPressCount; return MVM_E_SUCCESS;
    default: return MVM_E_UNRESOLVED_IMPORT;
  }
}

static void mvm_host_init(void)
{
  const uint8_t *bytecode = agent_bytecode;
  size_t bytecodeSize = sizeof(agent_bytecode);

  size_t uploadedSize;
  const uint8_t *uploaded = JsUpload_GetBytecode(&uploadedSize);
  if (uploaded != NULL)
  {
    bytecode = uploaded;
    bytecodeSize = uploadedSize;
    Debug_Printf("[JS task] using uploaded bytecode instead of the compiled-in default (see js_upload.h)\r\n");
  }

  Debug_Printf("[JS task] restoring %u bytes of bytecode...\r\n", (unsigned)bytecodeSize);

  mvm_TeError err = mvm_restore(&s_vm, (void *)bytecode, bytecodeSize, NULL, resolveImport);
  if (err != MVM_E_SUCCESS)
  {
    Debug_Printf("mvm_restore failed: MVM_E_* = %d\r\n", (int)err);
    Error_Handler();
  }

  const mvm_VMExportID exportIds[] = { MVM_EXPORT_ON_TICK };
  mvm_Value exportValues[1];
  err = mvm_resolveExports(s_vm, exportIds, exportValues, 1);
  if (err != MVM_E_SUCCESS)
  {
    Debug_Printf("mvm_resolveExports failed: MVM_E_* = %d (does agent.mvm.js still call vmExport(%d, onTick)?)\r\n",
           (int)err, (int)MVM_EXPORT_ON_TICK);
    Error_Handler();
  }
  s_onTick = exportValues[0];

  Debug_Printf("[JS task] VM ready, onTick exported OK.\r\n");
}

/*
 * Frees the current VM instance (mvm_free) -- the counterpart to
 * mvm_restore() in mvm_host_init(). After this, s_vm is NULL and
 * mvm_host_process() will create a fresh instance the next time it sees a
 * new button press.
 */
static void mvm_host_teardown(void)
{
  if (s_vm != NULL)
  {
    mvm_free(s_vm);
    s_vm = NULL;
  }
  /* Normally redundant -- onTick() already calls setLed(false) itself
   * before returning MVM_ACTION_DONE (see agent.mvm.js) -- but defensive
   * here too, since the next mvm_host_init() will run agent.mvm.js's
   * top-level code again from scratch, resetting its module state
   * (ledIsOn, pressStartMs, wasPressed) to "LED off" regardless. */
  led_write_raw(0);
}

int mvm_host_is_vm_active(void)
{
  return s_vm != NULL;
}

int mvm_host_poll_button(uint32_t nowMs)
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
    Debug_Printf("[t=%lu] PC13 electrical level -> %s\r\n", (unsigned long)nowMs, rawLevel == GPIO_PIN_SET ? "HIGH" : "LOW");
    prevRawLevel = rawLevel;
  }

  /*
   * B1 on this board settles through a real RC delay (a weak/high-value
   * external pull-up, plausibly paired with a hardware debounce cap) after
   * MX_GPIO_Init() switches PC13 from its reset-default analog state to a
   * digital input -- it can read an indeterminate/LOW level for a while
   * after that, not just microseconds. Confirmed on hardware: PC13 reads
   * LOW for 20ms+ right at scheduler start on every boot, with nobody
   * touching B1, which is long enough to clear the debounce below and get
   * latched as a false press. Ignore readings (without even feeding them
   * into debounced_button_read()'s state machine, so it starts learning
   * the real state from a clean slate once this window ends) until
   * comfortably past that settle time -- see APP_BUTTON_STARTUP_GRACE_MS.
   */
  if (nowMs < APP_BUTTON_STARTUP_GRACE_MS)
  {
    return 0;
  }

  int buttonState = debounced_button_read();
  s_lastButtonState = buttonState;
  if (buttonState != prevButtonState)
  {
    Debug_Printf("[t=%lu] button %s\r\n", (unsigned long)nowMs, buttonState ? "PRESSED" : "released");
    prevButtonState = buttonState;
  }

  /* Heartbeat so you can tell the loop is alive even when nothing else logs. */
  if ((uint32_t)(nowMs - lastHeartbeatMs) >= 1000u)
  {
    lastHeartbeatMs = nowMs;
    Debug_Printf("[t=%lu] heartbeat: PC13=%s button=%d\r\n", (unsigned long)nowMs,
           rawLevel == GPIO_PIN_SET ? "HIGH" : "LOW", buttonState);
  }

  return buttonState;
}

void mvm_host_process(uint32_t nowMs, int buttonState)
{
  static int lastPrintedSecondsRemaining = -1;
  /*
   * Tracks buttonState across calls so we can tell a genuine new press
   * (release -> press) apart from "the button is still held down from a
   * press whose VM already ended". Without this, holding B1 continuously
   * across the end of a countdown would look identical to a fresh press
   * the instant the VM is freed -- buttonState==1 and no VM running either
   * way -- and immediately restart forever for as long as it's held. This
   * is the same rising-edge bug (and fix) as in agent.mvm.js's onTick(),
   * just needed again here because the VM's entire lifetime, not just the
   * LED state, is now gated on press edges.
   */
  static int wasPressedForVmGate = 0;
  int isRisingEdge = buttonState && !wasPressedForVmGate;
  wasPressedForVmGate = buttonState;

  if (isRisingEdge)
  {
    s_pressCount++;
  }

  if (s_vm == NULL)
  {
    if (!isRisingEdge)
    {
      /* Either nothing pressed, or still held from an already-ended press
       * -- wait for an actual release-then-press before starting a VM. */
      return;
    }
    Debug_Printf("[JS task] button pressed -- starting Microvium VM\r\n");
    lastPrintedSecondsRemaining = -1;
    mvm_host_init();
  }

  mvm_Value args[2];
  args[0] = mvm_newNumber(s_vm, (MVM_FLOAT64)nowMs);
  args[1] = mvm_newBoolean(buttonState != 0);

  mvm_Value result;
  mvm_TeError err = mvm_call(s_vm, s_onTick, &result, args, 2);
  if (err != MVM_E_SUCCESS)
  {
    Debug_Printf("[t=%lu] mvm_call(onTick) failed: MVM_E_* = %d\r\n", (unsigned long)nowMs, (int)err);
    Error_Handler();
  }

  /*
   * The LED itself was already driven, if needed, by JS calling setLed()
   * directly during the mvm_call() above (see host_setLed()) -- nothing
   * left for C to decode there. The only thing this return value tells C
   * is whether to keep the VM alive or free it, since that's a decision
   * JS can't make for itself mid-call.
   */
  int32_t action = mvm_toInt32(s_vm, result);
  if (action >= MVM_ACTION_COUNTDOWN_BASE)
  {
    /* Still counting down. JS computed how many seconds remain (it's the
     * one that knows LIGHT_DURATION_MS); only print when that number
     * actually changes, so this shows up as one line per second rather
     * than one per 10ms tick. */
    int secondsRemaining = action - MVM_ACTION_COUNTDOWN_BASE;
    if (secondsRemaining != lastPrintedSecondsRemaining)
    {
      Debug_Printf("[JS task] countdown: %d s remaining\r\n", secondsRemaining);
      lastPrintedSecondsRemaining = secondsRemaining;
    }
    return;
  }

  /* action == MVM_ACTION_DONE */
  Debug_Printf("[JS task] countdown finished -- ending Microvium VM\r\n");
  mvm_host_teardown();
}

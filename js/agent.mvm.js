/**
 * agent.mvm.js
 *
 * All of the "how long should the LED stay on" logic lives here, not in C.
 * The C side (Core/Src/mvm_host.c) only ever reads the raw button GPIO,
 * writes the raw LED GPIO, and reports elapsed milliseconds -- this script
 * is what turns "button was pressed" into "light the LED for N seconds".
 *
 * Build step: tools/gen_bytecode_header.sh compiles this file with the
 * `microvium` CLI into build/agent_bytecode.h, which the firmware #includes.
 * Just edit this file and run `make` again; the bytecode regenerates
 * automatically (see the Makefile dependency on js/agent.mvm.js).
 */

// ---- Host function IDs (must match Core/Inc/mvm_host.h) ----
const ID_READ_BUTTON = 1;
const ID_SET_LED = 2;
const ID_GET_TIME = 3;
const ID_ON_TICK = 100;

/**
 * readButton()/getTime() are imported so this script *can* poll the
 * hardware on demand. The regular tick loop below doesn't call them because
 * onTick() already receives a fresh (nowMs, buttonState) pair as arguments
 * every 10ms (see mvm_host_tick() in C) -- that avoids a redundant
 * host-function round trip on the hot path. Keep them here for when you
 * extend this script to need an out-of-band reading (e.g. a long-press or
 * double-press feature).
 */
var readButton = vmImport(ID_READ_BUTTON);
var getTime = vmImport(ID_GET_TIME);
var setLed = vmImport(ID_SET_LED);

/**
 * How long the LED stays lit after a press, in milliseconds.
 * Change this one constant to adjust the "on" duration.
 */
const LIGHT_DURATION_MS = 3000;

/** Action codes returned by onTick(); interpreted by mvm_host_tick() in C. */
const ACTION_NONE = 0; // no change -- C won't call setLed()
const ACTION_LED_ON = 1; // C should call setLed(1)
const ACTION_LED_OFF = 2; // C should call setLed(0)

/**
 * ledIsOn:      mirrors what we last told C, so we only emit an action
 *               (and hence only call setLed()) on an actual state change.
 * pressStartMs: timestamp of the most recent rising edge (button went from
 *               released to pressed), or -1 if there hasn't been one yet.
 * wasPressed:   buttonState as of the previous tick, used to detect the
 *               rising edge (so a held-down button starts only one window,
 *               not a new one every tick).
 */
var ledIsOn = false;
var pressStartMs = -1;
var wasPressed = false;

/**
 * Exported to C (see vmExport below). Called every APP_TICK_INTERVAL_MS
 * (Core/Inc/app_config.h) with the current uptime and the debounced button
 * state (1 = held down, 0 = released).
 *
 * Returns one of the ACTION_* codes above.
 */
function onTick(nowMs, buttonState) {
  // Only start a new window on the actual rising edge (release -> press).
  // This is what makes a held-down button start exactly one window: the
  // LED goes off at LIGHT_DURATION_MS and stays off until an actual
  // release-then-press happens, even if the button is still held down.
  if (buttonState && !wasPressed) {
    pressStartMs = nowMs;
  }
  wasPressed = buttonState;

  var shouldBeOn = pressStartMs >= 0 && (nowMs - pressStartMs) < LIGHT_DURATION_MS;

  if (shouldBeOn && !ledIsOn) {
    ledIsOn = true;
    return ACTION_LED_ON;
  }
  if (!shouldBeOn && ledIsOn) {
    ledIsOn = false;
    return ACTION_LED_OFF;
  }
  return ACTION_NONE;
}

vmExport(ID_ON_TICK, onTick);

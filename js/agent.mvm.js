/**
 * agent.mvm.js
 *
 * All of the "how long should the LED stay on" logic lives here, not in C.
 * The C side (Core/Src/mvm_host.c) only ever reads the raw button GPIO,
 * writes the raw LED GPIO (when this script tells it to, via setLed()),
 * and reports elapsed milliseconds -- this script is what turns "button
 * was pressed" into "light the LED for N seconds".
 *
 * This script actively controls the LED itself, by calling setLed()
 * directly -- it doesn't return an on/off code for C to act on afterward.
 * onTick()'s return value is used for only one thing: telling C whether to
 * keep this VM alive or free it, since that's a decision this script can't
 * make for itself mid-call (see ACTION_DONE below).
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
const ID_GET_PRESS_COUNT = 4;
const ID_ON_TICK = 100;

/**
 * readButton()/getTime() are imported so this script *can* poll the
 * hardware on demand. onTick() below doesn't call them because it already
 * receives a fresh (nowMs, buttonState) pair as arguments every 10ms (see
 * mvm_host_process() in C) -- that avoids a redundant host-function round
 * trip on the hot path. Keep them here for when you extend this script to
 * need an out-of-band reading (e.g. a long-press or double-press feature).
 *
 * getPressCount() is a plain "read a value C owns" import -- C increments
 * s_pressCount on every rising edge (mvm_host.c), and this script just asks
 * for the current value. It's called once below purely to demonstrate the
 * C -> JS read direction (the reverse of setLed()'s JS -> C write); nothing
 * in the LED logic depends on it.
 */
var readButton = vmImport(ID_READ_BUTTON);
var getTime = vmImport(ID_GET_TIME);
var setLed = vmImport(ID_SET_LED);
var getPressCount = vmImport(ID_GET_PRESS_COUNT);

/**
 * How long the LED stays lit after a press, in milliseconds.
 * Change this one constant to adjust the "on" duration.
 */
const LIGHT_DURATION_MS = 2000;

/**
 * Action codes returned by onTick(); interpreted by mvm_host_process() in
 * C. These say nothing about the LED (this script already handled that
 * itself via setLed() by the time it returns) -- only whether the VM
 * should keep running or be freed. Must match Core/Inc/mvm_host.h.
 *
 *   ACTION_DONE                    -- countdown finished (setLed(false)
 *                                      already called); C should free the VM.
 *   ACTION_COUNTDOWN_BASE + N      -- still counting down, N seconds
 *                                      remaining. Purely informational, for
 *                                      C's console log -- C takes no
 *                                      hardware action for this range.
 */
const ACTION_DONE = 0;
const ACTION_COUNTDOWN_BASE = 100;

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
 * state (1 = held down, 0 = released). C only calls this while the VM
 * exists -- it creates the VM fresh on the first press and frees it again
 * once this returns ACTION_DONE, so this function effectively only ever
 * runs during an active countdown (plus the one tick that starts it).
 *
 * Drives the LED itself (setLed()); returns ACTION_DONE or
 * ACTION_COUNTDOWN_BASE + N -- see the comment on those constants above.
 */
function onTick(nowMs, buttonState) {
  // Only start a new window on the actual rising edge (release -> press).
  // This is what makes a held-down button start exactly one window: the
  // LED goes off at LIGHT_DURATION_MS and stays off until an actual
  // release-then-press happens, even if the button is still held down.
  if (buttonState && !wasPressed) {
    pressStartMs = nowMs;
    // Reads a value C owns (the reverse of setLed()'s "JS writes into C").
    // Nothing computed from it yet -- see host_getPressCount() in
    // mvm_host.c for the log line this produces.
    getPressCount();
  }
  wasPressed = buttonState;

  var shouldBeOn = pressStartMs >= 0 && (nowMs - pressStartMs) < LIGHT_DURATION_MS;

  if (shouldBeOn && !ledIsOn) {
    ledIsOn = true;
    setLed(true);
  }
  if (!shouldBeOn && ledIsOn) {
    ledIsOn = false;
    setLed(false);
    return ACTION_DONE;
  }
  if (shouldBeOn) {
    // Integer seconds remaining, using `| 0` to truncate instead of
    // Math.floor (keeps this working even on engines/subsets without Math).
    var msRemaining = LIGHT_DURATION_MS - (nowMs - pressStartMs);
    var secondsRemaining = (msRemaining / 1000) | 0;
    return ACTION_COUNTDOWN_BASE + secondsRemaining;
  }
  return ACTION_DONE;
}

vmExport(ID_ON_TICK, onTick);

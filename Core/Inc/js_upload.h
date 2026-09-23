/**
 * js_upload.h
 *
 * Lets a new agent.mvm.js bytecode image be sent over the same USART2
 * console (see debug_uart.h) instead of reflashing the whole firmware --
 * for quickly iterating on the JS without a full `make flash` each time.
 *
 * Wire format, read one byte at a time by JsUpload_Poll():
 *   [0]     0xAA               (magic byte 0)
 *   [1]     0x55               (magic byte 1)
 *   [2..3]  length, uint16 little-endian (1..JS_UPLOAD_MAX_SIZE)
 *   [4..]   `length` bytes of raw Microvium bytecode
 *   [last]  checksum: 8-bit sum of all payload bytes
 *
 * See tools/send_bytecode.py for the host-side sender, which compiles
 * agent.mvm.js the same way tools/gen_bytecode_header.sh does and frames it
 * exactly as above.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define JS_UPLOAD_MAX_SIZE 2048u

/* Drains any bytes currently waiting on the serial console and feeds them
 * through the framing state machine above. Call once per tick from the
 * GPIO task (see app_tasks.c) -- cheap no-op when nothing has arrived. */
void JsUpload_Poll(void);

/*
 * Returns the most recently *committed* upload (a complete, checksum-valid
 * image received while no VM was running -- see js_upload.c for why a VM
 * being active defers/rejects the commit), or NULL if none has been
 * received since boot. mvm_host_init() (mvm_host.c) calls this once, right
 * before restoring a VM, to decide between this and the compiled-in
 * agent_bytecode[] default.
 */
const uint8_t *JsUpload_GetBytecode(size_t *out_size);

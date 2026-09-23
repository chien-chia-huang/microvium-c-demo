/**
 * js_upload.c -- see js_upload.h for the wire format.
 */
#include "js_upload.h"
#include "debug_uart.h"
#include "mvm_host.h"

#include <string.h>

typedef enum
{
  STATE_MAGIC0,
  STATE_MAGIC1,
  STATE_LEN_LO,
  STATE_LEN_HI,
  STATE_PAYLOAD,
  STATE_CHECKSUM,
} upload_state_t;

static upload_state_t s_state = STATE_MAGIC0;
static uint16_t s_expectedLen = 0;
static uint16_t s_received = 0;
static uint8_t s_checksum = 0;
/* Staging buffer: filled while a transfer is in progress. Only copied into
 * s_activeBuf once the checksum validates, so a transfer that's aborted or
 * corrupted midway never disturbs whatever was already committed. */
static uint8_t s_stagingBuf[JS_UPLOAD_MAX_SIZE];

static uint8_t s_activeBuf[JS_UPLOAD_MAX_SIZE];
static size_t s_activeSize = 0;
static int s_hasUpload = 0;

static void handle_byte(uint8_t b)
{
  switch (s_state)
  {
    case STATE_MAGIC0:
      s_state = (b == 0xAA) ? STATE_MAGIC1 : STATE_MAGIC0;
      return;

    case STATE_MAGIC1:
      s_state = (b == 0x55) ? STATE_LEN_LO : STATE_MAGIC0;
      return;

    case STATE_LEN_LO:
      s_expectedLen = b;
      s_state = STATE_LEN_HI;
      return;

    case STATE_LEN_HI:
      s_expectedLen = (uint16_t)(s_expectedLen | ((uint16_t)b << 8));
      if (s_expectedLen == 0 || s_expectedLen > JS_UPLOAD_MAX_SIZE)
      {
        Debug_Printf("[upload] bad length %u (max %u), aborting\r\n", s_expectedLen, JS_UPLOAD_MAX_SIZE);
        s_state = STATE_MAGIC0;
      }
      else
      {
        s_received = 0;
        s_checksum = 0;
        s_state = STATE_PAYLOAD;
      }
      return;

    case STATE_PAYLOAD:
      s_stagingBuf[s_received++] = b;
      s_checksum = (uint8_t)(s_checksum + b);
      if (s_received == s_expectedLen)
      {
        s_state = STATE_CHECKSUM;
      }
      return;

    case STATE_CHECKSUM:
      if (b != s_checksum)
      {
        Debug_Printf("[upload] checksum mismatch (got 0x%02x, expected 0x%02x) -- discarding %u bytes\r\n",
                     b, s_checksum, s_expectedLen);
      }
      else if (mvm_host_is_vm_active())
      {
        Debug_Printf("[upload] VM currently active -- ignoring this upload, retry once the countdown ends\r\n");
      }
      else
      {
        memcpy(s_activeBuf, s_stagingBuf, s_expectedLen);
        s_activeSize = s_expectedLen;
        s_hasUpload = 1;
        Debug_Printf("[upload] received new bytecode: %u bytes -- will be used on the next button press\r\n",
                     (unsigned)s_expectedLen);
      }
      s_state = STATE_MAGIC0;
      return;
  }
}

void JsUpload_Poll(void)
{
  uint8_t b;
  while (Debug_UART_TryReadByte(&b))
  {
    handle_byte(b);
  }
}

const uint8_t *JsUpload_GetBytecode(size_t *out_size)
{
  if (!s_hasUpload)
  {
    return NULL;
  }
  *out_size = s_activeSize;
  return s_activeBuf;
}

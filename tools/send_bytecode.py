#!/usr/bin/env python3
"""
send_bytecode.py -- compiles agent.mvm.js and sends it to the running
firmware over the serial console, framed per Core/Inc/js_upload.h, instead
of reflashing the whole firmware.

Usage:
    tools/send_bytecode.py <serial-port> [input.mvm.js]

Only depends on the standard library (termios/tty -- macOS/Linux only,
matching this project's other serial-console tooling; Windows users need a
different serial layer, e.g. pyserial, to send the same framed bytes).
"""
import sys
import subprocess
import os
import termios

MAGIC = bytes([0xAA, 0x55])
MAX_SIZE = 2048  # must match JS_UPLOAD_MAX_SIZE in Core/Inc/js_upload.h

# Left behind on disk (not a tempdir that deletes itself) so you can inspect
# exactly what got compiled/sent -- e.g. `xxd build/agent_upload.mvm`.
OUT_PATH = "build/agent_upload.mvm"


def compile_bytecode(input_path: str) -> bytes:
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    result = subprocess.run(
        ["npx", "--no-install", "microvium", input_path, "--output-bytes", "-s", OUT_PATH],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        sys.exit(f"error: microvium compile of {input_path} failed")
    with open(OUT_PATH, "rb") as f:
        return f.read()


def open_serial(port: str):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    # Raw mode, 115200 8N1, no flow control -- matches debug_uart.c.
    attrs[0] = 0  # iflag
    attrs[1] = 0  # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0  # lflag
    attrs[4] = termios.B115200  # ispeed
    attrs[5] = termios.B115200  # ospeed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def main():
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        sys.exit(f"usage: {sys.argv[0]} <serial-port> [input.mvm.js]")
    port = sys.argv[1]
    input_path = sys.argv[2] if len(sys.argv) == 3 else "js/agent.mvm.js"

    if "/tty." in port:
        cu_port = port.replace("/tty.", "/cu.")
        print(f"Note: on macOS, /dev/cu.* (not /dev/tty.*) is the device node meant "
              f"for one-shot outgoing writes like this. If sending doesn't work, "
              f"try: {cu_port}", file=sys.stderr)

    bytecode = compile_bytecode(input_path)
    if len(bytecode) == 0 or len(bytecode) > MAX_SIZE:
        sys.exit(f"error: bytecode is {len(bytecode)} bytes, must be 1..{MAX_SIZE} "
                  f"(see JS_UPLOAD_MAX_SIZE in Core/Inc/js_upload.h)")

    length_bytes = bytes([len(bytecode) & 0xFF, (len(bytecode) >> 8) & 0xFF])
    checksum = bytes([sum(bytecode) & 0xFF])
    frame = MAGIC + length_bytes + bytecode + checksum

    fd = open_serial(port)
    try:
        os.write(fd, frame)
        # Without this, close() can happen before the kernel has actually
        # pushed all the bytes out over the USB-CDC bridge, silently
        # truncating or dropping the transfer entirely.
        termios.tcdrain(fd)
    finally:
        os.close(fd)

    print(f"Compiled bytecode saved to {OUT_PATH} ({len(bytecode)} bytes) -- inspect it with e.g. `xxd {OUT_PATH}`.")
    print(f"Sent {len(bytecode)} bytes of bytecode from {input_path} to {port}.")
    print("Watch the serial console for '[upload] received new bytecode: ...' "
          "-- it takes effect on the next button press.")


if __name__ == "__main__":
    main()

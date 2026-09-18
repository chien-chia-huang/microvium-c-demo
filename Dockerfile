# ARM Cortex-M bare-metal build toolchain (Ubuntu 22.04).
#
# This image is deliberately generic: it only installs the cross-compiler
# and build tools, not anything specific to this project (no source files,
# no board headers, no linker scripts). That's what makes it reusable for
# *any* Cortex-M target, not just this one -- arm-none-eabi-gcc is a
# bare-metal cross-compiler for the whole ARM architecture (Cortex-M0 up
# through Cortex-M7/M33, and beyond), selected per-project via -mcpu/-mfpu/
# -mfloat-abi flags in that project's own Makefile. Everything that's
# actually chip/board-specific -- CMSIS device headers, HAL/LL drivers, the
# linker script, the startup file -- lives in each project's own repo (as
# it does in this one, under Drivers/, startup/, linker/), not in this
# image. So: same image, any Cortex-M project, as long as that project
# brings its own vendor files and points its own Makefile at the right
# -mcpu.
#
# Build:  docker build -t arm-cortex-m-toolchain:22.04 .
# Use:    docker run --rm -v "$PWD":/work -w /work arm-cortex-m-toolchain:22.04 \
#           bash -c "npm install && make"
#
# Node.js is included because *this* project's build additionally needs the
# `microvium` CLI (see tools/gen_bytecode_header.sh) to compile agent.mvm.js
# to bytecode -- that part is project-specific, not part of the generic
# Cortex-M toolchain. A pure-C Cortex-M project wouldn't need it, but
# leaving it in doesn't hurt reusability for projects that do.
#
# Flashing is intentionally NOT done from inside this container -- it needs
# direct USB access to the ST-LINK, which is awkward to pass through to a
# Linux container from macOS/Windows Docker Desktop. Build in the
# container, flash from the host (see README.md "Flashing").

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    libnewlib-arm-none-eabi \
    make \
    git \
    ca-certificates \
    curl \
  && rm -rf /var/lib/apt/lists/*

# Node.js 20.x LTS -- only needed by projects (like this one) whose build
# also compiles JavaScript via the `microvium` CLI. See note above.
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
  && apt-get install -y --no-install-recommends nodejs \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["bash"]

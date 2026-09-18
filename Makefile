#
# Makefile for the STM32L433RC + Microvium button/LED demo.
#
# Targets:
#   make               - build build/firmware.elf/.bin/.hex (needs arm-none-eabi-gcc + node/npm on your PATH)
#   make docker-build  - same build, inside the Docker image (see Dockerfile) -- no local toolchain needed
#   make docker-shell  - interactive shell in that same container, project mounted at /work
#   make flash         - build then flash via STM32_Programmer_CLI (verified working on macOS)
#   make flash-stlink  - build then flash via st-flash (alternative; may need sudo on macOS)
#   make flash-openocd - build then flash via openocd (alternative)
#   make size          - print flash/RAM usage summary
#   make clean         - remove the build/ directory
#
# See README.md for the full build/flash walkthrough and how the
# agent.mvm.js -> agent_bytecode.h step fits into this.
#

TARGET     := firmware
BUILD_DIR  := build

PREFIX     := arm-none-eabi-
CC         := $(PREFIX)gcc
OBJCOPY    := $(PREFIX)objcopy
SIZE       := $(PREFIX)size

# --- Microvium JS -> bytecode step ---
MICROVIUM_JS      := js/agent.mvm.js
AGENT_BYTECODE_H  := $(BUILD_DIR)/agent_bytecode.h
GEN_BYTECODE      := tools/gen_bytecode_header.sh

# --- Toolchain flags ---
MCU_FLAGS  := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

DEFS       := -DSTM32L433xx -DUSE_HAL_DRIVER

INCLUDES   := \
  -ICore/Inc \
  -IDrivers/CMSIS/Include \
  -IDrivers/CMSIS/Device/ST/STM32L4xx/Include \
  -IDrivers/STM32L4xx_HAL_Driver/Inc \
  -Ithird_party/microvium \
  -I$(BUILD_DIR)

CFLAGS     := $(MCU_FLAGS) $(DEFS) $(INCLUDES) \
              -Wall -Wextra -ffunction-sections -fdata-sections -fno-common \
              -Og -g3 -std=gnu11 -MMD -MP

ASFLAGS    := $(MCU_FLAGS) -x assembler-with-cpp

LDFLAGS    := $(MCU_FLAGS) -specs=nano.specs \
              -Tlinker/STM32L433RCTX_FLASH.ld \
              -Wl,-Map=$(BUILD_DIR)/$(TARGET).map -Wl,--gc-sections \
              -lc -lm

# --- Sources ---
C_SOURCES := \
  Core/Src/main.c \
  Core/Src/stm32l4xx_it.c \
  Core/Src/stm32l4xx_hal_msp.c \
  Core/Src/mvm_host.c \
  Core/Src/debug_uart.c \
  Core/Src/syscalls.c \
  Core/Src/sysmem.c \
  Drivers/CMSIS/Device/ST/STM32L4xx/Source/Templates/system_stm32l4xx.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_rcc_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_gpio.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_cortex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_pwr_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ex.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_flash_ramfunc.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_dma.c \
  Drivers/STM32L4xx_HAL_Driver/Src/stm32l4xx_hal_uart.c \
  third_party/microvium/microvium.c

ASM_SOURCES := startup/startup_stm32l433xx.s

OBJECTS := $(C_SOURCES:%.c=$(BUILD_DIR)/%.o) $(ASM_SOURCES:%.s=$(BUILD_DIR)/%.o)
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all flash flash-stlink flash-openocd size clean docker-image docker-build docker-shell

all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/$(TARGET).hex size

# --- JS bytecode generation (prerequisite for the firmware build) ---
$(AGENT_BYTECODE_H): $(MICROVIUM_JS) $(GEN_BYTECODE)
	@mkdir -p $(dir $@)
	$(GEN_BYTECODE) $(MICROVIUM_JS) $@ agent_bytecode

# --- Compile rules ---
$(BUILD_DIR)/%.o: %.c $(AGENT_BYTECODE_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

# --- Flashing ---
# STM32_Programmer_CLI (from STM32CubeProgrammer) uses ST's own signed
# ST-LINK driver rather than raw libusb, so unlike st-flash/openocd it works
# on macOS without needing sudo or hitting the libusb kernel-driver-detach
# permission error. This is the flashing path actually verified to work.
flash: $(BUILD_DIR)/$(TARGET).bin
	STM32_Programmer_CLI --connect port=SWD --write $< 0x08000000 --start

# st-flash alternative: talks to the ST-LINK directly, no config file
# needed, but on macOS may need `sudo env "PATH=$$PATH" make flash-stlink`
# to get past a libusb kernel-driver permission error.
flash-stlink: $(BUILD_DIR)/$(TARGET).bin
	st-flash write $< 0x8000000

# openocd alternative, in case neither of the above is available.
flash-openocd: $(BUILD_DIR)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32l4x.cfg \
	  -c "program $< verify reset exit"

# --- Docker build (see Dockerfile) ---
DOCKER_IMAGE := arm-cortex-m-toolchain:22.04

docker-image:
	docker build -t $(DOCKER_IMAGE) .

# node_modules gets its own volume (not bind-mounted from the host) because
# the `microvium` package has a native addon: a copy built on the host
# (e.g. macOS) can't load inside the Linux container, and vice versa. This
# keeps a separate, container-native node_modules that persists across runs.
docker-build: docker-image
	docker run --rm \
	  -v "$(CURDIR)":/work \
	  -v microvium_node_modules:/work/node_modules \
	  -w /work \
	  $(DOCKER_IMAGE) \
	  bash -c "npm install && make"

docker-shell: docker-image
	docker run --rm -it \
	  -v "$(CURDIR)":/work \
	  -v microvium_node_modules:/work/node_modules \
	  -w /work \
	  $(DOCKER_IMAGE) \
	  bash

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)

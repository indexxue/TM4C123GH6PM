#===============================================================================
#  TM4C123GH6PM 裸机项目 Makefile
#
#  用法:
#    make              — 编译（自动检测工具链，未安装时提示）
#    make clean        — 清理
#    make install-toolchain — 安装 ARM GNU Toolchain
#    make flash        — 烧录（需 UniFlash + .ccxml）
#===============================================================================

PROJECT  ?= tm4c123-project
MCU      ?= TM4C123GH6PM

# --- 工具链（优先查找 PATH，也可在此硬编码）-----------------------------------
PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size
OBJDUMP  := $(PREFIX)objdump

# --- 目录 --------------------------------------------------------------------
ROOT       := $(CURDIR)
BUILD_DIR  := $(ROOT)/build
SRC_DIR    := $(ROOT)/src
INC_DIR    := $(ROOT)/include
LD_DIR     := $(ROOT)/ld
SCRIPT_DIR := $(ROOT)/scripts

LD_SCRIPT  := $(LD_DIR)/tm4c123gh6pm.ld

# --- CPU 标志 ----------------------------------------------------------------
CPU_FLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
DEFINES    := -D$(MCU) -DPART_TM4C123GH6PM
INCLUDES   := -I$(INC_DIR)

CFLAGS     := $(CPU_FLAGS) $(DEFINES) $(INCLUDES) \
              -std=c11 -Wall -Wextra -Wpedantic \
              -ffunction-sections -fdata-sections \
              -Os -g3

LDFLAGS    := $(CPU_FLAGS) \
              -T$(LD_SCRIPT) \
              -Wl,--gc-sections \
              -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map \
              -nostartfiles \
              -specs=nosys.specs

# --- 源文件 ------------------------------------------------------------------
SOURCES    := $(SRC_DIR)/startup_tm4c123gh6pm.c \
              $(SRC_DIR)/main.c \
              $(SRC_DIR)/syscalls.c \
              $(SRC_DIR)/systick.c
OBJECTS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

# --- 目标 --------------------------------------------------------------------
.PHONY: all clean flash rebuild install-toolchain

all: install-toolchain $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin
	$(SIZE) $(BUILD_DIR)/$(PROJECT).elf
	$(OBJDUMP) -h $(BUILD_DIR)/$(PROJECT).elf

# 自动安装工具链（幂等，已装则跳过）
install-toolchain:
	@$(CC) --version >NUL 2>NUL || ( \
		echo "ARM GNU Toolchain not found. Installing..." && \
		powershell -ExecutionPolicy Bypass -File "$(SCRIPT_DIR)\install-toolchain.ps1" \
	)

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

# --- 辅助 --------------------------------------------------------------------
rebuild: clean all

clean:
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"

flash: $(BUILD_DIR)/$(PROJECT).bin
	powershell -ExecutionPolicy Bypass -File $(SCRIPT_DIR)\flash-uniflash.ps1 -Image "$(BUILD_DIR)\$(PROJECT).bin"

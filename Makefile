#===============================================================================
#  TM4C123GH6PM 裸机项目 Makefile
#  用法: make            (编译)
#        make clean      (清理)
#        make flash       (烧录，需 UniFlash + .ccxml)
#===============================================================================

PROJECT  ?= tm4c123-project
MCU      ?= TM4C123GH6PM

# --- SDK 路径 (按实际位置修改或通过环境变量覆盖) --------------------------------
SDK_ROOT ?= D:/Ti/tm4c123gh6pm-sdk

# --- 工具链 ------------------------------------------------------------------
PREFIX   := $(SDK_ROOT)/tools/bin/arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size
OBJDUMP  := $(PREFIX)objdump

# --- 目录 --------------------------------------------------------------------
ROOT       := $(CURDIR)
BUILD_DIR  := $(ROOT)/build
SRC_DIR    := $(ROOT)/src
INC_DIR    := $(ROOT)/include
SCRIPT_DIR := $(ROOT)/scripts

# --- 来自 SDK 的外部文件 -----------------------------------------------------
STARTUP    := $(SDK_ROOT)/src/startup_tm4c123gh6pm.c
LD_SCRIPT  := $(SDK_ROOT)/ld/tm4c123gh6pm.ld
SDK_INC    := $(SDK_ROOT)/include

# --- CPU 标志 ----------------------------------------------------------------
CPU_FLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
DEFINES    := -D$(MCU) -DPART_TM4C123GH6PM
INCLUDES   := -I$(INC_DIR) -I$(SDK_INC)

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
SOURCES    := $(SRC_DIR)/main.c $(SRC_DIR)/syscalls.c $(SRC_DIR)/systick.c
OBJECTS    := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))
# 启动文件单独编译
STARTUP_OBJ := $(BUILD_DIR)/startup_tm4c123gh6pm.o

# --- 目标 --------------------------------------------------------------------
.PHONY: all clean flash rebuild check-toolchain

all: check-toolchain $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin
	$(SIZE) $(BUILD_DIR)/$(PROJECT).elf
	$(OBJDUMP) -h $(BUILD_DIR)/$(PROJECT).elf

check-toolchain:
	@$(CC) --version >NUL 2>NUL || (echo arm-none-eabi-gcc not found. Check SDK path: $(SDK_ROOT) && exit 1)

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

# 启动对象 (来自 SDK)
$(STARTUP_OBJ): $(STARTUP) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 项目源文件
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 链接
$(BUILD_DIR)/$(PROJECT).elf: $(STARTUP_OBJ) $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

# --- 辅助 --------------------------------------------------------------------
rebuild: clean all

clean:
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"

flash: $(BUILD_DIR)/$(PROJECT).bin
	powershell -ExecutionPolicy Bypass -File $(SCRIPT_DIR)\flash-uniflash.ps1 -Image "$(BUILD_DIR)\$(PROJECT).bin"

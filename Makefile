PROJECT  ?= tm4c123-project
MCU      ?= TM4C123GH6PM
PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size
OBJDUMP  := $(PREFIX)objdump

ROOT       := $(CURDIR)
BUILD_DIR  := $(ROOT)/build
SRC_DIR    := $(ROOT)/src
INC_DIR    := $(ROOT)/include
LD_DIR     := $(ROOT)/ld
SCRIPT_DIR := $(ROOT)/scripts

LD_SCRIPT  := $(LD_DIR)/tm4c123gh6pm.ld

# --- TivaWare (optional) ---
TIVAWARE_ROOT ?= D:/Ti/TivaWare_C_Series-2.2.0.295
ifneq ($(wildcard $(SRC_DIR)/generated/ti_drivers_config.c),)
    CFLAGS  += -I$(TIVAWARE_ROOT)/inc
    LDFLAGS += -L$(TIVAWARE_ROOT)/driverlib/gcc -ldriver -lc -lgcc
    SOURCES += $(SRC_DIR)/generated/ti_drivers_config.c
endif

CPU_FLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=soft
DEFINES    := -D$(MCU) -DPART_TM4C123GH6PM
INCLUDES   := -I$(INC_DIR) -I$(SRC_DIR)/generated

CFLAGS += $(CPU_FLAGS) $(DEFINES) $(INCLUDES) \
          -std=c11 -Wall -Wextra -Wpedantic \
          -ffunction-sections -fdata-sections -Os -g3

LDFLAGS += $(CPU_FLAGS) -T$(LD_SCRIPT) \
           -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map \
           -nostartfiles -specs=nosys.specs

SOURCES += $(SRC_DIR)/startup_tm4c123gh6pm.c \
           $(SRC_DIR)/main.c \
           $(SRC_DIR)/syscalls.c \
           $(SRC_DIR)/systick.c

OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all clean flash rebuild install-toolchain install-sysconfig

all: install-toolchain $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin
	$(SIZE) $(BUILD_DIR)/$(PROJECT).elf

install-toolchain:
	@$(CC) --version >NUL 2>NUL || ( \
		echo "ARM GCC not found. Installing..." && \
		powershell -ExecutionPolicy Bypass -File "$(SCRIPT_DIR)\install-toolchain.ps1")

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

rebuild: clean all

clean:
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"

flash: $(BUILD_DIR)/$(PROJECT).bin
	powershell -ExecutionPolicy Bypass -File $(SCRIPT_DIR)\flash-uniflash.ps1 -Image "$(BUILD_DIR)\$(PROJECT).bin"
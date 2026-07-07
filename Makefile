PROJECT  ?= tm4c123-project
MCU      ?= TM4C123GH6PM
PREFIX   ?= arm-none-eabi-
CC       := $(PREFIX)gcc
OBJCOPY  := $(PREFIX)objcopy
SIZE     := $(PREFIX)size

ROOT       := $(CURDIR)
BUILD_DIR  := $(ROOT)/build
SRC_DIR    := $(ROOT)/src
INC_DIR    := $(ROOT)/include
COMMON_INC := $(ROOT)/Common/inc
COMMON_SRC := $(ROOT)/Common/src
CBB_WS2812 := $(ROOT)/cbb/ws2812b
LD_DIR     := $(ROOT)/ld
SCRIPT_DIR := $(ROOT)/scripts

LD_SCRIPT  := $(LD_DIR)/tm4c123gh6pm.ld

TIVAWARE_ROOT ?= $(ROOT)/sdk/TivaWare_C_Series-2.2.0.295
FREERTOS_ROOT := $(TIVAWARE_ROOT)/third_party/FreeRTOS/Source
FREERTOS_PORT := $(FREERTOS_ROOT)/portable/GCC/ARM_CM4F

CPU_FLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
DEFINES    := -D$(MCU) -DPART_TM4C123GH6PM
INCLUDES   := -I$(INC_DIR) -I$(COMMON_INC) -I$(CBB_WS2812) \
              -I$(FREERTOS_ROOT)/include -I$(FREERTOS_PORT) \
              -I$(TIVAWARE_ROOT) -I$(TIVAWARE_ROOT)/inc

CFLAGS += $(CPU_FLAGS) $(DEFINES) $(INCLUDES) \
          -std=c11 -Wall -Wextra -Wpedantic \
          -ffunction-sections -fdata-sections -Os -g3

LDFLAGS += $(CPU_FLAGS) -T$(LD_SCRIPT) \
           -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map \
           -nostartfiles -specs=nosys.specs \
           -L$(TIVAWARE_ROOT)/driverlib/gcc -ldriver -lc -lgcc

APP_SOURCES := $(SRC_DIR)/startup_tm4c123gh6pm.c \
               $(SRC_DIR)/main.c \
               $(SRC_DIR)/init.c \
               $(SRC_DIR)/app.c \
               $(SRC_DIR)/freertos_hooks.c \
               $(SRC_DIR)/syscalls.c \
               $(COMMON_SRC)/pinout.c \
               $(COMMON_SRC)/peripheral.c \
               $(COMMON_SRC)/type.c \
               $(COMMON_SRC)/log.c \
               $(COMMON_SRC)/cmd.c \
               $(COMMON_SRC)/battery.c \
               $(COMMON_SRC)/button.c \
               $(COMMON_SRC)/flexible_button.c \
               $(COMMON_SRC)/led_scene.c \
               $(CBB_WS2812)/ws2812b.c

RTOS_SOURCES := $(FREERTOS_ROOT)/tasks.c \
                $(FREERTOS_ROOT)/queue.c \
                $(FREERTOS_ROOT)/list.c \
                $(FREERTOS_PORT)/port.c \
                $(FREERTOS_ROOT)/portable/MemMang/heap_4.c

SOURCES := $(APP_SOURCES) $(RTOS_SOURCES)

OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(SOURCES)))
VPATH := $(SRC_DIR) $(COMMON_SRC) $(CBB_WS2812) $(FREERTOS_ROOT) $(FREERTOS_PORT) $(FREERTOS_ROOT)/portable/MemMang

.PHONY: all clean rebuild install-toolchain

all: install-toolchain $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin
	$(SIZE) $(BUILD_DIR)/$(PROJECT).elf

install-toolchain:
	@$(CC) --version >NUL 2>NUL || ( \
		echo "ARM GCC not found. Installing..." && \
		powershell -ExecutionPolicy Bypass -File "$(SCRIPT_DIR)\install-toolchain.ps1")

$(BUILD_DIR):
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $@

$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	$(OBJCOPY) -O binary $< $@

rebuild: clean all

clean:
	@if exist "$(BUILD_DIR)" rmdir /S /Q "$(BUILD_DIR)"

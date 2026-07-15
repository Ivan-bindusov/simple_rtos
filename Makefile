######################################
# 1. ВЫБОР ЦЕЛЕВОГО МИКРОКОНТРОЛЛЕРА
# Измените на stm32f411, чтобы переключить сборку на новый чип Black Pill!
######################################
MCU_TARGET ?= stm32f411

#меняем кодировку терминала на utf-8 для windows
ifeq ($(OS),Windows_NT)
    SHELL := cmd.exe
    # Вызываем chcp 65001 перед запуском любых рецептов
    .SHELLFLAGS := /K chcp 65001 > nul && /c
endif

TARGET = stm32_rtos_project
DEBUG = 1
OPT = -Og -g3

# Автоматическое определение текущей директории (независимость от путей)
..PROJECT_DIR := $(patsubst %/,%,$(dir $(mkfile_path)))

# Get the directory of the Makefile itself
PROJECT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

BUILD_DIR = build

######################################
# 2. НАСТРОЙКИ АРХИТЕКТУРЫ ПОД ЧИПЫ
######################################
ifeq ($(MCU_TARGET), stm32f103)
# Настройки для STM32F103 (Blue Pill)
CPU = -mcpu=cortex-m3
FPU = 
FLOAT-ABI = 
C_DEFS = -DSTM32F103xB
LDSCRIPT = stm32f103c8tx_flash.ld
STARTUP_SRC = src/bsp/stm32f103/startup_stm32f103xb.s
SYSTEM_SRC = src/bsp/stm32f103/system_stm32f1xx.c
OPENOCD_TARGET = target/stm32f1x.cfg
else ifeq ($(MCU_TARGET), stm32f411)
# Настройки для STM32F411 (Black Pill)
CPU = -mcpu=cortex-m4
# Включаем аппаратную поддержку Floating Point Unit (FPU) для Cortex-M4!
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
C_DEFS = -DSTM32F411xE
LDSCRIPT = stm32f411ceu6_flash.ld
STARTUP_SRC = src/bsp/stm32f411/startup_stm32f411xe.s
SYSTEM_SRC = src/bsp/stm32f411/system_stm32f4xx.c
OPENOCD_TARGET = target/stm32f4x.cfg
endif

MCU = $(CPU) -mthumb $(FPU) $(FLOAT-ABI)

# Инструменты компиляции (предполагается, что они прописаны в PATH системы)
PREFIX = arm-none-eabi-
CC = $(PREFIX)gcc
AS = $(PREFIX)gcc -x assembler-with-cpp
CP = $(PREFIX)objcopy
SZ = $(PREFIX)size

######################################
# 3. СПИСОК ИСХОДНЫХ ФАЙЛОВ (УНИВЕРСАЛЬНЫЙ)
######################################
C_SOURCES =  src/main.c
C_SOURCES += $(wildcard src/bsp/stm32f411/*.c)
C_SOURCES += $(wildcard src/rtos/core/*.c)
C_SOURCES += $(wildcard src/rtos/mem/*.c)
C_SOURCES += $(wildcard src/rtos/ipc/*.c)
C_SOURCES += $(wildcard src/rtos/timers/*.c)

#$(SYSTEM_SRC)

ASM_SOURCES =  \
$(STARTUP_SRC)

######################################
# 4. ПУТИ К ЗАГОЛОВОЧНЫМ ФАЙЛАМ
######################################
C_INCLUDES =  \
-IDrivers/CMSIS/Core/Include \
-Isrc \
-Isrc/rtos \
-Isrc/rtos/core \
-Isrc/rtos/mem \
-Isrc/rtos/ipc \
-Isrc/rtos/timers \
-Isrc/bsp/stm32f411

ifeq ($(MCU_TARGET), stm32f103)
C_INCLUDES += -IDrivers/CMSIS/Device/ST/STM32F1xx/Include
else ifeq ($(MCU_TARGET), stm32f411)
C_INCLUDES += -IDrivers/CMSIS/Device/ST/STM32F4xx/Include
endif

######################################
# 5. ФЛАГИ КОМПИЛЯЦИИ И ЛИНКОВКИ
######################################
ASFLAGS = $(MCU) $(AS_DEFS) $(AS_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections
CFLAGS = $(MCU) $(C_DEFS) $(C_INCLUDES) $(OPT) -Wall -fdata-sections -ffunction-sections

ifeq ($(DEBUG), 1)
CFLAGS += -g -gdwarf-2
endif

# Флаги генерации зависимостей
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"

LDFLAGS = $(MCU) -specs=nano.specs -T$(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--gc-sections

# Правило сборки по умолчанию
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

######################################
# РАБОТА С ОБЪЕКТНЫМИ ФАЙЛАМИ
######################################
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "Compiling c file: $<"
	@$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	@echo "Compiling ASM file: $<"
	@$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo "Linking project to ELF..."
	@$(CC) $(OBJECTS) $(LDFLAGS) -o $@
	@$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@$(CP) -O ihex $< $@
	
$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	@$(CP) -O binary -S $< $@	
	
$(BUILD_DIR):
	@mkdir $@

# Флаг /s удаляет дерево папок, флаг /q отключает подтверждения (quiet)
clean:
	@echo "Cleaning build directory..."
	@if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)

######################################
# 6. АВТОНОМНЫЙ ЗАПУСК OPENOCD
######################################
OPENOCD = "C:/dev_tools/STM32_tools/OpenOCD-20260302-0.12.0/bin/openocd.exe"
OCD_SCRIPTS = "C:/dev_tools/STM32_tools/OpenOCD-20260302-0.12.0/share/openocd/scripts"

openocd-server:
	$(OPENOCD) -s $(OCD_SCRIPTS) -f interface/stlink.cfg -f $(OPENOCD_TARGET)

flash: all
	$(OPENOCD) -s $(OCD_SCRIPTS) -f interface/stlink.cfg -f $(OPENOCD_TARGET) \
		-c "program $(BUILD_DIR)/$(TARGET).elf verify reset exit"

.PHONY: all clean flash openocd-server

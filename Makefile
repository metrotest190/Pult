###################################################################
#
#  Makefile: Pult_Encod_Koda (STM32F103C8Tx, Cortex-M3, arm-none-eabi-gcc)
#
#  Targets:
#    all   - build build/Pult_Encod_Koda.elf / .bin / .hex
#    clean - remove build/ directory
#    flash - program .elf into STM32F103C8 via ST-Link SWD
#            (STM32_Programmer_CLI.exe must be in PATH)
#
#  MCU:       STM32F103C8Tx (LQFP48, 64 KB FLASH, 20 KB RAM)
#  Clock:     72 MHz, HSE 8 MHz -> PLL *9
#  Toolchain: arm-none-eabi-gcc (GNU Tools for STM32 14.3.x)
#  Uses:      STM32 HAL (F1) + FreeModbus RTU + TJC USART HMI
#
###################################################################

# --- Project name and paths ---
PROJECT       := Pult_Encod_Koda
BUILD_DIR     := build
ROOT_DIR      := $(CURDIR)
LDSCRIPT      := $(ROOT_DIR)/STM32F103C8TX_FLASH.ld

# --- MCU, FPU, core flags ---
TARGET_MCU    := -mcpu=cortex-m3 -mthumb
FLOAT_ABI     := -msoft-float
MCU_DEFS      := -DSTM32F103xB -DUSE_HAL_DRIVER

# --- Toolchain ---
CC            := arm-none-eabi-gcc
AS            := arm-none-eabi-gcc -x assembler-with-cpp
LD            := arm-none-eabi-gcc
OBJCOPY       := arm-none-eabi-objcopy
SIZE          := arm-none-eabi-size
RM            := rm -rf

# --- Common C flags (Warning: treat warnings as errors OFF — HAL has legacy casts) ---
COMMON_CFLAGS  = $(TARGET_MCU) $(FLOAT_ABI) $(MCU_DEFS) -Wall -Wextra -Wno-unused-parameter \
                 -Wno-sign-compare -fdata-sections -ffunction-sections           \
                 -Og -g3 -ffreestanding -fno-common -fmessage-length=0 -specs=nano.specs

# --- Include paths ---
INCLUDES := \
    -I$(ROOT_DIR)/Core/Inc \
    -I$(ROOT_DIR)/Drivers/CMSIS/Device/ST/STM32F1xx/Include \
    -I$(ROOT_DIR)/Drivers/CMSIS/Include \
    -I$(ROOT_DIR)/Drivers/STM32F1xx_HAL_Driver/Inc \
    -I$(ROOT_DIR)/Drivers/STM32F1xx_HAL_Driver/Inc/Legacy \
    -I$(ROOT_DIR)/Modules/modbus/include \
    -I$(ROOT_DIR)/Modules/modbus/port \
    -I$(ROOT_DIR)/Modules/modbus/rtu \
    -I$(ROOT_DIR)/Modules/modbus/functions

# --- Source files ---

# Core/Src application code (main, it, hal_msp, syscalls, sysmem, system_stm32f1xx)
C_SOURCES += \
    Core/Src/main.c \
    Core/Src/stm32f1xx_hal_msp.c \
    Core/Src/stm32f1xx_it.c \
    Core/Src/syscalls.c \
    Core/Src/sysmem.c \
    Core/Src/system_stm32f1xx.c

# STM32 HAL F1 drivers
C_SOURCES += \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_cortex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_exti.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_flash_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_gpio_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_pwr.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_rcc_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim_ex.c \
    Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_uart.c

# FreeModbus RTU stack (core + RTU layer + function codes + STM32 port)
C_SOURCES += \
    Modules/modbus/mb.c \
    Modules/modbus/rtu/mbrtu.c \
    Modules/modbus/rtu/mbcrc.c \
    Modules/modbus/functions/mbfunccoils.c \
    Modules/modbus/functions/mbfuncdiag.c \
    Modules/modbus/functions/mbfuncdisc.c \
    Modules/modbus/functions/mbfuncholding.c \
    Modules/modbus/functions/mbfuncinput.c \
    Modules/modbus/functions/mbfuncother.c \
    Modules/modbus/functions/mbutils.c \
    Modules/modbus/port/mt_port.c \
    Modules/modbus/port/portevent.c \
    Modules/modbus/port/portserial.c \
    Modules/modbus/port/porttimer.c

# Startup assembly (STM32F103C8Tx, Cortex-M3, GCC)
ASM_SOURCES := Core/Startup/startup_stm32f103c8tx.s

# --- Build object list under $(BUILD_DIR) ---
OBJECTS     := $(addprefix $(BUILD_DIR)/,$(C_SOURCES:.c=.o))
OBJECTS     += $(addprefix $(BUILD_DIR)/,$(ASM_SOURCES:.s=.o))
DEPS        := $(OBJECTS:.o=.d)

# --- Assembler flags ---
ASFLAGS     := $(TARGET_MCU) $(FLOAT_ABI) $(MCU_DEFS) -Og -g3 -specs=nano.specs -Wa,--no-warn

# --- Linker flags: --gc-sections drops unused functions/sections
#   NOTE: -lnosys is NOT used because Core/Src/syscalls.c already provides
#         _exit/_close/_lseek/_read/_write/_kill/_getpid, and Core/Src/sysmem.c
#         provides _sbrk (STM32CubeMX-generated stubs). Adding -lnosys would
#         create multiple definition linker errors.
#   We pass user objects BEFORE libc (by using $(OBJECTS) before $(LDFLAGS))
#   so our syscalls take precedence over libc_nano weak placeholders. ---
LDFLAGS     := $(TARGET_MCU) $(FLOAT_ABI) -specs=nano.specs -T$(LDSCRIPT) \
               -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT).map -Wl,--print-memory-usage \
               -Wl,--cref -lm

# --- Compiler flags, dependency generation (-MMD -MP) ---
CFLAGS      := $(COMMON_CFLAGS) -MMD -MP $(INCLUDES)

# --- Default target ---------------------------------------------------------
.PHONY: all clean flash

all: $(BUILD_DIR)/$(PROJECT).elf $(BUILD_DIR)/$(PROJECT).bin $(BUILD_DIR)/$(PROJECT).hex

# --- Link: .elf ---
$(BUILD_DIR)/$(PROJECT).elf: $(OBJECTS) | $(BUILD_DIR)
	@echo "LD   $@"
	@$(LD) $(LDFLAGS) -o $@ $(OBJECTS)
	@$(SIZE) --format=berkeley $@

# --- Objcopy: .bin ---
$(BUILD_DIR)/$(PROJECT).bin: $(BUILD_DIR)/$(PROJECT).elf
	@echo "BIN  $@"
	@$(OBJCOPY) -O binary $< $@

# --- Objcopy: .hex ---
$(BUILD_DIR)/$(PROJECT).hex: $(BUILD_DIR)/$(PROJECT).elf
	@echo "HEX  $@"
	@$(OBJCOPY) -O ihex $< $@

# --- Compile .c -> .o ---
$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# --- Assemble .s -> .o ---
$(BUILD_DIR)/%.o: %.s | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@echo "AS   $<"
	@$(AS) $(ASFLAGS) -c $< -o $@

# --- Create build/ directory skeleton ---
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# --- Clean ---
clean:
	@echo "CLEAN $(BUILD_DIR)"
	@$(RM) $(BUILD_DIR)

# --- Flash via STM32_Programmer_CLI (ST-Link SWD, auto-connect) ---
STM32_PROG := STM32_Programmer_CLI.exe

flash: $(BUILD_DIR)/$(PROJECT).elf
	@echo "FLASH $(PROJECT).elf -> STM32F103C8 via ST-Link SWD"
	@$(STM32_PROG) -c port=SWD mode=UR -w "$(CURDIR)\$(BUILD_DIR)\$(PROJECT).elf" -v -rst

-include $(DEPS)

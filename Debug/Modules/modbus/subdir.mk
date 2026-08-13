################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (13.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Modules/modbus/mb.c 

OBJS += \
./Modules/modbus/mb.o 

C_DEPS += \
./Modules/modbus/mb.d 


# Each subdirectory must supply rules for building sources it contributes
Modules/modbus/%.o Modules/modbus/%.su Modules/modbus/%.cyclo: ../Modules/modbus/%.c Modules/modbus/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -I"C:/Users/190/Desktop/new/workspace/Pult_Encod_Koda/Modules/modbus/rtu" -I"C:/Users/190/Desktop/new/workspace/Pult_Encod_Koda/Modules/modbus/port" -I"C:/Users/190/Desktop/new/workspace/Pult_Encod_Koda/Modules/modbus/include" -I"C:/Users/190/Desktop/new/workspace/Pult_Encod_Koda/Modules/modbus/functions" -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Modules-2f-modbus

clean-Modules-2f-modbus:
	-$(RM) ./Modules/modbus/mb.cyclo ./Modules/modbus/mb.d ./Modules/modbus/mb.o ./Modules/modbus/mb.su

.PHONY: clean-Modules-2f-modbus


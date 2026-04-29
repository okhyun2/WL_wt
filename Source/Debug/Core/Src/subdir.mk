################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/app_clock.c \
../Core/Src/app_debug.c \
../Core/Src/app_error.c \
../Core/Src/app_gpio_lp.c \
../Core/Src/app_log.c \
../Core/Src/app_msgq.c \
../Core/Src/app_scheduler.c \
../Core/Src/app_selftest.c \
../Core/Src/app_system.c \
../Core/Src/app_task_main.c \
../Core/Src/app_tasks.c \
../Core/Src/main.c \
../Core/Src/stm32l0xx_hal_msp.c \
../Core/Src/stm32l0xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32l0xx.c 

OBJS += \
./Core/Src/app_clock.o \
./Core/Src/app_debug.o \
./Core/Src/app_error.o \
./Core/Src/app_gpio_lp.o \
./Core/Src/app_log.o \
./Core/Src/app_msgq.o \
./Core/Src/app_scheduler.o \
./Core/Src/app_selftest.o \
./Core/Src/app_system.o \
./Core/Src/app_task_main.o \
./Core/Src/app_tasks.o \
./Core/Src/main.o \
./Core/Src/stm32l0xx_hal_msp.o \
./Core/Src/stm32l0xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32l0xx.o 

C_DEPS += \
./Core/Src/app_clock.d \
./Core/Src/app_debug.d \
./Core/Src/app_error.d \
./Core/Src/app_gpio_lp.d \
./Core/Src/app_log.d \
./Core/Src/app_msgq.d \
./Core/Src/app_scheduler.d \
./Core/Src/app_selftest.d \
./Core/Src/app_system.d \
./Core/Src/app_task_main.d \
./Core/Src/app_tasks.d \
./Core/Src/main.d \
./Core/Src/stm32l0xx_hal_msp.d \
./Core/Src/stm32l0xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32l0xx.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m0plus -std=gnu11 -g3 -DDEBUG -DMY_DEBUG -DUSE_HAL_DRIVER -DSTM32L073xx -c -I../Core/Inc -I../Drivers/STM32L0xx_HAL_Driver/Inc -I../Drivers/STM32L0xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32L0xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/app_clock.cyclo ./Core/Src/app_clock.d ./Core/Src/app_clock.o ./Core/Src/app_clock.su ./Core/Src/app_debug.cyclo ./Core/Src/app_debug.d ./Core/Src/app_debug.o ./Core/Src/app_debug.su ./Core/Src/app_error.cyclo ./Core/Src/app_error.d ./Core/Src/app_error.o ./Core/Src/app_error.su ./Core/Src/app_gpio_lp.cyclo ./Core/Src/app_gpio_lp.d ./Core/Src/app_gpio_lp.o ./Core/Src/app_gpio_lp.su ./Core/Src/app_log.cyclo ./Core/Src/app_log.d ./Core/Src/app_log.o ./Core/Src/app_log.su ./Core/Src/app_msgq.cyclo ./Core/Src/app_msgq.d ./Core/Src/app_msgq.o ./Core/Src/app_msgq.su ./Core/Src/app_scheduler.cyclo ./Core/Src/app_scheduler.d ./Core/Src/app_scheduler.o ./Core/Src/app_scheduler.su ./Core/Src/app_selftest.cyclo ./Core/Src/app_selftest.d ./Core/Src/app_selftest.o ./Core/Src/app_selftest.su ./Core/Src/app_system.cyclo ./Core/Src/app_system.d ./Core/Src/app_system.o ./Core/Src/app_system.su ./Core/Src/app_task_main.cyclo ./Core/Src/app_task_main.d ./Core/Src/app_task_main.o ./Core/Src/app_task_main.su ./Core/Src/app_tasks.cyclo ./Core/Src/app_tasks.d ./Core/Src/app_tasks.o ./Core/Src/app_tasks.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/stm32l0xx_hal_msp.cyclo ./Core/Src/stm32l0xx_hal_msp.d ./Core/Src/stm32l0xx_hal_msp.o ./Core/Src/stm32l0xx_hal_msp.su ./Core/Src/stm32l0xx_it.cyclo ./Core/Src/stm32l0xx_it.d ./Core/Src/stm32l0xx_it.o ./Core/Src/stm32l0xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32l0xx.cyclo ./Core/Src/system_stm32l0xx.d ./Core/Src/system_stm32l0xx.o ./Core/Src/system_stm32l0xx.su

.PHONY: clean-Core-2f-Src


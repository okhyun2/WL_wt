#include "boot_slot.h"

#include "stm32l0xx_hal.h"

typedef void (*BootJumpFunction_t)(void);

uint32_t BootSlotGetAddress(uint32_t slotId)
{
    return (slotId == 2u) ? BOOT_SLOT2_ADDR : BOOT_SLOT1_ADDR;
}

uint8_t BootSlotIsValid(uint32_t baseAddress)
{
    uint32_t stackPtr = *(volatile uint32_t *)baseAddress;
    uint32_t resetHandler = *(volatile uint32_t *)(baseAddress + 4u);

    if ((stackPtr < BOOT_SRAM_START) || (stackPtr > BOOT_SRAM_END))
    {
        return 0u;
    }

    if ((resetHandler < baseAddress) || (resetHandler >= (baseAddress + BOOT_SLOT_SIZE_BYTES)))
    {
        return 0u;
    }

    return 1u;
}

void BootSlotJump(uint32_t baseAddress)
{
    uint32_t stackPtr = *(volatile uint32_t *)baseAddress;
    uint32_t resetHandler = *(volatile uint32_t *)(baseAddress + 4u);
    BootJumpFunction_t jumpFunction = (BootJumpFunction_t)resetHandler;

    HAL_RCC_DeInit();
    HAL_DeInit();
    __disable_irq();

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    SCB->VTOR = baseAddress;
    __set_MSP(stackPtr);
    jumpFunction();

    while (1)
    {
    }
}

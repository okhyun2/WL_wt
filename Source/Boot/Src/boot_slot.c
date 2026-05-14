#include "boot_slot.h"
#include "boot_info.h"
#include "boot_crc.h"

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

    uint32_t calculatedCRC;
    uint32_t                  storedCRC;
    const BootImageHeader_t  *pHeader;
    volatile uint32_t        *pCRCField;

    if ((stackPtr < BOOT_SRAM_START) || (stackPtr > BOOT_SRAM_END))
    {
        return 0u;
    }

    if ((resetHandler < baseAddress) || (resetHandler >= (baseAddress + BOOT_SLOT_SIZE_BYTES)))
    {
        return 0u;
    }

    /* Magic number check */
    pHeader = (const BootImageHeader_t *)(baseAddress + BOOT_IMAGE_HEADER_OFFSET);

    if (pHeader->magicNumber != BOOT_MAGIC_NUMBER)
    {
        return 0u;
    }

    /* CRC32 verification */
    storedCRC = pHeader->firmwareCRC;
    /* CRC field offset from baseAddress */
    uint32_t crcFieldOffset = BOOT_IMAGE_HEADER_OFFSET
                          + offsetof(BootImageHeader_t, firmwareCRC);

    BootCRC_Init();
    calculatedCRC = BootCRC_CalculateWithPatch(baseAddress,
                                           pHeader->firmwareSize,
                                           crcFieldOffset);
    BootCRC_DeInit();

    if (calculatedCRC != storedCRC)
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

    __disable_irq();

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL  = 0u;

    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;

    HAL_RCC_DeInit();
    HAL_DeInit();

    SCB->VTOR = baseAddress;
    __DSB();
    __ISB();

    __set_MSP(stackPtr);
    __set_PRIMASK(0);   // 또는 __enable_irq();

    jumpFunction();

    while(1)
    {

    }
}

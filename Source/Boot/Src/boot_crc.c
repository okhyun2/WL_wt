#include "boot_crc.h"
#include "stm32l0xx_hal.h"

static CRC_HandleTypeDef hcrc;

void BootCRC_Init(void)
{
    __HAL_RCC_CRC_CLK_ENABLE();

    hcrc.Instance                     = CRC;
    hcrc.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;
    hcrc.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;
    hcrc.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_NONE;
    hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
    hcrc.InputDataFormat              = CRC_INPUTDATA_FORMAT_BYTES;  

    HAL_CRC_Init(&hcrc);
}

void BootCRC_DeInit(void)
{
    HAL_CRC_DeInit(&hcrc);
    __HAL_RCC_CRC_CLK_DISABLE();
}

uint32_t BootCRC_CalculateWithPatch(uint32_t baseAddress,
                                    uint32_t sizeBytes,
                                    uint32_t patchOffset)
{
    const uint8_t *pData        = (const uint8_t *)baseAddress;
    uint32_t       crc          = 0u;
    uint8_t        zeroBuf[4u]  = {0x00u, 0x00u, 0x00u, 0x00u};

    /* Reset CRC unit */
    __HAL_CRC_DR_RESET(&hcrc);

    /* Section 1: baseAddress ~ patchOffset (before CRC field) */
    if (patchOffset > 0u)
    {
        crc = HAL_CRC_Accumulate(&hcrc, (uint32_t *)pData, patchOffset);
    }

    /* Section 2: CRC field replaced with 0x00000000 (4 bytes) */
    crc = HAL_CRC_Accumulate(&hcrc, (uint32_t *)zeroBuf, 4u);

    /* Section 3: after CRC field ~ end of firmware */
    uint32_t remainOffset = patchOffset + 4u;
    uint32_t remainSize   = sizeBytes - remainOffset;

    if (remainSize > 0u)
    {
        crc = HAL_CRC_Accumulate(&hcrc,
                                  (uint32_t *)(pData + remainOffset),
                                  remainSize);
    }

    return crc;
}


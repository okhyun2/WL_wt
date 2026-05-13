#include "boot_info.h"

#include <string.h>

#define BOOT_INFO_PTR                          ((const BootInfo_t *)BOOT_INFO_ADDR)

static uint32_t BootInfoCrc32(const uint8_t *p_data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t index;
    uint32_t bit;

    for (index = 0u; index < length; index++)
    {
        crc ^= p_data[index];
        for (bit = 0u; bit < 8u; bit++)
        {
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }

    return ~crc;
}

static uint32_t BootInfoCalcCrc(const BootInfo_t *p_info)
{
    return BootInfoCrc32((const uint8_t *)p_info, sizeof(BootInfo_t) - sizeof(uint32_t));
}

void BootInfoSetDefaults(BootInfo_t *p_info)
{
    (void)memset(p_info, 0, sizeof(*p_info));
    p_info->magic = BOOT_INFO_MAGIC;
    p_info->version = BOOT_INFO_VERSION;
    p_info->activeSlot = BOOT_SLOT1;
    p_info->pendingSlot = 0u;
    p_info->bootMode = BOOT_MODE_NORMAL;
    p_info->bootState = BOOT_STATE_IDLE;
    p_info->bootCounter = 0u;
    p_info->crc32 = BootInfoCalcCrc(p_info);
}

uint8_t BootInfoIsValid(const BootInfo_t *p_info)
{
    if ((p_info->magic != BOOT_INFO_MAGIC) ||
        (p_info->version != BOOT_INFO_VERSION))
    {
        return 0u;
    }

    if ((p_info->activeSlot != BOOT_SLOT1) && (p_info->activeSlot != BOOT_SLOT2))
    {
        return 0u;
    }

    return (p_info->crc32 == BootInfoCalcCrc(p_info)) ? 1u : 0u;
}

void BootInfoLoad(BootInfo_t *p_info)
{
    (void)memcpy(p_info, BOOT_INFO_PTR, sizeof(*p_info));
    if (BootInfoIsValid(p_info) == 0u)
    {
        BootInfoSetDefaults(p_info);
    }
}

HAL_StatusTypeDef BootInfoSave(const BootInfo_t *p_info)
{
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    uint32_t index;
    uint32_t wordCount;
    BootInfo_t temp;
    const uint32_t *p_words;

    temp = *p_info;
    temp.crc32 = BootInfoCalcCrc(&temp);

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return HAL_ERROR;
    }

    (void)__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_OPTVERR);

    (void)memset(&eraseInit, 0, sizeof(eraseInit));
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = BOOT_INFO_ADDR;
    eraseInit.NbPages = 1u;
    pageError = 0u;
    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return HAL_ERROR;
    }

    p_words = (const uint32_t *)&temp;
    wordCount = (uint32_t)(sizeof(BootInfo_t) / sizeof(uint32_t));
    for (index = 0u; index < wordCount; index++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_INFO_ADDR + (index * 4u), p_words[index]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            return HAL_ERROR;
        }
    }

    return HAL_FLASH_Lock();
}

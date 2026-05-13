#include "app_dualboot.h"

#include <string.h>

#include "app_build_config.h"

#define APP_DUALBOOT_INFO_PTR                 ((const AppDualBootInfo_t *)APP_BOOT_INFO_BASE_ADDR)
#define APP_DUALBOOT_INFO_PAGE_ADDRESS        (APP_BOOT_INFO_BASE_ADDR)
#define APP_DUALBOOT_INFO_PAGE_COUNT          (1u)

static AppDualBootInfo_t g_appDualBootInfo;
static uint8_t g_appDualBootInitialized;

static uint32_t App_DualBootCrc32(const uint8_t *p_data, uint32_t length)
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

static uint32_t App_DualBootCalcInfoCrc(const AppDualBootInfo_t *p_info)
{
    return App_DualBootCrc32((const uint8_t *)p_info, sizeof(AppDualBootInfo_t) - sizeof(uint32_t));
}

static void App_DualBootSetDefaults(AppDualBootInfo_t *p_info)
{
    (void)memset(p_info, 0, sizeof(*p_info));
    p_info->magic = APP_DUALBOOT_INFO_MAGIC;
    p_info->version = APP_DUALBOOT_INFO_VERSION;
    p_info->activeSlot = APP_DUALBOOT_SLOT1;
    p_info->pendingSlot = 0u;
    p_info->bootMode = APP_DUALBOOT_MODE_NORMAL;
    p_info->bootState = APP_DUALBOOT_STATE_IDLE;
    p_info->crc32 = App_DualBootCalcInfoCrc(p_info);
}

static uint8_t App_DualBootIsValid(const AppDualBootInfo_t *p_info)
{
    if ((p_info->magic != APP_DUALBOOT_INFO_MAGIC) ||
        (p_info->version != APP_DUALBOOT_INFO_VERSION))
    {
        return APP_FALSE;
    }

    if ((p_info->activeSlot != APP_DUALBOOT_SLOT1) &&
        (p_info->activeSlot != APP_DUALBOOT_SLOT2))
    {
        return APP_FALSE;
    }

    return (p_info->crc32 == App_DualBootCalcInfoCrc(p_info)) ? APP_TRUE : APP_FALSE;
}

static AppStatus_t App_DualBootWriteInfo(const AppDualBootInfo_t *p_info)
{
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;
    uint32_t index;
    uint32_t wordCount;
    AppDualBootInfo_t temp;
    const uint32_t *p_words;

    APP_RETURN_IF_FALSE((p_info != NULL), APP_STATUS_INVALID_PARAM);

    temp = *p_info;
    temp.crc32 = App_DualBootCalcInfoCrc(&temp);

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Unlock(), APP_STATUS_FATAL);

    (void)__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | FLASH_FLAG_SIZERR | FLASH_FLAG_OPTVERR);

    (void)memset(&eraseInit, 0, sizeof(eraseInit));
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = APP_DUALBOOT_INFO_PAGE_ADDRESS;
    eraseInit.NbPages = APP_DUALBOOT_INFO_PAGE_COUNT;
    pageError = 0u;
    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return APP_STATUS_FATAL;
    }

    p_words = (const uint32_t *)&temp;
    wordCount = (uint32_t)(sizeof(AppDualBootInfo_t) / sizeof(uint32_t));
    for (index = 0u; index < wordCount; index++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, APP_DUALBOOT_INFO_PAGE_ADDRESS + (index * 4u), p_words[index]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            return APP_STATUS_FATAL;
        }
    }

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Lock(), APP_STATUS_FATAL);
    g_appDualBootInfo = temp;
    return APP_STATUS_OK;
}

static void App_DualBootLoadInfo(void)
{
    (void)memcpy(&g_appDualBootInfo, APP_DUALBOOT_INFO_PTR, sizeof(g_appDualBootInfo));
    if (App_DualBootIsValid(&g_appDualBootInfo) != APP_TRUE)
    {
        App_DualBootSetDefaults(&g_appDualBootInfo);
    }
}

AppStatus_t App_DualBootInit(void)
{
    App_DualBootLoadInfo();
    g_appDualBootInitialized = APP_TRUE;
    return APP_STATUS_OK;
}

void App_DualBootService(void)
{
    if (g_appDualBootInitialized != APP_TRUE)
    {
        return;
    }
}

AppStatus_t App_DualBootRequestUpdateToSlot2(void)
{
    AppDualBootInfo_t info;

    if (g_appDualBootInitialized != APP_TRUE)
    {
        (void)App_DualBootInit();
    }

    info = g_appDualBootInfo;
    info.pendingSlot = APP_DUALBOOT_SLOT2;
    info.bootMode = APP_DUALBOOT_MODE_ENTER_ROM_BOOT;
    info.bootState = APP_DUALBOOT_STATE_UPDATE_REQUESTED;
    return App_DualBootWriteInfo(&info);
}

AppStatus_t App_DualBootCancelUpdateRequest(void)
{
    AppDualBootInfo_t info;

    if (g_appDualBootInitialized != APP_TRUE)
    {
        (void)App_DualBootInit();
    }

    info = g_appDualBootInfo;
    info.pendingSlot = 0u;
    info.bootMode = APP_DUALBOOT_MODE_NORMAL;
    info.bootState = APP_DUALBOOT_STATE_IDLE;
    return App_DualBootWriteInfo(&info);
}

AppStatus_t App_DualBootConfirmSlot2(void)
{
    AppDualBootInfo_t info;

    if (g_appDualBootInitialized != APP_TRUE)
    {
        (void)App_DualBootInit();
    }

    if ((APP_SLOT_ID != APP_DUALBOOT_SLOT2) ||
        (g_appDualBootInfo.bootState != APP_DUALBOOT_STATE_TRIAL_PENDING) ||
        (g_appDualBootInfo.activeSlot != APP_DUALBOOT_SLOT2))
    {
        return APP_STATUS_OK;
    }

    info = g_appDualBootInfo;
    info.activeSlot = APP_DUALBOOT_SLOT2;
    info.pendingSlot = 0u;
    info.bootMode = APP_DUALBOOT_MODE_NORMAL;
    info.bootState = APP_DUALBOOT_STATE_CONFIRMED;
    return App_DualBootWriteInfo(&info);
}

const AppDualBootInfo_t *App_DualBootGetInfo(void)
{
    return &g_appDualBootInfo;
}

uint32_t App_DualBootGetCurrentSlotId(void)
{
    return APP_SLOT_ID;
}

const char *App_DualBootGetCurrentSlotName(void)
{
    return APP_SLOT_NAME;
}

const char *App_DualBootGetTargetSlotName(void)
{
    return "slot2";
}

uint32_t App_DualBootGetTargetSlotAddress(void)
{
    return APP_SLOT2_BASE_ADDR;
}

const char *App_DualBootGetBootStateString(void)
{
    switch (g_appDualBootInfo.bootState)
    {
        case APP_DUALBOOT_STATE_IDLE: return "idle";
        case APP_DUALBOOT_STATE_UPDATE_REQUESTED: return "update_req";
        case APP_DUALBOOT_STATE_TRIAL_PENDING: return "trial";
        case APP_DUALBOOT_STATE_CONFIRMED: return "confirmed";
        case APP_DUALBOOT_STATE_ROLLBACK: return "rollback";
        default: return "unknown";
    }
}

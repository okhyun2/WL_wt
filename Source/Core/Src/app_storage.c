#include "app_storage.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_log.h"

#define APP_STORAGE_PARAM_MAGIC                     (0x57545337u)
#define APP_STORAGE_INVALID_ADDRESS                 (0xFFFFFFFFu)
#define APP_STORAGE_REQUESTER_NONE                  (0xFFu)
#define APP_STORAGE_MAX_REQUESTS_PER_RUN            (4u)

typedef struct
{
    uint32_t magic;
    uint32_t layoutRev;
    uint32_t payloadSize;
    uint32_t sequence;
    uint32_t flags;
    uint32_t rtcWakePeriodMs;
    uint32_t stopQualifyCount;
    uint32_t userData0;
    uint32_t userData1;
    uint32_t crc32;
} AppStorageParameterBlock_t;

typedef struct
{
    uint8_t initialized;
    uint8_t defaultsApplied;
    uint8_t eepromValid;
    uint8_t flashValid;
    uint8_t commitPending;
    uint8_t flashCommitPending;
    uint8_t pendingBackend;
    uint8_t pendingRequester;
    uint8_t pendingOperation;
    uint32_t pendingRequestTickMs;
    uint32_t lastCommitTickMs;
    uint32_t eepromWriteCount;
    uint32_t flashWriteCount;
    uint32_t activeEepromAddress;
    uint32_t activeFlashAddress;
    AppStatus_t lastLoadStatus;
    AppStatus_t lastCommitStatus;
    AppStorageParameterBlock_t active;
} AppStorageContext_t;

static AppStorageContext_t g_appStorageContext;
static AppStorageSummary_t g_appStorageSummary;

//common////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static uint32_t App_StorageIf_CalculateCrc32(const AppStorageParameterBlock_t *p_block)
{
    AppStorageParameterBlock_t blockCopy;

    if (p_block == NULL)
    {
        return 0u;
    }

    blockCopy = *p_block;
    blockCopy.crc32 = 0u;
    return HAL_CRC_Calculate(APP_CRC_HANDLE,
                             (uint32_t *)&blockCopy,
                             (uint32_t)(sizeof(blockCopy) / sizeof(uint32_t)));
}

static uint8_t App_StorageIf_IsBlank(const void *p_data, uint32_t sizeBytes)
{
    const uint32_t *p_words;
    uint32_t wordCount;
    uint32_t index;

    p_words = (const uint32_t *)p_data;
    wordCount = sizeBytes / sizeof(uint32_t);
    for (index = 0u; index < wordCount; index++)
    {
        if (p_words[index] != 0xFFFFFFFFu)
        {
            return APP_FALSE;
        }
    }

    return APP_TRUE;
}

static uint8_t App_StorageIf_IsBlockValid(const AppStorageParameterBlock_t *p_block)
{
    if (p_block == NULL)
    {
        return APP_FALSE;
    }

    if (App_StorageIf_IsBlank(p_block, sizeof(*p_block)) == APP_TRUE)
    {
        return APP_FALSE;
    }

    if (p_block->magic != APP_STORAGE_PARAM_MAGIC)
    {
        return APP_FALSE;
    }

    if (p_block->layoutRev != APP_STORAGE_LAYOUT_REV)
    {
        return APP_FALSE;
    }

    if (p_block->payloadSize != sizeof(AppStorageParameterBlock_t))
    {
        return APP_FALSE;
    }

    if ((p_block->flags & APP_STORAGE_PARAM_FLAGS_VALID) == 0u)
    {
        return APP_FALSE;
    }

    return (App_StorageIf_CalculateCrc32(p_block) == p_block->crc32) ? APP_TRUE : APP_FALSE;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



//eeprom///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static uint32_t App_StorageIf_GetEepromSlotAddress(uint32_t slotIndex)
{
    return (DATA_EEPROM_BASE + (slotIndex * APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES));
}


static AppStatus_t App_StorageIf_WriteEepromBlock(const AppStorageParameterBlock_t *p_block, uint32_t address)
{
    const uint32_t *p_words;
    uint32_t wordIndex;

    APP_RETURN_IF_FALSE((p_block != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES >= sizeof(*p_block), APP_STATUS_INVALID_PARAM);

    APP_RETURN_IF_HAL_ERROR(HAL_FLASHEx_DATAEEPROM_Unlock(), APP_STATUS_INIT_FAILED);

    p_words = (const uint32_t *)p_block;
    for (wordIndex = 0u; wordIndex < (sizeof(*p_block) / sizeof(uint32_t)); wordIndex++)
    {
        if (HAL_FLASHEx_DATAEEPROM_Program(FLASH_TYPEPROGRAMDATA_WORD,
                                           address + (wordIndex * sizeof(uint32_t)),
                                           p_words[wordIndex]) != HAL_OK)
        {
            (void)HAL_FLASHEx_DATAEEPROM_Lock();
            App_ErrorRecord(APP_STATUS_INIT_FAILED, __FILE__, __LINE__);
            return APP_STATUS_INIT_FAILED;
        }
    }

    APP_RETURN_IF_HAL_ERROR(HAL_FLASHEx_DATAEEPROM_Lock(), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_StorageIf_LoadLatestEeprom(AppStorageParameterBlock_t *p_block, uint32_t *p_address)
{
    AppStorageParameterBlock_t candidate;
    AppStorageParameterBlock_t bestBlock;
    uint32_t candidateAddress;
    uint32_t bestAddress;
    uint8_t found;
    uint32_t slotIndex;

    found = APP_FALSE;
    bestAddress = APP_STORAGE_INVALID_ADDRESS;
    (void)memset(&bestBlock, 0, sizeof(bestBlock));

    for (slotIndex = 0u; slotIndex < APP_STORAGE_DATA_EEPROM_SLOT_COUNT; slotIndex++)
    {
        candidateAddress = App_StorageIf_GetEepromSlotAddress(slotIndex);
        (void)memcpy(&candidate, (const void *)candidateAddress, sizeof(candidate));

        if (App_StorageIf_IsBlockValid(&candidate) != APP_TRUE)
        {
            continue;
        }

        if ((found == APP_FALSE) || (candidate.sequence >= bestBlock.sequence))
        {
            bestBlock = candidate;
            bestAddress = candidateAddress;
            found = APP_TRUE;
        }
    }

    APP_RETURN_IF_FALSE(found == APP_TRUE, APP_STATUS_INIT_FAILED);
    *p_block = bestBlock;
    *p_address = bestAddress;
    return APP_STATUS_OK;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//flash////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t App_StorageIf_GetFlashPartitionStart(void)
{
    uint32_t flashSizeBytes;
    uint32_t partitionBytes;

    flashSizeBytes = ((uint32_t)(*(__IO uint16_t *)FLASHSIZE_BASE)) * 1024u;
    partitionBytes = APP_STORAGE_FLASH_PARTITION_PAGE_COUNT * FLASH_PAGE_SIZE;
    return (FLASH_BASE + flashSizeBytes - partitionBytes);
}

static uint32_t App_StorageIf_GetFlashPartitionEnd(void)
{
    uint32_t flashSizeBytes;

    flashSizeBytes = ((uint32_t)(*(__IO uint16_t *)FLASHSIZE_BASE)) * 1024u;
    return (FLASH_BASE + flashSizeBytes);
}


static AppStatus_t App_StorageIf_EraseFlashPartition(void)
{
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;

    (void)memset(&eraseInit, 0, sizeof(eraseInit));
    pageError = 0u;

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Unlock(), APP_STATUS_INIT_FAILED);

    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = App_StorageIf_GetFlashPartitionStart();
    eraseInit.NbPages = APP_STORAGE_FLASH_PARTITION_PAGE_COUNT;
    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        App_ErrorRecord(APP_STATUS_INIT_FAILED, __FILE__, __LINE__);
        return APP_STATUS_INIT_FAILED;
    }

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Lock(), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}


static AppStatus_t App_StorageIf_LoadLatestFlash(AppStorageParameterBlock_t *p_block, uint32_t *p_address)
{
    AppStorageParameterBlock_t candidate;
    AppStorageParameterBlock_t bestBlock;
    uint32_t candidateAddress;
    uint32_t bestAddress;
    uint32_t partitionStart;
    uint32_t partitionEnd;
    uint8_t found;

    found = APP_FALSE;
    bestAddress = APP_STORAGE_INVALID_ADDRESS;
    partitionStart = App_StorageIf_GetFlashPartitionStart();
    partitionEnd = App_StorageIf_GetFlashPartitionEnd();
    (void)memset(&bestBlock, 0, sizeof(bestBlock));

    for (candidateAddress = partitionStart;
         candidateAddress < partitionEnd;
         candidateAddress += APP_STORAGE_FLASH_RECORD_STRIDE_BYTES)
    {
        (void)memcpy(&candidate, (const void *)candidateAddress, sizeof(candidate));

        if (App_StorageIf_IsBlockValid(&candidate) != APP_TRUE)
        {
            continue;
        }

        if ((found == APP_FALSE) || (candidate.sequence >= bestBlock.sequence))
        {
            bestBlock = candidate;
            bestAddress = candidateAddress;
            found = APP_TRUE;
        }
    }

    APP_RETURN_IF_FALSE(found == APP_TRUE, APP_STATUS_INIT_FAILED);
    *p_block = bestBlock;
    *p_address = bestAddress;
    return APP_STATUS_OK;
}


static AppStatus_t App_StorageIf_WriteFlashBlock(const AppStorageParameterBlock_t *p_block, uint32_t *p_writtenAddress)
{
    const uint32_t *p_words;
    uint32_t targetAddress;
    uint32_t partitionStart;
    uint32_t partitionEnd;
    uint32_t wordIndex;

    APP_RETURN_IF_FALSE((p_block != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_FLASH_RECORD_STRIDE_BYTES >= sizeof(*p_block), APP_STATUS_INVALID_PARAM);

    partitionStart = App_StorageIf_GetFlashPartitionStart();
    partitionEnd = App_StorageIf_GetFlashPartitionEnd();
    targetAddress = APP_STORAGE_INVALID_ADDRESS;

    for (targetAddress = partitionStart;
         targetAddress < partitionEnd;
         targetAddress += APP_STORAGE_FLASH_RECORD_STRIDE_BYTES)
    {
        if (*(const uint32_t *)targetAddress == 0xFFFFFFFFu)
        {
            break;
        }
    }

    if (targetAddress >= partitionEnd)
    {
        APP_RETURN_IF_FALSE(App_StorageIf_EraseFlashPartition() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
        targetAddress = partitionStart;
    }

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Unlock(), APP_STATUS_INIT_FAILED);

    p_words = (const uint32_t *)p_block;
    for (wordIndex = 0u; wordIndex < (sizeof(*p_block) / sizeof(uint32_t)); wordIndex++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              targetAddress + (wordIndex * sizeof(uint32_t)),
                              p_words[wordIndex]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            App_ErrorRecord(APP_STATUS_INIT_FAILED, __FILE__, __LINE__);
            return APP_STATUS_INIT_FAILED;
        }
    }

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Lock(), APP_STATUS_INIT_FAILED);
    *p_writtenAddress = targetAddress;
    return APP_STATUS_OK;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//application////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void App_StorageUpdateSummary(void)
{
    g_appStorageSummary.initialized = g_appStorageContext.initialized;
    g_appStorageSummary.defaultsApplied = g_appStorageContext.defaultsApplied;
    g_appStorageSummary.eepromValid = g_appStorageContext.eepromValid;
    g_appStorageSummary.flashValid = g_appStorageContext.flashValid;
    g_appStorageSummary.commitPending = g_appStorageContext.commitPending;
    g_appStorageSummary.flashCommitPending = g_appStorageContext.flashCommitPending;
    g_appStorageSummary.sequence = g_appStorageContext.active.sequence;
    g_appStorageSummary.activeEepromAddress = g_appStorageContext.activeEepromAddress;
    g_appStorageSummary.activeFlashAddress = g_appStorageContext.activeFlashAddress;
    g_appStorageSummary.activeUserData0 = g_appStorageContext.active.userData0;
    g_appStorageSummary.activeUserData1 = g_appStorageContext.active.userData1;
    g_appStorageSummary.eepromWriteCount = g_appStorageContext.eepromWriteCount;
    g_appStorageSummary.flashWriteCount = g_appStorageContext.flashWriteCount;
    g_appStorageSummary.lastCommitTickMs = g_appStorageContext.lastCommitTickMs;
    g_appStorageSummary.lastLoadStatus = g_appStorageContext.lastLoadStatus;
    g_appStorageSummary.lastCommitStatus = g_appStorageContext.lastCommitStatus;
}

const AppStorageSummary_t *App_StorageGetSummary(void)
{
    return &g_appStorageSummary;
}

static void App_StorageIf_SetDefaultsParam(AppStorageParameterBlock_t *p_block)
{
    (void)memset(p_block, 0, sizeof(*p_block));
    p_block->magic = APP_STORAGE_PARAM_MAGIC;
    p_block->layoutRev = APP_STORAGE_LAYOUT_REV;
    p_block->payloadSize = sizeof(AppStorageParameterBlock_t);
    p_block->sequence = 1u;
    p_block->flags = APP_STORAGE_PARAM_FLAGS_VALID;
    p_block->rtcWakePeriodMs = APP_RTC_WAKEUP_PERIOD_MS;
    p_block->stopQualifyCount = APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT;
    p_block->userData0 = 0u;
    p_block->userData1 = 0u;
    p_block->crc32 = App_StorageIf_CalculateCrc32(p_block);
}


AppStatus_t App_StorageIf_LoadParameterBlocks(void)
{
    AppStorageParameterBlock_t eepromBlock;
    AppStorageParameterBlock_t flashBlock;
    AppStatus_t eepromStatus;
    AppStatus_t flashStatus;
    uint32_t eepromAddress;
    uint32_t flashAddress;

    (void)memset(&g_appStorageContext, 0, sizeof(g_appStorageContext));
    (void)memset(&g_appStorageSummary, 0, sizeof(g_appStorageSummary));
    g_appStorageContext.activeEepromAddress = APP_STORAGE_INVALID_ADDRESS;
    g_appStorageContext.activeFlashAddress = APP_STORAGE_INVALID_ADDRESS;
    g_appStorageContext.lastLoadStatus = APP_STATUS_INIT_FAILED;
    g_appStorageContext.lastCommitStatus = APP_STATUS_NOT_INITIALIZED;
    g_appStorageContext.pendingRequester = APP_STORAGE_REQUESTER_NONE;

    APP_RETURN_IF_FALSE(APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES >= sizeof(AppStorageParameterBlock_t), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_FLASH_RECORD_STRIDE_BYTES >= sizeof(AppStorageParameterBlock_t), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_FLASH_PARTITION_PAGE_COUNT != 0u, APP_STATUS_INVALID_PARAM);

    eepromStatus = App_StorageIf_LoadLatestEeprom(&eepromBlock, &eepromAddress);
    flashStatus = App_StorageIf_LoadLatestFlash(&flashBlock, &flashAddress);

    g_appStorageContext.eepromValid = (eepromStatus == APP_STATUS_OK) ? APP_TRUE : APP_FALSE;
    g_appStorageContext.flashValid = (flashStatus == APP_STATUS_OK) ? APP_TRUE : APP_FALSE;

    if ((eepromStatus == APP_STATUS_OK) && (flashStatus == APP_STATUS_OK))
    {
        if (eepromBlock.sequence >= flashBlock.sequence)
        {
            g_appStorageContext.active = eepromBlock;
            g_appStorageContext.activeEepromAddress = eepromAddress;
            g_appStorageContext.activeFlashAddress = flashAddress;
        }
        else
        {
            g_appStorageContext.active = flashBlock;
            g_appStorageContext.activeFlashAddress = flashAddress;
            g_appStorageContext.commitPending = APP_TRUE;
            g_appStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_EEPROM;
        }
    }
    else if (eepromStatus == APP_STATUS_OK)
    {
        g_appStorageContext.active = eepromBlock;
        g_appStorageContext.activeEepromAddress = eepromAddress;
        g_appStorageContext.commitPending = APP_TRUE;
        g_appStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_FLASH;
    }
    else if (flashStatus == APP_STATUS_OK)
    {
        g_appStorageContext.active = flashBlock;
        g_appStorageContext.activeFlashAddress = flashAddress;
        g_appStorageContext.commitPending = APP_TRUE;
        g_appStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_EEPROM;
    }
    else
    {
        App_StorageIf_SetDefaultsParam(&g_appStorageContext.active);
        g_appStorageContext.defaultsApplied = APP_TRUE;
        g_appStorageContext.commitPending = APP_TRUE;
        g_appStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_BOTH;
    }

    g_appStorageContext.initialized = APP_TRUE;
    g_appStorageContext.lastLoadStatus = APP_STATUS_OK;
    App_StorageUpdateSummary();

    (void)APP_LOGI("STOR", "init eeprom=%u flash=%u seq=%lu commit=%u backend=%u",
                         (unsigned int)g_appStorageContext.eepromValid,
                         (unsigned int)g_appStorageContext.flashValid,
                         (unsigned long)g_appStorageContext.active.sequence,
                         (unsigned int)g_appStorageContext.commitPending,
                         (unsigned int)g_appStorageContext.pendingBackend);
    return APP_STATUS_OK;
}

AppStatus_t App_StorageLoadDefaults(void)
{
    uint32_t nextSequence;

    APP_RETURN_IF_FALSE(g_appStorageContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    nextSequence = (g_appStorageContext.active.sequence == 0u) ? 1u : (g_appStorageContext.active.sequence + 1u);
    App_StorageIf_SetDefaultsParam(&g_appStorageContext.active);
    g_appStorageContext.active.sequence = nextSequence;
    g_appStorageContext.active.crc32 = App_StorageIf_CalculateCrc32(&g_appStorageContext.active);
    g_appStorageContext.defaultsApplied = APP_TRUE;
    g_appStorageContext.commitPending = APP_TRUE;
    g_appStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_BOTH;
    g_appStorageContext.pendingRequester = APP_STORAGE_REQUESTER_NONE;
    g_appStorageContext.pendingOperation = (uint8_t)APP_STORAGE_QUEUE_OP_SAVE;
    g_appStorageContext.pendingRequestTickMs = HAL_GetTick();
    App_StorageUpdateSummary();
    return APP_STATUS_OK;
}










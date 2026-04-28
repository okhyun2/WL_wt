#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_msgq.h"

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
    uint32_t meterPeriodMs;
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
} AppTaskStorageContext_t;

static AppTaskStorageContext_t g_appTaskStorageContext;
static AppTaskStorageSummary_t g_appTaskStorageSummary;

static void App_TaskStorageUpdateSummary(void)
{
    g_appTaskStorageSummary.initialized = g_appTaskStorageContext.initialized;
    g_appTaskStorageSummary.defaultsApplied = g_appTaskStorageContext.defaultsApplied;
    g_appTaskStorageSummary.eepromValid = g_appTaskStorageContext.eepromValid;
    g_appTaskStorageSummary.flashValid = g_appTaskStorageContext.flashValid;
    g_appTaskStorageSummary.commitPending = g_appTaskStorageContext.commitPending;
    g_appTaskStorageSummary.flashCommitPending = g_appTaskStorageContext.flashCommitPending;
    g_appTaskStorageSummary.sequence = g_appTaskStorageContext.active.sequence;
    g_appTaskStorageSummary.activeEepromAddress = g_appTaskStorageContext.activeEepromAddress;
    g_appTaskStorageSummary.activeFlashAddress = g_appTaskStorageContext.activeFlashAddress;
    g_appTaskStorageSummary.activeUserData0 = g_appTaskStorageContext.active.userData0;
    g_appTaskStorageSummary.activeUserData1 = g_appTaskStorageContext.active.userData1;
    g_appTaskStorageSummary.eepromWriteCount = g_appTaskStorageContext.eepromWriteCount;
    g_appTaskStorageSummary.flashWriteCount = g_appTaskStorageContext.flashWriteCount;
    g_appTaskStorageSummary.lastCommitTickMs = g_appTaskStorageContext.lastCommitTickMs;
    g_appTaskStorageSummary.lastLoadStatus = g_appTaskStorageContext.lastLoadStatus;
    g_appTaskStorageSummary.lastCommitStatus = g_appTaskStorageContext.lastCommitStatus;
}

static uint32_t App_TaskStorageIf_GetFlashPartitionStart(void)
{
    uint32_t flashSizeBytes;
    uint32_t partitionBytes;

    flashSizeBytes = ((uint32_t)(*(__IO uint16_t *)FLASHSIZE_BASE)) * 1024u;
    partitionBytes = APP_STORAGE_FLASH_PARTITION_PAGE_COUNT * FLASH_PAGE_SIZE;
    return (FLASH_BASE + flashSizeBytes - partitionBytes);
}

static uint32_t App_TaskStorageIf_GetFlashPartitionEnd(void)
{
    uint32_t flashSizeBytes;

    flashSizeBytes = ((uint32_t)(*(__IO uint16_t *)FLASHSIZE_BASE)) * 1024u;
    return (FLASH_BASE + flashSizeBytes);
}

static uint32_t App_TaskStorageIf_GetEepromSlotAddress(uint32_t slotIndex)
{
    return (DATA_EEPROM_BASE + (slotIndex * APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES));
}

static uint32_t App_TaskStorageIf_CalculateCrc32(const AppStorageParameterBlock_t *p_block)
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

static uint8_t App_TaskStorageIf_IsBlank(const void *p_data, uint32_t sizeBytes)
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

static uint8_t App_TaskStorageIf_IsBlockValid(const AppStorageParameterBlock_t *p_block)
{
    if (p_block == NULL)
    {
        return APP_FALSE;
    }

    if (App_TaskStorageIf_IsBlank(p_block, sizeof(*p_block)) == APP_TRUE)
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

    return (App_TaskStorageIf_CalculateCrc32(p_block) == p_block->crc32) ? APP_TRUE : APP_FALSE;
}

static void App_TaskStorageIf_SetDefaults(AppStorageParameterBlock_t *p_block)
{
    (void)memset(p_block, 0, sizeof(*p_block));
    p_block->magic = APP_STORAGE_PARAM_MAGIC;
    p_block->layoutRev = APP_STORAGE_LAYOUT_REV;
    p_block->payloadSize = sizeof(AppStorageParameterBlock_t);
    p_block->sequence = 1u;
    p_block->flags = APP_STORAGE_PARAM_FLAGS_VALID;
    p_block->meterPeriodMs = APP_SCHEDULER_TASK_METER_PERIOD_MS;
    p_block->rtcWakePeriodMs = APP_RTC_WAKEUP_PERIOD_MS;
    p_block->stopQualifyCount = APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT;
    p_block->userData0 = 0u;
    p_block->userData1 = 0u;
    p_block->crc32 = App_TaskStorageIf_CalculateCrc32(p_block);
}

static AppStatus_t App_TaskStorageIf_LoadLatestEeprom(AppStorageParameterBlock_t *p_block, uint32_t *p_address)
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
        candidateAddress = App_TaskStorageIf_GetEepromSlotAddress(slotIndex);
        (void)memcpy(&candidate, (const void *)candidateAddress, sizeof(candidate));

        if (App_TaskStorageIf_IsBlockValid(&candidate) != APP_TRUE)
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

static AppStatus_t App_TaskStorageIf_LoadLatestFlash(AppStorageParameterBlock_t *p_block, uint32_t *p_address)
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
    partitionStart = App_TaskStorageIf_GetFlashPartitionStart();
    partitionEnd = App_TaskStorageIf_GetFlashPartitionEnd();
    (void)memset(&bestBlock, 0, sizeof(bestBlock));

    for (candidateAddress = partitionStart;
         candidateAddress < partitionEnd;
         candidateAddress += APP_STORAGE_FLASH_RECORD_STRIDE_BYTES)
    {
        (void)memcpy(&candidate, (const void *)candidateAddress, sizeof(candidate));

        if (App_TaskStorageIf_IsBlockValid(&candidate) != APP_TRUE)
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

static AppStatus_t App_TaskStorageIf_WriteEepromBlock(const AppStorageParameterBlock_t *p_block, uint32_t address)
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

static AppStatus_t App_TaskStorageIf_EraseFlashPartition(void)
{
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t pageError;

    (void)memset(&eraseInit, 0, sizeof(eraseInit));
    pageError = 0u;

    APP_RETURN_IF_HAL_ERROR(HAL_FLASH_Unlock(), APP_STATUS_INIT_FAILED);

    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = App_TaskStorageIf_GetFlashPartitionStart();
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

static AppStatus_t App_TaskStorageIf_WriteFlashBlock(const AppStorageParameterBlock_t *p_block, uint32_t *p_writtenAddress)
{
    const uint32_t *p_words;
    uint32_t targetAddress;
    uint32_t partitionStart;
    uint32_t partitionEnd;
    uint32_t wordIndex;

    APP_RETURN_IF_FALSE((p_block != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_FLASH_RECORD_STRIDE_BYTES >= sizeof(*p_block), APP_STATUS_INVALID_PARAM);

    partitionStart = App_TaskStorageIf_GetFlashPartitionStart();
    partitionEnd = App_TaskStorageIf_GetFlashPartitionEnd();
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
        APP_RETURN_IF_FALSE(App_TaskStorageIf_EraseFlashPartition() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
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

static AppStatus_t App_TaskStoragePublishResponse(uint8_t destinationId,
                                                  uint8_t operation,
                                                  uint8_t backend,
                                                  uint32_t requestTickMs,
                                                  AppStatus_t status,
                                                  uint32_t userData0,
                                                  uint32_t userData1,
                                                  uint32_t sequence)
{
    AppMsgqMessage_t message;

    if (destinationId == APP_STORAGE_REQUESTER_NONE)
    {
        return APP_STATUS_OK;
    }

    (void)memset(&message, 0, sizeof(message));
    message.type = APP_MSGQ_TYPE_STORAGE_RESPONSE;
    message.sourceId = (uint8_t)APP_TASK_ID_STORAGE;
    message.reserved0 = operation;
    message.reserved1 = backend;
    message.tickMs = requestTickMs;
    message.param0 = (uint32_t)status;
    message.param1 = userData0;
    message.param2 = userData1;
    message.param3 = sequence;
    return App_MsgqPush(&message);
}

static AppStatus_t App_TaskStorageIf_LoadParameterBlocks(void)
{
    AppStorageParameterBlock_t eepromBlock;
    AppStorageParameterBlock_t flashBlock;
    AppStatus_t eepromStatus;
    AppStatus_t flashStatus;
    uint32_t eepromAddress;
    uint32_t flashAddress;

    (void)memset(&g_appTaskStorageContext, 0, sizeof(g_appTaskStorageContext));
    (void)memset(&g_appTaskStorageSummary, 0, sizeof(g_appTaskStorageSummary));
    g_appTaskStorageContext.activeEepromAddress = APP_STORAGE_INVALID_ADDRESS;
    g_appTaskStorageContext.activeFlashAddress = APP_STORAGE_INVALID_ADDRESS;
    g_appTaskStorageContext.lastLoadStatus = APP_STATUS_INIT_FAILED;
    g_appTaskStorageContext.lastCommitStatus = APP_STATUS_NOT_INITIALIZED;
    g_appTaskStorageContext.pendingRequester = APP_STORAGE_REQUESTER_NONE;

    APP_RETURN_IF_FALSE(APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES >= sizeof(AppStorageParameterBlock_t), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_FLASH_RECORD_STRIDE_BYTES >= sizeof(AppStorageParameterBlock_t), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(APP_STORAGE_FLASH_PARTITION_PAGE_COUNT != 0u, APP_STATUS_INVALID_PARAM);

    eepromStatus = App_TaskStorageIf_LoadLatestEeprom(&eepromBlock, &eepromAddress);
    flashStatus = App_TaskStorageIf_LoadLatestFlash(&flashBlock, &flashAddress);

    g_appTaskStorageContext.eepromValid = (eepromStatus == APP_STATUS_OK) ? APP_TRUE : APP_FALSE;
    g_appTaskStorageContext.flashValid = (flashStatus == APP_STATUS_OK) ? APP_TRUE : APP_FALSE;

    if ((eepromStatus == APP_STATUS_OK) && (flashStatus == APP_STATUS_OK))
    {
        if (eepromBlock.sequence >= flashBlock.sequence)
        {
            g_appTaskStorageContext.active = eepromBlock;
            g_appTaskStorageContext.activeEepromAddress = eepromAddress;
            g_appTaskStorageContext.activeFlashAddress = flashAddress;
        }
        else
        {
            g_appTaskStorageContext.active = flashBlock;
            g_appTaskStorageContext.activeFlashAddress = flashAddress;
            g_appTaskStorageContext.commitPending = APP_TRUE;
            g_appTaskStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_EEPROM;
        }
    }
    else if (eepromStatus == APP_STATUS_OK)
    {
        g_appTaskStorageContext.active = eepromBlock;
        g_appTaskStorageContext.activeEepromAddress = eepromAddress;
        g_appTaskStorageContext.commitPending = APP_TRUE;
        g_appTaskStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_FLASH;
    }
    else if (flashStatus == APP_STATUS_OK)
    {
        g_appTaskStorageContext.active = flashBlock;
        g_appTaskStorageContext.activeFlashAddress = flashAddress;
        g_appTaskStorageContext.commitPending = APP_TRUE;
        g_appTaskStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_EEPROM;
    }
    else
    {
        App_TaskStorageIf_SetDefaults(&g_appTaskStorageContext.active);
        g_appTaskStorageContext.defaultsApplied = APP_TRUE;
        g_appTaskStorageContext.commitPending = APP_TRUE;
        g_appTaskStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_BOTH;
    }

    g_appTaskStorageContext.initialized = APP_TRUE;
    g_appTaskStorageContext.lastLoadStatus = APP_STATUS_OK;
    App_TaskStorageUpdateSummary();

    APP_TASK_DEBUG_PRINT("STOR",
                         "init eeprom=%u flash=%u seq=%lu commit=%u backend=%u",
                         (unsigned int)g_appTaskStorageContext.eepromValid,
                         (unsigned int)g_appTaskStorageContext.flashValid,
                         (unsigned long)g_appTaskStorageContext.active.sequence,
                         (unsigned int)g_appTaskStorageContext.commitPending,
                         (unsigned int)g_appTaskStorageContext.pendingBackend);
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskStorageProcessOneRequest(const AppMsgqMessage_t *p_request)
{
    AppStorageParameterBlock_t loadedBlock;
    AppStatus_t status;
    uint32_t loadedAddress;

    APP_RETURN_IF_FALSE((p_request != NULL), APP_STATUS_INVALID_PARAM);

    if (p_request->reserved0 == (uint8_t)APP_STORAGE_QUEUE_OP_SAVE)
    {
        APP_RETURN_IF_FALSE((p_request->reserved1 == (uint8_t)APP_STORAGE_TARGET_EEPROM) ||
                            (p_request->reserved1 == (uint8_t)APP_STORAGE_TARGET_FLASH) ||
                            (p_request->reserved1 == (uint8_t)APP_STORAGE_TARGET_BOTH), APP_STATUS_INVALID_PARAM);

        if (g_appTaskStorageContext.active.sequence == 0u)
        {
            g_appTaskStorageContext.active.sequence = 1u;
        }
        else
        {
            g_appTaskStorageContext.active.sequence++;
        }

        g_appTaskStorageContext.active.magic = APP_STORAGE_PARAM_MAGIC;
        g_appTaskStorageContext.active.layoutRev = APP_STORAGE_LAYOUT_REV;
        g_appTaskStorageContext.active.payloadSize = sizeof(AppStorageParameterBlock_t);
        g_appTaskStorageContext.active.flags = APP_STORAGE_PARAM_FLAGS_VALID;
        g_appTaskStorageContext.active.userData0 = p_request->param0;
        g_appTaskStorageContext.active.userData1 = p_request->param1;
        g_appTaskStorageContext.commitPending = APP_TRUE;
        g_appTaskStorageContext.pendingBackend = p_request->reserved1;
        g_appTaskStorageContext.pendingRequester = p_request->sourceId;
        g_appTaskStorageContext.pendingOperation = p_request->reserved0;
        g_appTaskStorageContext.pendingRequestTickMs = p_request->tickMs;
        g_appTaskStorageContext.defaultsApplied = APP_FALSE;
        g_appTaskStorageContext.lastCommitStatus = APP_STATUS_OK;
        App_TaskStorageUpdateSummary();
        return APP_STATUS_OK;
    }

    if (p_request->reserved0 == (uint8_t)APP_STORAGE_QUEUE_OP_LOAD)
    {
        APP_RETURN_IF_FALSE((p_request->reserved1 == (uint8_t)APP_STORAGE_TARGET_EEPROM) ||
                            (p_request->reserved1 == (uint8_t)APP_STORAGE_TARGET_FLASH), APP_STATUS_INVALID_PARAM);

        if (p_request->reserved1 == (uint8_t)APP_STORAGE_TARGET_EEPROM)
        {
            status = App_TaskStorageIf_LoadLatestEeprom(&loadedBlock, &loadedAddress);
        }
        else
        {
            status = App_TaskStorageIf_LoadLatestFlash(&loadedBlock, &loadedAddress);
        }

        if (status == APP_STATUS_OK)
        {
            APP_RETURN_IF_FALSE(App_TaskStoragePublishResponse(p_request->sourceId,
                                                               p_request->reserved0,
                                                               p_request->reserved1,
                                                               p_request->tickMs,
                                                               APP_STATUS_OK,
                                                               loadedBlock.userData0,
                                                               loadedBlock.userData1,
                                                               loadedBlock.sequence) == APP_STATUS_OK,
                                APP_STATUS_MSGQ_FULL);
        }
        else
        {
            APP_RETURN_IF_FALSE(App_TaskStoragePublishResponse(p_request->sourceId,
                                                               p_request->reserved0,
                                                               p_request->reserved1,
                                                               p_request->tickMs,
                                                               status,
                                                               0u,
                                                               0u,
                                                               0u) == APP_STATUS_OK,
                                APP_STATUS_MSGQ_FULL);
        }

        return APP_STATUS_OK;
    }

    return APP_STATUS_INVALID_PARAM;
}

static AppStatus_t App_TaskStorageDrainRequests(void)
{
    AppMsgqMessage_t request;
    AppStatus_t status;
    uint32_t processed;

    processed = 0u;
    while (processed < APP_STORAGE_MAX_REQUESTS_PER_RUN)
    {
        status = App_MsgqTakeFirstByType(APP_MSGQ_TYPE_STORAGE_REQUEST, &request);
        if (status == APP_STATUS_MSGQ_EMPTY)
        {
            return APP_STATUS_OK;
        }
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        APP_RETURN_IF_FALSE(App_TaskStorageProcessOneRequest(&request) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
        processed++;

        if (g_appTaskStorageContext.commitPending == APP_TRUE)
        {
            break;
        }
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_TaskStorageIf_CommitOneRecord(void)
{
    AppStorageParameterBlock_t commitBlock;
    AppStatus_t status;
    uint32_t nextEepromAddress;
    uint32_t writtenFlashAddress;
    uint32_t currentSlotIndex;
    uint8_t writeEeprom;
    uint8_t writeFlash;

    if (g_appTaskStorageContext.commitPending != APP_TRUE)
    {
        g_appTaskStorageContext.lastCommitStatus = APP_STATUS_OK;
        App_TaskStorageUpdateSummary();
        return APP_STATUS_OK;
    }

    commitBlock = g_appTaskStorageContext.active;
    commitBlock.crc32 = App_TaskStorageIf_CalculateCrc32(&commitBlock);

    writeEeprom = ((g_appTaskStorageContext.pendingBackend == (uint8_t)APP_STORAGE_TARGET_EEPROM) ||
                   (g_appTaskStorageContext.pendingBackend == (uint8_t)APP_STORAGE_TARGET_BOTH)) ? APP_TRUE : APP_FALSE;
    writeFlash = ((g_appTaskStorageContext.pendingBackend == (uint8_t)APP_STORAGE_TARGET_FLASH) ||
                  (g_appTaskStorageContext.pendingBackend == (uint8_t)APP_STORAGE_TARGET_BOTH)) ? APP_TRUE : APP_FALSE;

    if (writeEeprom == APP_TRUE)
    {
        if (g_appTaskStorageContext.activeEepromAddress == APP_STORAGE_INVALID_ADDRESS)
        {
            currentSlotIndex = 0u;
        }
        else
        {
            currentSlotIndex = (g_appTaskStorageContext.activeEepromAddress - DATA_EEPROM_BASE) / APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES;
            currentSlotIndex = (currentSlotIndex + 1u) % APP_STORAGE_DATA_EEPROM_SLOT_COUNT;
        }
        nextEepromAddress = App_TaskStorageIf_GetEepromSlotAddress(currentSlotIndex);

        status = App_TaskStorageIf_WriteEepromBlock(&commitBlock, nextEepromAddress);
        if (status != APP_STATUS_OK)
        {
            g_appTaskStorageContext.lastCommitStatus = status;
            (void)App_TaskStoragePublishResponse(g_appTaskStorageContext.pendingRequester,
                                                g_appTaskStorageContext.pendingOperation,
                                                g_appTaskStorageContext.pendingBackend,
                                                g_appTaskStorageContext.pendingRequestTickMs,
                                                status,
                                                0u,
                                                0u,
                                                0u);
            App_TaskStorageUpdateSummary();
            return status;
        }

        g_appTaskStorageContext.activeEepromAddress = nextEepromAddress;
        g_appTaskStorageContext.eepromValid = APP_TRUE;
        g_appTaskStorageContext.eepromWriteCount++;
    }

    if (writeFlash == APP_TRUE)
    {
        status = App_TaskStorageIf_WriteFlashBlock(&commitBlock, &writtenFlashAddress);
        if (status != APP_STATUS_OK)
        {
            g_appTaskStorageContext.lastCommitStatus = status;
            (void)App_TaskStoragePublishResponse(g_appTaskStorageContext.pendingRequester,
                                                g_appTaskStorageContext.pendingOperation,
                                                g_appTaskStorageContext.pendingBackend,
                                                g_appTaskStorageContext.pendingRequestTickMs,
                                                status,
                                                0u,
                                                0u,
                                                0u);
            App_TaskStorageUpdateSummary();
            return status;
        }

        g_appTaskStorageContext.activeFlashAddress = writtenFlashAddress;
        g_appTaskStorageContext.flashValid = APP_TRUE;
        g_appTaskStorageContext.flashWriteCount++;
    }

    g_appTaskStorageContext.active = commitBlock;
    g_appTaskStorageContext.commitPending = APP_FALSE;
    g_appTaskStorageContext.flashCommitPending = (writeFlash == APP_TRUE) ? APP_TRUE : APP_FALSE;
    g_appTaskStorageContext.lastCommitTickMs = HAL_GetTick();
    g_appTaskStorageContext.lastCommitStatus = APP_STATUS_OK;

    status = App_TaskStoragePublishResponse(g_appTaskStorageContext.pendingRequester,
                                            g_appTaskStorageContext.pendingOperation,
                                            g_appTaskStorageContext.pendingBackend,
                                            g_appTaskStorageContext.pendingRequestTickMs,
                                            APP_STATUS_OK,
                                            g_appTaskStorageContext.active.userData0,
                                            g_appTaskStorageContext.active.userData1,
                                            g_appTaskStorageContext.active.sequence);
    APP_RETURN_IF_FALSE((status == APP_STATUS_OK) || (g_appTaskStorageContext.pendingRequester == APP_STORAGE_REQUESTER_NONE), APP_STATUS_MSGQ_FULL);

    g_appTaskStorageContext.pendingRequester = APP_STORAGE_REQUESTER_NONE;
    g_appTaskStorageContext.pendingOperation = 0u;
    g_appTaskStorageContext.pendingRequestTickMs = 0u;
    App_TaskStorageUpdateSummary();

    APP_TASK_DEBUG_PRINT("STOR",
                         "commit seq=%lu backend=%u eep=0x%08lX flash=0x%08lX data=%08lX/%08lX",
                         (unsigned long)g_appTaskStorageContext.active.sequence,
                         (unsigned int)g_appTaskStorageContext.pendingBackend,
                         (unsigned long)g_appTaskStorageContext.activeEepromAddress,
                         (unsigned long)g_appTaskStorageContext.activeFlashAddress,
                         (unsigned long)g_appTaskStorageContext.active.userData0,
                         (unsigned long)g_appTaskStorageContext.active.userData1);
    return APP_STATUS_OK;
}

AppStatus_t App_TaskStorage(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_STORAGE_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskStorageIf_LoadParameterBlocks() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = (g_appTaskStorageContext.commitPending == APP_TRUE) ? APP_TRUE : APP_FALSE;
            p_module->eventPending = p_module->busy;
            APP_TASK_SET_STATE(p_module, APP_TASK_STORAGE_STATE_SCAN_QUEUE);
            break;

        case APP_TASK_STORAGE_STATE_SCAN_QUEUE:
            if (g_appTaskStorageContext.commitPending == APP_TRUE)
            {
                p_module->busy = APP_TRUE;
                p_module->eventPending = APP_TRUE;
                APP_TASK_SET_STATE(p_module, APP_TASK_STORAGE_STATE_COMMIT_ONE);
                break;
            }

            APP_RETURN_IF_FALSE(App_TaskStorageDrainRequests() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = (g_appTaskStorageContext.commitPending == APP_TRUE) ? APP_TRUE : APP_FALSE;
            p_module->eventPending = p_module->busy;
            if (g_appTaskStorageContext.commitPending == APP_TRUE)
            {
                APP_TASK_SET_STATE(p_module, APP_TASK_STORAGE_STATE_COMMIT_ONE);
            }
            else
            {
                p_module->lastActionTickMs = HAL_GetTick();
            }
            break;

        case APP_TASK_STORAGE_STATE_COMMIT_ONE:
        default:
            APP_RETURN_IF_FALSE(App_TaskStorageIf_CommitOneRecord() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->eventPending = APP_FALSE;
            p_module->lastActionTickMs = HAL_GetTick();
            APP_TASK_SET_STATE(p_module, APP_TASK_STORAGE_STATE_SCAN_QUEUE);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

const AppTaskStorageSummary_t *App_TaskStorageGetSummary(void)
{
    return &g_appTaskStorageSummary;
}

AppStatus_t App_TaskStorageRequestSave(void)
{
    return App_TaskMainRequestStorageSave(APP_STORAGE_TARGET_BOTH,
                                          g_appTaskStorageContext.active.userData0,
                                          g_appTaskStorageContext.active.userData1);
}

AppStatus_t App_TaskStorageLoadDefaults(void)
{
    uint32_t nextSequence;

    APP_RETURN_IF_FALSE(g_appTaskStorageContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    nextSequence = (g_appTaskStorageContext.active.sequence == 0u) ? 1u : (g_appTaskStorageContext.active.sequence + 1u);
    App_TaskStorageIf_SetDefaults(&g_appTaskStorageContext.active);
    g_appTaskStorageContext.active.sequence = nextSequence;
    g_appTaskStorageContext.active.crc32 = App_TaskStorageIf_CalculateCrc32(&g_appTaskStorageContext.active);
    g_appTaskStorageContext.defaultsApplied = APP_TRUE;
    g_appTaskStorageContext.commitPending = APP_TRUE;
    g_appTaskStorageContext.pendingBackend = (uint8_t)APP_STORAGE_TARGET_BOTH;
    g_appTaskStorageContext.pendingRequester = APP_STORAGE_REQUESTER_NONE;
    g_appTaskStorageContext.pendingOperation = (uint8_t)APP_STORAGE_QUEUE_OP_SAVE;
    g_appTaskStorageContext.pendingRequestTickMs = HAL_GetTick();
    App_TaskStorageUpdateSummary();
    return APP_STATUS_OK;
}

#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

typedef struct
{
    uint8_t initialized;
    uint8_t defaultsApplied;
    uint8_t eepromValid;
    uint8_t flashValid;
    uint8_t commitPending;
    uint8_t flashCommitPending;
    uint32_t sequence;
    uint32_t activeEepromAddress;
    uint32_t activeFlashAddress;
    uint32_t activeUserData0;
    uint32_t activeUserData1;
    uint32_t eepromWriteCount;
    uint32_t flashWriteCount;
    uint32_t lastCommitTickMs;
    AppStatus_t lastLoadStatus;
    AppStatus_t lastCommitStatus;
} AppStorageSummary_t;


typedef enum
{
    APP_STORAGE_TARGET_EEPROM = 1u,
    APP_STORAGE_TARGET_FLASH = 2u,
    APP_STORAGE_TARGET_BOTH = 3u
} AppStorageTarget_t;

typedef enum
{
    APP_STORAGE_QUEUE_OP_SAVE = 1u,
    APP_STORAGE_QUEUE_OP_LOAD = 2u
} AppStorageQueueOp_t;

AppStatus_t App_StorageIf_LoadParameterBlocks(void);
AppStatus_t App_StorageLoadDefaults(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_STORAGE_H */

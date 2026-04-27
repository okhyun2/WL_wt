#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"

static AppStatus_t App_TaskStorageIf_LoadParameterBlocks(void)
{
    /* PSEUDO external interface:
     * - HAL_CRC_Calculate(APP_CRC_HANDLE, ...)
     * - EEPROM/Flash read driver
     * - parameter version validation
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskStorageIf_CommitOneRecord(void)
{
    /* PSEUDO external interface:
     * - Flash erase/program
     * - EEPROM write
     * - configuration dirty-flag clear
     */
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
            p_module->state = APP_TASK_STORAGE_STATE_SCAN_QUEUE;
            break;

        case APP_TASK_STORAGE_STATE_SCAN_QUEUE:
            /* PSEUDO: inspect save requests from NFC/server/meter/main task. */
            p_module->eventPending = APP_FALSE;
            p_module->state = APP_TASK_STORAGE_STATE_COMMIT_ONE;
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskStorageIf_CommitOneRecord() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->state = APP_TASK_STORAGE_STATE_SCAN_QUEUE;
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

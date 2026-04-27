#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_error.h"
#include "app_log.h"

static AppStatus_t App_TaskHousekeepingIf_SnapshotDiagnostics(void)
{
    /* PSEUDO external interface:
     * - App_ErrorGetLast()
     * - App_LogPrintf(APP_LOG_LEVEL_DEBUG, ...)
     * - future counters/health modules
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskHousekeepingIf_RotateDeferredWork(void)
{
    /* PSEUDO external interface:
     * - deferred diagnostic buffers
     * - maintenance queues
     * - low priority telemetry preparation
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskHousekeeping(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_HOUSEKEEPING_STATE_INIT:
            APP_TASK_SET_STATE(p_module, APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT);
            break;

        case APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT:
            APP_RETURN_IF_FALSE(App_TaskHousekeepingIf_SnapshotDiagnostics() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_module, APP_TASK_HOUSEKEEPING_STATE_ROTATE);
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskHousekeepingIf_RotateDeferredWork() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_module, APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT);
            break;
    }

    p_module->busy = APP_FALSE;
    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

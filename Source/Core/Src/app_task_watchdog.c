#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"

static AppStatus_t App_TaskWatchdogIf_RefreshHardwareWatchdog(void)
{
    /* External interface: HAL_IWDG_Refresh(APP_IWDG_HANDLE) */
    APP_RETURN_IF_HAL_ERROR(HAL_IWDG_Refresh(APP_IWDG_HANDLE), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

AppStatus_t App_TaskWatchdog(void *p_context)
{
    AppTaskModuleContext_t *p_module;
    AppStatus_t status;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    p_module->state = (p_module->state == APP_TASK_WATCHDOG_STATE_INIT) ? APP_TASK_WATCHDOG_STATE_REFRESH : APP_TASK_WATCHDOG_STATE_REFRESH;
    status = App_TaskWatchdogIf_RefreshHardwareWatchdog();
    p_module->busy = APP_FALSE;
    p_module->lastActionTickMs = HAL_GetTick();
    return App_TasksCompleteRun(p_module, status);
}

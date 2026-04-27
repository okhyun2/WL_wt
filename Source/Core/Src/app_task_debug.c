#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_debug.h"

static AppStatus_t App_TaskDebugIf_ProcessConsole(void)
{
    /* External interface: App_DebugConsoleProcess() */
    return App_DebugConsoleProcess();
}

AppStatus_t App_TaskDebug(void *p_context)
{
    AppTaskModuleContext_t *p_module;
    AppStatus_t status;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    if (p_module->state == APP_TASK_DEBUG_STATE_INIT)
    {
        APP_TASK_SET_STATE(p_module, APP_TASK_DEBUG_STATE_POLL);
#ifdef DEBUG
        APP_TASK_DEBUG_PRINT("DEBUG", "console task entered POLL state");
#endif
    }

    status = App_TaskDebugIf_ProcessConsole();
    p_module->busy = APP_FALSE;
    return App_TasksCompleteRun(p_module, status);
}

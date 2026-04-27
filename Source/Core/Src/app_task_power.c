#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_system.h"

static AppStatus_t App_TaskPowerIf_LoadPolicy(void)
{
    /* PSEUDO external interface:
     * - board power thresholds
     * - wake source masks
     * - stop-entry guards
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskPowerIf_EvaluatePowerConditions(void)
{
    /* PSEUDO external interface:
     * - App_SystemPrepareForStop()
     * - App_SystemRecoverFromStop()
     * - HAL_PWR_EnterSTOPMode(...)
     * - App_SystemSetNbiotPowered(...)
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskPower(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_POWER_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskPowerIf_LoadPolicy() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->state = APP_TASK_POWER_STATE_EVALUATE;
            break;

        case APP_TASK_POWER_STATE_EVALUATE:
            APP_RETURN_IF_FALSE(App_TaskPowerIf_EvaluatePowerConditions() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->eventPending = APP_FALSE;
            p_module->state = APP_TASK_POWER_STATE_DECIDE_IDLE;
            break;

        default:
            /* PSEUDO: if all modules are quiescent, request low power transition next cycle. */
            p_module->busy = APP_FALSE;
            p_module->state = APP_TASK_POWER_STATE_EVALUATE;
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

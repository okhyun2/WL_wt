#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_system.h"

static AppStatus_t App_TaskPowerIf_LoadPolicy(void)
{
    /* PSEUDO external interface:
     * - board power thresholds
     * - wake source masks
     * - sleep/stop entry guards
     */
#ifdef DEBUG
    APP_TASK_DEBUG_PRINT("POWER",
                         "policy loaded: power_period=%lu main_period=%lu idle_delay=%lu",
                         (unsigned long)APP_SCHEDULER_TASK_POWER_PERIOD_MS,
                         (unsigned long)APP_SCHEDULER_TASK_MAIN_PERIOD_MS,
                         (unsigned long)APP_SCHEDULER_IDLE_DELAY_MS);
#endif
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskPowerIf_EvaluatePowerConditions(void)
{
    AppTaskMainDecision_t decision;
    uint8_t allowLowPower;

    /* PSEUDO external interface:
     * - App_TaskMainGetDecision()
     * - App_SystemRequestLowPower(APP_TRUE/APP_FALSE)
     * - App_SystemSetNbiotPowered(APP_TRUE/APP_FALSE)
     * - battery / modem / session guard checks
     */
    decision = App_TaskMainGetDecision();
    allowLowPower = (decision == APP_TASK_MAIN_DECISION_ALLOW_IDLE) ? APP_TRUE : APP_FALSE;
#ifdef DEBUG
    APP_TASK_DEBUG_PRINT("POWER",
                         "evaluate: main=%s allow_stop=%u",
                         App_TaskMainGetDecisionString(),
                         (unsigned int)allowLowPower);
#endif
    return App_SystemRequestLowPower(allowLowPower);
}

AppStatus_t App_TaskPower(void *p_context)
{
    AppTaskModuleContext_t *p_module;
    AppStatus_t status;
#ifdef DEBUG
    const AppSystemContext_t *p_system;
#endif

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    status = APP_STATUS_OK;
    p_module->busy = APP_TRUE;
#ifdef DEBUG
    p_system = App_SystemGetContext();
#endif

    switch (p_module->state)
    {
        case APP_TASK_POWER_STATE_INIT:
            status = App_TaskPowerIf_LoadPolicy();
            APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("POWER", "state=INIT complete");
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_POWER_STATE_EVALUATE);
            break;

        case APP_TASK_POWER_STATE_EVALUATE:
            status = App_TaskPowerIf_EvaluatePowerConditions();
            APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
            p_module->eventPending = APP_FALSE;
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("POWER",
                                 "state=EVALUATE stop_req=%u wake=%s",
                                 (unsigned int)p_system->stopRequested,
                                 App_SystemGetWakeSourceString());
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_POWER_STATE_DECIDE_IDLE);
            break;

        case APP_TASK_POWER_STATE_DECIDE_IDLE:
        default:
            status = App_TaskPowerIf_EvaluatePowerConditions();
            APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
            p_module->busy = APP_FALSE;
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("POWER",
                                 "state=DECIDE_IDLE next=EVALUATE stop_req=%u",
                                 (unsigned int)p_system->stopRequested);
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_POWER_STATE_EVALUATE);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_system.h"

#define APP_POWER_MAX_REQUESTS_PER_RUN    (4u)
#define APP_POWER_BOOT_HOLD_RESET_MS      (1000u)

static uint8_t g_appTaskPowerResetBootPending;

static AppStatus_t App_TaskPowerIf_LoadPolicy(void)
{
#ifdef DEBUG
    APP_TASK_DEBUG_PRINT("POWER", "policy loaded: power_period=%lu main_period=%lu idle_delay=%lu",
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

    decision = App_TaskMainGetDecision();
    allowLowPower = (decision == APP_TASK_MAIN_DECISION_ALLOW_IDLE) ? APP_TRUE : APP_FALSE;

#ifdef DEBUG
    APP_TASK_DEBUG_PRINT("POWER", "evaluate: main=%s allow_stop=%u resetBoot_pending=%u",
                         App_TaskMainGetDecisionString(),
                         (unsigned int)allowLowPower,
                         (unsigned int)g_appTaskPowerResetBootPending);
#endif
    return App_SystemRequestLowPower(allowLowPower);
}

static AppStatus_t App_TaskPowerProcessOneRequest(const AppMsgqMessage_t *p_request)
{
    APP_RETURN_IF_FALSE((p_request != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_request->type == APP_MSGQ_TYPE_POWER_REQUEST, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_request->reserved0 == (uint8_t)APP_POWER_QUEUE_OP_RESET_BOOT, APP_STATUS_INVALID_PARAM);

    g_appTaskPowerResetBootPending = APP_TRUE;

#ifdef DEBUG
    APP_TASK_DEBUG_PRINT("POWER", "resetboot requested by task=%u tick=%lu",
                         (unsigned int)p_request->sourceId,
                         (unsigned long)p_request->tickMs);
#endif
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskPowerDrainRequests(void)
{
    AppMsgqMessage_t request;
    AppStatus_t status;
    uint32_t processed;

    processed = 0u;
    while (processed < APP_POWER_MAX_REQUESTS_PER_RUN)
    {
        status = App_MsgqTakeFirstByType(APP_MSGQ_TYPE_POWER_REQUEST, &request);
        if (status == APP_STATUS_MSGQ_EMPTY)
        {
            return APP_STATUS_OK;
        }
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        APP_RETURN_IF_FALSE(App_TaskPowerProcessOneRequest(&request) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
        processed++;

        if (g_appTaskPowerResetBootPending == APP_TRUE)
        {
            break;
        }
    }

    return APP_STATUS_OK;
}

static void App_TaskPowerExecutePendingResetBoot(void)
{
    if (g_appTaskPowerResetBootPending != APP_TRUE)
    {
        return;
    }

    (void)APP_LOGW("POWER", "executing reset boot-hold=%lu ms",
                         (unsigned long)APP_POWER_BOOT_HOLD_RESET_MS);
    App_HwSetChargeBoot0(GPIO_PIN_SET);
    HAL_Delay(APP_POWER_BOOT_HOLD_RESET_MS);
    __disable_irq();
    NVIC_SystemReset();
    while (1)
    {
    }
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
    p_module->eventPending = (g_appTaskPowerResetBootPending == APP_TRUE) ? APP_TRUE : APP_FALSE;
#ifdef DEBUG
    p_system = App_SystemGetContext();
#endif

    switch (p_module->state)
    {
        case APP_TASK_POWER_STATE_INIT:
            g_appTaskPowerResetBootPending = APP_FALSE;
            status = App_TaskPowerIf_LoadPolicy();
            APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("POWER", "state=INIT complete");
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_POWER_STATE_EVALUATE);
            break;

        case APP_TASK_POWER_STATE_EVALUATE:
            APP_RETURN_IF_FALSE(App_TaskPowerDrainRequests() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            if (g_appTaskPowerResetBootPending == APP_TRUE)
            {
                p_module->eventPending = APP_TRUE;
                p_module->lastActionTickMs = HAL_GetTick();
                App_TaskPowerExecutePendingResetBoot();
            }

            status = App_TaskPowerIf_EvaluatePowerConditions();
            APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
            p_module->eventPending = APP_FALSE;
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("POWER", "state=EVALUATE stop_req=%u wake=%s",
                                 (unsigned int)p_system->stopRequested,
                                 App_SystemGetWakeSourceString());
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_POWER_STATE_DECIDE_IDLE);
            break;

        case APP_TASK_POWER_STATE_DECIDE_IDLE:
        default:
            APP_RETURN_IF_FALSE(App_TaskPowerDrainRequests() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            if (g_appTaskPowerResetBootPending == APP_TRUE)
            {
                p_module->eventPending = APP_TRUE;
                p_module->lastActionTickMs = HAL_GetTick();
                App_TaskPowerExecutePendingResetBoot();
            }

            status = App_TaskPowerIf_EvaluatePowerConditions();
            APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
            p_module->busy = APP_FALSE;
            p_module->eventPending = APP_FALSE;
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("POWER", "state=DECIDE_IDLE next=EVALUATE stop_req=%u",
                                 (unsigned int)p_system->stopRequested);
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_POWER_STATE_EVALUATE);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_system.h"

static uint32_t g_appTaskRtcWakeHandleCount;

static AppStatus_t App_TaskRtcIf_InitService(void)
{
    g_appTaskRtcWakeHandleCount = 0u;
    APP_LOGI("RTC", "service init: wake_period=%lu ms",
                         (unsigned long)APP_RTC_WAKEUP_PERIOD_MS);
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskRtcIf_CheckSchedule(uint8_t eventPending)
{
    if (((App_SystemGetWakeSourceMask() & APP_SYSTEM_WAKE_SRC_RTC) != 0u) || (eventPending == APP_TRUE))
    {
        g_appTaskRtcWakeHandleCount++;
        APP_LOGD("RTC", "wake handled: count=%lu wake=%s",
                             (unsigned long)g_appTaskRtcWakeHandleCount,
                             App_SystemGetWakeSourceString());
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_TaskRtcIf_ApplySync(void)
{
    /* PSEUDO external interface:
     * - apply network/NFC time sync
     * - compute next wake alarm policy
     * - current implementation uses fixed periodic RTC wake-up in system layer
     */
    APP_LOGD("RTC", "schedule confirmed: periodic wake=%lu ms",
                         (unsigned long)APP_RTC_WAKEUP_PERIOD_MS);
    return APP_STATUS_OK;
}

AppStatus_t App_TaskRtc(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    p_module->busy = APP_TRUE;

    switch (p_module->state)
    {
        case APP_TASK_RTC_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskRtcIf_InitService() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_module, APP_TASK_RTC_STATE_CHECK_SCHEDULE);
            break;

        case APP_TASK_RTC_STATE_CHECK_SCHEDULE:
            APP_RETURN_IF_FALSE(App_TaskRtcIf_CheckSchedule(p_module->eventPending) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->eventPending = APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_RTC_STATE_APPLY_SYNC);
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskRtcIf_ApplySync() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->eventPending = APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_RTC_STATE_CHECK_SCHEDULE);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

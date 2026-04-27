#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"

static AppStatus_t App_TaskRtcIf_InitService(void)
{
    /* PSEUDO external interface:
     * - RTC service init
     * - drift/epoch cache init
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskRtcIf_CheckSchedule(void)
{
    /* PSEUDO external interface:
     * - wake alarm scan
     * - periodic acquisition schedule
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskRtcIf_ApplySync(void)
{
    /* PSEUDO external interface:
     * - apply network/NFC time sync
     * - compute next wake alarm
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskRtc(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_RTC_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskRtcIf_InitService() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->state = APP_TASK_RTC_STATE_CHECK_SCHEDULE;
            break;

        case APP_TASK_RTC_STATE_CHECK_SCHEDULE:
            APP_RETURN_IF_FALSE(App_TaskRtcIf_CheckSchedule() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->state = APP_TASK_RTC_STATE_APPLY_SYNC;
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskRtcIf_ApplySync() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->state = APP_TASK_RTC_STATE_CHECK_SCHEDULE;
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

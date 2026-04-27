#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"

static AppTaskWatchdogSummary_t g_appTaskWatchdogContext;

static uint32_t App_TaskWatchdogIf_GetExternalPulseTicks(void)
{
    uint32_t autoReloadTicks;
    uint32_t pulseTicks;

    autoReloadTicks = __HAL_TIM_GET_AUTORELOAD(APP_TIM_WD_FEED_HANDLE);
    if (autoReloadTicks == 0u)
    {
        autoReloadTicks = 1u;
    }

    pulseTicks = (((autoReloadTicks + 1u) * APP_WATCHDOG_EXTERNAL_FEED_DUTY_PERCENT) / 100u);
    if ((pulseTicks == 0u) || (pulseTicks > autoReloadTicks))
    {
        pulseTicks = autoReloadTicks / 2u;
        if (pulseTicks == 0u)
        {
            pulseTicks = 1u;
        }
    }

    return pulseTicks;
}

static AppStatus_t App_TaskWatchdogIf_ConfigureDebugFreeze(void)
{
#ifdef DEBUG
    /* External interface: __HAL_DBGMCU_FREEZE_IWDG(), __HAL_DBGMCU_FREEZE_TIM22() */
    __HAL_DBGMCU_FREEZE_IWDG();
    __HAL_DBGMCU_FREEZE_TIM22();
#endif
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskWatchdogIf_InitExternalFeed(void)
{
    uint32_t pulseTicks;

    /* External interface: HAL_TIM_PWM_Stop(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2) */
    APP_RETURN_IF_FALSE(APP_TIM_WD_FEED_HANDLE->Instance == TIM22, APP_STATUS_HW_HANDLE_INVALID);
    pulseTicks = App_TaskWatchdogIf_GetExternalPulseTicks();
    __HAL_TIM_SET_COMPARE(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2, 0u);
    __HAL_TIM_SET_COUNTER(APP_TIM_WD_FEED_HANDLE, 0u);
    (void)HAL_TIM_PWM_Stop(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2);

    g_appTaskWatchdogContext.externalFeedEnabled = (pulseTicks > 0u) ? APP_TRUE : APP_FALSE;
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskWatchdogIf_RefreshHardwareWatchdog(void)
{
    /* External interface: HAL_IWDG_Refresh(APP_IWDG_HANDLE) */
    APP_RETURN_IF_FALSE(APP_IWDG_HANDLE->Instance == IWDG, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_HAL_ERROR(HAL_IWDG_Refresh(APP_IWDG_HANDLE), APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskWatchdogIf_FeedExternalWatchdog(void)
{
    uint32_t pulseTicks;

    /* External interface sequence:
     * - __HAL_TIM_SET_COMPARE(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2, pulseTicks)
     * - HAL_TIM_PWM_Start(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2)
     * - HAL_Delay(APP_WATCHDOG_EXTERNAL_FEED_PULSE_MS)
     * - HAL_TIM_PWM_Stop(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2)
     */
    APP_RETURN_IF_FALSE(APP_TIM_WD_FEED_HANDLE->Instance == TIM22, APP_STATUS_HW_HANDLE_INVALID);

    pulseTicks = App_TaskWatchdogIf_GetExternalPulseTicks();
    APP_RETURN_IF_FALSE((pulseTicks > 0u), APP_STATUS_INVALID_PARAM);

    __HAL_TIM_SET_COUNTER(APP_TIM_WD_FEED_HANDLE, 0u);
    __HAL_TIM_SET_COMPARE(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2, pulseTicks);
    APP_RETURN_IF_HAL_ERROR(HAL_TIM_PWM_Start(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2), APP_STATUS_INIT_FAILED);
    HAL_Delay(APP_WATCHDOG_EXTERNAL_FEED_PULSE_MS);
    APP_RETURN_IF_HAL_ERROR(HAL_TIM_PWM_Stop(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2), APP_STATUS_INIT_FAILED);
    __HAL_TIM_SET_COMPARE(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2, 0u);

    return APP_STATUS_OK;
}

static void App_TaskWatchdogRecordRefresh(void)
{
    g_appTaskWatchdogContext.iwdgRefreshCount++;
    g_appTaskWatchdogContext.lastIwdgRefreshTickMs = HAL_GetTick();
}

static void App_TaskWatchdogRecordExternalFeed(void)
{
    g_appTaskWatchdogContext.externalFeedCount++;
    g_appTaskWatchdogContext.lastExternalFeedTickMs = HAL_GetTick();
}

const AppTaskWatchdogSummary_t *App_TaskWatchdogGetSummary(void)
{
    return &g_appTaskWatchdogContext;
}

AppStatus_t App_TaskWatchdog(void *p_context)
{
    AppTaskModuleContext_t *p_module;
    AppStatus_t status;
    uint32_t nowTick;
    uint32_t primeIndex;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    status = APP_STATUS_OK;
    p_module->busy = APP_TRUE;

    switch (p_module->state)
    {
        case APP_TASK_WATCHDOG_STATE_INIT:
            (void)memset(&g_appTaskWatchdogContext, 0, sizeof(g_appTaskWatchdogContext));
            status = App_TaskWatchdogIf_ConfigureDebugFreeze();
            if (status != APP_STATUS_OK)
            {
                break;
            }

            status = App_TaskWatchdogIf_InitExternalFeed();
            if (status != APP_STATUS_OK)
            {
                break;
            }

            g_appTaskWatchdogContext.initialized = APP_TRUE;

            for (primeIndex = 0u;
                 (primeIndex < APP_WATCHDOG_EXTERNAL_FEED_BOOT_PRIME_CNT) && (g_appTaskWatchdogContext.externalFeedEnabled == APP_TRUE);
                 primeIndex++)
            {
                status = App_TaskWatchdogIf_FeedExternalWatchdog();
                if (status != APP_STATUS_OK)
                {
                    break;
                }
                App_TaskWatchdogRecordExternalFeed();
            }
            if (status != APP_STATUS_OK)
            {
                break;
            }
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("WDOG", "watchdog init done: ext_feed=%u prime=%lu",
                                 (unsigned int)g_appTaskWatchdogContext.externalFeedEnabled,
                                 (unsigned long)APP_WATCHDOG_EXTERNAL_FEED_BOOT_PRIME_CNT);
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_WATCHDOG_STATE_SERVICE_IWDG);
            /* fall through */

        case APP_TASK_WATCHDOG_STATE_SERVICE_IWDG:
            status = App_TaskWatchdogIf_RefreshHardwareWatchdog();
            if (status != APP_STATUS_OK)
            {
                break;
            }

            App_TaskWatchdogRecordRefresh();
            APP_TASK_SET_STATE(p_module, APP_TASK_WATCHDOG_STATE_FEED_EXTERNAL);
            /* fall through */

        case APP_TASK_WATCHDOG_STATE_FEED_EXTERNAL:
            if (g_appTaskWatchdogContext.externalFeedEnabled == APP_TRUE)
            {
                status = App_TaskWatchdogIf_FeedExternalWatchdog();
                if (status != APP_STATUS_OK)
                {
                    break;
                }
                App_TaskWatchdogRecordExternalFeed();
            }
            APP_TASK_SET_STATE(p_module, APP_TASK_WATCHDOG_STATE_SERVICE_IWDG);
            break;

        default:
            APP_TASK_SET_STATE(p_module, APP_TASK_WATCHDOG_STATE_INIT);
            status = APP_STATUS_INVALID_PARAM;
            break;
    }

    nowTick = HAL_GetTick();
    g_appTaskWatchdogContext.lastServiceTickMs = nowTick;
    g_appTaskWatchdogContext.lastStatus = status;
    g_appTaskWatchdogContext.lastServiceOk = (status == APP_STATUS_OK) ? APP_TRUE : APP_FALSE;

#ifdef DEBUG
    if (status == APP_STATUS_OK)
    {
        APP_TASK_DEBUG_PRINT("WDOG",
                             "service ok: iwdg=%lu ext=%lu tick=%lu",
                             (unsigned long)g_appTaskWatchdogContext.iwdgRefreshCount,
                             (unsigned long)g_appTaskWatchdogContext.externalFeedCount,
                             (unsigned long)g_appTaskWatchdogContext.lastServiceTickMs);
    }
    else
    {
        APP_TASK_DEBUG_PRINT("WDOG", "service fail: status=%lu", (unsigned long)status);
    }
#endif

    p_module->busy = APP_FALSE;
    p_module->eventPending = APP_FALSE;
    p_module->lastActionTickMs = nowTick;
    return App_TasksCompleteRun(p_module, status);
}

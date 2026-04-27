#include "app_system.h"

#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_debug.h"
#include "app_gpio_lp.h"
#include "app_hw.h"
#include "app_log.h"
#include "app_msgq.h"
#include "app_scheduler.h"
#include "app_selftest.h"
#include "app_tasks.h"

static AppSystemContext_t g_appSystemContext;
static const char g_appVersionString[] = "0.6.0";
static AppGpioLpConfig_t g_appGpioLpConfig;
static char g_appSystemWakeString[64];

static const char *App_SystemBuildWakeSourceString(uint32_t wakeMask)
{
    uint8_t first;

    if (wakeMask == APP_SYSTEM_WAKE_SRC_NONE)
    {
        return "NONE";
    }

    g_appSystemWakeString[0] = '\0';
    first = APP_TRUE;

#define APP_SYSTEM_APPEND_WAKE(name_literal)                     \
    do                                                           \
    {                                                            \
        if (first == APP_FALSE)                                  \
        {                                                        \
            (void)strncat(g_appSystemWakeString, "|", sizeof(g_appSystemWakeString) - strlen(g_appSystemWakeString) - 1u); \
        }                                                        \
        (void)strncat(g_appSystemWakeString, (name_literal), sizeof(g_appSystemWakeString) - strlen(g_appSystemWakeString) - 1u); \
        first = APP_FALSE;                                       \
    } while (0)

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_NBIOT_RI) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("NBIOT_RI");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_NFC_ED) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("NFC_ED");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_REED) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("REED");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_ESI_INT) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("ESI_INT");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_RTC) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("RTC");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_UNKNOWN) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("UNKNOWN");
    }

#undef APP_SYSTEM_APPEND_WAKE

    return g_appSystemWakeString;
}

#ifdef DEBUG
static uint8_t App_SystemCanDebugLog(void)
{
    const AppLogContext_t *p_logContext;

    p_logContext = App_LogGetContext();
    return ((g_appSystemContext.logReady == APP_TRUE) &&
            (p_logContext != NULL) &&
            (p_logContext->initialized == APP_TRUE)) ? APP_TRUE : APP_FALSE;
}
#endif

const char *App_SystemGetLowPowerModeString(void)
{
    switch (g_appSystemContext.lastLowPowerMode)
    {
        case APP_SYSTEM_LP_MODE_SLEEP: return "SLEEP";
        case APP_SYSTEM_LP_MODE_STOP:  return "STOP";
        case APP_SYSTEM_LP_MODE_RUN:
        default:                       return "RUN";
    }
}

const char *App_SystemGetWakeSourceString(void)
{
    return App_SystemBuildWakeSourceString(g_appSystemContext.wakeSourceMask);
}

uint32_t App_SystemGetWakeSourceMask(void)
{
    return g_appSystemContext.wakeSourceMask;
}

void App_SystemNotifyWakeSource(uint32_t sourceMask)
{
    if (sourceMask == 0u)
    {
        return;
    }

    g_appSystemContext.wakeSourceMask |= sourceMask;
    g_appSystemContext.lastWakeTickMs = HAL_GetTick();

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("WAKE",
                       "source=%s mask=0x%08lX tick=%lu",
                       App_SystemBuildWakeSourceString(g_appSystemContext.wakeSourceMask),
                       (unsigned long)g_appSystemContext.wakeSourceMask,
                       (unsigned long)g_appSystemContext.lastWakeTickMs);
    }
#endif
}

static void App_SystemSetTaskWakeEvent(AppTaskId_t taskId)
{
    AppTaskModuleContext_t *p_module;

    p_module = App_TasksGetModuleContextMutable(taskId);
    if (p_module != NULL)
    {
        p_module->eventPending = APP_TRUE;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    switch (GPIO_Pin)
    {
        case NBIoT_RI_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_NBIOT_RI);
            App_SystemSetTaskWakeEvent(APP_TASK_ID_NBIOT);
            break;

        case NFC_ED_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_NFC_ED);
            App_SystemSetTaskWakeEvent(APP_TASK_ID_NFC);
            break;

        case REED_IN_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_REED);
            App_SystemSetTaskWakeEvent(APP_TASK_ID_METER);
            break;

        case ESI_Int_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_ESI_INT);
            App_SystemSetTaskWakeEvent(APP_TASK_ID_ESI);
            break;

        default:
            break;
    }
}

static void App_SystemConfigureWakeupInterrupts(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_NVIC_SetPriority(EXTI0_1_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
    HAL_NVIC_SetPriority(EXTI2_3_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static AppStatus_t App_SystemValidateHandles(void)
{
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_UART_NBIOT_HANDLE->Instance == LPUART1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_I2C_ESI_HANDLE->Instance == I2C1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_I2C_NFC_HANDLE->Instance == I2C2, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_I2C_AUX_HANDLE->Instance == I2C3, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_ADC_BATTERY_HANDLE->Instance == ADC1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_CRC_HANDLE->Instance == CRC, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_IWDG_HANDLE->Instance == IWDG, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_TIM_PIEZO_HANDLE->Instance == TIM3, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_TIM_WD_FEED_HANDLE->Instance == TIM22, APP_STATUS_HW_HANDLE_INVALID);
    return APP_STATUS_OK;
}

static void App_SystemApplySafeOutputs(void)
{
    App_HwSetNbiotEnable(GPIO_PIN_RESET);
    App_HwSetNbiotReset(GPIO_PIN_SET);
    App_HwSetChargeBoot0(GPIO_PIN_RESET);
}

static AppStatus_t App_SystemInitLowPowerGpio(void)
{
    AppStatus_t status;

    App_GpioLpGetDefaultConfig(&g_appGpioLpConfig);

    if ((APP_BUILD_IS_PRODUCTION == APP_TRUE) &&
        (APP_GPIO_LP_DISABLE_SWD_IN_PRODUCTION == APP_TRUE))
    {
        g_appGpioLpConfig.swdPolicy = APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION;
    }
    else
    {
        g_appGpioLpConfig.swdPolicy = APP_GPIO_LP_SWD_KEEP;
    }

    g_appGpioLpConfig.keepDebugUartPinsInStop = APP_FALSE;
    g_appGpioLpConfig.keepMeterUartPinsInStop = APP_FALSE;
    g_appGpioLpConfig.keepEsiI2cPinsInStop = APP_FALSE;
    g_appGpioLpConfig.keepNfcI2cPinsInStop = APP_FALSE;
    g_appGpioLpConfig.keepTempI2cPinsInStop = APP_FALSE;
    g_appGpioLpConfig.keepPiezoPinInStop = APP_FALSE;
    g_appGpioLpConfig.keepExternalWatchdogPinInStop = APP_FALSE;
    g_appGpioLpConfig.keepNbiotRiWakeWhenPowered = APP_TRUE;
    g_appGpioLpConfig.isolateNbiotInterfaceWhenPoweredOff = APP_TRUE;
    g_appGpioLpConfig.restoreNbiotInterfaceAfterWake = APP_TRUE;
    g_appGpioLpConfig.stopClockDisableMask =
        APP_GPIO_LP_CLK_ADC1 |
        APP_GPIO_LP_CLK_CRC |
        APP_GPIO_LP_CLK_TIM3 |
        APP_GPIO_LP_CLK_TIM22 |
        APP_GPIO_LP_CLK_USART1 |
        APP_GPIO_LP_CLK_USART2 |
        APP_GPIO_LP_CLK_LPUART1 |
        APP_GPIO_LP_CLK_I2C1 |
        APP_GPIO_LP_CLK_I2C2 |
        APP_GPIO_LP_CLK_I2C3;

    status = App_GpioLpInit(&g_appGpioLpConfig);
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_GPIO_LP_INIT_FAILED;
    }

    status = App_GpioLpSetNbiotPowered(APP_FALSE);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_GpioLpApplyRunBaseState();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemPrintBootLogs(void)
{
    const AppClockContext_t *p_clockContext;

    p_clockContext = App_ClockGetContext();

    APP_RETURN_IF_FALSE(App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_BANNER APP_DEBUG_CONSOLE_EOL) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_LogInit() == APP_STATUS_OK, APP_STATUS_LOG_INIT_FAILED);

    g_appSystemContext.logReady = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_LOG_READY;

    APP_RETURN_IF_FALSE(APP_LOGI("SYS", "Boot complete: %s v%s", APP_NAME_STRING, App_SystemGetVersionString()) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("CLK", "SYS=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu MSI=%lu LSI=%u",
                                 (unsigned long)p_clockContext->sysclkHz,
                                 (unsigned long)p_clockContext->hclkHz,
                                 (unsigned long)p_clockContext->pclk1Hz,
                                 (unsigned long)p_clockContext->pclk2Hz,
                                 (unsigned long)p_clockContext->msiRange,
                                 (unsigned int)p_clockContext->lsiReady) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("GPIO", "LP policy ready: SWD=%lu NB-IoT-wake=%u",
                                 (unsigned long)g_appGpioLpConfig.swdPolicy,
                                 (unsigned int)g_appGpioLpConfig.keepNbiotRiWakeWhenPowered) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("DBG", "USART1 debug console ready at %lu baud",
                                 (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsolePrintPrompt() == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
#ifdef DEBUG
    APP_RETURN_IF_FALSE(APP_LOGD("SYS", "Boot path complete: clock/log/debug ready") == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
#endif

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRunBootSelfTest(void)
{
    AppStatus_t status;

    status = App_SelfTestInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_SELFTEST_INIT_FAILED;
    }

    status = App_SelfTestRunBootSequence();
    g_appSystemContext.selfTestCompleted = APP_TRUE;
    g_appSystemContext.selfTestStatus = status;
    g_appSystemContext.selfTestFailed = (status == APP_STATUS_OK) ? APP_FALSE : APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_SELFTEST_DONE;

    if (status == APP_STATUS_OK)
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Boot self-test finished without failures") == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
        return APP_STATUS_OK;
    }

    APP_RETURN_IF_FALSE(APP_LOGW("SELF", "Boot self-test completed with one or more failures") == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    if (APP_SELFTEST_FAIL_STOPS_BOOT == APP_TRUE)
    {
        return status;
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemInitScheduler(void)
{
    AppStatus_t status;

    status = App_SchedulerInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_SCHEDULER_INIT_FAILED;
    }

    status = App_MsgqInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_TasksInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_SCHEDULER_INIT_FAILED;
    }

    status = App_TasksRegisterAll();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.schedulerReady = APP_TRUE;
    g_appSystemContext.schedulerStatus = APP_STATUS_OK;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_SCHEDULER_READY;

    APP_RETURN_IF_FALSE(APP_LOGI("SCH", "Scheduler ready: %u tasks registered, queue=%u",
                                 (unsigned int)App_SchedulerGetContext()->taskCount,
                                 (unsigned int)APP_MSGQ_CAPACITY) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemEnterStopMode(void)
{
    AppStatus_t status;

    status = App_SystemPrepareForStop();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_STOP;
    g_appSystemContext.lastStopEntryTickMs = HAL_GetTick();
    g_appSystemContext.wakeSourceMask = APP_SYSTEM_WAKE_SRC_NONE;

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("LP",
                       "STOP entry: idle=%lu sleep=%lu stop=%lu",
                       (unsigned long)g_appSystemContext.idleCounter,
                       (unsigned long)g_appSystemContext.sleepEntryCount,
                       (unsigned long)g_appSystemContext.stopEntryCount);
    }
#endif

    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    status = App_ClockRecoverAfterStop();
    HAL_ResumeTick();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemRecoverFromStop();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    if (g_appSystemContext.wakeSourceMask == APP_SYSTEM_WAKE_SRC_NONE)
    {
        App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_UNKNOWN);
    }

    g_appSystemContext.stopEntryCount++;

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("LP", "STOP exit wake=%s count=%lu tick=%lu",
                       App_SystemGetWakeSourceString(),
                       (unsigned long)g_appSystemContext.stopEntryCount,
                       (unsigned long)g_appSystemContext.lastWakeTickMs);
    }
#endif

    return APP_STATUS_OK;
}

static void App_SystemHandleIdle(void)
{
    AppStatus_t status;

    g_appSystemContext.idleCounter++;

    if ((g_appSystemContext.stopRequested == APP_TRUE) &&
        (App_TaskMainGetDecision() == APP_TASK_MAIN_DECISION_ALLOW_IDLE))
    {
        g_appSystemContext.stopRequested = APP_FALSE;
        status = App_SystemEnterStopMode();
        if (status != APP_STATUS_OK)
        {
            g_appSystemContext.schedulerStatus = status;
            App_ErrorRecord(status, __FILE__, __LINE__);
#ifdef DEBUG
            (void)APP_LOGE("LP", "STOP entry/exit failed: status=%lu", (unsigned long)status);
#endif
        }
        return;
    }

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("SYS",
                       "Entering idle path: mode=%s stop_req=%u idle=%lu",
                       (APP_SCHEDULER_USE_WFI_IDLE == APP_TRUE) ? "WFI" : "delay",
                       (unsigned int)g_appSystemContext.stopRequested,
                       (unsigned long)g_appSystemContext.idleCounter);
    }
#endif

    if (APP_SCHEDULER_USE_WFI_IDLE == APP_TRUE)
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_SLEEP;
        g_appSystemContext.lastSleepEntryTickMs = HAL_GetTick();
        g_appSystemContext.sleepEntryCount++;
#ifdef DEBUG
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            (void)APP_LOGD("LP", "SLEEP entry count=%lu tick=%lu",
                           (unsigned long)g_appSystemContext.sleepEntryCount,
                           (unsigned long)g_appSystemContext.lastSleepEntryTickMs);
        }
#endif
         __WFI();
#ifdef DEBUG
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            (void)APP_LOGD("LP", "SLEEP wake tick=%lu wake=%s",
                           (unsigned long)HAL_GetTick(),
                           App_SystemGetWakeSourceString());
        }
#endif
    }
    else
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;
        HAL_Delay(APP_SCHEDULER_IDLE_DELAY_MS);
    }
}

AppStatus_t App_SystemInit(void)
{
    AppStatus_t status;
    const AppClockContext_t *clockContext;

    (void)memset(&g_appSystemContext, 0, sizeof(g_appSystemContext));
    App_ErrorInit();

    g_appSystemContext.bootStage = APP_BOOT_STAGE_HAL_READY;
    g_appSystemContext.selfTestStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSystemContext.schedulerStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;

    APP_RETURN_IF_FALSE(App_ClockIsInitialized() == APP_TRUE, APP_STATUS_CLOCK_NOT_INITIALIZED);

    clockContext = App_ClockGetContext();
    APP_RETURN_IF_FALSE(clockContext->initialized == APP_TRUE, APP_STATUS_CLOCK_NOT_INITIALIZED);

    g_appSystemContext.bootSysClockHz = clockContext->sysclkHz;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_CLOCK_READY;

    status = App_SystemValidateHandles();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.bootStage = APP_BOOT_STAGE_PERIPH_READY;
    App_SystemApplySafeOutputs();
    App_SystemConfigureWakeupInterrupts();

    status = App_SystemInitLowPowerGpio();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.bootStage = APP_BOOT_STAGE_GPIO_LP_READY;

    status = App_DebugConsoleInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_DEBUG_INIT_FAILED;
    }

    g_appSystemContext.debugReady = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_DEBUG_READY;

    status = App_SystemPrintBootLogs();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemRunBootSelfTest();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemInitScheduler();
    if (status != APP_STATUS_OK)
    {
        g_appSystemContext.schedulerStatus = status;
        return status;
    }

    g_appSystemContext.initialized = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_APP_READY;
#ifdef DEBUG
    APP_RETURN_IF_FALSE(APP_LOGD("SYS", "Application ready: boot=%lu stop_req=%u",
                                 (unsigned long)g_appSystemContext.bootStage,
                                 (unsigned int)g_appSystemContext.stopRequested) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
#endif
    return APP_STATUS_OK;
}

void App_SystemProcess(void)
{
    AppStatus_t status;
    const AppSchedulerContext_t *p_schedulerContext;

    if (g_appSystemContext.initialized != APP_TRUE)
    {
        App_ErrorRecord(APP_STATUS_NOT_INITIALIZED, __FILE__, __LINE__);
        App_ErrorTrap();
    }

    status = App_SchedulerRunOnce();
    g_appSystemContext.schedulerStatus = status;
    if (status != APP_STATUS_OK)
    {
        App_ErrorRecord(status, __FILE__, __LINE__);
#ifdef DEBUG
        (void)APP_LOGE("SYS", "Scheduler run failed: status=%lu", (unsigned long)status);
#endif
    }

    p_schedulerContext = App_SchedulerGetContext();
    if (p_schedulerContext->lastDispatchCount == 0u)
    {
        App_SystemHandleIdle();
    }
    else
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;
    }

    g_appSystemContext.loopCounter++;
}

AppStatus_t App_SystemOnBeforeStopEnter(void)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    status = App_GpioLpOnBeforeStopEnter();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("LP", "before STOP status=%lu", (unsigned long)status);
    }
#endif
    return status;
}

AppStatus_t App_SystemOnAfterStopExit(void)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    status = App_GpioLpOnAfterStopExit();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("LP", "after STOP status=%lu wake=%s", (unsigned long)status, App_SystemGetWakeSourceString());
    }
#endif
    return status;
}

AppStatus_t App_SystemPrepareForStop(void)
{
    return App_SystemOnBeforeStopEnter();
}

AppStatus_t App_SystemRecoverFromStop(void)
{
    return App_SystemOnAfterStopExit();
}

AppStatus_t App_SystemSetNbiotPowered(uint8_t powered)
{
    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    return App_GpioLpSetNbiotPowered(powered);
}

AppStatus_t App_SystemRequestLowPower(uint8_t allowStop)
 {
    uint8_t previousRequest;
#ifdef DEBUG
    const AppTaskMainSummary_t *p_mainSummary;
#endif

    APP_RETURN_IF_FALSE((allowStop == APP_FALSE) || (allowStop == APP_TRUE), APP_STATUS_INVALID_PARAM);
    previousRequest = g_appSystemContext.stopRequested;
    g_appSystemContext.stopRequested = allowStop;
#ifdef DEBUG
    p_mainSummary = App_TaskMainGetSummary();
    if ((previousRequest != allowStop) && (App_SystemCanDebugLog() == APP_TRUE))
    {
        (void)APP_LOGD("LP",
                       "stop_request=%u decision=%s busy=%lu stale=%lu",
                       (unsigned int)allowStop,
                       App_TaskMainGetDecisionString(),
                       (unsigned long)p_mainSummary->busyCount,
                       (unsigned long)p_mainSummary->staleCount);
    }
#endif
     return APP_STATUS_OK;
}

const AppSystemContext_t *App_SystemGetContext(void)
{
    return &g_appSystemContext;
}

const char *App_SystemGetVersionString(void)
{
    return g_appVersionString;
}

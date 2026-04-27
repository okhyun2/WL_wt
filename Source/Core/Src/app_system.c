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
    g_appGpioLpConfig.keepNbiotRiWakeWhenPowered = APP_FALSE;
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
    APP_RETURN_IF_FALSE(APP_LOGI("GPIO", "LP policy ready: SWD=%lu NB-IoT-isolation=%u",
                                 (unsigned long)g_appGpioLpConfig.swdPolicy,
                                 (unsigned int)g_appGpioLpConfig.isolateNbiotInterfaceWhenPoweredOff) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("DBG", "USART1 debug console ready at %lu baud",
                                 (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("WDOG", "IWDG ready reload=%lu ext_pulse=%lu ms prime=%u",
                                 (unsigned long)APP_IWDG_HANDLE->Init.Reload,
                                 (unsigned long)APP_WATCHDOG_EXTERNAL_FEED_PULSE_MS,
                                 (unsigned int)APP_WATCHDOG_EXTERNAL_FEED_BOOT_PRIME_CNT) == APP_STATUS_OK,
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

static void App_SystemHandleIdle(void)
{
    g_appSystemContext.idleCounter++;

#ifdef DEBUG
    (void)APP_LOGD("SYS", "Entering idle path: mode=%s", (APP_SCHEDULER_USE_WFI_IDLE == APP_TRUE) ? "WFI" : "delay");
#endif
    if (APP_SCHEDULER_USE_WFI_IDLE == APP_TRUE)
    {
        __WFI();
    }
    else
    {
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

    g_appSystemContext.loopCounter++;
}

AppStatus_t App_SystemOnBeforeStopEnter(void)
{
    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    return App_GpioLpOnBeforeStopEnter();
}

AppStatus_t App_SystemOnAfterStopExit(void)
{
    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    return App_GpioLpOnAfterStopExit();
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

const AppSystemContext_t *App_SystemGetContext(void)
{
    return &g_appSystemContext;
}

const char *App_SystemGetVersionString(void)
{
    return g_appVersionString;
}

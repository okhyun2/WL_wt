#include "app_system.h"

#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_debug.h"
#include "app_hw.h"
#include "app_log.h"

/**
 * @file    app_system.c
 * @brief   Application bootstrap and minimal superloop processing.
 */

/** @brief Internal runtime context. */
static AppSystemContext_t g_appSystemContext;

/** @brief Static firmware version string. */
static const char g_appVersionString[] = "0.3.0";

/**
 * @brief Validate CubeMX-generated peripheral bindings.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
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

/**
 * @brief Apply safe default output states after GPIO init.
 *
 * @note  Final power policy is handled in later low-power/NB-IoT steps.
 */
static void App_SystemApplySafeOutputs(void)
{
    App_HwSetNbiotEnable(GPIO_PIN_RESET);
    App_HwSetNbiotReset(GPIO_PIN_SET);
    App_HwSetChargeBoot0(GPIO_PIN_RESET);
}

/**
 * @brief Print boot banner and baseline status logs.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
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
    APP_RETURN_IF_FALSE(APP_LOGI("SYS", "Clock SYS=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu MSI=%lu LSI=%u",
                                 (unsigned long)p_clockContext->sysclkHz,
                                 (unsigned long)p_clockContext->hclkHz,
                                 (unsigned long)p_clockContext->pclk1Hz,
                                 (unsigned long)p_clockContext->pclk2Hz,
                                 (unsigned long)p_clockContext->msiRange,
                                 (unsigned int)p_clockContext->lsiReady) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("DBG", "USART1 debug console ready at %lu baud", (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsolePrintPrompt() == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

AppStatus_t App_SystemInit(void)
{
    AppStatus_t status;
    const AppClockContext_t *clockContext;

    (void)memset(&g_appSystemContext, 0, sizeof(g_appSystemContext));
    App_ErrorInit();

    g_appSystemContext.bootStage = APP_BOOT_STAGE_HAL_READY;

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

    g_appSystemContext.initialized = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_APP_READY;

    return APP_STATUS_OK;
}

void App_SystemProcess(void)
{
    AppStatus_t status;

    if (g_appSystemContext.initialized != APP_TRUE)
    {
        App_ErrorRecord(APP_STATUS_NOT_INITIALIZED, __FILE__, __LINE__);
        App_ErrorTrap();
    }

    status = App_DebugConsoleProcess();
    if (status != APP_STATUS_OK)
    {
        App_ErrorRecord(status, __FILE__, __LINE__);
    }

    g_appSystemContext.loopCounter++;

    HAL_IWDG_Refresh(&hiwdg);

    HAL_Delay(APP_SUPERLOOP_IDLE_DELAY_MS);
}

const AppSystemContext_t *App_SystemGetContext(void)
{
    return &g_appSystemContext;
}

const char *App_SystemGetVersionString(void)
{
    return g_appVersionString;
}

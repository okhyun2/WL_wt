#include "app_selftest.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_log.h"

/**
 * @file    app_selftest.c
 * @brief   Boot-time peripheral initialization/check pseudo-code runner.
 */

/** @brief Internal runtime context. */
static AppSelfTestContext_t g_appSelfTestContext;

/**
 * @brief Convert self-test item to short text label.
 *
 * @param item Self-test item.
 * @return Constant item string.
 */
static const char *App_SelfTestItemToString(AppSelfTestItem_t item)
{
    switch (item)
    {
        case APP_SELFTEST_ITEM_BUZZER:
            return "BUZZ";

        case APP_SELFTEST_ITEM_CRC:
            return "CRC";

        case APP_SELFTEST_ITEM_BATTERY_ADC:
            return "ADC";

        case APP_SELFTEST_ITEM_DEBUG_UART:
            return "DBG";

        case APP_SELFTEST_ITEM_METER_UART:
            return "METER";

        case APP_SELFTEST_ITEM_NBIOT_UART:
            return "NBIOT";

        case APP_SELFTEST_ITEM_ESI_I2C:
            return "ESI";

        case APP_SELFTEST_ITEM_NFC_I2C:
            return "NFC";

        case APP_SELFTEST_ITEM_AUX_I2C:
            return "TEMP";

        case APP_SELFTEST_ITEM_EXT_WATCHDOG:
            return "EWDT";

        case APP_SELFTEST_ITEM_GPIO_INPUTS:
            return "GPIO";

        default:
            return "SELF";
    }
}

/**
 * @brief Store one test result in the runtime context.
 *
 * @param item Self-test item index.
 * @param status Result status.
 */
static void App_SelfTestRecordResult(AppSelfTestItem_t item, AppStatus_t status)
{
    if ((uint32_t)item >= (uint32_t)APP_SELFTEST_ITEM_COUNT)
    {
        return;
    }

    g_appSelfTestContext.items[item].executed = APP_TRUE;
    g_appSelfTestContext.items[item].passed = (status == APP_STATUS_OK) ? APP_TRUE : APP_FALSE;
    g_appSelfTestContext.items[item].status = status;
    g_appSelfTestContext.items[item].tickMs = HAL_GetTick();

    if (status == APP_STATUS_OK)
    {
        g_appSelfTestContext.passCount++;
    }
    else
    {
        g_appSelfTestContext.failCount++;
    }
}

/**
 * @brief Play a buzzer pattern using TIM3 PWM channel 2.
 *
 * @param count Number of beeps.
 * @param onMs Active time per beep.
 * @param offMs Silent time between beeps.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestPlayBuzzerPattern(uint8_t count, uint32_t onMs, uint32_t offMs)
{
    uint8_t index;
    uint32_t periodTicks;
    uint32_t pulseTicks;

    APP_RETURN_IF_FALSE(APP_TIM_PIEZO_HANDLE->Instance == TIM3, APP_STATUS_HW_HANDLE_INVALID);

    periodTicks = __HAL_TIM_GET_AUTORELOAD(APP_TIM_PIEZO_HANDLE);
    if (periodTicks == 0u)
    {
        periodTicks = 1u;
    }

    pulseTicks = (((periodTicks + 1u) * APP_SELFTEST_BUZZER_DUTY_PERCENT) / 100u);
    if ((pulseTicks == 0u) || (pulseTicks > periodTicks))
    {
        pulseTicks = periodTicks / 2u;
        if (pulseTicks == 0u)
        {
            pulseTicks = 1u;
        }
    }

    APP_RETURN_IF_HAL_ERROR(HAL_TIM_PWM_Start(APP_TIM_PIEZO_HANDLE, TIM_CHANNEL_2), APP_STATUS_SELFTEST_FAILED);
    HAL_Delay(APP_SELFTEST_PWM_SETTLE_DELAY_MS);

    for (index = 0u; index < count; index++)
    {
        __HAL_TIM_SET_COMPARE(APP_TIM_PIEZO_HANDLE, TIM_CHANNEL_2, pulseTicks);
        HAL_Delay(onMs);
        __HAL_TIM_SET_COMPARE(APP_TIM_PIEZO_HANDLE, TIM_CHANNEL_2, 0u);
        HAL_Delay(offMs);
    }

    APP_RETURN_IF_HAL_ERROR(HAL_TIM_PWM_Stop(APP_TIM_PIEZO_HANDLE, TIM_CHANNEL_2), APP_STATUS_SELFTEST_FAILED);

    return APP_STATUS_OK;
}

/**
 * @brief Buzzer confirmation test. This is intentionally first.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckBuzzer(void)
{
    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "[1st] Buzzer test start") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    APP_RETURN_IF_FALSE(App_SelfTestPlayBuzzerPattern(APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT,
                                                      APP_SELFTEST_BUZZER_BEEP_ON_MS,
                                                      APP_SELFTEST_BUZZER_BEEP_OFF_MS) == APP_STATUS_OK,
                        APP_STATUS_SELFTEST_FAILED);

    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "[1st] Buzzer test pass") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

/**
 * @brief CRC peripheral sanity test.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckCrc(void)
{
    uint32_t testVector[2] = {0x12345678u, 0xA55AA55Au};
    uint32_t crcValue;

    APP_RETURN_IF_FALSE(APP_CRC_HANDLE->Instance == CRC, APP_STATUS_HW_HANDLE_INVALID);

    crcValue = HAL_CRC_Calculate(APP_CRC_HANDLE, testVector, 2u);
    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "CRC sanity value = 0x%08lX", (unsigned long)crcValue) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

/**
 * @brief Battery ADC single-shot sanity test.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckBatteryAdc(void)
{
    uint32_t rawAdc;

    APP_RETURN_IF_FALSE(APP_ADC_BATTERY_HANDLE->Instance == ADC1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_HAL_ERROR(HAL_ADC_Start(APP_ADC_BATTERY_HANDLE), APP_STATUS_SELFTEST_FAILED);
    APP_RETURN_IF_FALSE(HAL_ADC_PollForConversion(APP_ADC_BATTERY_HANDLE, APP_SELFTEST_ADC_TIMEOUT_MS) == HAL_OK,
                        APP_STATUS_SELFTEST_TIMEOUT);

    rawAdc = HAL_ADC_GetValue(APP_ADC_BATTERY_HANDLE);
    (void)HAL_ADC_Stop(APP_ADC_BATTERY_HANDLE);

    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Battery ADC raw = %lu", (unsigned long)rawAdc) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

/**
 * @brief Debug UART confirmation.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckDebugUart(void)
{
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Debug UART online at %lu baud",
                                 (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

/**
 * @brief Meter UART pseudo connectivity probe.
 *
 * @note Real protocol wake-up / request / response parsing should be added when
 *       meter command frames and timeout policy are finalized.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckMeterUart(void)
{
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    if (APP_SELFTEST_ENABLE_REAL_METER_PROBE == APP_TRUE)
    {
        static const uint8_t meterWakeFrame[] = {0x2Fu, 0x3Fu, 0x21u, 0x0Du, 0x0Au};
        uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE];

        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Meter UART real probe start") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
        APP_RETURN_IF_HAL_ERROR(HAL_UART_Transmit(APP_UART_METER_HANDLE,
                                                  (uint8_t *)meterWakeFrame,
                                                  (uint16_t)sizeof(meterWakeFrame),
                                                  APP_SELFTEST_UART_TIMEOUT_MS),
                                APP_STATUS_SELFTEST_DEVICE_NOT_READY);
        APP_RETURN_IF_HAL_ERROR(HAL_UART_Receive(APP_UART_METER_HANDLE,
                                                 meterReply,
                                                 APP_SELFTEST_UART_EXPECTED_RX_MIN_LEN,
                                                 APP_SELFTEST_UART_REPLY_TIMEOUT_MS),
                                APP_STATUS_SELFTEST_TIMEOUT);

        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Meter UART reply received (%u bytes minimum)",
                                     (unsigned int)APP_SELFTEST_UART_EXPECTED_RX_MIN_LEN) == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }
    else
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Meter UART probe pseudo only: TODO wake meter, send request, validate frame") == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

/**
 * @brief NB-IoT pseudo connectivity probe.
 *
 * @note Real module boot and AT response parsing should be added when BC95-GV
 *       power-up timing and AT command policy are finalized.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckNbiot(void)
{
    APP_RETURN_IF_FALSE(APP_UART_NBIOT_HANDLE->Instance == LPUART1, APP_STATUS_HW_HANDLE_INVALID);

    if (APP_SELFTEST_ENABLE_REAL_NBIOT_PROBE == APP_TRUE)
    {
        static const uint8_t atCommand[] = {'A', 'T', '\r', '\n'};
        uint8_t replyBuffer[APP_SELFTEST_UART_RX_BUFFER_SIZE];

        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "NB-IoT real probe start") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

        App_HwSetNbiotEnable(GPIO_PIN_SET);
        HAL_Delay(APP_SELFTEST_NBIOT_BOOT_DELAY_MS);
        App_HwSetNbiotReset(GPIO_PIN_SET);
        HAL_Delay(APP_SELFTEST_NBIOT_RESET_RELEASE_DELAY_MS);

        APP_RETURN_IF_HAL_ERROR(HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                                  (uint8_t *)atCommand,
                                                  (uint16_t)sizeof(atCommand),
                                                  APP_SELFTEST_UART_TIMEOUT_MS),
                                APP_STATUS_SELFTEST_DEVICE_NOT_READY);
        APP_RETURN_IF_HAL_ERROR(HAL_UART_Receive(APP_UART_NBIOT_HANDLE,
                                                 replyBuffer,
                                                 APP_SELFTEST_UART_EXPECTED_RX_MIN_LEN,
                                                 APP_SELFTEST_UART_REPLY_TIMEOUT_MS),
                                APP_STATUS_SELFTEST_TIMEOUT);

        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "NB-IoT AT reply received (%u bytes minimum)",
                                     (unsigned int)APP_SELFTEST_UART_EXPECTED_RX_MIN_LEN) == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);

        App_HwSetNbiotEnable(GPIO_PIN_RESET);
    }
    else
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "NB-IoT probe pseudo only: TODO power on BC95, send AT, parse OK, power off") == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

/**
 * @brief Shared I2C pseudo/real ready check.
 *
 * @param p_i2cHandle I2C handle.
 * @param itemName Log label.
 * @param realProbeEnabled APP_TRUE to execute HAL_I2C_IsDeviceReady.
 * @param address7bit 7-bit target address. Use 0 to skip real ready check.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckI2cDevice(I2C_HandleTypeDef *p_i2cHandle,
                                              const char *p_itemName,
                                              uint8_t realProbeEnabled,
                                              uint8_t address7bit)
{
    APP_RETURN_IF_FALSE((p_i2cHandle != NULL), APP_STATUS_INVALID_PARAM);

    if ((realProbeEnabled == APP_TRUE) && (address7bit != 0u))
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "%s I2C real probe start: addr=0x%02X",
                                     p_itemName,
                                     (unsigned int)address7bit) == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);

        APP_RETURN_IF_FALSE(HAL_I2C_IsDeviceReady(p_i2cHandle,
                                                  (uint16_t)((uint16_t)address7bit << 1u),
                                                  APP_SELFTEST_I2C_READY_TRIALS,
                                                  APP_SELFTEST_I2C_READY_TIMEOUT_MS) == HAL_OK,
                            APP_STATUS_SELFTEST_DEVICE_NOT_READY);
    }
    else
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "%s I2C probe pseudo only: TODO read WHOAMI / config register", p_itemName) == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

/**
 * @brief ESI I2C peripheral check.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckEsiI2c(void)
{
    APP_RETURN_IF_FALSE(APP_I2C_ESI_HANDLE->Instance == I2C1, APP_STATUS_HW_HANDLE_INVALID);

    return App_SelfTestCheckI2cDevice(APP_I2C_ESI_HANDLE,
                                      "ESI",
                                      APP_SELFTEST_ENABLE_REAL_ESI_PROBE,
                                      APP_SELFTEST_ESI_I2C_ADDRESS_7BIT);
}

/**
 * @brief NFC I2C peripheral check.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckNfcI2c(void)
{
    APP_RETURN_IF_FALSE(APP_I2C_NFC_HANDLE->Instance == I2C2, APP_STATUS_HW_HANDLE_INVALID);

    return App_SelfTestCheckI2cDevice(APP_I2C_NFC_HANDLE,
                                      "NFC",
                                      APP_SELFTEST_ENABLE_REAL_NFC_PROBE,
                                      APP_SELFTEST_NFC_I2C_ADDRESS_7BIT);
}

/**
 * @brief Auxiliary temperature/sensor I2C peripheral check.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckAuxI2c(void)
{
    APP_RETURN_IF_FALSE(APP_I2C_AUX_HANDLE->Instance == I2C3, APP_STATUS_HW_HANDLE_INVALID);

    return App_SelfTestCheckI2cDevice(APP_I2C_AUX_HANDLE,
                                      "TEMP",
                                      APP_SELFTEST_ENABLE_REAL_AUX_PROBE,
                                      APP_SELFTEST_AUX_I2C_ADDRESS_7BIT);
}

/**
 * @brief External watchdog output pseudo check.
 *
 * @note Real TPL5010 feeding policy should be implemented in the watchdog step.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckExternalWatchdog(void)
{
    APP_RETURN_IF_FALSE(APP_TIM_WD_FEED_HANDLE->Instance == TIM22, APP_STATUS_HW_HANDLE_INVALID);

    if (APP_SELFTEST_ENABLE_REAL_EXTERNAL_WD_PULSE == APP_TRUE)
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "External watchdog pulse test start") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
        APP_RETURN_IF_HAL_ERROR(HAL_TIM_PWM_Start(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2), APP_STATUS_SELFTEST_FAILED);
        HAL_Delay(APP_SELFTEST_EXTERNAL_WD_PULSE_MS);
        APP_RETURN_IF_HAL_ERROR(HAL_TIM_PWM_Stop(APP_TIM_WD_FEED_HANDLE, TIM_CHANNEL_2), APP_STATUS_SELFTEST_FAILED);
    }
    else
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "External watchdog probe pseudo only: TODO emit keep-alive pulse and verify reset path") == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

/**
 * @brief Read wake/interrupt GPIO input states.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckInputLines(void)
{
    GPIO_PinState riState;
    GPIO_PinState nfcEventState;
    GPIO_PinState esiIntState;
    GPIO_PinState reedState;

    riState = App_HwReadNbiotRi();
    nfcEventState = App_HwReadNfcEvent();
    esiIntState = App_HwReadEsiInterrupt();
    reedState = HAL_GPIO_ReadPin(REED_IN_GPIO_Port, REED_IN_Pin);

    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "GPIO inputs RI=%u NFC_ED=%u ESI_INT=%u REED=%u",
                                 (unsigned int)riState,
                                 (unsigned int)nfcEventState,
                                 (unsigned int)esiIntState,
                                 (unsigned int)reedState) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

/**
 * @brief Run one test item, record the result, and beep on failure.
 *
 * @param item Target self-test item.
 * @param p_checkFunction Test function pointer.
 */
static void App_SelfTestRunItem(AppSelfTestItem_t item, AppStatus_t (*p_checkFunction)(void))
{
    AppStatus_t status;

    if (p_checkFunction == NULL)
    {
        status = APP_STATUS_INVALID_PARAM;
    }
    else
    {
        status = p_checkFunction();
    }

    App_SelfTestRecordResult(item, status);

    if (status == APP_STATUS_OK)
    {
        (void)APP_LOGI("SELF", "%s PASS", App_SelfTestItemToString(item));
    }
    else
    {
        (void)APP_LOGE("SELF", "%s FAIL status=%lu",
                       App_SelfTestItemToString(item),
                       (unsigned long)status);
        (void)App_SelfTestSignalErrorBuzzer();
    }
}

AppStatus_t App_SelfTestInit(void)
{
    (void)memset(&g_appSelfTestContext, 0, sizeof(g_appSelfTestContext));
    g_appSelfTestContext.initialized = APP_TRUE;
    g_appSelfTestContext.lastSequenceStatus = APP_STATUS_NOT_INITIALIZED;

    return APP_STATUS_OK;
}

AppStatus_t App_SelfTestRunBootSequence(void)
{
    APP_RETURN_IF_FALSE(g_appSelfTestContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(App_LogGetContext()->initialized == APP_TRUE, APP_STATUS_LOG_INIT_FAILED);

    g_appSelfTestContext.running = APP_TRUE;
    g_appSelfTestContext.lastRunTickMs = HAL_GetTick();
    g_appSelfTestContext.passCount = 0u;
    g_appSelfTestContext.failCount = 0u;
    (void)memset(g_appSelfTestContext.items, 0, sizeof(g_appSelfTestContext.items));

    App_SelfTestRunItem(APP_SELFTEST_ITEM_BUZZER, App_SelfTestCheckBuzzer);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_CRC, App_SelfTestCheckCrc);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_BATTERY_ADC, App_SelfTestCheckBatteryAdc);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_DEBUG_UART, App_SelfTestCheckDebugUart);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterUart);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_NBIOT_UART, App_SelfTestCheckNbiot);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_ESI_I2C, App_SelfTestCheckEsiI2c);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_NFC_I2C, App_SelfTestCheckNfcI2c);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_AUX_I2C, App_SelfTestCheckAuxI2c);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_EXT_WATCHDOG, App_SelfTestCheckExternalWatchdog);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_GPIO_INPUTS, App_SelfTestCheckInputLines);

    g_appSelfTestContext.running = APP_FALSE;
    g_appSelfTestContext.lastSequenceStatus = (g_appSelfTestContext.failCount == 0u) ? APP_STATUS_OK : APP_STATUS_SELFTEST_FAILED;

    APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Boot self-test summary: pass=%lu fail=%lu",
                                 (unsigned long)g_appSelfTestContext.passCount,
                                 (unsigned long)g_appSelfTestContext.failCount) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return g_appSelfTestContext.lastSequenceStatus;
}

AppStatus_t App_SelfTestSignalErrorBuzzer(void)
{
    return App_SelfTestPlayBuzzerPattern(APP_SELFTEST_BUZZER_ERROR_BEEP_COUNT,
                                         APP_SELFTEST_BUZZER_ERROR_ON_MS,
                                         APP_SELFTEST_BUZZER_ERROR_OFF_MS);
}

const AppSelfTestContext_t *App_SelfTestGetContext(void)
{
    return &g_appSelfTestContext;
}

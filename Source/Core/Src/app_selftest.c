#include "app_selftest.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_gpio_lp.h"
#include "app_meter.h"
#include "app_aux.h"
#include "app_nbiot.h"
#include "app_log.h"

#if defined(SUPPORT_SELFTEST) || (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE)
/**
 * @file    app_selftest.c
 * @brief   Boot-time peripheral initialization/check pseudo-code runner.
 */

/** @brief Internal runtime context. */
static AppSelfTestContext_t g_appSelfTestContext;
static uint8_t g_appSelfTestNbiotExecuted;

typedef struct
{
    UART_HandleTypeDef *p_huart;
    uint8_t *p_buffer;
    uint16_t targetLength;
    volatile uint16_t receivedLength;
    volatile uint8_t active;
    volatile uint8_t completed;
    volatile uint8_t error;
} AppSelfTestUartRxItContext_t;

static AppSelfTestUartRxItContext_t g_appSelfTestUartRxItContext;

static AppStatus_t App_SelfTestUartReceiveIt(UART_HandleTypeDef *p_huart,
                                             uint8_t *p_buffer,
                                             uint16_t length,
                                             uint32_t timeoutMs)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;

    APP_RETURN_IF_FALSE((p_huart != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buffer != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length > 0u), APP_STATUS_INVALID_PARAM);

    g_appSelfTestUartRxItContext.p_huart = p_huart;
    g_appSelfTestUartRxItContext.p_buffer = p_buffer;
    g_appSelfTestUartRxItContext.targetLength = length;
    g_appSelfTestUartRxItContext.receivedLength = 0u;
    g_appSelfTestUartRxItContext.active = APP_TRUE;
    g_appSelfTestUartRxItContext.completed = APP_FALSE;
    g_appSelfTestUartRxItContext.error = APP_FALSE;

    __HAL_UART_CLEAR_FLAG(p_huart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(p_huart, &p_buffer[0], 1u);
    APP_RETURN_IF_FALSE((halStatus == HAL_OK), APP_STATUS_UART_RX_FAILED);

    startTick = HAL_GetTick();
    while (g_appSelfTestUartRxItContext.completed != APP_TRUE)
    {
        if (g_appSelfTestUartRxItContext.error == APP_TRUE)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            g_appSelfTestUartRxItContext.active = APP_FALSE;
            return APP_STATUS_UART_RX_ERROR;
        }

        if ((HAL_GetTick() - startTick) >= timeoutMs)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            __HAL_UART_CLEAR_FLAG(p_huart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
            __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
            g_appSelfTestUartRxItContext.active = APP_FALSE;
            return APP_STATUS_SELFTEST_TIMEOUT;
        }
    }

    g_appSelfTestUartRxItContext.active = APP_FALSE;
    return APP_STATUS_OK;
}


static AppStatus_t App_SelfTestReinitMeterUart(uint32_t settleDelayMs)
{
    HAL_StatusTypeDef halStatus;

    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    App_GpioLpRestoreMeterUartPins();

    (void)HAL_UART_AbortReceive_IT(APP_UART_METER_HANDLE);
    __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    //(void)HAL_UART_DeInit(APP_UART_METER_HANDLE);
    HAL_Delay(APP_SELFTEST_UART_METER_REINIT_PREP_DELAY_MS);

    //halStatus = HAL_UART_Init(APP_UART_METER_HANDLE);
    //APP_RETURN_IF_FALSE((halStatus == HAL_OK), APP_STATUS_UART_RX_FAILED);

    //USART2->CR1 |= USART_CR1_RE;
    __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);
    //__HAL_UART_CLEAR_IDLEFLAG(APP_UART_METER_HANDLE);

    HAL_Delay(settleDelayMs);

    APP_LOGI("SELF", "Meter UART full re-init done (settle=%lu ms)",
             (unsigned long)settleDelayMs);

    return APP_STATUS_OK;
}

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
 * @brief Play a buzzer pattern using gpio
 *
 * @param count Number of beeps.
 * @param onMs Active time per beep.
 * @param offMs Silent time between beeps.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestPlayBuzzerPattern(uint8_t count, uint32_t onMs, uint32_t offMs)
{
    uint8_t index;

    for (index = 0u; index < count; index++)
    {
        HAL_GPIO_WritePin(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin, GPIO_PIN_SET);
        HAL_Delay(onMs);
        HAL_GPIO_WritePin(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin, GPIO_PIN_RESET);
        HAL_Delay(offMs);
    }

    return APP_STATUS_OK;
}

/**
 * @brief Buzzer confirmation test. This is intentionally first.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckBuzzer(void)
{
    APP_LOGI("SELF", "Buzzer test start");

    APP_RETURN_IF_FALSE(App_SelfTestPlayBuzzerPattern(APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT,
                                                      APP_SELFTEST_BUZZER_BEEP_ON_MS,
                                                      APP_SELFTEST_BUZZER_BEEP_OFF_MS) == APP_STATUS_OK,
                        APP_STATUS_SELFTEST_FAILED);

    APP_LOGI("SELF", "Buzzer test pass");

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
    const uint32_t result = 0x995A00E3;
    uint32_t crcValue;

    APP_RETURN_IF_FALSE(APP_CRC_HANDLE->Instance == CRC, APP_STATUS_HW_HANDLE_INVALID);

    crcValue = HAL_CRC_Calculate(APP_CRC_HANDLE, testVector, 2u);
    APP_LOGI("SELF", "CRC sanity value = 0x%08lX", (unsigned long)crcValue);

    if(crcValue == result) {
        return APP_STATUS_OK;
    }
    else {
        return APP_STATUS_FATAL;
    }
}

/**
 * @brief Battery ADC single-shot sanity test.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckBatteryAdc(void)
{
    uint32_t adc_vref = 0, adc_vbat = 0, vbat_mv = 0, vdda_mv = 0;

    APP_RETURN_IF_FALSE(APP_ADC_BATTERY_HANDLE->Instance == ADC1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_HAL_ERROR(Battery_ReadVoltage_Averaged_mV(&adc_vref, &adc_vbat, &vdda_mv, &vbat_mv), APP_STATUS_SELFTEST_FAILED);

    APP_LOGI("SELF", "ADC(vref:%lu, vbat:%lu) Volt(vdda:%lumV, vbat:%lumV)", 
        (unsigned long)adc_vref,
        (unsigned long)adc_vbat,
        (unsigned long)vdda_mv,
        (unsigned long)vbat_mv);

    {
        uint8_t voltX10 = (uint8_t)((vbat_mv + 50u) / 100u);
        App_UpdateBatteryToOptions(voltX10, 0u);
    }

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
    APP_LOGI("SELF", "Debug UART online at %lu baud", (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate);

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
static AppStatus_t App_SelfTestCheckMeterNormalUart(void)
{
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    AppStatus_t status = APP_STATUS_OK;
    static const uint8_t meterWakeFrame[] = {0x10, 0x5B, 0x01, 0x5C, 0x16};
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {
        0,
    };
    const uint8_t SYNC_START = 0x68;
    const uint8_t SYNC_STOP = 0x16;
    int i = 0; 

    APP_LOGI("SELF", "Meter(Normal) UART real probe start");

    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_SET);
    HAL_Delay(50); //>= meter spec. 50ms
    APP_RETURN_IF_FALSE(App_SelfTestReinitMeterUart((g_appSelfTestNbiotExecuted == APP_TRUE) ?
                                                    APP_SELFTEST_UART_METER_POST_NBIOT_SETTLE_DELAY_MS :
                                                    APP_SELFTEST_UART_METER_REINIT_SETTLE_DELAY_MS) == APP_STATUS_OK,
                        APP_STATUS_UART_RX_FAILED);

    APP_RETURN_IF_HAL_ERROR(HAL_UART_Transmit(APP_UART_METER_HANDLE,
                                              (uint8_t *)meterWakeFrame,
                                              (uint16_t)sizeof(meterWakeFrame),
                                              APP_SELFTEST_UART_TIMEOUT_MS),
                            APP_STATUS_SELFTEST_DEVICE_NOT_READY);
    status = App_SelfTestUartReceiveIt(APP_UART_METER_HANDLE,
                                       meterReply,
                                       APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN,
                                       APP_SELFTEST_UART_REPLY_METER_NORMAL_TIMEOUT_MS);
    APP_RETURN_IF_FALSE((status == APP_STATUS_OK), status);

    HAL_Delay(100); //>= meter spec. 100ms
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    App_LogHexDump(APP_LOG_LEVEL_INFO, "SELF", (const uint8_t *)meterReply, APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    APP_RETURN_IF_FALSE((status == APP_STATUS_OK), status);

    APP_LOGI("SELF", "Meter UART reply received (%u bytes minimum)", (unsigned int)APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);

    //App_MeterSetStorageEnabled(APP_FALSE);
    //kiki test forcely saving
    App_MeterSetStorageEnabled(APP_TRUE);
    status = App_MeterProcessReceivedData((const uint8_t *)meterReply, APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    App_MeterSetStorageEnabled(APP_TRUE);
    return (status);
}

static AppStatus_t App_SelfTestCheckMeterSC1xxxUart(void)
{
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    AppStatus_t status = APP_STATUS_OK;
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {
        0,
    };
    const uint8_t SYNC_START = 0x02;
    const uint8_t SYNC_STOP = 0x03;
    int i = 0; 

    APP_LOGI("SELF", "Meter(SC1xxx) UART real probe start");

    //Read protocols meter(SC1xxx)
    //for(i = 0; i < 100; i++)
    {
        App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        HAL_Delay(125);
        HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_SET);
        HAL_Delay(125);
        HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        HAL_Delay(125);
        HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_SET);
        HAL_Delay(125);
        HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        HAL_Delay(300);
        App_GpioLpRestoreMeterUartPins();
        status = App_SelfTestUartReceiveIt(APP_UART_METER_HANDLE,
                                           meterReply,
                                           APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN,
                                           APP_SELFTEST_UART_REPLY_METER_SC1xxx_TIMEOUT_MS);
        APP_RETURN_IF_FALSE((status == APP_STATUS_OK), status);

        HAL_Delay(100); //>= 100ms
        App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        HAL_Delay(100); 
        App_LogHexDump(APP_LOG_LEVEL_INFO, "SELF", (const uint8_t *)meterReply, APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);
    }

    App_MeterSetStorageEnabled(APP_TRUE);
    status = App_MeterSC1xxxProcessReceivedData((const uint8_t *)meterReply, APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);
    App_MeterSetStorageEnabled(APP_TRUE);
    return (status);
}

#ifndef SUPPORT_SELFTEST_SENDNBIOT
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
    AppStatus_t status = APP_STATUS_OK;
    APP_RETURN_IF_FALSE(APP_UART_NBIOT_HANDLE->Instance == LPUART1, APP_STATUS_HW_HANDLE_INVALID);

    APP_LOGI("SELF", "NB-IoT real probe start");
    g_appSelfTestNbiotExecuted = APP_TRUE;

    APP_RETURN_IF_FALSE(App_GpioLpSetNbiotPowered(APP_TRUE) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    APP_WWDGFeed();
    if (App_NBIoTBringUpWithReset(2u) != APP_STATUS_OK)
    {
        status = APP_STATUS_FATAL;
        goto cleanup;
    }
    APP_WWDGFeed();
    if (App_NBIoTNetworkBringUp() != APP_STATUS_OK)
    {
        status = APP_STATUS_FATAL;
        goto cleanup;
    }
    APP_WWDGFeed();
    (void)App_NBIoTReadIdentity(APP_FALSE);
    (void)App_NBIoTReadQuality(APP_FALSE);

cleanup:
    (void)App_GpioLpSetNbiotPowered(APP_FALSE);
    HAL_Delay(APP_SELFTEST_UART_METER_POST_NBIOT_SETTLE_DELAY_MS);
    return status;
}
#endif // SUPPORT_SELFTEST_SENDNBIOT

/**
 * @brief Shared I2C pseudo/real ready check.
 *
 * @param p_i2cHandle I2C handle.
 * @param itemName Log label.
 * @param address7bit 7-bit target address. Use 0 to skip real ready check.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckI2cDevice(I2C_HandleTypeDef *p_i2cHandle,
                                              const char *p_itemName,
                                              uint8_t address7bit)
{
    APP_RETURN_IF_FALSE((p_i2cHandle != NULL), APP_STATUS_INVALID_PARAM);

    if (address7bit != 0u)
    {
        APP_LOGI("SELF", "%s I2C real probe start: addr=0x%02X", p_itemName, (unsigned int)address7bit);

        APP_RETURN_IF_FALSE(HAL_I2C_IsDeviceReady(p_i2cHandle,
                                                  (uint16_t)((uint16_t)address7bit << 1u),
                                                  APP_SELFTEST_I2C_READY_TRIALS,
                                                  APP_SELFTEST_I2C_READY_TIMEOUT_MS) == HAL_OK,
                            APP_STATUS_SELFTEST_DEVICE_NOT_READY);
    }

    return APP_STATUS_OK;
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
                                      APP_SELFTEST_NFC_I2C_ADDRESS_7BIT);
}

/**
 * @brief Auxiliary temperature/sensor I2C peripheral check.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckAuxI2c(void)
{
    SHTC3_Data_t th;

    APP_RETURN_IF_FALSE(APP_I2C_AUX_HANDLE->Instance == I2C3, APP_STATUS_HW_HANDLE_INVALID);

    APP_RETURN_IF_FALSE(App_SelfTestCheckI2cDevice(APP_I2C_AUX_HANDLE, "TEMP", APP_SELFTEST_AUX_I2C_ADDRESS_7BIT) == APP_STATUS_OK,
                        APP_STATUS_INVALID_PARAM);

    if (SHTC3_ReadTempHumidity(APP_I2C_AUX_HANDLE, &th) == HAL_OK)
    {
        int t_int = (int)th.temperature;
        int t_dec = (int)((th.temperature - t_int) * 100);
        int h_int = (int)th.humidity;
        int h_dec = (int)((th.humidity - h_int) * 100);
        if (t_dec < 0)
            t_dec = -t_dec;

        APP_LOGI("SELF", "T = %d.%02d C, RH = %d.%02d %%", t_int, t_dec, h_int, h_dec);
        return APP_STATUS_OK;
    }
    else
    {
        APP_LOGE("AUX", "Read error");
        return APP_STATUS_FATAL;
    }
}

/**
 * @brief External watchdog output pseudo check.
 *
 * @note Runtime feed policy is handled by App_TaskWatchdog; this self-test only checks the output path.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckExternalWatchdog(void)
{
    App_HwFeedEWD();

    return APP_STATUS_OK;
}

/**
 * @brief Read wake/interrupt GPIO input states.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckInputLines(void)
{
    GPIO_PinState nfcEventState;

    nfcEventState = App_HwReadNfcEvent();

    APP_LOGI("SELF", "GPIO inputs NFC_ED=%u", (unsigned int)nfcEventState);

    return APP_STATUS_OK;
}

/**
 * @brief Run one test item, record the result, and beep on failure.
 *
 * @param item Target self-test item.
 * @param p_checkFunction Test function pointer.
 */
static void App_SelfTestRunItemWithPolicy(AppSelfTestItem_t item,
                                      AppStatus_t (*p_checkFunction)(void),
                                      uint8_t beepOnFail)
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
        APP_LOGI("SELF", "%s PASS", App_SelfTestItemToString(item));
    }
    else
    {
        APP_LOGE("SELF", "%s FAIL status=%lu", App_SelfTestItemToString(item), (unsigned long)status);
        if (beepOnFail == APP_TRUE)
        {
            (void)App_SelfTestSignalErrorBuzzer();
        }
    }
}

static void App_SelfTestRunItem(AppSelfTestItem_t item, AppStatus_t (*p_checkFunction)(void))
{
    App_SelfTestRunItemWithPolicy(item, p_checkFunction, APP_TRUE);
}

AppStatus_t App_SelfTestInit(void)
{
    (void)memset(&g_appSelfTestContext, 0, sizeof(g_appSelfTestContext));
    g_appSelfTestContext.initialized = APP_TRUE;
    g_appSelfTestContext.lastSequenceStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSelfTestNbiotExecuted = APP_FALSE;

    return APP_STATUS_OK;
}

void App_SelfTestSetNbiotExecutedHint(uint8_t executed)
{
    g_appSelfTestNbiotExecuted = (executed != APP_FALSE) ? APP_TRUE : APP_FALSE;
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
#ifndef SUPPORT_SELFTEST_SENDNBIOT
    App_SelfTestRunItem(APP_SELFTEST_ITEM_NBIOT_UART, App_SelfTestCheckNbiot);
#endif // SUPPORT_SELFTEST_SENDNBIOT
#if defined(SUPPORT_METER_NORMAL)
    App_SelfTestRunItem(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterNormalUart);
#elif defined(SUPPORT_METER_SC1xxx)
    App_SelfTestRunItem(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterSC1xxxUart);
#endif
    App_SelfTestRunItem(APP_SELFTEST_ITEM_NFC_I2C, App_SelfTestCheckNfcI2c);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_AUX_I2C, App_SelfTestCheckAuxI2c);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_EXT_WATCHDOG, App_SelfTestCheckExternalWatchdog);
    App_SelfTestRunItem(APP_SELFTEST_ITEM_GPIO_INPUTS, App_SelfTestCheckInputLines);

    g_appSelfTestContext.running = APP_FALSE;
    g_appSelfTestContext.lastSequenceStatus = (g_appSelfTestContext.failCount == 0u) ? APP_STATUS_OK : APP_STATUS_SELFTEST_FAILED;

    APP_LOGI("SELF", "------ Boot self-test summary: pass=%lu fail=%lu",
                                 (unsigned long)g_appSelfTestContext.passCount,
                                 (unsigned long)g_appSelfTestContext.failCount);

    return g_appSelfTestContext.lastSequenceStatus;
}

AppStatus_t App_SelfTestRunDataCollectionSequence(void)
{
    APP_RETURN_IF_FALSE(g_appSelfTestContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(App_LogGetContext()->initialized == APP_TRUE, APP_STATUS_LOG_INIT_FAILED);

    g_appSelfTestContext.running = APP_TRUE;
    g_appSelfTestContext.lastRunTickMs = HAL_GetTick();
    g_appSelfTestContext.passCount = 0u;
    g_appSelfTestContext.failCount = 0u;
    (void)memset(g_appSelfTestContext.items, 0, sizeof(g_appSelfTestContext.items));

    APP_LOGI("SELF", "Operational data collection start");

    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_BATTERY_ADC, App_SelfTestCheckBatteryAdc, APP_FALSE);
#if defined(SUPPORT_METER_NORMAL)
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterNormalUart, APP_FALSE);
#elif defined(SUPPORT_METER_SC1xxx)
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterSC1xxxUart, APP_FALSE);
#endif
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_NFC_I2C, App_SelfTestCheckNfcI2c, APP_FALSE);
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_AUX_I2C, App_SelfTestCheckAuxI2c, APP_FALSE);
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_EXT_WATCHDOG, App_SelfTestCheckExternalWatchdog, APP_FALSE);
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_GPIO_INPUTS, App_SelfTestCheckInputLines, APP_FALSE);

    g_appSelfTestContext.running = APP_FALSE;
    g_appSelfTestContext.lastSequenceStatus = (g_appSelfTestContext.failCount == 0u) ? APP_STATUS_OK : APP_STATUS_SELFTEST_FAILED;

    APP_LOGI("SELF", "Operational data collection done: pass=%lu fail=%lu",
                                 (unsigned long)g_appSelfTestContext.passCount,
                                 (unsigned long)g_appSelfTestContext.failCount);

    return g_appSelfTestContext.lastSequenceStatus;
}

AppStatus_t App_SelfTestSignalErrorBuzzer(void)
{
    return App_SelfTestPlayBuzzerPattern(APP_SELFTEST_BUZZER_ERROR_BEEP_COUNT,
                                         APP_SELFTEST_BUZZER_ERROR_ON_MS,
                                         APP_SELFTEST_BUZZER_ERROR_OFF_MS);
}

void App_SelfTestOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
    HAL_StatusTypeDef halStatus;
    uint16_t nextIndex;

    if ((p_huart == NULL) || (g_appSelfTestUartRxItContext.active != APP_TRUE))
    {
        return;
    }

    if (p_huart != g_appSelfTestUartRxItContext.p_huart)
    {
        return;
    }

    nextIndex = (uint16_t)(g_appSelfTestUartRxItContext.receivedLength + 1u);
    g_appSelfTestUartRxItContext.receivedLength = nextIndex;
    if (nextIndex >= g_appSelfTestUartRxItContext.targetLength)
    {
        g_appSelfTestUartRxItContext.completed = APP_TRUE;
        return;
    }

    halStatus = HAL_UART_Receive_IT(p_huart,
                                    &g_appSelfTestUartRxItContext.p_buffer[nextIndex],
                                    1u);
    if (halStatus != HAL_OK)
    {
        g_appSelfTestUartRxItContext.error = APP_TRUE;
    }
}

void App_SelfTestOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
    if ((p_huart == NULL) || (g_appSelfTestUartRxItContext.active != APP_TRUE))
    {
        return;
    }

    if (p_huart != g_appSelfTestUartRxItContext.p_huart)
    {
        return;
    }

    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
    g_appSelfTestUartRxItContext.error = APP_TRUE;
}

const AppSelfTestContext_t *App_SelfTestGetContext(void)
{
    return &g_appSelfTestContext;
}

#endif // SUPPORT_SELFTEST
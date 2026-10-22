#include "app_selftest.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_gpio_lp.h"
#include "app_meter.h"
#include "app_aux.h"
#include "app_nbiot.h"
#include "app_log.h"
#include "app_system.h"

const char *App_SelfTestResetCauseToString(void)
{
    switch (App_SystemGetBootResetCause())
    {
        case APP_BOOT_RESET_POWER_ON:               return "POWER_ON";
        case APP_BOOT_RESET_WWDG_INTERNAL:           return "WWDG_INTERNAL";
        case APP_BOOT_RESET_EXT_WATCHDOG_CONFIRMED:  return "EXT_WATCHDOG";
        case APP_BOOT_RESET_NRST_UNKNOWN:            return "NRST_UNKNOWN";
        default:                                     return "OTHER";
    }
}

#if defined(SUPPORT_SELFTEST) || (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE)
/**
 * @file    app_selftest.c
 * @brief   Boot-time peripheral initialization/check pseudo-code runner.
 */

/** @brief Internal runtime context. */
static AppSelfTestContext_t g_appSelfTestContext;
static uint8_t g_appSelfTestNbiotExecuted;
static uint8_t g_appSelfTestMeterLastReply[APP_SELFTEST_UART_RX_BUFFER_SIZE];
static uint16_t g_appSelfTestMeterLastReplyLen;
static uint8_t g_appSelfTestMeterLastReplyValid;
static uint8_t g_appSelfTestMeterLastReplyIsSc1xxx;

static const char *App_SelfTestItemToString(AppSelfTestItem_t item);
static AppStatus_t App_SelfTestCheckMeterCrc(void);

/* Self-test 전용 NB-IoT short probe
 * - 일반 boot/attach용 WaitUntilReady()를 쓰지 않고
 * - 짧은 settle 후 짧은 ping 몇 회만 확인하여 NFC 세션 점유 시간을 줄인다.
 */
#define APP_SELFTEST_NBIOT_POWER_SETTLE_MS        (500u)
#define APP_SELFTEST_NBIOT_PING_TIMEOUT_MS        (250u)
#define APP_SELFTEST_NBIOT_PING_RETRY_MAX         (3u)
#define APP_SELFTEST_NBIOT_PING_RETRY_DELAY_MS    (150u)

////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE) && (APP_EPC_ACTIVE_TEST_ID == 3u)
/** @brief TEST3 자가진단 반복 실행 횟수(부팅 후 누적, seq 필드로 사용). */
static uint32_t g_epcSelfDiagAttemptSeq = 0u;

/**
 * @brief 현재까지 기록된 self-test 항목 결과를 모아
 *        [SELFDIAG] test=TEST3,seq=...,dut=...,fault=...,judge=... 한 줄을 출력한다.
 *
 * @note METER_UART 항목만 실패하면 judge=METER_FAULT, 그 외 항목이 하나라도
 *       실패하면 judge=TERMINAL_FAULT, 모두 통과하면 judge=NORMAL,fault=NONE.
 */
static void App_SelfTestReportTest3Result(void)
{
    char faultBuf[96] = {0};
    uint8_t faultCount = 0u;
    uint8_t anyFault = APP_FALSE;
    uint8_t onlyMeterFault = APP_TRUE;
    uint32_t idx;
    const char *p_judge;

    g_epcSelfDiagAttemptSeq++;

    for (idx = 0u; idx < (uint32_t)APP_SELFTEST_ITEM_COUNT; idx++)
    {
        AppSelfTestItem_t item = (AppSelfTestItem_t)idx;
        const AppSelfTestItemResult_t *p_result = &g_appSelfTestContext.items[item];

        if ((p_result->executed == APP_TRUE) && (p_result->passed != APP_TRUE))
        {
            const char *p_code = App_SelfTestItemToString(item);

            if (faultCount > 0u)
            {
                (void)strncat(faultBuf, "+", sizeof(faultBuf) - strlen(faultBuf) - 1u);
            }
            (void)strncat(faultBuf, p_code, sizeof(faultBuf) - strlen(faultBuf) - 1u);
            faultCount++;
            anyFault = APP_TRUE;

            if ((item != APP_SELFTEST_ITEM_METER_UART) &&
                (item != APP_SELFTEST_ITEM_METER_CRC))
            {
                onlyMeterFault = APP_FALSE;
            }
        }
    }

    if (anyFault != APP_TRUE)
    {
        (void)strncpy(faultBuf, "NONE", sizeof(faultBuf) - 1u);
        p_judge = "NORMAL";
    }
    else if (onlyMeterFault == APP_TRUE)
    {
        p_judge = "METER_FAULT";
    }
    else
    {
        p_judge = "TERMINAL_FAULT";
    }

    SELFDIAG_LOGI("test=%s,seq=%lu,dut=%s,resetCause=%s,fault=%s,judge=%s",
         APP_EPC_TEST_ID_STRING,
         (unsigned long)g_epcSelfDiagAttemptSeq,
         APP_EPC_TEST3_DUT_LABEL,
         App_SelfTestResetCauseToString(),
         faultBuf,
         p_judge);
}
#endif /* APP_EPC_TEST_MODE_ENABLE && APP_EPC_ACTIVE_TEST_ID == 3u */
////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
    //HAL_StatusTypeDef halStatus;

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

    APP_LOGD("SELF", "meter uart reinit settle=%lums", (unsigned long)settleDelayMs);

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

        case APP_SELFTEST_ITEM_METER_UART_LINE:
            return "METER_LINE";

        case APP_SELFTEST_ITEM_METER_UART:
            return "METER";

        case APP_SELFTEST_ITEM_METER_CRC:
            return "METER_CRC";

        case APP_SELFTEST_ITEM_NBIOT_UART:
            return "NBIOT";

        case APP_SELFTEST_ITEM_NFC_I2C:
            return "NFC";

        case APP_SELFTEST_ITEM_AUX_I2C:
            return "TEMP";

        case APP_SELFTEST_ITEM_EXT_WATCHDOG:
            return "EWDT";

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
    APP_LOGI("SELF", "buzzer start");

    APP_RETURN_IF_FALSE(App_SelfTestPlayBuzzerPattern(APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT,
                                                      APP_SELFTEST_BUZZER_BEEP_ON_MS,
                                                      APP_SELFTEST_BUZZER_BEEP_OFF_MS) == APP_STATUS_OK,
                        APP_STATUS_SELFTEST_FAILED);

    APP_LOGI("SELF", "buzzer pass");

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
    APP_LOGI("SELF", "crc=0x%08lX", (unsigned long)crcValue);

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

    APP_LOGI("SELF", "adc vref=%lu vbat=%lu vdda=%lumV vb=%lumV",
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
    APP_LOGI("SELF", "dbg uart %lu baud", (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate);

    return APP_STATUS_OK;
}

/**
 * @brief Meter UART pseudo connectivity probe.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
static AppStatus_t App_SelfTestCheckMeterUartLineOk(void)
{
    AppStatus_t status = APP_STATUS_OK;
    APP_LOGD("SELF", "METER_LINE skip, METER_UART ok");

    return (status);
}

static AppStatus_t App_SelfTestCheckMeterUartLine(void)
{
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    AppStatus_t status = APP_STATUS_OK;
    const uint8_t meterCheckFrame[10] = {1, 0, 1, 0, 1, 1, 0, 0, 1, 0};
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {
        0,
    };
    int i;
    //const uint8_t SYNC_START = 0x68;
    //const uint8_t SYNC_STOP = 0x16;

    APP_LOGI("SELF", "meter line probe start");
    App_GpioLpConfigOutput(Meter_UART_Loop_GPIO_Port, Meter_UART_Loop_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
    App_GpioLpConfigInput(Meter_RX_GPIO_Port, Meter_RX_Pin);
    HAL_Delay(50);

    for(i = 0; i < 10; i++)
    {
        App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, (meterCheckFrame[i] > 0) ? GPIO_PIN_SET:GPIO_PIN_RESET);
        meterReply[i] = App_GpioLpReadInputIsSet(Meter_RX_GPIO_Port, Meter_RX_Pin);
    }

    HAL_Delay(100); //>= meter spec. 100ms
    App_GpioLpConfigAnalogNoPull(Meter_RX_GPIO_Port, Meter_RX_Pin);
    HAL_Delay(100); //>= meter spec. 100ms
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    HAL_Delay(100); //>= meter spec. 100ms
    App_GpioLpConfigOutput(Meter_UART_Loop_GPIO_Port, Meter_UART_Loop_Pin, GPIO_PIN_RESET);

    App_GpioLpRestoreMeterUartPins();   /* 검사 후 UART 모드로 원복 */

    for(i = 0; i < 10; i++)
    {
        if(meterReply[i] != meterCheckFrame[i])
        {
            status = APP_STATUS_UART_RX_ERROR;
            break;
        }
    }

    return (status);
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
    const uint8_t meterWakeFrame[] = {0x10, 0x5B, 0x01, 0x5C, 0x16};
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {
        0,
    };
    //const uint8_t SYNC_START = 0x68;
    //const uint8_t SYNC_STOP = 0x16;

    g_appSelfTestMeterLastReplyValid = APP_FALSE;
    g_appSelfTestMeterLastReplyLen = 0u;
    g_appSelfTestMeterLastReplyIsSc1xxx = APP_FALSE;

    APP_LOGI("SELF", "meter normal probe start");

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

    (void)memcpy(g_appSelfTestMeterLastReply,
                 meterReply,
                 APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    g_appSelfTestMeterLastReplyLen = APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN;
    g_appSelfTestMeterLastReplyValid = APP_TRUE;
    g_appSelfTestMeterLastReplyIsSc1xxx = APP_FALSE;

    HAL_Delay(100); //>= meter spec. 100ms
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    App_LogHexDump(APP_LOG_LEVEL_DEBUG, "SELF", (const uint8_t *)meterReply, APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    APP_RETURN_IF_FALSE((status == APP_STATUS_OK), status);

    uint8_t storageEnabledPrev;

    APP_LOGI("SELF", "meter reply >=%uB", (unsigned int)APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    APP_LOGI("SELF", "selftest-only, no storage push");

    storageEnabledPrev = App_MeterIsStorageEnabled();
    App_MeterSetStorageEnabled(APP_FALSE);
    status = App_MeterProcessReceivedData((const uint8_t *)meterReply, APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    App_MeterSetStorageEnabled(storageEnabledPrev);
    return (status);
}

static AppStatus_t App_SelfTestCheckMeterSC1xxxUart(void)
{
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    AppStatus_t status = APP_STATUS_OK;
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {
        0,
    };
    //const uint8_t SYNC_START = 0x02;
    //const uint8_t SYNC_STOP = 0x03;
    //int i = 0; 

    g_appSelfTestMeterLastReplyValid = APP_FALSE;
    g_appSelfTestMeterLastReplyLen = 0u;
    g_appSelfTestMeterLastReplyIsSc1xxx = APP_FALSE;

    APP_LOGI("SELF", "meter SC1 probe start");

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

        (void)memcpy(g_appSelfTestMeterLastReply,
                     meterReply,
                     APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);
        g_appSelfTestMeterLastReplyLen = APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN;
        g_appSelfTestMeterLastReplyValid = APP_TRUE;
        g_appSelfTestMeterLastReplyIsSc1xxx = APP_TRUE;

        HAL_Delay(100); //>= 100ms
        App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        HAL_Delay(100); 
        App_LogHexDump(APP_LOG_LEVEL_DEBUG, "SELF", (const uint8_t *)meterReply, APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);
    }

    {
        uint8_t storageEnabledPrev = App_MeterIsStorageEnabled();
        APP_LOGI("SELF", "selftest-only, no storage push");
        App_MeterSetStorageEnabled(APP_FALSE);
        status = App_MeterSC1xxxProcessReceivedData((const uint8_t *)meterReply, APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);
        App_MeterSetStorageEnabled(storageEnabledPrev);
    }
    return (status);
}

static AppStatus_t App_SelfTestCheckMeterCrc(void)
{
    App_MeterResult_t meterResult;

    if (g_appSelfTestContext.items[APP_SELFTEST_ITEM_METER_UART].passed != APP_TRUE)
    {
        APP_LOGD("SELF", "METER_CRC fail-skip by METER_UART fail");
        return APP_STATUS_SELFTEST_FAILED;
    }

    APP_RETURN_IF_FALSE((g_appSelfTestMeterLastReplyValid == APP_TRUE), APP_STATUS_SELFTEST_FAILED);
    APP_RETURN_IF_FALSE((g_appSelfTestMeterLastReplyLen > 0u), APP_STATUS_SELFTEST_FAILED);

#if defined(SUPPORT_METER_SC1xxx)
    if (g_appSelfTestMeterLastReplyIsSc1xxx == APP_TRUE)
    {
        App_MeterSC1xxxUnion_t rxFrame = {0};
        meterResult = App_MeterSC1xxxParseFrame(&rxFrame,
                                                g_appSelfTestMeterLastReply,
                                                g_appSelfTestMeterLastReplyLen);
        if (meterResult != APP_METER_OK)
        {
            APP_LOGE("SELF", "MCRC(SC1) fail parse=%d", (int)meterResult);
            return APP_STATUS_SELFTEST_FAILED;
        }

        APP_LOGI("SELF", "MCRC(SC1) pass");
        return APP_STATUS_OK;
    }
#endif

    {
        App_MeterUnion_t rxFrame = {0};
        meterResult = App_MeterParseFrame(&rxFrame,
                                          g_appSelfTestMeterLastReply,
                                          g_appSelfTestMeterLastReplyLen);
        if (meterResult != APP_METER_OK)
        {
            APP_LOGE("SELF", "MCRC(norm) fail parse=%d", (int)meterResult);
            return APP_STATUS_SELFTEST_FAILED;
        }
    }

    APP_LOGI("SELF", "MCRC(norm) pass");
    return APP_STATUS_OK;
}

static AppStatus_t App_SelfTestProbeNbiotUartShort(void)
{
    AppStatus_t status = APP_STATUS_UART_TIMEOUT;
    uint32_t retry;
    uint32_t startTick;

    startTick = HAL_GetTick();

    APP_WWDGFeed();
    HAL_Delay(APP_SELFTEST_NBIOT_POWER_SETTLE_MS);

    for (retry = 0u; retry < APP_SELFTEST_NBIOT_PING_RETRY_MAX; retry++)
    {
        APP_WWDGFeed();
        status = App_Bc95AtPing(APP_SELFTEST_NBIOT_PING_TIMEOUT_MS);
        if (status == APP_STATUS_OK)
        {
            APP_LOGI("SELF", "nbiot probe pass try=%lu t=%lums",
                     (unsigned long)retry,
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }

        if ((retry + 1u) < APP_SELFTEST_NBIOT_PING_RETRY_MAX)
        {
            HAL_Delay(APP_SELFTEST_NBIOT_PING_RETRY_DELAY_MS);
        }
    }

    APP_LOGW("SELF", "nbiot probe fail last=%d t=%lums", (int)status, (unsigned long)(HAL_GetTick() - startTick));
    return status;
}

static AppStatus_t App_SelfTestCheckNbiotUart(void)
{
    AppStatus_t status;
    uint8_t wasPowered;

    APP_RETURN_IF_FALSE(APP_UART_NBIOT_HANDLE->Instance == LPUART1, APP_STATUS_HW_HANDLE_INVALID);

    APP_LOGI("SELF", "nbiot uart probe start");

    /* 검사 시작 전 현재 전원 상태를 저장해, 검사 종료 후 원래 상태로 복원한다. */
    wasPowered = App_GpioLpGetContext()->nbiotPowered;

    if (wasPowered != APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_GpioLpSetNbiotPowered(APP_TRUE) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    status = App_SelfTestProbeNbiotUartShort();

    APP_WWDGFeed();

    if (wasPowered != APP_TRUE)
    {
        (void)App_GpioLpSetNbiotPowered(APP_FALSE);
        HAL_Delay(APP_SELFTEST_UART_METER_POST_NBIOT_SETTLE_DELAY_MS);
    }

    return status;
}

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
        APP_LOGI("SELF", "%s i2c probe addr=0x%02X", p_itemName, (unsigned int)address7bit);

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

        APP_LOGI("SELF", "T=%d.%02dC RH=%d.%02d%%", t_int, t_dec, h_int, h_dec);
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
        APP_LOGI("SELF", "%s pass", App_SelfTestItemToString(item));
    }
    else
    {
        APP_LOGE("SELF", "%s fail st=%lu", App_SelfTestItemToString(item), (unsigned long)status);
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

typedef enum
{
    APP_SELFTEST_SEQ_BOOT = 0,
    APP_SELFTEST_SEQ_DATA_COLLECTION,
    APP_SELFTEST_SEQ_PERIODIC_METER_WAKE
} AppSelfTestSequenceKind_t;

typedef struct
{
    AppSelfTestItem_t item;
    AppStatus_t (*p_checkFunction)(void);
    uint8_t beepOnFail;
} AppSelfTestSeqStep_t;

static void App_SelfTestPrepareSequenceContext(uint8_t resetMeterReplyCache)
{
    g_appSelfTestContext.running = APP_TRUE;
    g_appSelfTestContext.lastRunTickMs = HAL_GetTick();
    g_appSelfTestContext.passCount = 0u;
    g_appSelfTestContext.failCount = 0u;
    (void)memset(g_appSelfTestContext.items, 0, sizeof(g_appSelfTestContext.items));

    if (resetMeterReplyCache == APP_TRUE)
    {
        g_appSelfTestMeterLastReplyLen = 0u;
        g_appSelfTestMeterLastReplyValid = APP_FALSE;
        g_appSelfTestMeterLastReplyIsSc1xxx = APP_FALSE;
    }
}

static void App_SelfTestFinalizeSequenceContext(const char *p_summaryFmt)
{
    g_appSelfTestContext.running = APP_FALSE;
    g_appSelfTestContext.lastSequenceStatus =
        (g_appSelfTestContext.failCount == 0u) ? APP_STATUS_OK : APP_STATUS_SELFTEST_FAILED;

    APP_LOGN("SELF", p_summaryFmt,
             (unsigned long)g_appSelfTestContext.passCount,
             (unsigned long)g_appSelfTestContext.failCount);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE) && (APP_EPC_ACTIVE_TEST_ID == 3u)
    App_SelfTestReportTest3Result();
#endif
}

static void App_SelfTestRunCommonSteps(const AppSelfTestSeqStep_t *p_steps, uint32_t count)
{
    uint32_t i;
    for (i = 0u; i < count; i++)
    {
        App_SelfTestRunItemWithPolicy(p_steps[i].item,
                                      p_steps[i].p_checkFunction,
                                      p_steps[i].beepOnFail);
    }
}

static AppStatus_t App_SelfTestRunSequenceInternal(AppSelfTestSequenceKind_t kind,
                                                   AppStatus_t meterProbeStatus)
{
    static const AppSelfTestSeqStep_t kBootPrefix[] =
    {
        { APP_SELFTEST_ITEM_BUZZER,       App_SelfTestCheckBuzzer,           APP_TRUE },
        { APP_SELFTEST_ITEM_CRC,          App_SelfTestCheckCrc,              APP_TRUE },
        { APP_SELFTEST_ITEM_BATTERY_ADC,  App_SelfTestCheckBatteryAdc,       APP_TRUE },
        { APP_SELFTEST_ITEM_DEBUG_UART,   App_SelfTestCheckDebugUart,        APP_TRUE },
    };

    static const AppSelfTestSeqStep_t kCommonSuffixBeep[] =
    {
        { APP_SELFTEST_ITEM_METER_CRC,    App_SelfTestCheckMeterCrc,         APP_TRUE  },
        { APP_SELFTEST_ITEM_NBIOT_UART,   App_SelfTestCheckNbiotUart,        APP_TRUE  },
        { APP_SELFTEST_ITEM_NFC_I2C,      App_SelfTestCheckNfcI2c,           APP_TRUE  },
        { APP_SELFTEST_ITEM_AUX_I2C,      App_SelfTestCheckAuxI2c,           APP_TRUE  },
        { APP_SELFTEST_ITEM_EXT_WATCHDOG, App_SelfTestCheckExternalWatchdog, APP_TRUE  },
    };

    static const AppSelfTestSeqStep_t kCommonSuffixSilent[] =
    {
        { APP_SELFTEST_ITEM_METER_CRC,    App_SelfTestCheckMeterCrc,         APP_FALSE },
        { APP_SELFTEST_ITEM_NBIOT_UART,   App_SelfTestCheckNbiotUart,        APP_FALSE },
        { APP_SELFTEST_ITEM_NFC_I2C,      App_SelfTestCheckNfcI2c,           APP_FALSE },
        { APP_SELFTEST_ITEM_AUX_I2C,      App_SelfTestCheckAuxI2c,           APP_FALSE },
        { APP_SELFTEST_ITEM_EXT_WATCHDOG, App_SelfTestCheckExternalWatchdog, APP_FALSE },
    };

    APP_RETURN_IF_FALSE(g_appSelfTestContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(App_LogGetContext()->initialized == APP_TRUE, APP_STATUS_LOG_INIT_FAILED);

    App_SelfTestPrepareSequenceContext((kind == APP_SELFTEST_SEQ_PERIODIC_METER_WAKE) ? APP_FALSE : APP_TRUE);

    if (kind == APP_SELFTEST_SEQ_BOOT)
    {
        App_SelfTestRunCommonSteps(kBootPrefix,
                                   (uint32_t)(sizeof(kBootPrefix) / sizeof(kBootPrefix[0])));
#if defined(SUPPORT_METER_NORMAL)
        App_SelfTestRunItem(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterNormalUart);
#elif defined(SUPPORT_METER_SC1xxx)
        App_SelfTestRunItem(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterSC1xxxUart);
#endif
        App_SelfTestRunItem((g_appSelfTestContext.items[APP_SELFTEST_ITEM_METER_UART].passed == APP_TRUE)
                                ? APP_SELFTEST_ITEM_METER_UART_LINE
                                : APP_SELFTEST_ITEM_METER_UART_LINE,
                            (g_appSelfTestContext.items[APP_SELFTEST_ITEM_METER_UART].passed == APP_TRUE)
                                ? App_SelfTestCheckMeterUartLineOk
                                : App_SelfTestCheckMeterUartLine);
        App_SelfTestRunCommonSteps(kCommonSuffixBeep,
                                   (uint32_t)(sizeof(kCommonSuffixBeep) / sizeof(kCommonSuffixBeep[0])));
        App_SelfTestFinalizeSequenceContext("------ Boot self-test summary: pass=%lu fail=%lu");
        return g_appSelfTestContext.lastSequenceStatus;
    }

    if (kind == APP_SELFTEST_SEQ_DATA_COLLECTION)
    {
        APP_LOGN("SELF", "op-data start");
        App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_BATTERY_ADC, App_SelfTestCheckBatteryAdc, APP_FALSE);
#if defined(SUPPORT_METER_NORMAL)
        App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterNormalUart, APP_FALSE);
#elif defined(SUPPORT_METER_SC1xxx)
        App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_METER_UART, App_SelfTestCheckMeterSC1xxxUart, APP_FALSE);
#endif
        App_SelfTestRunItemWithPolicy(
            APP_SELFTEST_ITEM_METER_UART_LINE,
            (g_appSelfTestContext.items[APP_SELFTEST_ITEM_METER_UART].passed == APP_TRUE)
                ? App_SelfTestCheckMeterUartLineOk
                : App_SelfTestCheckMeterUartLine,
            APP_FALSE);
        App_SelfTestRunCommonSteps(kCommonSuffixSilent,
                                   (uint32_t)(sizeof(kCommonSuffixSilent) / sizeof(kCommonSuffixSilent[0])));
        App_SelfTestFinalizeSequenceContext("Operational data collection done: pass=%lu fail=%lu");
        return g_appSelfTestContext.lastSequenceStatus;
    }

    APP_LOGN("SELF", "meter-wake selftest start");
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_BATTERY_ADC, App_SelfTestCheckBatteryAdc, APP_FALSE);
    App_SelfTestRecordResult(APP_SELFTEST_ITEM_METER_UART, meterProbeStatus);
    if (meterProbeStatus == APP_STATUS_OK)
    {
        APP_LOGI("SELF", "%s PASS", App_SelfTestItemToString(APP_SELFTEST_ITEM_METER_UART));
    }
    else
    {
        APP_LOGE("SELF", "%s FAIL status=%lu",
                 App_SelfTestItemToString(APP_SELFTEST_ITEM_METER_UART),
                 (unsigned long)meterProbeStatus);
    }
    App_SelfTestRunItemWithPolicy(APP_SELFTEST_ITEM_METER_UART_LINE,
                                  (meterProbeStatus == APP_STATUS_OK)
                                      ? App_SelfTestCheckMeterUartLineOk
                                      : App_SelfTestCheckMeterUartLine,
                                  APP_FALSE);
    App_SelfTestRunCommonSteps(kCommonSuffixSilent,
                               (uint32_t)(sizeof(kCommonSuffixSilent) / sizeof(kCommonSuffixSilent[0])));
    App_SelfTestFinalizeSequenceContext("Periodic meter-wake self-test done: pass=%lu fail=%lu");
    return g_appSelfTestContext.lastSequenceStatus;
}

AppStatus_t App_SelfTestInit(void)
{
    (void)memset(&g_appSelfTestContext, 0, sizeof(g_appSelfTestContext));
    g_appSelfTestContext.initialized = APP_TRUE;
    g_appSelfTestContext.lastSequenceStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSelfTestNbiotExecuted = APP_FALSE;

    g_appSelfTestMeterLastReplyLen = 0u;
    g_appSelfTestMeterLastReplyValid = APP_FALSE;
    g_appSelfTestMeterLastReplyIsSc1xxx = APP_FALSE;
    (void)memset(g_appSelfTestMeterLastReply, 0, sizeof(g_appSelfTestMeterLastReply));

    return APP_STATUS_OK;
}

void App_SelfTestSetNbiotExecutedHint(uint8_t executed)
{
    g_appSelfTestNbiotExecuted = (executed != APP_FALSE) ? APP_TRUE : APP_FALSE;
}

AppStatus_t App_SelfTestRunBootSequence(void)
{
    return App_SelfTestRunSequenceInternal(APP_SELFTEST_SEQ_BOOT, APP_STATUS_OK);
}

AppStatus_t App_SelfTestRunDataCollectionSequence(void)
{
    return App_SelfTestRunSequenceInternal(APP_SELFTEST_SEQ_DATA_COLLECTION, APP_STATUS_OK);
}

AppStatus_t App_SelfTestRunPeriodicMeterWakeSequence(AppStatus_t meterProbeStatus)
{
   return App_SelfTestRunSequenceInternal(APP_SELFTEST_SEQ_PERIODIC_METER_WAKE, meterProbeStatus);
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

#if !(defined(SUPPORT_SELFTEST) || (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE))
AppStatus_t App_SelfTestRunPeriodicMeterWakeSequence(AppStatus_t meterProbeStatus)
{
    (void)meterProbeStatus;
    return APP_STATUS_OK;
}
#endif
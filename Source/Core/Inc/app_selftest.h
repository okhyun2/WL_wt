#ifndef APP_SELFTEST_H
#define APP_SELFTEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_selftest.h
 * @brief   Boot-time peripheral initialization/check pseudo-code runner.
 */

/**
 * @brief Individual boot self-test items.
 */
typedef enum
{
    APP_SELFTEST_ITEM_BUZZER = 0,
    APP_SELFTEST_ITEM_CRC,
    APP_SELFTEST_ITEM_BATTERY_ADC,
    APP_SELFTEST_ITEM_DEBUG_UART,
    APP_SELFTEST_ITEM_METER_UART_LINE,
    APP_SELFTEST_ITEM_METER_UART,
    APP_SELFTEST_ITEM_METER_CRC,
    APP_SELFTEST_ITEM_NBIOT_UART,
    APP_SELFTEST_ITEM_NFC_I2C,
    APP_SELFTEST_ITEM_AUX_I2C,
    APP_SELFTEST_ITEM_EXT_WATCHDOG,
    APP_SELFTEST_ITEM_COUNT
} AppSelfTestItem_t;

/**
 * @brief Result of one boot self-test item.
 */
typedef struct
{
    uint8_t executed;
    uint8_t passed;
    AppStatus_t status;
    uint32_t tickMs;
} AppSelfTestItemResult_t;

/**
 * @brief Runtime context of the boot self-test module.
 */
typedef struct
{
    uint8_t initialized;
    uint8_t running;
    uint32_t lastRunTickMs;
    uint32_t passCount;
    uint32_t failCount;
    AppStatus_t lastSequenceStatus;
    AppSelfTestItemResult_t items[APP_SELFTEST_ITEM_COUNT];
} AppSelfTestContext_t;

const char *App_SelfTestResetCauseToString(void);

/**
 * @brief Initialize self-test runtime context.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_SelfTestInit(void);

/**
 * @brief Run boot-time peripheral check sequence.
 *
 * @note The buzzer test is intentionally executed first. Remaining peripheral
 *       failures raise an error buzzer pattern but do not necessarily stop boot.
 *
 * @return APP_STATUS_OK when all checks pass, APP_STATUS_SELFTEST_FAILED when
 *         one or more checks fail, or another error code on fatal setup issues.
 */
AppStatus_t App_SelfTestRunBootSequence(void);
void App_SelfTestSetNbiotExecutedHint(uint8_t executed);
AppStatus_t App_SelfTestRunDataCollectionSequence(void);

AppStatus_t App_SelfTestRunPeriodicMeterWakeSequence(AppStatus_t meterProbeStatus);

/**
 * @brief Play the error buzzer pattern.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_SelfTestSignalErrorBuzzer(void);

/* [DIAG-BITS] 진단 비트 (0xD2 TLV diag[6] 예약 비트 bit11~15에 보고):
 * bit11=LSE_FAIL, bit12=METER_OPEN(단선), bit13=METER_DEGRADED(불량), bit14~15=예약 */
#define APP_DIAG_BIT_LSE_FAIL          (0x0800u)
#define APP_DIAG_BIT_METER_OPEN        (0x1000u)
#define APP_DIAG_BIT_METER_DEGRADED    (0x2000u)
void App_SelfTestSetDiagBit(uint16_t bitMask);
void App_SelfTestClearDiagBit(uint16_t bitMask);
uint16_t App_SelfTestGetDiagBits(void);
uint8_t App_SelfTestProbeTx0MonActivityPublic(void);

/**
 * @brief Route UART RX complete interrupt to self-test receive helper.
 *
 * @param p_huart UART handle from HAL callback.
 */
void App_SelfTestOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart);

/**
 * @brief Route UART error interrupt to self-test receive helper.
 *
 * @param p_huart UART handle from HAL callback.
 */
void App_SelfTestOnUartErrorIsr(UART_HandleTypeDef *p_huart);

/**
 * @brief Get immutable self-test runtime context.
 *
 * @return Pointer to internal context.
 */
const AppSelfTestContext_t *App_SelfTestGetContext(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SELFTEST_H */

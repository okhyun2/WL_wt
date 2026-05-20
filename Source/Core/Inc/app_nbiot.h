#ifndef APP_NBIOT_H
#define APP_NBIOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "stm32l0xx_hal.h"
#include "app_build_config.h"
#include "app_error.h"

#define APP_BC95_IMEI_DIGITS                 (15u)
#define APP_BC95_IMEI_BCD_BYTES              (8u)
#define APP_BC95_IMSI_DIGITS                 (15u)
#define APP_BC95_IMSI_BCD_BYTES              (8u)

#define APP_BC95_AT_RX_BUF_SIZE              (512u)
#define APP_BC95_AT_TX_TIMEOUT_MS            (500u)
#define APP_BC95_AT_RX_TIMEOUT_MS            (3000u)

typedef enum
{
    APP_BC95_AT_OK              = 0,
    APP_BC95_AT_ERR_PARAM       = 1,
    APP_BC95_AT_ERR_TIMEOUT     = 2,
    APP_BC95_AT_ERR_NO_PREFIX   = 3,
    APP_BC95_AT_ERR_FORMAT      = 4,
    APP_BC95_AT_ERR_RANGE       = 5,
    APP_BC95_AT_ERR_AT_ERROR    = 6,
    APP_BC95_AT_ERR_CME_ERROR   = 7,
    APP_BC95_AT_ERR_NO_OK       = 8
} AppBc95AtStatus_t;

#define APP_BC95_QUALITY_BYTES               (10u)

typedef struct
{
    int16_t  rssiDbm;        /* -0 ~ -150 dBm 범위, AT+CSQ -> rssi */
    uint8_t  ber;            /* 0~7, 99=unknown (reserved) */
    uint32_t cellId;         /* AT+NUESTATS -> Cell ID */
    int16_t  rsrpDbm;        /* dBm (centibels / 10) */
    int16_t  rsrqDbm;        /* dBm (centibels / 10) */
    int16_t  snrDb;          /* dB  (centibels / 10) */
    uint8_t  valid;          /* APP_TRUE if 모든 필드 채워짐 */
} AppBc95Quality_t;


AppStatus_t        App_Bc95AtSendCommand(const char *p_cmd, uint8_t *p_rxBuf, uint16_t rxBufSize, uint32_t rxTimeoutMs, uint16_t *p_rxLengthOut);

AppBc95AtStatus_t  App_Bc95AtCheckResponse(const char *p_resp, int32_t *p_cmeErrOut);

AppBc95AtStatus_t  App_Bc95AtExtractImeiString(const char *p_resp, char *p_out, uint32_t outSize);
AppBc95AtStatus_t  App_Bc95AtImeiToBcd(const char *p_imeiStr, uint8_t *p_bcdOut, uint32_t bcdSize);
AppBc95AtStatus_t  App_Bc95AtParseImeiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize);

AppBc95AtStatus_t  App_Bc95AtExtractImsiString(const char *p_resp, char *p_out, uint32_t outSize);
AppBc95AtStatus_t  App_Bc95AtImsiToBcd(const char *p_imsiStr, uint8_t *p_bcdOut, uint32_t bcdSize);
AppBc95AtStatus_t  App_Bc95AtParseImsiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize);

AppStatus_t        App_Bc95AtFetchImei(uint8_t *p_imeiBcd, uint32_t bcdSize);
AppStatus_t        App_Bc95AtFetchImsi(uint8_t *p_imsiBcd, uint32_t bcdSize);

const uint8_t     *App_Bc95AtGetImei(void);
const uint8_t     *App_Bc95AtGetImsi(void);
const char        *App_Bc95AtGetStatusString(AppBc95AtStatus_t status);

void               App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart);
void               App_Bc95AtOnUartErrorIsr(UART_HandleTypeDef *p_huart);

AppBc95AtStatus_t  App_Bc95AtParseCsq(const char *p_resp, int32_t *p_rssiRaw, int32_t *p_ber);
AppBc95AtStatus_t  App_Bc95AtParseNuestatsRadio(const char *p_resp,
                                                int32_t *p_signalPowerCb,
                                                uint32_t *p_cellId,
                                                int32_t *p_snrCb,
                                                int32_t *p_rsrqCb);

AppStatus_t        App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality);
AppStatus_t        App_Bc95AtFetchQualityBytes(uint8_t *p_buf, uint32_t bufSize);

const AppBc95Quality_t *App_Bc95AtGetQuality(void);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

AppStatus_t App_NBIoTAtInit(void);
AppStatus_t App_NBIoTReadIdentity(void);
AppStatus_t App_NBIoTReadQuality(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* APP_NBIOT_H */

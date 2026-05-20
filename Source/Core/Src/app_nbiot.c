#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_build_config.h"

#include "app_nbiot.h"
#include "app_hw.h"
#include "app_log.h"

#define APP_BC95_AT_CGSN_PREFIX_LEN          (6u)
#define APP_BC95_AT_CME_ERROR_PREFIX         "+CME ERROR:"
#define APP_BC95_AT_CME_ERROR_PREFIX_LEN     (11u)
#define APP_BC95_AT_ERROR_TOKEN              "ERROR"
#define APP_BC95_AT_ERROR_TOKEN_LEN          (5u)
#define APP_BC95_AT_OK_TOKEN                 "OK"
#define APP_BC95_AT_OK_TOKEN_LEN             (2u)
#define APP_BC95_AT_BCD_PAD_NIBBLE           (0x0Fu)
#define APP_BC95_AT_NIBBLE_PAIR_COUNT        (16u)

#define APP_BC95_AT_CMD_IMEI                 "AT+CGSN=1\r\n"
#define APP_BC95_AT_CMD_IMSI                 "AT+CIMI\r\n"
#define APP_BC95_AT_CMD_CSQ                  "AT+CSQ\r\n"
#define APP_BC95_AT_CMD_NUESTATS             "AT+NUESTATS\r\n"
#define APP_BC95_AT_CSQ_PREFIX               "+CSQ:"
#define APP_BC95_AT_CSQ_PREFIX_LEN           (5u)
#define APP_BC95_AT_DBM_ABS_MAX              (150)
#define APP_BC95_AT_CSQ_RSSI_UNKNOWN         (99)
#define APP_BC95_AT_CSQ_BER_UNKNOWN          (99)

typedef struct
{
    UART_HandleTypeDef *p_huart;
    uint8_t *p_buffer;
    uint16_t bufferSize;
    volatile uint16_t receivedLength;
    volatile uint8_t active;
    volatile uint8_t completed;
    volatile uint8_t error;
} AppBc95AtRxContext_t;

static AppBc95AtRxContext_t g_appBc95AtRxContext;
static uint8_t  g_appBc95AtInitialized;
static uint8_t  g_appBc95AtImeiBcd[APP_BC95_IMEI_BCD_BYTES];
static uint8_t  g_appBc95AtImsiBcd[APP_BC95_IMSI_BCD_BYTES];
static uint8_t  g_appBc95AtRxBuf[APP_BC95_AT_RX_BUF_SIZE];

#ifdef DEBUG
typedef struct
{
    volatile uint32_t errorCode;     /* huart->ErrorCode 스냅샷 */
    volatile uint16_t errorAtIndex;  /* 에러 발생 시점의 수신 길이 */
    volatile uint8_t  errorCount;
} AppBc95AtRxErrorInfo_t;

static AppBc95AtRxErrorInfo_t g_appBc95AtRxErrorInfo;
#endif // DEBUG

static uint8_t App_Bc95AtIsLineStart(const char *p_buf, const char *p_pos)
{
    if (p_pos == p_buf)
    {
        return APP_TRUE;
    }

    if ((*(p_pos - 1) == '\r') || (*(p_pos - 1) == '\n'))
    {
        return APP_TRUE;
    }

    return APP_FALSE;
}

static uint8_t App_Bc95AtIsLineEnd(char ch)
{
    if ((ch == '\0') || (ch == '\r') || (ch == '\n'))
    {
        return APP_TRUE;
    }

    return APP_FALSE;
}

static const char *App_Bc95AtFindOkBoundary(const char *p_resp)
{
    const char *p_scan = p_resp;
    const char *p_token;

    while (*p_scan != '\0')
    {
        p_token = strstr(p_scan, APP_BC95_AT_OK_TOKEN);
        if (p_token == NULL)
        {
            return NULL;
        }

        if ((App_Bc95AtIsLineStart(p_resp, p_token) == APP_TRUE) &&
            (App_Bc95AtIsLineEnd(*(p_token + APP_BC95_AT_OK_TOKEN_LEN)) == APP_TRUE))
        {
            return p_token;
        }

        p_scan = p_token + APP_BC95_AT_OK_TOKEN_LEN;
    }

    return NULL;
}

static uint8_t App_Bc95AtHasErrorToken(const char *p_resp)
{
    const char *p_scan = p_resp;
    const char *p_token;

    while (*p_scan != '\0')
    {
        p_token = strstr(p_scan, APP_BC95_AT_ERROR_TOKEN);
        if (p_token == NULL)
        {
            return APP_FALSE;
        }

        if ((App_Bc95AtIsLineStart(p_resp, p_token) == APP_TRUE) &&
            (App_Bc95AtIsLineEnd(*(p_token + APP_BC95_AT_ERROR_TOKEN_LEN)) == APP_TRUE))
        {
            return APP_TRUE;
        }

        p_scan = p_token + APP_BC95_AT_ERROR_TOKEN_LEN;
    }

    return APP_FALSE;
}

static const char *App_Bc95AtFindCgsnPrefix(const char *p_resp)
{
    const char *p_scan = p_resp;

    while (*p_scan != '\0')
    {
        if ((p_scan[0] == '+') &&
            ((p_scan[1] == 'C') || (p_scan[1] == 'c')) &&
            ((p_scan[2] == 'G') || (p_scan[2] == 'g')) &&
            ((p_scan[3] == 'S') || (p_scan[3] == 's')) &&
            ((p_scan[4] == 'N') || (p_scan[4] == 'n')) &&
            (p_scan[5] == ':'))
        {
            return (p_scan + APP_BC95_AT_CGSN_PREFIX_LEN);
        }
        p_scan++;
    }

    return NULL;
}

static uint8_t App_Bc95AtIsResponseTerminated(const uint8_t *p_buf, uint16_t length)
{
    /* AT 응답 종료 판정:
     *   - 정상 종료: \r\nOK\r\n
     *   - 비정상 종료: \r\nERROR\r\n
     *   - 비정상 종료: +CME ERROR: <code>\r\n
     */
    const char *p_str;
    char saved;

    if ((p_buf == NULL) || (length < 6u))
    {
        return APP_FALSE;
    }

    /* NUL 종료 처리: 호출자 버퍼 끝에 NUL 보장 (수신 함수에서 항상 확보) */
    p_str = (const char *)p_buf;
    saved = (char)p_buf[length];                  /* 호출자가 length 위치에 NUL 보장 */
    (void)saved;

    if (App_Bc95AtFindOkBoundary(p_str) != NULL)
    {
        return APP_TRUE;
    }

    if (App_Bc95AtHasErrorToken(p_str) == APP_TRUE)
    {
        return APP_TRUE;
    }

    if (strstr(p_str, APP_BC95_AT_CME_ERROR_PREFIX) != NULL)
    {
        /* +CME ERROR: 가 들어왔다면 줄 끝(\r 또는 \n)이 도달했는지 확인 */
        if ((strchr(p_str, '\n') != NULL) || (strchr(p_str, '\r') != NULL))
        {
            return APP_TRUE;
        }
    }

    return APP_FALSE;
}

static AppBc95AtStatus_t App_Bc95AtConvertDigitsToBcd(const char *p_digits, uint32_t digitCount, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    uint8_t nibble[APP_BC95_AT_NIBBLE_PAIR_COUNT];
    uint32_t index;
    uint32_t pairIndex;

    if ((p_digits == NULL) || (p_bcdOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    if (digitCount != (uint32_t)APP_BC95_IMEI_DIGITS)
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    if (bcdSize < (uint32_t)APP_BC95_IMEI_BCD_BYTES)
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    for (index = 0u; index < digitCount; index++)
    {
        if ((p_digits[index] < '0') || (p_digits[index] > '9'))
        {
            return APP_BC95_AT_ERR_FORMAT;
        }
        nibble[index] = (uint8_t)(p_digits[index] - '0');
    }
    nibble[APP_BC95_AT_NIBBLE_PAIR_COUNT - 1u] = APP_BC95_AT_BCD_PAD_NIBBLE;

    for (pairIndex = 0u; pairIndex < (uint32_t)APP_BC95_IMEI_BCD_BYTES; pairIndex++)
    {
        p_bcdOut[pairIndex] = (uint8_t)((nibble[2u * pairIndex] << 4u) |
                                        (nibble[(2u * pairIndex) + 1u] & 0x0Fu));
    }

    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtCheckResponse(const char *p_resp, int32_t *p_cmeErrOut)
{
    const char *p_cme;
    const char *p_ok;

    if (p_cmeErrOut != NULL)
    {
        *p_cmeErrOut = 0;
    }

    if ((p_resp == NULL) || (p_resp[0] == '\0'))
    {
        return APP_BC95_AT_ERR_TIMEOUT;
    }

    p_cme = strstr(p_resp, APP_BC95_AT_CME_ERROR_PREFIX);
    if (p_cme != NULL)
    {
        if (p_cmeErrOut != NULL)
        {
            const char *p_num = p_cme + APP_BC95_AT_CME_ERROR_PREFIX_LEN;
            while ((*p_num == ' ') || (*p_num == '\t'))
            {
                p_num++;
            }
            *p_cmeErrOut = (int32_t)atoi(p_num);
        }
        return APP_BC95_AT_ERR_CME_ERROR;
    }

    if (App_Bc95AtHasErrorToken(p_resp) == APP_TRUE)
    {
        return APP_BC95_AT_ERR_AT_ERROR;
    }

    p_ok = App_Bc95AtFindOkBoundary(p_resp);
    if (p_ok == NULL)
    {
        return APP_BC95_AT_ERR_NO_OK;
    }

    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtExtractImeiString(const char *p_resp, char *p_out, uint32_t outSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    uint32_t digitCount;

    if ((p_resp == NULL) || (p_out == NULL) || (outSize <= (uint32_t)APP_BC95_IMEI_DIGITS))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    p_out[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_payload = App_Bc95AtFindCgsnPrefix(p_resp);
    if (p_payload == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }

    while ((*p_payload == ' ') || (*p_payload == '\t'))
    {
        p_payload++;
    }

    digitCount = 0u;
    while ((*p_payload != '\0') && (digitCount <= (uint32_t)APP_BC95_IMEI_DIGITS))
    {
        if ((*p_payload >= '0') && (*p_payload <= '9'))
        {
            if (digitCount < (uint32_t)APP_BC95_IMEI_DIGITS)
            {
                p_out[digitCount] = *p_payload;
            }
            digitCount++;
        }
        else if ((*p_payload == '\r') || (*p_payload == '\n'))
        {
            break;
        }
        else
        {
            /* hyphen, space 등은 무시 */
        }
        p_payload++;
    }

    if (digitCount > (uint32_t)APP_BC95_IMEI_DIGITS)
    {
        p_out[APP_BC95_IMEI_DIGITS] = '\0';
        return APP_BC95_AT_ERR_RANGE;
    }
    p_out[digitCount] = '\0';

    if (digitCount != (uint32_t)APP_BC95_IMEI_DIGITS)
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtImeiToBcd(const char *p_imeiStr, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    if ((p_imeiStr == NULL) || (p_bcdOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    if (strlen(p_imeiStr) != (size_t)APP_BC95_IMEI_DIGITS)
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    return App_Bc95AtConvertDigitsToBcd(p_imeiStr, (uint32_t)APP_BC95_IMEI_DIGITS, p_bcdOut, bcdSize);
}

AppBc95AtStatus_t App_Bc95AtParseImeiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    AppBc95AtStatus_t status;
    char imeiStr[APP_BC95_IMEI_DIGITS + 1u];

    if ((p_bcdOut == NULL) || (bcdSize < (uint32_t)APP_BC95_IMEI_BCD_BYTES))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    (void)memset(p_bcdOut, 0, APP_BC95_IMEI_BCD_BYTES);

    status = App_Bc95AtExtractImeiString(p_resp, imeiStr, sizeof(imeiStr));
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    status = App_Bc95AtImeiToBcd(imeiStr, p_bcdOut, bcdSize);
    if (status == APP_BC95_AT_OK)
    {
        (void)memcpy(g_appBc95AtImeiBcd, p_bcdOut, APP_BC95_IMEI_BCD_BYTES);
    }

    return status;
}

AppBc95AtStatus_t App_Bc95AtExtractImsiString(const char *p_resp, char *p_out, uint32_t outSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_okBoundary;
    const char *p_end;
    const char *p_scan;
    const char *p_digitStart;
    uint32_t digitLen;

    if ((p_resp == NULL) || (p_out == NULL) || (outSize <= (uint32_t)APP_BC95_IMSI_DIGITS))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    p_out[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_okBoundary = App_Bc95AtFindOkBoundary(p_resp);
    p_end = (p_okBoundary != NULL) ? p_okBoundary : (p_resp + strlen(p_resp));

    p_scan = p_resp;
    p_digitStart = NULL;
    digitLen = 0u;

    while (p_scan < p_end)
    {
        if (App_Bc95AtIsLineStart(p_resp, p_scan) == APP_TRUE)
        {
            const char *p_lineStart = p_scan;

            while ((p_lineStart < p_end) && ((*p_lineStart == ' ') || (*p_lineStart == '\t')))
            {
                p_lineStart++;
            }

            if ((p_lineStart < p_end) && (*p_lineStart >= '0') && (*p_lineStart <= '9'))
            {
                const char *p_lineEnd = p_lineStart;
                uint32_t count = 0u;

                while ((p_lineEnd < p_end) && (*p_lineEnd >= '0') && (*p_lineEnd <= '9'))
                {
                    count++;
                    p_lineEnd++;
                }

                if ((p_lineEnd == p_end) || (*p_lineEnd == '\r') || (*p_lineEnd == '\n'))
                {
                    p_digitStart = p_lineStart;
                    digitLen = count;
                    break;
                }
            }
        }
        p_scan++;
    }

    if (p_digitStart == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }

    if (digitLen != (uint32_t)APP_BC95_IMSI_DIGITS)
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    (void)memcpy(p_out, p_digitStart, APP_BC95_IMSI_DIGITS);
    p_out[APP_BC95_IMSI_DIGITS] = '\0';

    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtImsiToBcd(const char *p_imsiStr, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    if ((p_imsiStr == NULL) || (p_bcdOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    if (strlen(p_imsiStr) != (size_t)APP_BC95_IMSI_DIGITS)
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    return App_Bc95AtConvertDigitsToBcd(p_imsiStr, (uint32_t)APP_BC95_IMSI_DIGITS, p_bcdOut, bcdSize);
}

AppBc95AtStatus_t App_Bc95AtParseImsiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    AppBc95AtStatus_t status;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    if ((p_bcdOut == NULL) || (bcdSize < (uint32_t)APP_BC95_IMSI_BCD_BYTES))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    (void)memset(p_bcdOut, 0, APP_BC95_IMSI_BCD_BYTES);

    status = App_Bc95AtExtractImsiString(p_resp, imsiStr, sizeof(imsiStr));
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    status = App_Bc95AtImsiToBcd(imsiStr, p_bcdOut, bcdSize);
    if (status == APP_BC95_AT_OK)
    {
        (void)memcpy(g_appBc95AtImsiBcd, p_bcdOut, APP_BC95_IMSI_BCD_BYTES);
    }

    return status;
}

/* ============================================================
 *  UART IT 기반 가변 길이 AT 응답 수신
 *  - 1바이트씩 IT 수신, 매 바이트마다 종료 시퀀스(\r\nOK\r\n /
 *    \r\nERROR\r\n / +CME ERROR:...\r\n) 도달 여부를 검사한다.
 *  - 종료가 감지되면 completed = APP_TRUE.
 *  - 버퍼 끝까지 차거나 타임아웃이면 별도 처리.
 * ============================================================ */
static AppStatus_t App_Bc95AtUartReceiveResponse(UART_HandleTypeDef *p_huart, uint8_t *p_buffer, uint16_t bufferSize, uint32_t timeoutMs, uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;

    APP_RETURN_IF_FALSE((p_huart != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buffer != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufferSize > 1u), APP_STATUS_INVALID_PARAM);

    (void)memset(p_buffer, 0, bufferSize);
    if (p_rxLengthOut != NULL)
    {
        *p_rxLengthOut = 0u;
    }

    g_appBc95AtRxContext.p_huart = p_huart;
    g_appBc95AtRxContext.p_buffer = p_buffer;
    g_appBc95AtRxContext.bufferSize = (uint16_t)(bufferSize - 1u);   /* NUL 종료용 1바이트 확보 */
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.active = APP_TRUE;
    g_appBc95AtRxContext.completed = APP_FALSE;
    g_appBc95AtRxContext.error = APP_FALSE;

    __HAL_UART_CLEAR_FLAG(p_huart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(p_huart, &p_buffer[0], 1u);
    if (halStatus != HAL_OK)
    {
        g_appBc95AtRxContext.active = APP_FALSE;
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (g_appBc95AtRxContext.completed != APP_TRUE)
    {
        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            g_appBc95AtRxContext.active = APP_FALSE;

            #ifdef DEBUG
            APP_LOGE("NBIOT", "UART error: code=0x%08lX, atIndex=%u, partialLen=%u",
                     (unsigned long)g_appBc95AtRxErrorInfo.errorCode,
                     (unsigned)g_appBc95AtRxErrorInfo.errorAtIndex,
                     (unsigned)g_appBc95AtRxContext.receivedLength);
            #endif // DEBUG

            if (p_rxLengthOut != NULL)
            {
                *p_rxLengthOut = g_appBc95AtRxContext.receivedLength;
            }
            return APP_STATUS_UART_RX_ERROR;
        }

        if ((HAL_GetTick() - startTick) >= timeoutMs)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            __HAL_UART_CLEAR_FLAG(p_huart, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
            __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
            g_appBc95AtRxContext.active = APP_FALSE;
            /* 타임아웃이라도 수신된 데이터가 있으면 호출 측에서 활용 가능 */
            if (p_rxLengthOut != NULL)
            {
                *p_rxLengthOut = g_appBc95AtRxContext.receivedLength;
            }
            return APP_STATUS_SELFTEST_TIMEOUT;
        }
    }

    g_appBc95AtRxContext.active = APP_FALSE;
    p_buffer[g_appBc95AtRxContext.receivedLength] = (uint8_t)'\0';
    if (p_rxLengthOut != NULL)
    {
        *p_rxLengthOut = g_appBc95AtRxContext.receivedLength;
    }
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtSendCommand(const char *p_cmd, uint8_t *p_rxBuf, uint16_t rxBufSize,
                                  uint32_t rxTimeoutMs, uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    AppStatus_t status;
    uint16_t cmdLen;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_cmd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_rxBuf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((rxBufSize > 1u), APP_STATUS_INVALID_PARAM);

    cmdLen = (uint16_t)strlen(p_cmd);
    APP_RETURN_IF_FALSE((cmdLen > 0u), APP_STATUS_INVALID_PARAM);

    /* ----- 추가: 명령 송신 전 RX 라인/에러 플래그 완전 초기화 ----- */
    (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    /* 모듈이 IMSI 응답 후 추가 \r\n을 더 보낼 시간을 짧게 보장
     * (BC95는 명령 처리 후 약간의 지연이 있음) */
    HAL_Delay(20);
    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);
    /* ----- 끝 ----- */

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)p_cmd,
                                  cmdLen,
                                  APP_BC95_AT_TX_TIMEOUT_MS);
    APP_RETURN_IF_FALSE((halStatus == HAL_OK), APP_STATUS_UART_TX_FAILED);

    status = App_Bc95AtUartReceiveResponse(APP_UART_NBIOT_HANDLE,
                                           p_rxBuf,
                                           rxBufSize,
                                           rxTimeoutMs,
                                           p_rxLengthOut);
    return status;
}

AppStatus_t App_Bc95AtFetchImei(uint8_t *p_imeiBcd, uint32_t bcdSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;

    APP_RETURN_IF_FALSE((p_imeiBcd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bcdSize >= (uint32_t)APP_BC95_IMEI_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    rxLen = 0u;
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMEI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    atStatus = App_Bc95AtParseImeiToBcd((const char *)g_appBc95AtRxBuf,
                                        p_imeiBcd,
                                        bcdSize);
    if (atStatus != APP_BC95_AT_OK)
    {
        return APP_STATUS_FATAL;
    }

    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchImsi(uint8_t *p_imsiBcd, uint32_t bcdSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;

    APP_RETURN_IF_FALSE((p_imsiBcd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bcdSize >= (uint32_t)APP_BC95_IMSI_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    rxLen = 0u;
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    atStatus = App_Bc95AtParseImsiToBcd((const char *)g_appBc95AtRxBuf,
                                        p_imsiBcd,
                                        bcdSize);
    if (atStatus != APP_BC95_AT_OK)
    {
        return APP_STATUS_FATAL;
    }

    return APP_STATUS_OK;
}

const uint8_t *App_Bc95AtGetImei(void)
{
    return g_appBc95AtImeiBcd;
}

const uint8_t *App_Bc95AtGetImsi(void)
{
    return g_appBc95AtImsiBcd;
}

const char *App_Bc95AtGetStatusString(AppBc95AtStatus_t status)
{
    switch (status)
    {
        case APP_BC95_AT_OK:            return "ok";
        case APP_BC95_AT_ERR_PARAM:     return "param";
        case APP_BC95_AT_ERR_TIMEOUT:   return "timeout";
        case APP_BC95_AT_ERR_NO_PREFIX: return "no_prefix";
        case APP_BC95_AT_ERR_FORMAT:    return "format";
        case APP_BC95_AT_ERR_RANGE:     return "range";
        case APP_BC95_AT_ERR_AT_ERROR:  return "at_error";
        case APP_BC95_AT_ERR_CME_ERROR: return "cme_error";
        case APP_BC95_AT_ERR_NO_OK:     return "no_ok";
        default:                        return "unknown";
    }
}

//////////////////////////////////////////////////////////////////////////
void App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
    HAL_StatusTypeDef halStatus;
    uint16_t nextIndex;
    uint16_t curIndex;

    if ((p_huart == NULL) || (g_appBc95AtRxContext.active != APP_TRUE))
    {
        return;
    }

    if (p_huart != g_appBc95AtRxContext.p_huart)
    {
        return;
    }

    curIndex = g_appBc95AtRxContext.receivedLength;
    nextIndex = (uint16_t)(curIndex + 1u);
    g_appBc95AtRxContext.receivedLength = nextIndex;

    /* NUL 종료 후 종료 시퀀스 감지 */
    g_appBc95AtRxContext.p_buffer[nextIndex] = (uint8_t)'\0';
    if (App_Bc95AtIsResponseTerminated(g_appBc95AtRxContext.p_buffer, nextIndex) == APP_TRUE)
    {
        g_appBc95AtRxContext.completed = APP_TRUE;
        return;
    }

    if (nextIndex >= g_appBc95AtRxContext.bufferSize)
    {
        /* 버퍼 끝까지 채워졌으면 더 이상 받지 못함 -> 완료로 간주
         * (호출 측에서 파서가 OK/ERROR 부재로 NO_OK 반환할 수 있음) */
        g_appBc95AtRxContext.completed = APP_TRUE;
        return;
    }

    halStatus = HAL_UART_Receive_IT(p_huart,
                                    &g_appBc95AtRxContext.p_buffer[nextIndex],
                                    1u);
    if (halStatus != HAL_OK)
    {
        g_appBc95AtRxContext.error = APP_TRUE;
    }
}

void App_Bc95AtOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
    if ((p_huart == NULL) || (g_appBc95AtRxContext.active != APP_TRUE))
    {
        return;
    }

    if (p_huart != g_appBc95AtRxContext.p_huart)
    {
        return;
    }

#ifdef DEBUG
    /* 디버그용: 에러 코드 캡처 */
    g_appBc95AtRxErrorInfo.errorCode = p_huart->ErrorCode;
    g_appBc95AtRxErrorInfo.errorAtIndex = g_appBc95AtRxContext.receivedLength;
    g_appBc95AtRxErrorInfo.errorCount++;
#endif // DEBUG

    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
    g_appBc95AtRxContext.error = APP_TRUE;
}

static AppBc95Quality_t g_appBc95AtQuality;

/* ------------------------------------------------------------
 * AT+CSQ 응답 파싱
 *   응답 예: "\r\n+CSQ:23,99\r\n\r\nOK\r\n"
 *   - rssi_raw : 0~31 또는 99(unknown)
 *   - ber      : 0~7  또는 99(unknown)
 * ------------------------------------------------------------ */
AppBc95AtStatus_t App_Bc95AtParseCsq(const char *p_resp, int32_t *p_rssiRaw, int32_t *p_ber)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int rssi;
    int ber;
    int matched;

    if ((p_resp == NULL) || (p_rssiRaw == NULL) || (p_ber == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    *p_rssiRaw = (int32_t)APP_BC95_AT_CSQ_RSSI_UNKNOWN;
    *p_ber     = (int32_t)APP_BC95_AT_CSQ_BER_UNKNOWN;

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_payload = strstr(p_resp, APP_BC95_AT_CSQ_PREFIX);
    if (p_payload == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_payload += APP_BC95_AT_CSQ_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t'))
    {
        p_payload++;
    }

    matched = sscanf(p_payload, "%d,%d", &rssi, &ber);
    if (matched != 2)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }

    *p_rssiRaw = (int32_t)rssi;
    *p_ber     = (int32_t)ber;
    return APP_BC95_AT_OK;
}

/* ------------------------------------------------------------
 * AT+NUESTATS=RADIO 응답 파싱
 *   응답 예 (라인 단위, prefix "NUESTATS:RADIO,"):
 *     NUESTATS:RADIO,Signal power:-842
 *     NUESTATS:RADIO,Cell ID:137262770
 *     NUESTATS:RADIO,SNR:226
 *     NUESTATS:RADIO,RSRQ:-108
 *
 * Signal power, SNR, RSRQ는 centibel(0.1dBm/0.1dB) 단위로 반환됨.
 * ------------------------------------------------------------ */
static const char *App_Bc95AtFindField(const char *p_resp, const char *p_key)
{
    const char *p_scan = p_resp;
    const char *p_token;
    size_t      keyLen;

    if ((p_resp == NULL) || (p_key == NULL))
    {
        return NULL;
    }
    keyLen = strlen(p_key);

    while (*p_scan != '\0')
    {
        p_token = strstr(p_scan, p_key);
        if (p_token == NULL)
        {
            return NULL;
        }

        /* 키 바로 뒤가 ':' 이어야 정확한 필드명으로 인정 */
        if (p_token[keyLen] == ':')
        {
            return (p_token + keyLen + 1u);
        }
        p_scan = p_token + keyLen;
    }
    return NULL;
}

AppBc95AtStatus_t App_Bc95AtParseNuestatsRadio(const char *p_resp,
                                               int32_t *p_signalPowerCb,
                                               uint32_t *p_cellId,
                                               int32_t *p_snrCb,
                                               int32_t *p_rsrqCb)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_field;
    int parsedInt;
    unsigned long parsedUlong;

    if ((p_resp == NULL) || (p_signalPowerCb == NULL) || (p_cellId == NULL) ||
        (p_snrCb == NULL) || (p_rsrqCb == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    *p_signalPowerCb = 0;
    *p_cellId        = 0u;
    *p_snrCb         = 0;
    *p_rsrqCb        = 0;

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_field = App_Bc95AtFindField(p_resp, "Signal power");
    if (p_field == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    if (sscanf(p_field, "%d", &parsedInt) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_signalPowerCb = (int32_t)parsedInt;

    p_field = App_Bc95AtFindField(p_resp, "Cell ID");
    if (p_field == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    if (sscanf(p_field, "%lu", &parsedUlong) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_cellId = (uint32_t)parsedUlong;

    p_field = App_Bc95AtFindField(p_resp, "SNR");
    if (p_field == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    if (sscanf(p_field, "%d", &parsedInt) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_snrCb = (int32_t)parsedInt;

    p_field = App_Bc95AtFindField(p_resp, "RSRQ");
    if (p_field == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    if (sscanf(p_field, "%d", &parsedInt) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_rsrqCb = (int32_t)parsedInt;

    return APP_BC95_AT_OK;
}

/* ------------------------------------------------------------
 * CSQ rssi(0~31, 99) -> dBm 변환
 *   0  -> -113dBm
 *   1  -> -111dBm
 *   2~30 -> -109 ~ -53dBm (2dBm 간격)
 *   31 -> -51dBm 이상
 *   99 -> unknown (-150 처리)
 * ------------------------------------------------------------ */
static int16_t App_Bc95AtCsqRssiToDbm(int32_t rssiRaw)
{
    int16_t dbm;

    if (rssiRaw == APP_BC95_AT_CSQ_RSSI_UNKNOWN)
    {
        dbm = (int16_t)(-APP_BC95_AT_DBM_ABS_MAX);
    }
    else if (rssiRaw <= 0)
    {
        dbm = (int16_t)(-113);
    }
    else if (rssiRaw >= 31)
    {
        dbm = (int16_t)(-51);
    }
    else
    {
        dbm = (int16_t)(-113 + (int32_t)(rssiRaw * 2));
    }
    return dbm;
}

/* ------------------------------------------------------------
 * dBm 값을 0~150 양의 절댓값으로 클램핑 (스펙: -0~-150 -> 0~150)
 * ------------------------------------------------------------ */
static uint8_t App_Bc95AtDbmToAbsByte(int16_t dbm)
{
    int32_t absVal;

    if (dbm >= 0)
    {
        absVal = 0;
    }
    else
    {
        absVal = (int32_t)(-dbm);
    }

    if (absVal > APP_BC95_AT_DBM_ABS_MAX)
    {
        absVal = APP_BC95_AT_DBM_ABS_MAX;
    }
    return (uint8_t)absVal;
}

static uint16_t App_Bc95AtDbmToAbsU16(int16_t dbm)
{
    int32_t absVal;

    if (dbm >= 0)
    {
        absVal = 0;
    }
    else
    {
        absVal = (int32_t)(-dbm);
    }

    if (absVal > APP_BC95_AT_DBM_ABS_MAX)
    {
        absVal = APP_BC95_AT_DBM_ABS_MAX;
    }
    return (uint16_t)absVal;
}

/* ------------------------------------------------------------
 * NB-IoT 무선 품질 구조체 채우기
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;
    int32_t rssiRaw;
    int32_t berRaw;
    int32_t signalPowerCb;
    uint32_t cellId;
    int32_t snrCb;
    int32_t rsrqCb;

    APP_RETURN_IF_FALSE((p_quality != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_quality, 0, sizeof(*p_quality));

    /* 1) AT+CSQ */
    rxLen = 0u;
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CSQ,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    atStatus = App_Bc95AtParseCsq((const char *)g_appBc95AtRxBuf, &rssiRaw, &berRaw);
    if (atStatus != APP_BC95_AT_OK)
    {
        return APP_STATUS_FATAL;
    }

    p_quality->rssiDbm = App_Bc95AtCsqRssiToDbm(rssiRaw);
    p_quality->ber = (berRaw == APP_BC95_AT_CSQ_BER_UNKNOWN) ? 0u : (uint8_t)berRaw;

    /* 2) AT+NUESTATS=RADIO */
    rxLen = 0u;
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NUESTATS,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    atStatus = App_Bc95AtParseNuestatsRadio((const char *)g_appBc95AtRxBuf,
                                            &signalPowerCb,
                                            &cellId,
                                            &snrCb,
                                            &rsrqCb);
    if (atStatus != APP_BC95_AT_OK)
    {
        return APP_STATUS_FATAL;
    }

    /* centibels -> dBm/dB 변환 (값/10, 반올림) */
    p_quality->rsrpDbm = (int16_t)((signalPowerCb >= 0) ? ((signalPowerCb + 5) / 10) : ((signalPowerCb - 5) / 10));
    p_quality->rsrqDbm = (int16_t)((rsrqCb        >= 0) ? ((rsrqCb        + 5) / 10) : ((rsrqCb        - 5) / 10));
    p_quality->snrDb   = (int16_t)((snrCb         >= 0) ? ((snrCb         + 5) / 10) : ((snrCb         - 5) / 10));
    p_quality->cellId  = cellId;
    p_quality->valid   = APP_TRUE;

    (void)memcpy(&g_appBc95AtQuality, p_quality, sizeof(g_appBc95AtQuality));
    return APP_STATUS_OK;
}

/* ------------------------------------------------------------
 * 10바이트 패킷 형태로 직렬화
 *   [0]     RSSI  (1B, |dBm| 0~150)
 *   [1]     BER   (1B)
 *   [2..3]  CID   (2B, big endian)
 *   [4..5]  RSRP  (2B, |dBm| 0~150, big endian)
 *   [6..7]  RSRQ  (2B, |dBm| 0~150, big endian)
 *   [8..9]  SNR   (2B, big endian, 부호 보존)
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtFetchQualityBytes(uint8_t *p_buf, uint32_t bufSize)
{
    AppStatus_t status;
    AppBc95Quality_t quality;
    uint16_t cellIdU16;
    uint16_t rsrpAbs;
    uint16_t rsrqAbs;
    int16_t  snrVal;

    APP_RETURN_IF_FALSE((p_buf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufSize >= (uint32_t)APP_BC95_QUALITY_BYTES), APP_STATUS_INVALID_PARAM);

    status = App_Bc95AtFetchQuality(&quality);
    if (status != APP_STATUS_OK)
    {
        (void)memset(p_buf, 0, APP_BC95_QUALITY_BYTES);
        return status;
    }

    (void)memset(p_buf, 0, APP_BC95_QUALITY_BYTES);

    /* [0] RSSI */
    p_buf[0] = App_Bc95AtDbmToAbsByte(quality.rssiDbm);

    /* [1] BER (reserved) */
    p_buf[1] = quality.ber;

    /* [2..3] CID : 32비트 cell id를 하위 16비트로 축약 (스펙: 2B) */
    cellIdU16 = (uint16_t)(quality.cellId & 0xFFFFu);
    p_buf[2] = (uint8_t)((cellIdU16 >> 8u) & 0xFFu);
    p_buf[3] = (uint8_t)(cellIdU16 & 0xFFu);

    /* [4..5] RSRP : |dBm| 0~150 */
    rsrpAbs = App_Bc95AtDbmToAbsU16(quality.rsrpDbm);
    p_buf[4] = (uint8_t)((rsrpAbs >> 8u) & 0xFFu);
    p_buf[5] = (uint8_t)(rsrpAbs & 0xFFu);

    /* [6..7] RSRQ : |dBm| 0~150 */
    rsrqAbs = App_Bc95AtDbmToAbsU16(quality.rsrqDbm);
    p_buf[6] = (uint8_t)((rsrqAbs >> 8u) & 0xFFu);
    p_buf[7] = (uint8_t)(rsrqAbs & 0xFFu);

    /* [8..9] SNR : 부호 있는 dB 값 (2's complement, big endian) */
    snrVal = quality.snrDb;
    p_buf[8] = (uint8_t)(((uint16_t)snrVal >> 8u) & 0xFFu);
    p_buf[9] = (uint8_t)((uint16_t)snrVal & 0xFFu);

    return APP_STATUS_OK;
}

const AppBc95Quality_t *App_Bc95AtGetQuality(void)
{
    return &g_appBc95AtQuality;
}

/////////////////////////////////////////////////////////////////////////////////////////////////

#define APP_BC95_PRINT_TEMP_LEN     (64u)

static void App_Bc95PrintBcd(const char *p_label, const uint8_t *p_bcd, uint32_t length)
{
    char     tempChar[APP_BC95_PRINT_TEMP_LEN];
    uint32_t index;
    int      written;
    uint32_t offset;
    uint32_t remaining;

    if ((p_label == NULL) || (p_bcd == NULL) || (length == 0u))
    {
        return;
    }

    tempChar[0] = '\0';
    offset = 0u;

    for (index = 0u; index < length; index++)
    {
        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u))
        {
            break;
        }

        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        written = snprintf(&tempChar[offset],
                           remaining,
                           "%02X%s",
                           p_bcd[index],
                           (index < (length - 1u)) ? "-" : "");
        if (written <= 0)
        {
            break;
        }

        if ((uint32_t)written >= remaining)
        {
            /* 잘림 발생 -> 버퍼 끝까지 채워짐 */
            offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u;
            break;
        }

        offset += (uint32_t)written;
    }

    APP_LOGI("NBIOT", "%s BCD: %s", p_label, tempChar);
}

static void App_Bc95PrintDecoded(const char *p_label, const uint8_t *p_bcd, uint32_t length)
{
    char     tempChar[APP_BC95_PRINT_TEMP_LEN];
    uint32_t index;
    uint8_t  highNibble;
    uint8_t  lowNibble;
    int      written;
    uint32_t offset;
    uint32_t remaining;

    if ((p_label == NULL) || (p_bcd == NULL) || (length == 0u))
    {
        return;
    }

    tempChar[0] = '\0';
    offset = 0u;

    for (index = 0u; index < length; index++)
    {
        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u))
        {
            break;
        }

        highNibble = (uint8_t)((p_bcd[index] >> 4u) & 0x0Fu);
        lowNibble  = (uint8_t)(p_bcd[index] & 0x0Fu);

        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;

        if (highNibble <= 9u)
        {
            written = snprintf(&tempChar[offset],
                               remaining,
                               "%u",
                               (unsigned int)highNibble);
        }
        else if (highNibble != 0x0Fu)
        {
            written = snprintf(&tempChar[offset],
                               remaining,
                               "?");
        }
        else
        {
            /* 0xF 패딩 nibble은 출력 생략 */
            written = 0;
        }

        if (written < 0)
        {
            break;
        }
        if ((uint32_t)written >= remaining)
        {
            offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u;
            break;
        }
        offset += (uint32_t)written;

        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u))
        {
            break;
        }

        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;

        if (lowNibble <= 9u)
        {
            written = snprintf(&tempChar[offset],
                               remaining,
                               "%u",
                               (unsigned int)lowNibble);
        }
        else if (lowNibble != 0x0Fu)
        {
            written = snprintf(&tempChar[offset],
                               remaining,
                               "?");
        }
        else
        {
            /* 0xF 패딩 nibble은 출력 생략 */
            written = 0;
        }

        if (written < 0)
        {
            break;
        }
        if ((uint32_t)written >= remaining)
        {
            offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u;
            break;
        }
        offset += (uint32_t)written;
    }

    APP_LOGI("NBIOT", "%s     : %s", p_label, tempChar);
}

AppStatus_t App_NBIoTAtInit(void)
{
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(&g_appBc95AtRxContext, 0, sizeof(g_appBc95AtRxContext));
    (void)memset(g_appBc95AtImeiBcd, 0, sizeof(g_appBc95AtImeiBcd));
    (void)memset(g_appBc95AtImsiBcd, 0, sizeof(g_appBc95AtImsiBcd));
    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));

    g_appBc95AtInitialized = APP_TRUE;
    return APP_STATUS_OK;
}

static void App_NBIoTPrintQuality(const AppBc95Quality_t *p_q)
{
    if (p_q == NULL)
    {
        return;
    }

    APP_LOGI("NBIOT", "RSSI : %d dBm", (int)p_q->rssiDbm);
    APP_LOGI("NBIOT", "BER  : %u",      (unsigned)p_q->ber);
    APP_LOGI("NBIOT", "CID  : %lu (0x%08lX)",
             (unsigned long)p_q->cellId, (unsigned long)p_q->cellId);
    APP_LOGI("NBIOT", "RSRP : %d dBm", (int)p_q->rsrpDbm);
    APP_LOGI("NBIOT", "RSRQ : %d dBm", (int)p_q->rsrqDbm);
    APP_LOGI("NBIOT", "SNR  : %d dB",  (int)p_q->snrDb);
}

static void App_NBIoTPrintQualityBytes(const uint8_t *p_buf, uint32_t length)
{
    char     tempChar[64];
    uint32_t index;
    int      written;
    uint32_t offset;
    uint32_t remaining;

    if ((p_buf == NULL) || (length == 0u))
    {
        return;
    }

    tempChar[0] = '\0';
    offset = 0u;

    for (index = 0u; index < length; index++)
    {
        if (offset >= (sizeof(tempChar) - 1u))
        {
            break;
        }
        remaining = (uint32_t)sizeof(tempChar) - offset;
        written = snprintf(&tempChar[offset], remaining, "%02X%s",
                           p_buf[index],
                           (index < (length - 1u)) ? "-" : "");
        if (written <= 0)
        {
            break;
        }
        if ((uint32_t)written >= remaining)
        {
            offset = (uint32_t)sizeof(tempChar) - 1u;
            break;
        }
        offset += (uint32_t)written;
    }

    APP_LOGI("NBIOT", "Quality bytes(%lu): %s", (unsigned long)length, tempChar);
}


static uint8_t g_appImeiBcd[APP_BC95_IMEI_BCD_BYTES];
static uint8_t g_appImsiBcd[APP_BC95_IMSI_BCD_BYTES];

AppStatus_t App_NBIoTReadIdentity(void)
{
    AppStatus_t status;

    /* --- IMEI --- */
    status = App_Bc95AtFetchImei(g_appImeiBcd, sizeof(g_appImeiBcd));
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "IMEI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMEI", g_appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);
    App_Bc95PrintDecoded("IMEI", g_appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);

    /* --- IMSI --- */
    status = App_Bc95AtFetchImsi(g_appImsiBcd, sizeof(g_appImsiBcd));
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "IMSI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMSI", g_appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);
    App_Bc95PrintDecoded("IMSI", g_appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);

    return APP_STATUS_OK;
}

/*
    AT+CSQ → +CSQ:28,99
    
    RSSI raw = 28 → -113 + 28×2 = -57 dBm → 절댓값 57 = 0x39
    BER = 99 (unknown) → 0x00
    
    AT+NUESTATS → 라인 단위 응답
        Signal power:-619
        Total power:-553
        TX power:-120
        TX time:686
        RX time:35391
        Cell ID:9438465
        ECL:0
        SNR:111
        EARFCN:2554
        PCI:74
        RSRQ:-106
        OPERATOR MODE:3
        CURRENT BAND:5
    Signal power (RSRP): -619 centibels → -61.9 dBm → 반올림 -62 dBm → 절댓값 62 = 0x003E
    Cell ID: 9438465 (0x0090_0801) → 하위 16비트 0x0801
    SNR: 111 centibels → 11.1 dB → 반올림 11 dB → 0x000B
    RSRQ: -106 centibels → -10.6 dB → 반올림 -11 dB → 절댓값 11 = 0x000B
*/
AppStatus_t App_NBIoTReadQuality(void)
{
    AppStatus_t      status;
    AppBc95Quality_t quality;
    uint8_t          qualityBytes[APP_BC95_QUALITY_BYTES];

    /* --- Quality --- */
    status = App_Bc95AtFetchQuality(&quality);
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "Quality fetch failed (status=%d)", (int)status);
        return status;
    }
    App_NBIoTPrintQuality(&quality);

    status = App_Bc95AtFetchQualityBytes(qualityBytes, sizeof(qualityBytes));
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "Quality bytes fetch failed (status=%d)", (int)status);
        return status;
    }
    App_NBIoTPrintQualityBytes(qualityBytes, (uint32_t)APP_BC95_QUALITY_BYTES);

    return APP_STATUS_OK;
}

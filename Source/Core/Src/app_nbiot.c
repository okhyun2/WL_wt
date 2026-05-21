#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_build_config.h"

#include "main.h"
#include "app_nbiot.h"
#include "app_hw.h"
#include "app_log.h"

#define MY_SERVER_DOMAIN        "acorp2.iptime.org"
#define WARMUPDNS_SERVER_DOMAIN "www.google.com"

#define APP_BC95_AT_CMD_PING             "AT\r\n"
#define APP_BC95_AT_CMD_IMEI                 "AT+CGSN=1\r\n"
#define APP_BC95_AT_CMD_IMSI                 "AT+CIMI\r\n"
#define APP_BC95_AT_CMD_NUESTATS_CELL        "AT+NUESTATS=CELL\r\n"

#define APP_BC95_AT_CGSN_PREFIX_LEN          (6u)
#define APP_BC95_AT_CME_ERROR_PREFIX         "+CME ERROR:"
#define APP_BC95_AT_CME_ERROR_PREFIX_LEN     (11u)
#define APP_BC95_AT_ERROR_TOKEN              "ERROR"
#define APP_BC95_AT_ERROR_TOKEN_LEN          (5u)
#define APP_BC95_AT_OK_TOKEN                 "OK"
#define APP_BC95_AT_OK_TOKEN_LEN             (2u)
#define APP_BC95_AT_BCD_PAD_NIBBLE           (0x0Fu)

#define APP_BC95_AT_NUESTATS_CELL_PREFIX     "NUESTATS:CELL,"
#define APP_BC95_AT_NUESTATS_CELL_PREFIX_LEN (14u)
#define APP_BC95_AT_NUESTATS_CELL_FIELDS     (7u)
#define APP_BC95_AT_DBM_ABS_MAX              (150)

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
static uint8_t  g_appBc95AtRxBuf[APP_BC95_AT_RX_BUF_SIZE];

static uint8_t  g_appBc95AtInitialized;
static uint8_t  g_appBc95AtImeiBcd[APP_BC95_IMEI_BCD_BYTES];
static uint8_t  g_appBc95AtImsiBcd[APP_BC95_IMSI_BCD_BYTES];
static uint8_t  g_appBc95AtQualityBcd[APP_BC95_QUALITY_BCD_BYTES];
static AppBc95Quality_t g_appBc95AtQuality;


#ifdef DEBUG
typedef struct
{
    volatile uint32_t errorCode;     /* huart->ErrorCode 스냅샷 */
    volatile uint16_t errorAtIndex;  /* 에러 발생 시점의 수신 길이 */
    volatile uint8_t  errorCount;
} AppBc95AtRxErrorInfo_t;

static AppBc95AtRxErrorInfo_t g_appBc95AtRxErrorInfo;
#endif // DEBUG

static void App_Bc95AtDrainRxLine(UART_HandleTypeDef *p_huart, uint32_t drainMs)
{
    uint32_t startTick;
    uint8_t  dummy;

    /* 이전 IT 컨텍스트 강제 종료 */
    (void)HAL_UART_AbortReceive_IT(p_huart);

    /* HW 에러 플래그 클리어 */
    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);

    /* RDR 강제 플러시 */
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    /* drainMs 동안 들어오는 바이트를 폴링으로 흡수 */
    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < drainMs)
    {
        if (HAL_UART_Receive(p_huart, &dummy, 1u, 5u) != HAL_OK)
        {
            /* 더 이상 들어오는 데이터 없음 -> 빠져나감 */
            break;
        }
    }

    /* 최종 플래그/RDR 다시 클리어 */
    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
}


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

static AppBc95AtStatus_t App_Bc95AtImeiConvertDigitsToBcd(const char *p_digits, uint32_t digitCount, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    uint8_t nibble[APP_BC95_IMEI_BCD_BYTES*2];
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
    nibble[(APP_BC95_IMEI_BCD_BYTES*2) - 1u] = APP_BC95_AT_BCD_PAD_NIBBLE;

    for (pairIndex = 0u; pairIndex < (uint32_t)APP_BC95_IMEI_BCD_BYTES; pairIndex++)
    {
        p_bcdOut[pairIndex] = (uint8_t)((nibble[2u * pairIndex] << 4u) |
                                        (nibble[(2u * pairIndex) + 1u] & 0x0Fu));
    }

    return APP_BC95_AT_OK;
}


static AppBc95AtStatus_t App_Bc95AtImsiConvertDigitsToBcd(const char *p_digits, uint32_t digitCount, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    uint8_t nibble[APP_BC95_IMSI_BCD_BYTES*2];
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
    nibble[(APP_BC95_IMSI_BCD_BYTES*2) - 1u] = APP_BC95_AT_BCD_PAD_NIBBLE;

    for (pairIndex = 0u; pairIndex < (uint32_t)APP_BC95_IMSI_BCD_BYTES; pairIndex++)
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

/* ------------------------------------------------------------
 * App_Bc95AtPing()
 *   "AT\r\n" 한 번 송신하고 "OK"가 오는지 확인.
 *   - 성공 : APP_STATUS_OK
 *   - 응답 없음/잘림 : APP_STATUS_UART_TIMEOUT
 *   - ERROR / CME ERROR : APP_STATUS_FATAL
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtPing(uint32_t timeoutMs)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;
    int32_t cmeErr;

    rxLen = 0u;
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_PING,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   timeoutMs,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        return APP_STATUS_FATAL;
    }

    return APP_STATUS_OK;
}

/* ------------------------------------------------------------
 * App_Bc95AtWaitForBoot()
 *   부팅 직후 "Neul\r\nOK\r\n" 배너를 기다린다.
 *   콜드 부팅(전원 인가 직후)에서만 의미가 있으며,
 *   이미 부팅된 모듈에서는 타임아웃이 발생할 수 있다.
 *
 *   - APP_STATUS_OK            : "Neul" 배너 감지됨 (콜드 부팅 확인)
 *   - APP_STATUS_UART_TIMEOUT: 타임아웃 (배너 미감지)
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtWaitForBoot(uint32_t bannerTimeoutMs)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint16_t curLen;
    uint16_t lastCheckedLen = 0u;
    uint16_t bufferCap;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));
    bufferCap = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = bufferCap;
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.active         = APP_TRUE;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;

    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | 
                          UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE, &g_appBc95AtRxBuf[0], 1u);
    if (halStatus != HAL_OK)
    {
        g_appBc95AtRxContext.active = APP_FALSE;
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (1)
    {
        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_RX_FAILED;
        }

        curLen = g_appBc95AtRxContext.receivedLength;

        if (curLen != lastCheckedLen)
        {
            g_appBc95AtRxBuf[curLen] = (uint8_t)'\0';

            /* "Neul" 시그니처 감지 */
            if (strstr((const char *)g_appBc95AtRxBuf, APP_BC95_BOOT_BANNER) != NULL)
            {
                /* 배너를 받았으므로 "OK"까지 한 번 더 기다린다.
                 * 약간의 여유를 두고 응답 라인을 완성시킨다. */
                uint32_t completeWaitStart = HAL_GetTick();
                while ((HAL_GetTick() - completeWaitStart) < 500u)
                {
                    curLen = g_appBc95AtRxContext.receivedLength;
                    g_appBc95AtRxBuf[curLen] = (uint8_t)'\0';
                    if (App_Bc95AtFindOkBoundary((const char *)g_appBc95AtRxBuf) != NULL)
                    {
                        break;
                    }
                }

                (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
                g_appBc95AtRxContext.active = APP_FALSE;
                APP_LOGD("NBIOT", "Boot banner detected (len=%u)", (unsigned)curLen);
                return APP_STATUS_OK;
            }

            lastCheckedLen = curLen;
        }

        if (curLen >= bufferCap)
        {
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_TIMEOUT;
        }

        if ((HAL_GetTick() - startTick) >= bannerTimeoutMs)
        {
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF | 
                                  UART_CLEAR_NEF | UART_CLEAR_PEF);
            __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_TIMEOUT;
        }
    }
}

/* ------------------------------------------------------------
 * App_Bc95AtWaitUntilReady()
 *   BC95-G(V) 부팅 완료를 종합적으로 확인한다.
 *
 *   순서:
 *     1) "Neul" 배너 대기 (콜드 부팅 감지, 최대 BOOT_WAIT_BANNER_MS)
 *        - 받으면 즉시 다음 단계로
 *        - 타임아웃이어도 다음 단계로 (이미 부팅된 상태일 수 있음)
 *     2) "AT"를 일정 간격으로 송신하면서 "OK" 응답을 기다린다.
 *        - 최초의 OK 응답 시점에 부팅 완료로 판정.
 *
 *   @param totalTimeoutMs : 전체 작업 타임아웃 (ms)
 *   @return APP_STATUS_OK / APP_STATUS_UART_TIMEOUT
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtWaitUntilReady(uint32_t totalTimeoutMs)
{
    AppStatus_t status;
    uint32_t startTick;
    uint32_t elapsedMs;
    uint32_t bannerWaitMs;
    uint32_t retryCount;
    uint8_t  bannerSeen = APP_FALSE;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    startTick = HAL_GetTick();

    /* --- 1단계: Neul 배너 대기 --- */
    bannerWaitMs = (totalTimeoutMs > APP_BC95_BOOT_WAIT_BANNER_MS)
                   ? APP_BC95_BOOT_WAIT_BANNER_MS
                   : totalTimeoutMs;

    APP_LOGI("NBIOT", "Wait for boot banner up to %lums...", (unsigned long)bannerWaitMs);
    status = App_Bc95AtWaitForBoot(bannerWaitMs);
    if (status == APP_STATUS_OK)
    {
        bannerSeen = APP_TRUE;
        APP_LOGD("NBIOT", "Boot banner received");
    }
    else
    {
        APP_LOGE("NBIOT", "Boot banner not received, will probe with AT");
    }

    /* --- 2단계: AT 핑으로 활성 상태 확인 --- */
    retryCount = 0u;
    while (1)
    {
        elapsedMs = HAL_GetTick() - startTick;
        if (elapsedMs >= totalTimeoutMs)
        {
            APP_LOGE("NBIOT", "Ready check timeout (banner=%u, retry=%lu)",
                     (unsigned)bannerSeen, (unsigned long)retryCount);
            return APP_STATUS_UART_TIMEOUT;
        }

        status = App_Bc95AtPing(APP_BC95_BOOT_PING_TIMEOUT_MS);
        if (status == APP_STATUS_OK)
        {
            APP_LOGI("NBIOT", "Module ready (banner=%u, retry=%lu, elapsed=%lums)",
                     (unsigned)bannerSeen, (unsigned long)retryCount,
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }

        retryCount++;
        if (retryCount >= APP_BC95_BOOT_PING_MAX_RETRY)
        {
            APP_LOGE("NBIOT", "Ready check exhausted retries (last status=%d)",
                     (int)status);
            return APP_STATUS_UART_TIMEOUT;
        }

        HAL_Delay(APP_BC95_BOOT_PING_INTERVAL_MS);
    }
}

/* ============================================================
 *  App_Bc95AtWaitForUsim()
 *    USIM이 준비될 때까지 AT+CIMI를 주기적으로 시도한다.
 *    매뉴얼 2.15절: "IMSI may not be displayed for a few
 *                   seconds after power-on."
 *
 *    종료 조건:
 *      - IMSI 추출 성공 (15자리 숫자 라인 도착) -> OK
 *      - 전체 타임아웃 -> TIMEOUT
 *      - 회복 불가능한 CME ERROR (USIM 미삽입 등) -> FATAL
 *
 *    회복 가능한 케이스 (재시도):
 *      - 빈 응답 (no_prefix)
 *      - 자릿수 부족 (range)
 *      - CME ERROR 14 (SIM busy)
 *      - UART 타임아웃 / UART RX 실패
 *
 *    회복 불가능한 케이스 (즉시 실패):
 *      - CME ERROR 10 (SIM not inserted)
 *      - CME ERROR 13 (SIM failure)
 *      - CME ERROR 311/313/315 등 SIM 계열 영구 오류
 * ============================================================ */
AppStatus_t App_Bc95AtWaitForUsim(uint32_t timeoutMs)
{
    AppStatus_t st;
    AppBc95AtStatus_t atSt;
    uint32_t startTick;
    uint32_t elapsed;
    uint32_t attempt = 0u;
    uint16_t rxLen;
    int32_t cmeErr;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    startTick = HAL_GetTick();
    APP_LOGI("NBIOT", "Wait for USIM ready (timeout=%lums)...",
             (unsigned long)timeoutMs);

    while (1)
    {
        attempt++;

        rxLen = 0u;
        st = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);

        if (st == APP_STATUS_OK)
        {
            atSt = App_Bc95AtExtractImsiString((const char *)g_appBc95AtRxBuf,
                                               imsiStr,
                                               sizeof(imsiStr));
            if (atSt == APP_BC95_AT_OK)
            {
                APP_LOGI("NBIOT", "USIM ready (attempt=%lu, elapsed=%lums)",
                         (unsigned long)attempt,
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }

            /* 회복 불가능한 USIM 에러 판별 */
            if (atSt == APP_BC95_AT_ERR_CME_ERROR)
            {
                cmeErr = 0;
                (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);

                /* CME 10 : SIM not inserted
                 * CME 13 : SIM failure
                 * CME 15 : SIM wrong
                 * CME 16 : SIM PUK required
                 * CME 311/313/315/317/318 등 USIM 계열 영구 오류 */
                if ((cmeErr == 10) || (cmeErr == 13) || (cmeErr == 15) ||
                    (cmeErr == 16) || (cmeErr == 311) || (cmeErr == 313) ||
                    (cmeErr == 315) || (cmeErr == 317) || (cmeErr == 318))
                {
                    APP_LOGE("NBIOT", "USIM fatal error (CME=%ld)", (long)cmeErr);
                    return APP_STATUS_FATAL;
                }

                /* CME 14 (SIM busy) 등은 일시적 - 재시도 */
                APP_LOGE("NBIOT", "USIM not ready yet (CME=%ld, attempt=%lu)",
                         (long)cmeErr, (unsigned long)attempt);
            }
            else
            {
                /* no_prefix / range / format 등 - 응답이 비었거나 짧음 */
                APP_LOGD("NBIOT", "USIM not ready yet (parse=%s, attempt=%lu)",
                         App_Bc95AtGetStatusString(atSt),
                         (unsigned long)attempt);
            }
        }
        else
        {
            /* UART 일시 오류 / 타임아웃 */
            APP_LOGI("NBIOT", "USIM probe UART issue (status=%d, attempt=%lu)",
                     (int)st, (unsigned long)attempt);
        }

        /* 타임아웃 검사 */
        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= timeoutMs)
        {
            APP_LOGE("NBIOT", "USIM wait timeout (attempt=%lu, elapsed=%lums)",
                     (unsigned long)attempt, (unsigned long)elapsed);
            return APP_STATUS_UART_TIMEOUT;
        }

        HAL_Delay(APP_BC95_USIM_READY_POLL_MS);
    }
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

    return App_Bc95AtImeiConvertDigitsToBcd(p_imeiStr, (uint32_t)APP_BC95_IMEI_DIGITS, p_bcdOut, bcdSize);
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

    return App_Bc95AtImsiConvertDigitsToBcd(p_imsiStr, (uint32_t)APP_BC95_IMSI_DIGITS, p_bcdOut, bcdSize);
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
static AppStatus_t App_Bc95AtUartReceiveResponse(UART_HandleTypeDef *p_huart,
                                                 uint8_t *p_buffer,
                                                 uint16_t bufferSize,
                                                 uint32_t timeoutMs,
                                                 uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint16_t curLen;
    uint16_t lastCheckedLen = 0u;

    APP_RETURN_IF_FALSE((p_huart != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buffer != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufferSize > 1u), APP_STATUS_INVALID_PARAM);

    (void)memset(p_buffer, 0, bufferSize);
    if (p_rxLengthOut != NULL)
    {
        *p_rxLengthOut = 0u;
    }

    g_appBc95AtRxContext.p_huart       = p_huart;
    g_appBc95AtRxContext.p_buffer      = p_buffer;
    g_appBc95AtRxContext.bufferSize    = (uint16_t)(bufferSize - 1u);
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.active        = APP_TRUE;
    g_appBc95AtRxContext.completed     = APP_FALSE;   /* 더 이상 사용 안 함 */
    g_appBc95AtRxContext.error         = APP_FALSE;

    __HAL_UART_CLEAR_FLAG(p_huart, 
        UART_CLEAR_OREF | UART_CLEAR_FEF | 
        UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(p_huart, &p_buffer[0], 1u);
    if (halStatus != HAL_OK)
    {
        g_appBc95AtRxContext.active = APP_FALSE;
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (1)
    {
        /* 1) 에러 발생 시 즉시 종료 */
        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            g_appBc95AtRxContext.active = APP_FALSE;
            if (p_rxLengthOut != NULL)
            {
                *p_rxLengthOut = g_appBc95AtRxContext.receivedLength;
            }
            return APP_STATUS_UART_RX_FAILED;
        }

        /* 2) 현재 수신 길이 스냅샷 */
        curLen = g_appBc95AtRxContext.receivedLength;

        /* 3) 새로 도착한 데이터가 있을 때만 종료 시퀀스 검사 */
        if (curLen != lastCheckedLen)
        {
            p_buffer[curLen] = (uint8_t)'\0';
            if (App_Bc95AtIsResponseTerminated(p_buffer, curLen) == APP_TRUE)
            {
                (void)HAL_UART_AbortReceive_IT(p_huart);
                g_appBc95AtRxContext.active = APP_FALSE;
                if (p_rxLengthOut != NULL)
                {
                    *p_rxLengthOut = curLen;
                }
                return APP_STATUS_OK;
            }
            lastCheckedLen = curLen;
        }

        /* 4) 버퍼 가득 */
        if (curLen >= g_appBc95AtRxContext.bufferSize)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            g_appBc95AtRxContext.active = APP_FALSE;
            p_buffer[curLen] = (uint8_t)'\0';
            if (p_rxLengthOut != NULL)
            {
                *p_rxLengthOut = curLen;
            }
            return APP_STATUS_OK;
        }

        /* 5) 타임아웃 */
        if ((HAL_GetTick() - startTick) >= timeoutMs)
        {
            (void)HAL_UART_AbortReceive_IT(p_huart);
            __HAL_UART_CLEAR_FLAG(p_huart, 
                UART_CLEAR_OREF | UART_CLEAR_FEF | 
                UART_CLEAR_NEF | UART_CLEAR_PEF);
            __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
            g_appBc95AtRxContext.active = APP_FALSE;
            p_buffer[curLen] = (uint8_t)'\0';
            if (p_rxLengthOut != NULL)
            {
                *p_rxLengthOut = curLen;
            }
            return (curLen > 0u) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
        }
    }
}

AppStatus_t App_Bc95AtSendCommand(const char *p_cmd, uint8_t *p_rxBuf, uint16_t rxBufSize,
                                  uint32_t rxTimeoutMs, uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    AppStatus_t status;
    uint16_t cmdLen;

    APP_WWDGFeed();
    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_cmd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_rxBuf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((rxBufSize > 1u), APP_STATUS_INVALID_PARAM);

    cmdLen = (uint16_t)strlen(p_cmd);
    APP_RETURN_IF_FALSE((cmdLen > 0u), APP_STATUS_INVALID_PARAM);

    /* 명령 송신 전 RX 라인 완전 비우기 (잔여 데이터 흡수) */
    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)p_cmd,
                                  cmdLen,
                                  APP_BC95_AT_TX_TIMEOUT_MS);
    APP_WWDGFeed();
    APP_RETURN_IF_FALSE((halStatus == HAL_OK), APP_STATUS_UART_TX_FAILED);

    status = App_Bc95AtUartReceiveResponse(APP_UART_NBIOT_HANDLE,
                                           p_rxBuf,
                                           rxBufSize,
                                           rxTimeoutMs,
                                           p_rxLengthOut);
    APP_WWDGFeed();
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

const uint8_t *App_Bc95AtGetImeiBcd(void)
{
    return g_appBc95AtImeiBcd;
}

const uint8_t *App_Bc95AtGetImsiBcd(void)
{
    return g_appBc95AtImsiBcd;
}

const AppBc95Quality_t *App_Bc95AtGetQuality(void)
{
    return &g_appBc95AtQuality;
}

const uint8_t *App_Bc95AtGetQualityBcd(void)
{
    return g_appBc95AtQualityBcd;
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
        case APP_BC95_AT_ERR_NO_DATA:   return "no_data";
        default:                        return "unknown";
    }
}

//////////////////////////////////////////////////////////////////////////
void App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
    HAL_StatusTypeDef halStatus;
    uint16_t nextIndex;

    if ((p_huart == NULL) || (g_appBc95AtRxContext.active != APP_TRUE))
    {
        return;
    }

    if (p_huart != g_appBc95AtRxContext.p_huart)
    {
        return;
    }

    /* 단순 카운트 증가만 한다. 종료 시퀀스 검사는 메인 루프에서 한다. */
    nextIndex = (uint16_t)(g_appBc95AtRxContext.receivedLength + 1u);
    g_appBc95AtRxContext.receivedLength = nextIndex;

    if (nextIndex >= g_appBc95AtRxContext.bufferSize)
    {
        /* 버퍼 가득 -> 더 이상 IT 등록하지 않음. 메인 루프가 처리. */
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
                          UART_CLEAR_OREF | UART_CLEAR_FEF | 
                          UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
    g_appBc95AtRxContext.error = APP_TRUE;
}

/* centibels -> dBm/dB 변환 (값/10, 반올림) */
static int16_t App_Bc95AtCentibelToDbm(int32_t centibel)
{
    int32_t result;

    if (centibel >= 0)
    {
        result = (centibel + 5) / 10;
    }
    else
    {
        result = (centibel - 5) / 10;
    }
    return (int16_t)result;
}

/* dBm 값을 |dBm| 0~150 양수로 클램핑 */
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

static uint8_t App_Bc95AtDbmToAbsByte(int16_t dbm)
{
    return (uint8_t)App_Bc95AtDbmToAbsU16(dbm);
}

/* ------------------------------------------------------------
 * AT+NUESTATS=CELL 응답 파싱
 *
 * 응답 예 (serving cell만):
 *   NUESTATS:CELL,2554,74,1,-619,-106,-619,111
 *
 * 응답 예 (이웃 셀 포함):
 *   NUESTATS:CELL,2554,74,1,-619,-106,-619,111
 *   NUESTATS:CELL,2554,75,0,-720,-130,-720,80
 *
 * primarycell=1 인 라인을 serving cell로 사용한다.
 * 1차 라인이 없으면 첫 번째 라인을 사용한다.
 * ------------------------------------------------------------ */
AppBc95AtStatus_t App_Bc95AtParseNuestatsCell(const char *p_resp, AppBc95Quality_t *p_quality)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_scan;
    const char *p_lineStart;
    const char *p_picked = NULL;
    int parsedFields;
    int earfcn;
    int pci;
    int primary;
    int rsrpCb;
    int rsrqCb;
    int rssiCb;
    int snrCb;

    if ((p_resp == NULL) || (p_quality == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    (void)memset(p_quality, 0, sizeof(*p_quality));

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    /* 모든 NUESTATS:CELL 라인을 스캔하면서, primarycell=1 우선 선택 */
    p_scan = p_resp;
    while ((p_lineStart = strstr(p_scan, APP_BC95_AT_NUESTATS_CELL_PREFIX)) != NULL)
    {
        const char *p_payload = p_lineStart + APP_BC95_AT_NUESTATS_CELL_PREFIX_LEN;

        parsedFields = sscanf(p_payload,
                              "%d,%d,%d,%d,%d,%d,%d",
                              &earfcn, &pci, &primary,
                              &rsrpCb, &rsrqCb, &rssiCb, &snrCb);

        if (parsedFields == (int)APP_BC95_AT_NUESTATS_CELL_FIELDS)
        {
            if (p_picked == NULL)
            {
                p_picked = p_payload;    /* 첫 라인 임시 저장 */
            }
            if (primary == 1)
            {
                p_picked = p_payload;    /* serving cell 발견 시 확정 */
                break;
            }
        }
        p_scan = p_payload;
    }

    if (p_picked == NULL)
    {
        /* OK는 받았지만 NUESTATS:CELL 라인이 0개
         *   -> 모듈이 셀에 캠프되어 있지 않음 (정상이지만 데이터 없음) */
        return APP_BC95_AT_ERR_NO_DATA;
    }

    parsedFields = sscanf(p_picked,
                          "%d,%d,%d,%d,%d,%d,%d",
                          &earfcn, &pci, &primary,
                          &rsrpCb, &rsrqCb, &rssiCb, &snrCb);
    if (parsedFields != (int)APP_BC95_AT_NUESTATS_CELL_FIELDS)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }

    p_quality->earfcn      = (uint16_t)((earfcn < 0) ? 0 : (earfcn & 0xFFFF));
    p_quality->pci         = (uint16_t)((pci    < 0) ? 0 : (pci    & 0xFFFF));
    p_quality->primaryCell = (uint8_t)((primary == 1) ? 1u : 0u);
    p_quality->rsrpDbm     = App_Bc95AtCentibelToDbm((int32_t)rsrpCb);
    p_quality->rsrqDbm     = App_Bc95AtCentibelToDbm((int32_t)rsrqCb);
    p_quality->rssiDbm     = App_Bc95AtCentibelToDbm((int32_t)rssiCb);
    p_quality->snrDb       = App_Bc95AtCentibelToDbm((int32_t)snrCb);
    p_quality->ber         = 0u;          /* reserved */
    p_quality->valid       = APP_TRUE;

    return APP_BC95_AT_OK;
}

/* ------------------------------------------------------------
 * 10바이트 패킷 직렬화
 *   [0]     RSSI   (|dBm| 0~150)
 *   [1]     BER    (reserved, 0)
 *   [2..3]  CID    (PCI, big endian)
 *   [4..5]  RSRP   (|dBm| 0~150, big endian)
 *   [6..7]  RSRQ   (|dB|  0~150, big endian)
 *   [8..9]  SNR    (signed dB, big endian)
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtQualityToBcd(AppBc95Quality_t *pQuality, uint8_t *p_buf, uint32_t bufSize)
{
    uint16_t rsrpAbs;
    uint16_t rsrqAbs;
    int16_t  snrVal;

    APP_RETURN_IF_FALSE((pQuality != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufSize >= (uint32_t)APP_BC95_QUALITY_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    (void)memset(p_buf, 0, APP_BC95_QUALITY_BCD_BYTES);

    p_buf[0] = App_Bc95AtDbmToAbsByte(pQuality->rssiDbm);
    p_buf[1] = pQuality->ber;

    p_buf[2] = (uint8_t)((pQuality->pci >> 8u) & 0xFFu);
    p_buf[3] = (uint8_t)(pQuality->pci & 0xFFu);

    rsrpAbs  = App_Bc95AtDbmToAbsU16(pQuality->rsrpDbm);
    p_buf[4] = (uint8_t)((rsrpAbs >> 8u) & 0xFFu);
    p_buf[5] = (uint8_t)(rsrpAbs & 0xFFu);

    rsrqAbs  = App_Bc95AtDbmToAbsU16(pQuality->rsrqDbm);
    p_buf[6] = (uint8_t)((rsrqAbs >> 8u) & 0xFFu);
    p_buf[7] = (uint8_t)(rsrqAbs & 0xFFu);

    snrVal   = pQuality->snrDb;
    p_buf[8] = (uint8_t)(((uint16_t)snrVal >> 8u) & 0xFFu);
    p_buf[9] = (uint8_t)((uint16_t)snrVal & 0xFFu);

    return APP_STATUS_OK;
}

static void App_NBIoTPrintQuality(const AppBc95Quality_t *p_q)
{
    if (p_q == NULL)
    {
        return;
    }

    APP_LOGI("NBIOT", "EARFCN: %u, PCI: %u, Primary: %u",
             (unsigned)p_q->earfcn, (unsigned)p_q->pci, (unsigned)p_q->primaryCell);
    APP_LOGI("NBIOT", "RSSI : %d dBm", (int)p_q->rssiDbm);
    APP_LOGI("NBIOT", "RSRP : %d dBm", (int)p_q->rsrpDbm);
    APP_LOGI("NBIOT", "RSRQ : %d dB",  (int)p_q->rsrqDbm);
    APP_LOGI("NBIOT", "SNR  : %d dB",  (int)p_q->snrDb);
    APP_LOGI("NBIOT", "BER  : %u (reserved)", (unsigned)p_q->ber);
}

/* ------------------------------------------------------------
 * NB-IoT 무선 품질 구조체 채우기 (AT+NUESTATS=CELL 1회 호출)
 * ------------------------------------------------------------ */
AppStatus_t App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf, uint32_t bcdBufSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;

    APP_RETURN_IF_FALSE((p_quality != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_quality, 0, sizeof(*p_quality));

    rxLen = 0u;
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NUESTATS_CELL,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "NUESTATS send fail (status=%d, rxLen=%u)",
                 (int)status, (unsigned)rxLen);
        APP_LOGE("NBIOT", "raw: [%s]", (const char *)g_appBc95AtRxBuf);
        return status;
    }

    atStatus = App_Bc95AtParseNuestatsCell((const char *)g_appBc95AtRxBuf, p_quality);
    if (atStatus == APP_BC95_AT_OK)
    {
        (void)memcpy(&g_appBc95AtQuality, p_quality, sizeof(g_appBc95AtQuality));

        status = App_Bc95AtQualityToBcd(p_quality, p_bcdBuf, bcdBufSize);
        if (status != APP_STATUS_OK)
        {
            APP_LOGE("NBIOT", "error! quality to bcd");
            return APP_STATUS_FATAL;
        }
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_NO_DATA)
    {
        /* 셀 정보 없음 - 일시적 상태, 호출자가 재시도 결정 */
        APP_LOGI("NBIOT", "NUESTATS: no cell info yet (not camped)");
        return APP_STATUS_UART_TIMEOUT;     /* 일시 실패로 매핑 */
    }

    APP_LOGE("NBIOT", "NUESTATS parse fail (status=%d, rxLen=%u)",
             (int)atStatus, (unsigned)rxLen);
    APP_LOGE("NBIOT", "raw: [%s]", (const char *)g_appBc95AtRxBuf);
    return APP_STATUS_FATAL;
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define APP_BC95_AT_CMD_CFUN_QUERY          "AT+CFUN?\r\n"
#define APP_BC95_AT_CMD_CFUN_SET_FULL       "AT+CFUN=1\r\n"
#define APP_BC95_AT_CMD_CGATT_QUERY         "AT+CGATT?\r\n"
#define APP_BC95_AT_CMD_CEREG_QUERY         "AT+CEREG?\r\n"
#define APP_BC95_AT_CMD_CGPADDR_QUERY       "AT+CGPADDR\r\n"

#define APP_BC95_AT_CFUN_PREFIX             "+CFUN:"
#define APP_BC95_AT_CFUN_PREFIX_LEN         (6u)
#define APP_BC95_AT_CGATT_PREFIX            "+CGATT:"
#define APP_BC95_AT_CGATT_PREFIX_LEN        (7u)
#define APP_BC95_AT_CEREG_PREFIX            "+CEREG:"
#define APP_BC95_AT_CEREG_PREFIX_LEN        (7u)
#define APP_BC95_AT_CGPADDR_PREFIX          "+CGPADDR:"
#define APP_BC95_AT_CGPADDR_PREFIX_LEN      (9u)

static AppBc95NetStatus_t g_appBc95AtNetStatus;

/* ============================================================
 *  응답 파서 4종
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtParseCfun(const char *p_resp, int32_t *p_funOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedFun;

    if ((p_resp == NULL) || (p_funOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    *p_funOut = 0;

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_payload = strstr(p_resp, APP_BC95_AT_CFUN_PREFIX);
    if (p_payload == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_payload += APP_BC95_AT_CFUN_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t'))
    {
        p_payload++;
    }

    if (sscanf(p_payload, "%d", &parsedFun) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_funOut = (int32_t)parsedFun;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCgatt(const char *p_resp, int32_t *p_stateOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedState;

    if ((p_resp == NULL) || (p_stateOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    *p_stateOut = 0;

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_payload = strstr(p_resp, APP_BC95_AT_CGATT_PREFIX);
    if (p_payload == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_payload += APP_BC95_AT_CGATT_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t'))
    {
        p_payload++;
    }

    if (sscanf(p_payload, "%d", &parsedState) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_stateOut = (int32_t)parsedState;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCereg(const char *p_resp, int32_t *p_nOut, int32_t *p_statOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedN;
    int parsedStat;

    if ((p_resp == NULL) || (p_statOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    if (p_nOut != NULL)
    {
        *p_nOut = 0;
    }
    *p_statOut = 0;

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_payload = strstr(p_resp, APP_BC95_AT_CEREG_PREFIX);
    if (p_payload == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_payload += APP_BC95_AT_CEREG_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t'))
    {
        p_payload++;
    }

    if (sscanf(p_payload, "%d,%d", &parsedN, &parsedStat) != 2)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    if (p_nOut != NULL)
    {
        *p_nOut = (int32_t)parsedN;
    }
    *p_statOut = (int32_t)parsedStat;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCgpaddr(const char *p_resp, char *p_ipOut, uint32_t ipBufSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    const char *p_comma;
    uint32_t i;

    if ((p_resp == NULL) || (p_ipOut == NULL) || (ipBufSize == 0u))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    p_ipOut[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK)
    {
        return status;
    }

    p_payload = strstr(p_resp, APP_BC95_AT_CGPADDR_PREFIX);
    if (p_payload == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_payload += APP_BC95_AT_CGPADDR_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t'))
    {
        p_payload++;
    }

    /* "+CGPADDR:0" 까지만 있으면 IP 미할당 상태 (정상) */
    p_comma = strchr(p_payload, ',');
    if (p_comma == NULL)
    {
        return APP_BC95_AT_OK;
    }

    p_comma++;
    while ((*p_comma == ' ') || (*p_comma == '\t') || (*p_comma == '"'))
    {
        p_comma++;
    }

    i = 0u;
    while ((i < (ipBufSize - 1u)) && (*p_comma != '\0') &&
           (*p_comma != '\r') && (*p_comma != '\n') &&
           (*p_comma != '"')  && (*p_comma != ','))
    {
        p_ipOut[i++] = *p_comma++;
    }
    p_ipOut[i] = '\0';

    return APP_BC95_AT_OK;
}

/* ============================================================
 *  단일 명령 송신 + 자동 재시도 (UART 일시 오류 회복)
 * ============================================================ */
static AppStatus_t App_Bc95AtSendWithRetry(const char *p_cmd,
                                           uint8_t *p_rxBuf,
                                           uint16_t rxBufSize,
                                           uint32_t rxTimeoutMs,
                                           uint16_t *p_rxLenOut,
                                           uint32_t maxRetry)
{
    AppStatus_t status = APP_STATUS_FATAL;
    uint32_t attempt;

    for (attempt = 0u; attempt < maxRetry; attempt++)
    {
        status = App_Bc95AtSendCommand(p_cmd, p_rxBuf, rxBufSize, rxTimeoutMs, p_rxLenOut);
        if (status == APP_STATUS_OK)
        {
            return APP_STATUS_OK;
        }

        if ((status == APP_STATUS_UART_RX_FAILED) ||
            (status == APP_STATUS_UART_TX_FAILED) ||
            (status == APP_STATUS_UART_TIMEOUT))
        {
            APP_LOGI("NBIOT", "cmd retry %lu/%lu (status=%d)",
                     (unsigned long)(attempt + 1u),
                     (unsigned long)maxRetry,
                     (int)status);
            HAL_Delay(APP_BC95_NET_CMD_RETRY_DELAY_MS);
            continue;
        }
        break;
    }
    return status;
}

/* ============================================================
 *  AT+CFUN=1 (UE를 full functionality로)
 * ============================================================ */
AppStatus_t App_Bc95AtSetFullFunction(void)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    APP_LOGI("NBIOT", "Set CFUN=1 (may take up to 85s)...");
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CFUN_SET_FULL,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_CFUN_RESP_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "CFUN=1 send fail (status=%d)", (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "CFUN=1 response not OK (parse=%s)",
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGI("NBIOT", "CFUN=1 done");
    return APP_STATUS_OK;
}

/* ============================================================
 *  CME ERROR 코드가 USIM 영구 오류인지 판단
 * ============================================================ */
static uint8_t App_Bc95AtIsUsimFatalCme(int32_t cmeErr)
{
    switch (cmeErr)
    {
        case 10:        /* SIM not inserted */
        case 13:        /* SIM failure */
        case 15:        /* SIM wrong */
        case 16:        /* SIM PUK required */
        case 311:       /* SIM PIN required */
        case 313:       /* SIM failure */
        case 315:       /* SIM wrong */
        case 317:       /* SIM PIN2 required */
        case 318:       /* SIM PUK2 required */
            return APP_TRUE;
        default:
            return APP_FALSE;
    }
}

/* ============================================================
 *  현재 네트워크 상태 1회 조회 (스냅샷)
 *  각 하위 명령은 개별로 실패해도 나머지를 시도한다.
 * ============================================================ */
AppStatus_t App_Bc95AtQueryNetStatus(AppBc95NetStatus_t *p_status)
{
    AppStatus_t st;
    AppBc95AtStatus_t atSt;
    uint16_t rxLen;
    int32_t funVal      = -1;
    int32_t cgattVal    = -1;
    int32_t ceregN      = -1;
    int32_t ceregStat   = -1;
    int32_t cmeErr      = 0;

    APP_RETURN_IF_FALSE((p_status != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(p_status, 0, sizeof(*p_status));
    p_status->phase      = APP_BC95_NET_PHASE_INIT;
    p_status->cfunValue  = 0xFFu;
    p_status->cgattState = 0xFFu;
    p_status->ceregStat  = APP_BC95_CEREG_NOT_REGISTERED;

    /* --- AT+CFUN? --- */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CFUN_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS,
                                 &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCfun((const char *)g_appBc95AtRxBuf, &funVal);
        if (atSt == APP_BC95_AT_OK)
        {
            p_status->cfunValue = (uint8_t)funVal;
        }
        else if (atSt == APP_BC95_AT_ERR_CME_ERROR)
        {
            cmeErr = 0;
            (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
            if (App_Bc95AtIsUsimFatalCme(cmeErr) == APP_TRUE)
            {
                APP_LOGE("NBIOT", "CFUN? USIM fatal (CME=%ld)", (long)cmeErr);
                p_status->phase          = APP_BC95_NET_PHASE_USIM_ERROR;
                p_status->lastUpdateTick = HAL_GetTick();
                return APP_STATUS_FATAL;
            }
        }
    }

    /* CFUN이 1이 아니면 더 진행해봐야 의미 없음 */
    if ((funVal >= 0) && (funVal != 1))
    {
        p_status->phase          = APP_BC95_NET_PHASE_CFUN_OFF;
        p_status->lastUpdateTick = HAL_GetTick();
        return APP_STATUS_OK;
    }

    /* --- AT+CEREG? --- */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CEREG_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS,
                                 &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCereg((const char *)g_appBc95AtRxBuf, &ceregN, &ceregStat);
        if (atSt == APP_BC95_AT_OK)
        {
            p_status->ceregStat = (AppBc95CeregStat_t)ceregStat;
        }
    }

    /* --- AT+CGATT? --- */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CGATT_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS,
                                 &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCgatt((const char *)g_appBc95AtRxBuf, &cgattVal);
        if (atSt == APP_BC95_AT_OK)
        {
            p_status->cgattState = (uint8_t)cgattVal;
        }
    }

    /* --- AT+CGPADDR (등록되었을 때만 의미있음) --- */
    if ((p_status->ceregStat == APP_BC95_CEREG_REGISTERED_HOME) ||
        (p_status->ceregStat == APP_BC95_CEREG_REGISTERED_ROAM))
    {
        rxLen = 0u;
        st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CGPADDR_QUERY,
                                     g_appBc95AtRxBuf,
                                     (uint16_t)sizeof(g_appBc95AtRxBuf),
                                     APP_BC95_AT_RX_TIMEOUT_MS,
                                     &rxLen,
                                     APP_BC95_NET_CMD_RETRY_MAX);
        if (st == APP_STATUS_OK)
        {
            atSt = App_Bc95AtParseCgpaddr((const char *)g_appBc95AtRxBuf,
                                          p_status->ipAddr,
                                          (uint32_t)sizeof(p_status->ipAddr));
            if ((atSt == APP_BC95_AT_OK) && (p_status->ipAddr[0] != '\0'))
            {
                p_status->hasIp = APP_TRUE;
            }
        }
    }

    /* --- phase 종합 판단 --- */
    if ((p_status->cfunValue != 0xFFu) && (p_status->cfunValue != 1u))
    {
        p_status->phase = APP_BC95_NET_PHASE_CFUN_OFF;
    }
    else if ((p_status->ceregStat == APP_BC95_CEREG_NOT_REGISTERED) ||
             (p_status->ceregStat == APP_BC95_CEREG_SEARCHING)      ||
             (p_status->ceregStat == APP_BC95_CEREG_UNKNOWN))
    {
        p_status->phase = APP_BC95_NET_PHASE_REGISTERING;
    }
    else if (p_status->ceregStat == APP_BC95_CEREG_DENIED)
    {
        p_status->phase = APP_BC95_NET_PHASE_DENIED;
    }
    else if ((p_status->ceregStat == APP_BC95_CEREG_REGISTERED_HOME) ||
             (p_status->ceregStat == APP_BC95_CEREG_REGISTERED_ROAM))
    {
        if (p_status->cgattState != 1u)
        {
            p_status->phase = APP_BC95_NET_PHASE_ATTACHING;
        }
        else if (p_status->hasIp != APP_TRUE)
        {
            p_status->phase = APP_BC95_NET_PHASE_WAITING_IP;
        }
        else
        {
            p_status->phase = APP_BC95_NET_PHASE_READY;
            p_status->ready = APP_TRUE;
        }
    }
    else
    {
        p_status->phase = APP_BC95_NET_PHASE_REGISTERING;
    }

    p_status->lastUpdateTick = HAL_GetTick();
    (void)memcpy(&g_appBc95AtNetStatus, p_status, sizeof(g_appBc95AtNetStatus));
    return APP_STATUS_OK;
}

/* ============================================================
 *  네트워크 사용 준비 완료까지 대기
 *
 *  처리되는 케이스:
 *    - CFUN=0   -> 자동으로 AT+CFUN=1 발행 (1회)
 *    - CEREG=2/4 -> 폴링 지속
 *    - CEREG=3   -> 누적 N회까지 허용 후 실패
 *    - CGATT=0   -> 폴링 지속
 *    - IP 없음   -> 폴링 지속
 *    - USIM 영구오류 -> 즉시 실패
 *    - UART 일시오류 -> 폴링 단위 재시도
 *
 *  @param totalTimeoutMs 전체 작업 타임아웃
 *  @param p_status (NULL 가능) 마지막 스냅샷 반환
 *  @return APP_STATUS_OK         = 네트워크 준비 완료
 *          APP_STATUS_UART_TIMEOUT = 타임아웃
 *          APP_STATUS_FATAL      = USIM 또는 등록 거부 누적
 * ============================================================ */
AppStatus_t App_Bc95AtWaitForNetwork(uint32_t totalTimeoutMs, AppBc95NetStatus_t *p_status)
{
    AppStatus_t st;
    AppBc95NetStatus_t snapshot;
    uint32_t startTick;
    uint32_t elapsed;
    uint32_t pollCount    = 0u;
    uint32_t deniedCount  = 0u;
    uint8_t  cfunFixDone  = APP_FALSE;
    AppBc95NetPhase_t lastPhase = APP_BC95_NET_PHASE_INIT;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    if (p_status != NULL)
    {
        (void)memset(p_status, 0, sizeof(*p_status));
    }
    (void)memset(&snapshot, 0, sizeof(snapshot));

    startTick = HAL_GetTick();
    APP_LOGI("NBIOT", "Wait for network (timeout=%lums)...",
             (unsigned long)totalTimeoutMs);

    while (1)
    {
        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= totalTimeoutMs)
        {
            APP_LOGE("NBIOT", "Network wait timeout (poll=%lu, lastPhase=%s, elapsed=%lums)",
                     (unsigned long)pollCount,
                     App_Bc95AtGetNetPhaseString(lastPhase),
                     (unsigned long)elapsed);
            if (p_status != NULL)
            {
                (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            }
            return APP_STATUS_UART_TIMEOUT;
        }

        /* --- 상태 조회 --- */
        st = App_Bc95AtQueryNetStatus(&snapshot);
        snapshot.pollCount   = ++pollCount;
        snapshot.deniedCount = deniedCount;

        if (st == APP_STATUS_FATAL)
        {
            APP_LOGE("NBIOT", "Net query fatal (phase=%s)",
                     App_Bc95AtGetNetPhaseString(snapshot.phase));
            if (p_status != NULL)
            {
                (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            }
            return APP_STATUS_FATAL;
        }

        if (st != APP_STATUS_OK)
        {
            /* UART 일시 오류 -> 다음 폴링 */
            APP_LOGD("NBIOT", "poll #%lu: query failed (status=%d), retry next cycle",
                     (unsigned long)pollCount, (int)st);
            HAL_Delay(APP_BC95_NET_POLL_INTERVAL_MS);
            continue;
        }

        /* phase 변화 시에만 로그 (스팸 방지) */
        if (snapshot.phase != lastPhase)
        {
            APP_LOGD("NBIOT", "poll #%lu: phase=%s, CFUN=%u, CEREG=%s, CGATT=%u, IP='%s'",
                     (unsigned long)pollCount,
                     App_Bc95AtGetNetPhaseString(snapshot.phase),
                     (unsigned)snapshot.cfunValue,
                     App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                     (unsigned)snapshot.cgattState,
                     snapshot.ipAddr);
            lastPhase = snapshot.phase;
        }

        /* --- 종료 조건 --- */
        if (snapshot.phase == APP_BC95_NET_PHASE_READY)
        {
            APP_LOGI("NBIOT", "Network ready (elapsed=%lums, ip=%s, poll=%lu)",
                     (unsigned long)elapsed,
                     snapshot.ipAddr,
                     (unsigned long)pollCount);
            if (p_status != NULL)
            {
                (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            }
            return APP_STATUS_OK;
        }

        if (snapshot.phase == APP_BC95_NET_PHASE_USIM_ERROR)
        {
            APP_LOGE("NBIOT", "USIM permanent error, give up");
            if (p_status != NULL)
            {
                (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            }
            return APP_STATUS_FATAL;
        }

        /* --- 자동 복구 분기 --- */
        if (snapshot.phase == APP_BC95_NET_PHASE_CFUN_OFF)
        {
            if (cfunFixDone != APP_TRUE)
            {
                cfunFixDone = APP_TRUE;
                APP_LOGD("NBIOT", "CFUN=0 detected, sending AT+CFUN=1");
                (void)App_Bc95AtSetFullFunction();
                /* 다음 폴링에서 재확인 */
            }
        }
        else if (snapshot.phase == APP_BC95_NET_PHASE_DENIED)
        {
            deniedCount++;
            if (deniedCount >= APP_BC95_NET_DENIED_RETRY_MAX)
            {
                APP_LOGE("NBIOT", "Registration denied %lu times, give up",
                         (unsigned long)deniedCount);
                if (p_status != NULL)
                {
                    (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                }
                return APP_STATUS_FATAL;
            }
            APP_LOGD("NBIOT", "Registration denied (%lu/%lu), keep polling",
                     (unsigned long)deniedCount,
                     (unsigned long)APP_BC95_NET_DENIED_RETRY_MAX);
        }
        /* REGISTERING / ATTACHING / WAITING_IP 는 계속 폴링 */

        HAL_Delay(APP_BC95_NET_POLL_INTERVAL_MS);
    }
}

/* ============================================================
 *  문자열 변환 / 접근자
 * ============================================================ */
const char *App_Bc95AtGetCeregStatString(AppBc95CeregStat_t stat)
{
    switch (stat)
    {
        case APP_BC95_CEREG_NOT_REGISTERED:   return "not_registered";
        case APP_BC95_CEREG_REGISTERED_HOME:  return "home";
        case APP_BC95_CEREG_SEARCHING:        return "searching";
        case APP_BC95_CEREG_DENIED:           return "denied";
        case APP_BC95_CEREG_UNKNOWN:          return "unknown";
        case APP_BC95_CEREG_REGISTERED_ROAM:  return "roaming";
        default:                              return "invalid";
    }
}

const char *App_Bc95AtGetNetPhaseString(AppBc95NetPhase_t phase)
{
    switch (phase)
    {
        case APP_BC95_NET_PHASE_INIT:         return "init";
        case APP_BC95_NET_PHASE_CFUN_OFF:     return "cfun_off";
        case APP_BC95_NET_PHASE_USIM_ERROR:   return "usim_error";
        case APP_BC95_NET_PHASE_REGISTERING:  return "registering";
        case APP_BC95_NET_PHASE_ATTACHING:    return "attaching";
        case APP_BC95_NET_PHASE_WAITING_IP:   return "waiting_ip";
        case APP_BC95_NET_PHASE_READY:        return "ready";
        case APP_BC95_NET_PHASE_DENIED:       return "denied";
        default:                              return "invalid";
    }
}

const AppBc95NetStatus_t *App_Bc95AtGetLastNetStatus(void)
{
    return &g_appBc95AtNetStatus;
}

#define APP_BC95_AT_CMD_NSOCR_UDP_FMT        "AT+NSOCR=DGRAM,17,%u,1,AF_INET\r\n"
#define APP_BC95_AT_CMD_NSOST_FMT_PREFIX     "AT+NSOST="
#define APP_BC95_AT_CMD_NSOCL_FMT            "AT+NSOCL=%ld\r\n"
#define APP_BC95_AT_CMD_QDNS_FMT             "AT+QDNS=0,%s\r\n"

#define APP_BC95_AT_QDNS_PREFIX              "+QDNS:"
#define APP_BC95_AT_QDNS_PREFIX_LEN          (6u)
#define APP_BC95_AT_NSOSTR_PREFIX            "+NSOSTR:"
#define APP_BC95_AT_NSOSTR_PREFIX_LEN        (8u)

/* AT 명령 송신용 작업 버퍼 (1358바이트 + 헤더 여유) */
//#define APP_BC95_AT_CMD_TX_BUF_SIZE          (3072u)
#define APP_BC95_AT_CMD_TX_BUF_SIZE          (1400u)

/* NSOSTR URC 감시용 — 폴링 기반으로 비동기 응답을 수집 */
typedef struct
{
    volatile uint8_t  pendingSeq;        /* 0 = 대기 없음, 그 외 = 기대하는 seq */
    volatile int32_t  pendingSocket;
    volatile uint8_t  confirmedStatus;   /* 0=error, 1=sent */
    volatile uint8_t  confirmed;
} AppBc95UrcContext_t;

static AppBc95UrcContext_t g_appBc95Urc;

/* ============================================================
 *  바이트 -> 16진수 ASCII 변환 (NSOST payload 인코딩)
 * ============================================================ */
static void App_Bc95AtBytesToHex(const uint8_t *p_in, uint32_t inLen, char *p_out)
{
    static const char hexTab[] = "0123456789ABCDEF";
    uint32_t i;

    for (i = 0u; i < inLen; i++)
    {
        p_out[(2u * i)]      = hexTab[(p_in[i] >> 4u) & 0x0Fu];
        p_out[(2u * i) + 1u] = hexTab[p_in[i] & 0x0Fu];
    }
    p_out[2u * inLen] = '\0';
}

/* ============================================================
 *  AT+QDNS 응답 파싱
 *
 *  응답:
 *    "AT+QDNS=0,acorp2.iptime.org\r\nOK\r\n"
 *    이후 비동기로 도착:
 *    "\r\n+QDNS:111.13.100.91\r\n"  또는
 *    "\r\n+QDNS:FAIL\r\n"
 * ============================================================ */
static AppBc95AtStatus_t App_Bc95AtParseQdnsResult(const char *p_resp,
                                                   char *p_ipOut,
                                                   uint32_t ipBufSize)
{
    const char *p_prefix;
    const char *p_src;
    uint32_t i;

    if ((p_resp == NULL) || (p_ipOut == NULL) || (ipBufSize == 0u))
    {
        return APP_BC95_AT_ERR_PARAM;
    }
    p_ipOut[0] = '\0';

    p_prefix = strstr(p_resp, APP_BC95_AT_QDNS_PREFIX);
    if (p_prefix == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_src = p_prefix + APP_BC95_AT_QDNS_PREFIX_LEN;
    while ((*p_src == ' ') || (*p_src == '\t'))
    {
        p_src++;
    }

    /* "FAIL" 응답 */
    if (strncmp(p_src, "FAIL", 4u) == 0)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }

    /* IP 문자열 복사 */
    i = 0u;
    while ((i < (ipBufSize - 1u)) && (*p_src != '\0') &&
           (*p_src != '\r') && (*p_src != '\n') && (*p_src != ' '))
    {
        p_ipOut[i++] = *p_src++;
    }
    p_ipOut[i] = '\0';

    /* 최소 길이 검증 (예: "1.1.1.1" 7자) */
    if (i < 7u)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    return APP_BC95_AT_OK;
}

/**
 * @brief BC95 AT+QDNS 를 사용해 호스트 이름을 IP 주소로 해석한다.
 *
 * 동작 순서:
 *   1) 입력 파라미터 검사
 *   2) RX 라인 드레인(이전 응답 잔여 제거)
 *   3) "AT+QDNS=0,<host>\r\n" 송신 후 "OK" 응답 확인 (짧은 타임아웃)
 *   4) 별도 폴링 루프에서 비동기 URC ("+QDNS:<ip>" 또는 "+QDNS:FAIL") 대기
 *   5) URC 파싱 -> 결과 반환
 *
 * 반환값:
 *   APP_STATUS_OK              : 해석 성공, p_ipOut 채워짐
 *   APP_STATUS_UART_TIMEOUT: 일시적 실패 (FAIL/타임아웃) -> 상위에서 재시도
 *   APP_STATUS_FATAL           : 영구적 실패 (파라미터 오류, UART 송신 실패)
 *   APP_STATUS_UART_RX_FAILED  : UART HW 오류
 */
AppStatus_t App_Bc95AtResolveHost(const char *p_hostname,
                                  char       *p_ipOut,
                                  uint32_t    ipBufSize)
{
    AppStatus_t       status;
    HAL_StatusTypeDef halStatus;
    char              atCmd[96];
    int               cmdLen;
    uint32_t          rxLen;
    uint32_t          startTick;
    uint32_t          elapsed;
    const char       *p_urc;
    const char       *p_colon;
    const char       *p_end;
    uint32_t          ipLen;
    uint32_t          hostLen;

    /* ---------- 1) 파라미터 검증 ---------- */
    if ((p_hostname == NULL) || (p_ipOut == NULL) ||
        (ipBufSize < APP_BC95_IP_STR_SIZE))
    {
        APP_LOGE("NBIOT", "ResolveHost: invalid param");
        return APP_STATUS_FATAL;
    }

    hostLen = (uint32_t)strlen(p_hostname);
    if ((hostLen == 0U) || (hostLen > APP_BC95_HOSTNAME_MAX_LEN))
    {
        APP_LOGE("NBIOT", "ResolveHost: bad hostname len=%lu",
                 (unsigned long)hostLen);
        return APP_STATUS_FATAL;
    }

    (void)memset(p_ipOut, 0, ipBufSize);

    /* 명령 송신 전 RX 라인 완전 비우기 (잔여 데이터 흡수) */
    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30U);
    HAL_Delay(APP_BC95_DNS_INTERCMD_DELAY_MS);

    /* ---------- AT+QDNS 명령 송신 + "OK" 수신 ---------- */
    cmdLen = snprintf(atCmd, sizeof(atCmd),
                      "AT+QDNS=0,%s\r\n", p_hostname);
    if ((cmdLen <= 0) || (cmdLen >= (int)sizeof(atCmd)))
    {
        APP_LOGE("NBIOT", "ResolveHost: cmd build fail");
        return APP_STATUS_FATAL;
    }

    APP_LOGD("NBIOT", "DNS query: %s", p_hostname);

    /* RX context 초기화 후 IT 수신 시작 */
    (void)memset(g_appBc95AtRxBuf, 0, APP_BC95_AT_RX_BUF_SIZE);
    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.receivedLength = 0U;
    g_appBc95AtRxContext.active         = APP_TRUE;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;

    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE,
                                    &g_appBc95AtRxBuf[0], 1U);
    if (halStatus != HAL_OK)
    {
        APP_LOGE("NBIOT", "ResolveHost: RX_IT start fail (%d)", halStatus);
        g_appBc95AtRxContext.active = APP_FALSE;
        return APP_STATUS_UART_RX_FAILED;
    }

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)atCmd,
                                  (uint16_t)cmdLen,
                                  APP_BC95_DNS_CMD_TIMEOUT_MS);
    if (halStatus != HAL_OK)
    {
        APP_LOGE("NBIOT", "ResolveHost: TX fail (%d)", halStatus);
        (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
        g_appBc95AtRxContext.active = APP_FALSE;
        return APP_STATUS_UART_RX_FAILED;
    }

    /* ---------- "OK" 또는 "ERROR" 1차 응답 대기 ---------- */
    startTick = HAL_GetTick();
    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen < (APP_BC95_AT_RX_BUF_SIZE - 1U))
        {
            g_appBc95AtRxBuf[rxLen] = '\0';
        }

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            APP_LOGE("NBIOT", "ResolveHost: UART err (rxLen=%lu)",
                     (unsigned long)rxLen);
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_RX_FAILED;
        }

        /* "AT+QDNS" 명령은 즉시 "OK" 가 오고, 잠시 후 +QDNS URC 가 옴 */
        if (strstr((char *)g_appBc95AtRxBuf, "\r\nOK\r\n") != NULL)
        {
            break; /* OK 수신, 이제 URC 대기 */
        }
        if (strstr((char *)g_appBc95AtRxBuf, "\r\nERROR\r\n") != NULL)
        {
            APP_LOGE("NBIOT", "ResolveHost: AT+QDNS returned ERROR");
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_TIMEOUT; /* transient, 재시도 가능 */
        }
        if (strstr((char *)g_appBc95AtRxBuf, "+CME ERROR") != NULL)
        {
            APP_LOGE("NBIOT", "ResolveHost: +CME ERROR");
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_TIMEOUT;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= APP_BC95_DNS_CMD_TIMEOUT_MS)
        {
            APP_LOGE("NBIOT", "ResolveHost: OK timeout (rxLen=%lu)",
                     (unsigned long)rxLen);
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            return APP_STATUS_UART_TIMEOUT;
        }

        HAL_Delay(20U);
    }

    /* ---------- +QDNS URC 대기 ---------- */
    /* 같은 RX 버퍼에 이어서 수신되므로 IT 는 그대로 유지 */
    startTick = HAL_GetTick();
    status    = APP_STATUS_UART_TIMEOUT;

    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen < (APP_BC95_AT_RX_BUF_SIZE - 1U))
        {
            g_appBc95AtRxBuf[rxLen] = '\0';
        }

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            APP_LOGE("NBIOT", "ResolveHost: UART err during URC wait");
            status = APP_STATUS_UART_RX_FAILED;
            break;
        }

        /* +QDNS:FAIL 검사 우선 (FAIL 이 IP 패턴과 헷갈리지 않도록) */
        if (strstr((char *)g_appBc95AtRxBuf, "+QDNS:FAIL") != NULL)
        {
            APP_LOGI("NBIOT", "DNS FAIL for %s (transient, will retry)",
                     p_hostname);
            status = APP_STATUS_UART_TIMEOUT;
            break;
        }

        /* +QDNS:<ip> 검사 */
        p_urc = strstr((char *)g_appBc95AtRxBuf, "+QDNS:");
        if (p_urc != NULL)
        {
            p_colon = p_urc + 6; /* skip "+QDNS:" */

            /* IP 문자열 끝(\r 또는 \n) 찾기 */
            p_end = p_colon;
            while ((*p_end != '\0') && (*p_end != '\r') && (*p_end != '\n'))
            {
                p_end++;
            }

            ipLen = (uint32_t)(p_end - p_colon);

            /* 최소 IPv4 길이(7: "0.0.0.0") 보다 짧으면 아직 미완성 -> 계속 대기 */
            if (ipLen >= 7U)
            {
                if (ipLen < ipBufSize)
                {
                    (void)memcpy(p_ipOut, p_colon, ipLen);
                    p_ipOut[ipLen] = '\0';

                    /* 간단 검증: 숫자/도트만 있는지 */
                    {
                        uint32_t k;
                        uint8_t  ok = APP_TRUE;
                        for (k = 0U; k < ipLen; k++)
                        {
                            char c = p_ipOut[k];
                            if (((c < '0') || (c > '9')) && (c != '.'))
                            {
                                ok = APP_FALSE;
                                break;
                            }
                        }
                        if (ok == APP_TRUE)
                        {
                            APP_LOGD("NBIOT", "DNS OK: %s -> %s",
                                     p_hostname, p_ipOut);
                            status = APP_STATUS_OK;
                            break;
                        }
                        else
                        {
                            APP_LOGE("NBIOT", "DNS bad IP fmt: [%s]",
                                     p_ipOut);
                            (void)memset(p_ipOut, 0, ipBufSize);
                            status = APP_STATUS_UART_TIMEOUT;
                            break;
                        }
                    }
                }
                else
                {
                    APP_LOGE("NBIOT", "DNS IP too long (%lu)",
                             (unsigned long)ipLen);
                    status = APP_STATUS_FATAL;
                    break;
                }
            }
            /* ipLen<7: 아직 \r\n 까지 안 옴 -> 계속 폴링 */
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= APP_BC95_DNS_URC_TIMEOUT_MS)
        {
            APP_LOGE("NBIOT", "ResolveHost: URC timeout (rxLen=%lu)",
                     (unsigned long)rxLen);
            status = APP_STATUS_UART_TIMEOUT;
            break;
        }

        HAL_Delay(APP_BC95_DNS_URC_POLL_MS);
    }

    /* ---------- 정리 ---------- */
    (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
    g_appBc95AtRxContext.active = APP_FALSE;

    return status;
}

/* ------------------------------------------------------------------
 *  Robust DNS 해석
 *
 *  로직:
 *   - APP_STATUS_OK    : 즉시 성공 반환
 *   - APP_STATUS_FATAL : 즉시 영구 실패 반환 (잘못된 파라미터 등)
 *   - 그 외(transient) : 단계 내 재시도, 모두 실패 시 다음 단계로
 *
 *  반환값:
 *   APP_STATUS_OK              : 해석 성공
 *   APP_STATUS_FATAL           : 영구 실패 (파라미터 오류)
 *   APP_STATUS_UART_TIMEOUT: 모든 단계/재시도 소진
 *   APP_STATUS_UART_RX_FAILED  : UART HW 영구 오류
 * ------------------------------------------------------------------ */
AppStatus_t App_Bc95AtResolveHostRobust(const char *p_hostname,
                                        char       *p_ipOut,
                                        uint32_t    ipBufSize)
{
    AppStatus_t status;
    uint32_t attempt;
    uint32_t totalAttempts;
    uint32_t uartErrCount;
    uint32_t startTick;

    /* ---------- 파라미터 검증 ---------- */
    if ((p_hostname == NULL) || (p_ipOut == NULL) || (ipBufSize < APP_BC95_IP_STR_SIZE))
    {
        APP_LOGE("NBIOT", "ResolveHostRobust: invalid param");
        return APP_STATUS_FATAL;
    }

    (void)memset(p_ipOut, 0, ipBufSize);

    APP_LOGI("NBIOT", "==== DNS resolve start: %s ====", p_hostname);

    startTick = HAL_GetTick();
    /* 재시도 루프 */
    for (attempt = 1U; attempt <= APP_BC95_DNS_FAIL_RETRY_MAX; attempt++)
    {
        totalAttempts++;

        APP_LOGD("NBIOT", "DNS attempt %lu (try=%lu/%lu)",
                 (unsigned long)totalAttempts,
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_DNS_FAIL_RETRY_MAX);

        status = App_Bc95AtResolveHost(p_hostname, p_ipOut, ipBufSize);

        if (status == APP_STATUS_OK)
        {
            APP_LOGI("NBIOT",
                     "==== DNS resolved: %s -> %s (attempts=%lu, elapsed=%lums) ====",
                     p_hostname, p_ipOut,
                     (unsigned long)totalAttempts,
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }

        if (status == APP_STATUS_FATAL)
        {
            APP_LOGE("NBIOT",
                     "DNS fatal error (attempt=%lu), abort",
                     (unsigned long)totalAttempts);
            return APP_STATUS_FATAL;
        }

        if (status == APP_STATUS_UART_RX_FAILED)
        {
            uartErrCount++;
            APP_LOGE("NBIOT", "DNS UART err count=%lu",
                     (unsigned long)uartErrCount);

            /* UART HW 오류가 3회 이상 연속이면 영구 실패로 간주 */
            if (uartErrCount >= 3U)
            {
                APP_LOGE("NBIOT",
                         "DNS abort: UART persistently failing");
                return APP_STATUS_UART_RX_FAILED;
            }
            /* 그렇지 않으면 transient 와 동일하게 진행 */
        }
        else
        {
            /* OK/FATAL/UART 이외(=transient timeout/FAIL)일 때
               UART 카운터는 리셋 */
            uartErrCount = 0U;
        }

        /* 마지막 시도가 아니면 지연 후 재시도 */
        if (attempt < APP_BC95_DNS_FAIL_RETRY_MAX)
        {
            APP_LOGI("NBIOT",
                     "DNS retry after %lums (status=%d)",
                     (unsigned long)APP_BC95_DNS_FAIL_RETRY_DELAY_MS,
                     (int)status);
            HAL_Delay(APP_BC95_DNS_FAIL_RETRY_DELAY_MS);
        }
    } /* end attempt loop */

    APP_LOGE("NBIOT",
             "==== DNS resolve FAILED: %s (totalAttempts=%lu, elapsed=%lums) ====",
             p_hostname,
             (unsigned long)totalAttempts,
             (unsigned long)(HAL_GetTick() - startTick));

    (void)memset(p_ipOut, 0, ipBufSize);
    return APP_STATUS_UART_TIMEOUT;
}

/* ============================================================
 *  App_Bc95AtWarmupDns()
 *    부팅 직후 호출. 본 통신 전에 DNS resolver를 깨워둔다.
 *    실패해도 무시 (다음 본 호출에서 어차피 재시도됨).
 * ============================================================ */
static AppStatus_t App_Bc95AtWarmupDns(const char *p_hostname)
{
    AppStatus_t status;
    char ipBuf[APP_BC95_IP_STR_SIZE];
    uint32_t attempt;

    APP_RETURN_IF_FALSE((p_hostname != NULL), APP_STATUS_INVALID_PARAM);

    APP_LOGI("NBIOT", "[DNS] warmup with host %s", p_hostname);

    for (attempt = 1u; attempt <= APP_BC95_DNS_FAIL_RETRY_MAX; attempt++)
    {
        status = App_Bc95AtResolveHostRobust(p_hostname, ipBuf, sizeof(ipBuf));
        if (status == APP_STATUS_OK)
        {
            APP_LOGI("NBIOT", "[DNS] warmup OK on attempt %lu (%s:ip=%s)",
                     (unsigned long)attempt, WARMUPDNS_SERVER_DOMAIN, ipBuf);
            return APP_STATUS_OK;
        }

        APP_LOGI("NBIOT", "[DNS] warmup attempt %lu/%lu failed, retry in %lums",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_DNS_FAIL_RETRY_MAX,
                 (unsigned long)APP_BC95_DNS_FAIL_RETRY_DELAY_MS);

        if (attempt < APP_BC95_DNS_FAIL_RETRY_MAX)
        {
            HAL_Delay(APP_BC95_DNS_FAIL_RETRY_DELAY_MS);
        }
        APP_WWDGFeed();
    }

    APP_LOGI("NBIOT", "[DNS] warmup did not succeed; will retry during send");
    return APP_STATUS_UART_TIMEOUT;
}

/* ============================================================
 *  App_Bc95AtCreateUdpSocket()
 *    AT+NSOCR=DGRAM,17,<localport>,1,AF_INET
 *    응답: "<socket>\r\nOK\r\n"
 * ============================================================ */
AppStatus_t App_Bc95AtCreateUdpSocket(uint16_t localPort, int32_t *p_socketOut)
{
    AppStatus_t  status;
    AppBc95AtStatus_t atStatus;
    uint16_t  rxLen;
    int  printed;
    int  parsedSock;
    const char *p_payload;
    const char *p_line;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_socketOut != NULL), APP_STATUS_INVALID_PARAM);

    *p_socketOut = -1;

    printed = snprintf(appBc95AtCmdTxBuf,
                       sizeof(appBc95AtCmdTxBuf),
                       APP_BC95_AT_CMD_NSOCR_UDP_FMT,
                       (unsigned)localPort);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
    {
        return APP_STATUS_INVALID_PARAM;
    }

    rxLen = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "NSOCR send fail (status=%d)", (int)status);
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "NSOCR response error (parse=%s, CME=%ld)",
                 App_Bc95AtGetStatusString(atStatus), (long)cmeErr);
        return APP_STATUS_FATAL;
    }

    /* 응답에서 OK 이전의 숫자 라인(socket id)을 찾는다 */
    p_payload = (const char *)g_appBc95AtRxBuf;
    parsedSock = -1;
    p_line = p_payload;
    while (*p_line != '\0')
    {
        /* 라인 시작 위치 */
        const char *p_lineStart = p_line;
        while ((*p_lineStart == '\r') || (*p_lineStart == '\n'))
        {
            p_lineStart++;
        }
        if ((*p_lineStart >= '0') && (*p_lineStart <= '9'))
        {
            if (sscanf(p_lineStart, "%d", &parsedSock) == 1)
            {
                break;
            }
        }

        /* 다음 라인으로 */
        while ((*p_line != '\0') && (*p_line != '\n'))
        {
            p_line++;
        }
        if (*p_line == '\n')
        {
            p_line++;
        }
    }

    if (parsedSock < 0)
    {
        APP_LOGE("NBIOT", "NSOCR socket id parse fail");
        APP_LOGE("NBIOT", "raw: [%s]", (const char *)g_appBc95AtRxBuf);
        return APP_STATUS_FATAL;
    }

    *p_socketOut = (int32_t)parsedSock;
    APP_LOGD("NBIOT", "UDP socket created: %ld", (long)parsedSock);
    return APP_STATUS_OK;
}

/* ============================================================
 *  App_Bc95AtUdpSend()
 *    AT+NSOST=<socket>,<ip>,<port>,<len>,<hex>[,<seq>]
 *    응답: "<socket>,<length>\r\nOK\r\n"
 * ============================================================ */
AppStatus_t App_Bc95AtUdpSend(int32_t socketId, const char *p_ip, uint16_t port,
                              const uint8_t *p_data, uint16_t length,
                              uint16_t *p_sentLenOut)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;
    uint32_t headerLen;
    int printed;
    int parsedSock;
    int parsedLen;
    int32_t cmeErr;
    const char *p_lineStart;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((socketId >= 0), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_ip != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);

    if (p_sentLenOut != NULL)
    {
        *p_sentLenOut = 0u;
    }

    /* "AT+NSOST=<sock>,<ip>,<port>,<len>," 부분 작성 */
    printed = snprintf(appBc95AtCmdTxBuf,
                       sizeof(appBc95AtCmdTxBuf),
                       "AT+NSOST=%ld,%s,%u,%u,",
                       (long)socketId, p_ip, (unsigned)port, (unsigned)length);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
    {
        return APP_STATUS_INVALID_PARAM;
    }
    headerLen = (uint32_t)printed;

    /* hex payload 작성. 2*length + "\r\n" + NUL이 들어갈 공간 확인 */
    if ((headerLen + (2u * (uint32_t)length) + 3u) >= sizeof(appBc95AtCmdTxBuf))
    {
        return APP_STATUS_INVALID_PARAM;
    }
    App_Bc95AtBytesToHex(p_data, length, &appBc95AtCmdTxBuf[headerLen]);
    headerLen += (2u * (uint32_t)length);

    /* "\r\n" 추가 */
    appBc95AtCmdTxBuf[headerLen++] = '\r';
    appBc95AtCmdTxBuf[headerLen++] = '\n';
    appBc95AtCmdTxBuf[headerLen]   = '\0';

    rxLen = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "NSOST send fail (status=%d)", (int)status);
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "NSOST response error (parse=%s, CME=%ld)",
                 App_Bc95AtGetStatusString(atStatus), (long)cmeErr);
        return APP_STATUS_FATAL;
    }

    /* "<socket>,<length>" 라인 파싱 */
    parsedSock = -1;
    parsedLen = -1;
    p_lineStart = (const char *)g_appBc95AtRxBuf;
    while (*p_lineStart != '\0')
    {
        while ((*p_lineStart == '\r') || (*p_lineStart == '\n'))
        {
            p_lineStart++;
        }
        if ((*p_lineStart >= '0') && (*p_lineStart <= '9'))
        {
            if (sscanf(p_lineStart, "%d,%d", &parsedSock, &parsedLen) == 2)
            {
                break;
            }
        }
        while ((*p_lineStart != '\0') && (*p_lineStart != '\n'))
        {
            p_lineStart++;
        }
        if (*p_lineStart == '\n')
        {
            p_lineStart++;
        }
    }

    if ((parsedSock != socketId) || (parsedLen <= 0))
    {
        APP_LOGE("NBIOT", "NSOST parse fail (sock=%d, len=%d)", parsedSock, parsedLen);
        APP_LOGE("NBIOT", "raw: [%s]", (const char *)g_appBc95AtRxBuf);
        return APP_STATUS_FATAL;
    }

    if (p_sentLenOut != NULL)
    {
        *p_sentLenOut = (uint16_t)parsedLen;
    }

    APP_LOGI("NBIOT", "NSOST accepted (sock=%ld, len=%d)", (long)socketId, parsedLen);
    return APP_STATUS_OK;
}

/* ============================================================
 *  App_Bc95AtUdpSendAndConfirm()
 *    seq 번호를 붙여 송신하고, 비동기로 도착하는
 *    "+NSOSTR:<sock>,<seq>,<status>" URC를 기다린다.
 * ============================================================ */
static AppBc95AtStatus_t App_Bc95AtParseNsostr(const char *p_resp,
                                               int *p_sock,
                                               int *p_seq,
                                               int *p_status)
{
    const char *p_pfx;

    if ((p_resp == NULL) || (p_sock == NULL) || (p_seq == NULL) || (p_status == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    p_pfx = strstr(p_resp, APP_BC95_AT_NSOSTR_PREFIX);
    if (p_pfx == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_pfx += APP_BC95_AT_NSOSTR_PREFIX_LEN;
    while ((*p_pfx == ' ') || (*p_pfx == '\t'))
    {
        p_pfx++;
    }
    if (sscanf(p_pfx, "%d,%d,%d", p_sock, p_seq, p_status) != 3)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    return APP_BC95_AT_OK;
}

/* ------------------------------------------------------------------
 *  App_Bc95AtUdpSendAndConfirm
 *
 *  AT+NSOST 로 UDP 데이터를 보내고 +NSOSTR URC 로 전송 확인을 받는다.
 *  내부 재시도는 하지 않고 단일 시도만 수행한다.
 *  상위에서 재시도가 필요하면 App_Bc95AtUdpSendAndConfirmRetry() 사용.
 *
 *  반환:
 *   APP_STATUS_OK              : +NSOSTR status=1 수신 (성공 확인)
 *   APP_STATUS_INVALID_PARAM   : 파라미터/버퍼 오류
 *   APP_STATUS_FATAL           : 모듈이 ERROR/+CME ERROR 또는 status!=1 응답
 *   APP_STATUS_UART_RX_FAILED  : UART HW 오류
 *   APP_STATUS_UART_TIMEOUT    : OK 또는 URC 타임아웃 (재시도 가능)
 * ------------------------------------------------------------------ */
AppStatus_t App_Bc95AtUdpSendAndConfirm(int32_t        socketId,
                                        const char    *p_ip,
                                        uint16_t       port,
                                        const uint8_t *p_data,
                                        uint16_t       length,
                                        uint8_t        seqNum,
                                        uint32_t       confirmTimeoutMs)
{
    HAL_StatusTypeDef halStatus;
    AppStatus_t       status;
    AppBc95AtStatus_t atStatus;
    uint32_t          headerLen;
    int               printed;
    uint16_t          rxLen;
    uint16_t          bufCap;
    uint32_t          startTick;
    uint32_t          elapsed;
    int               parsedSock;
    int               parsedSeq;
    int               parsedStatus;
    int32_t           cmeErr;
    char              appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    /* ---------- 파라미터 검증 ---------- */
    /* seqNum은 uint8_t이므로 0이 아니기만 하면 됨 */
    APP_RETURN_IF_FALSE(seqNum > 0u, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_ip != NULL) && (p_ip[0] != '\0'), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(socketId >= 0, APP_STATUS_INVALID_PARAM);

    /* ---------- AT 명령 빌드 ---------- */
    printed = snprintf(appBc95AtCmdTxBuf,
                       sizeof(appBc95AtCmdTxBuf),
                       "AT+NSOST=%ld,%s,%u,%u,",
                       (long)socketId, p_ip,
                       (unsigned)port, (unsigned)length);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
    {
        APP_LOGE("NBIOT", "NSOST: cmd header build fail");
        return APP_STATUS_INVALID_PARAM;
    }
    headerLen = (uint32_t)printed;

    /* hex payload + ",seq\r\n" 크기 검증 */
    if ((headerLen + (2u * (uint32_t)length) + 16u) >= sizeof(appBc95AtCmdTxBuf))
    {
        APP_LOGE("NBIOT", "NSOST: cmd buf too small for payload");
        return APP_STATUS_INVALID_PARAM;
    }

    App_Bc95AtBytesToHex(p_data, length, &appBc95AtCmdTxBuf[headerLen]);
    headerLen += (2u * (uint32_t)length);

    printed = snprintf(&appBc95AtCmdTxBuf[headerLen],
                       sizeof(appBc95AtCmdTxBuf) - headerLen,
                       ",%u\r\n", (unsigned)seqNum);
    if ((printed <= 0) ||
        ((headerLen + (uint32_t)printed) >= sizeof(appBc95AtCmdTxBuf)))
    {
        APP_LOGE("NBIOT", "NSOST: cmd tail build fail");
        return APP_STATUS_INVALID_PARAM;
    }

    APP_LOGI("NBIOT",
             "NSOST send: sock=%ld, ip=%s, port=%u, len=%u, seq=%u",
             (long)socketId, p_ip, (unsigned)port,
             (unsigned)length, (unsigned)seqNum);
    APP_LOGD("NBIOT", "send:%s", appBc95AtCmdTxBuf);

    /* ---------- 1차: NSOST 송신 + "OK" 수신 ---------- */
    rxLen  = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "NSOST send fail (status=%d, rxLen=%u)",
                 (int)status, (unsigned)rxLen);
        /* UART 오류면 그대로, 그 외는 재시도 가능 타임아웃으로 매핑 */
        if (status == APP_STATUS_UART_RX_FAILED)
        {
            return APP_STATUS_UART_RX_FAILED;
        }
        return APP_STATUS_UART_TIMEOUT;
    }

    cmeErr   = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "NSOST no OK (parse=%s, cme=%ld, raw=[%s])",
                 App_Bc95AtGetStatusString(atStatus),
                 (long)cmeErr,
                 (const char *)g_appBc95AtRxBuf);
        return APP_STATUS_FATAL;
    }

    /* ---------- (Fast path) 이미 +NSOSTR 가 같은 버퍼에 와 있는지 확인 ---------- */
    if (App_Bc95AtParseNsostr((const char *)g_appBc95AtRxBuf,
                              &parsedSock, &parsedSeq, &parsedStatus)
        == APP_BC95_AT_OK)
    {
        if ((parsedSock == socketId) && (parsedSeq == (int)seqNum))
        {
            if (parsedStatus == 1)
            {
                APP_LOGI("NBIOT",
                         "NSOSTR confirmed (fast): sock=%d, seq=%d",
                         parsedSock, parsedSeq);
                APP_LOGI("NBIOT", "recv:%s", g_appBc95AtRxBuf);
                return APP_STATUS_OK;
            }
            APP_LOGE("NBIOT",
                     "NSOSTR fail (fast): sock=%d, seq=%d, status=%d",
                     parsedSock, parsedSeq, parsedStatus);
            return APP_STATUS_FATAL;
        }
        /* sock/seq 불일치면 무시하고 계속 대기 */
        APP_LOGI("NBIOT",
                 "NSOSTR mismatch (fast): got sock=%d seq=%d, expected sock=%ld seq=%u",
                 parsedSock, parsedSeq,
                 (long)socketId, (unsigned)seqNum);
    }

    /* ---------- 2차: +NSOSTR URC 비동기 대기 ----------
     * 중요: 기존 RX 버퍼 내용을 지우지 않고 그대로 두고
     *       이미 수신된 길이 뒤에 이어서 IT 로 받아 누적 검색한다.
     *       (memset으로 버퍼를 지우면 OK 직후 따라온 URC 일부를 잃을 수 있음)
     */
    bufCap = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);

    /* 현재 누적 길이 = 1차 응답 끝까지 */
    if (rxLen >= bufCap)
    {
        /* 1차 응답이 이미 버퍼를 거의 다 채웠다면 URC 들어올 공간이 없음 */
        APP_LOGE("NBIOT", "NSOSTR wait: rx buf already full after OK (rxLen=%u)",
                 (unsigned)rxLen);
        return APP_STATUS_UART_TIMEOUT;
    }

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = bufCap;
    g_appBc95AtRxContext.receivedLength = rxLen;   /* 누적 유지 */
    g_appBc95AtRxContext.active         = APP_TRUE;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;

    /* 오류 플래그만 클리어 (RDR flush는 하지 않음 - URC 일부가 들어와 있을 수 있음) */
    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);

    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE,
                                    &g_appBc95AtRxBuf[rxLen], 1u);
    if (halStatus != HAL_OK)
    {
        g_appBc95AtRxContext.active = APP_FALSE;
        APP_LOGE("NBIOT", "NSOSTR wait: RX_IT start fail (%d)", (int)halStatus);
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();

    while (1)
    {
        /* UART HW 오류 */
        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF  | UART_CLEAR_PEF);
            __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE,
                                UART_RXDATA_FLUSH_REQUEST);
            g_appBc95AtRxContext.active = APP_FALSE;
            APP_LOGE("NBIOT", "NSOSTR wait: UART error (rxLen=%u)",
                     (unsigned)g_appBc95AtRxContext.receivedLength);
            return APP_STATUS_UART_RX_FAILED;
        }

        rxLen = g_appBc95AtRxContext.receivedLength;

        if (rxLen > 0u)
        {
            g_appBc95AtRxBuf[rxLen] = '\0';

            if (App_Bc95AtParseNsostr((const char *)g_appBc95AtRxBuf,
                                      &parsedSock, &parsedSeq, &parsedStatus)
                == APP_BC95_AT_OK)
            {
                if ((parsedSock == socketId) && (parsedSeq == (int)seqNum))
                {
                    (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
                    g_appBc95AtRxContext.active = APP_FALSE;

                    if (parsedStatus == 1)
                    {
                        APP_LOGD("NBIOT", "recv:%s", g_appBc95AtRxBuf);
                        APP_LOGI("NBIOT",
                                 "NSOSTR confirmed: sock=%d, seq=%d (elapsed=%lums)",
                                 parsedSock, parsedSeq,
                                 (unsigned long)(HAL_GetTick() - startTick));
                        return APP_STATUS_OK;
                    }
                    APP_LOGE("NBIOT",
                             "NSOSTR error: sock=%d, seq=%d, status=%d",
                             parsedSock, parsedSeq, parsedStatus);
                    return APP_STATUS_FATAL;
                }
                /* sock/seq 가 다르면 다른 소켓의 URC -> 무시, 계속 대기 */
            }
        }

        /* 버퍼 가득 참 */
        if (rxLen >= bufCap)
        {
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            g_appBc95AtRxContext.active = APP_FALSE;
            APP_LOGE("NBIOT",
                     "NSOSTR wait: buffer full without match (rxLen=%u)",
                     (unsigned)rxLen);
            return APP_STATUS_UART_TIMEOUT;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= confirmTimeoutMs)
        {
            (void)HAL_UART_AbortReceive_IT(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF  | UART_CLEAR_PEF);
            g_appBc95AtRxContext.active = APP_FALSE;
            APP_LOGE("NBIOT",
                     "NSOSTR wait timeout (sock=%ld, seq=%u, rxLen=%u, raw=[%s])",
                     (long)socketId, (unsigned)seqNum,
                     (unsigned)rxLen,
                     (const char *)g_appBc95AtRxBuf);
            return APP_STATUS_UART_TIMEOUT;
        }

        HAL_Delay(20u);  /* 폴링 주기 */
    }
}

/* ============================================================
 *  App_Bc95AtCloseSocket()
 *    AT+NSOCL=<sock>
 * ============================================================ */
AppStatus_t App_Bc95AtCloseSocket(int32_t socketId)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen;
    int printed;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((socketId >= 0), APP_STATUS_INVALID_PARAM);

    printed = snprintf(appBc95AtCmdTxBuf,
                       sizeof(appBc95AtCmdTxBuf),
                       APP_BC95_AT_CMD_NSOCL_FMT,
                       (long)socketId);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
    {
        return APP_STATUS_INVALID_PARAM;
    }

    rxLen = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        /* 닫기 실패는 치명적이지 않음 - 로그만 남기고 진행 */
        APP_LOGI("NBIOT", "NSOCL send fail (sock=%ld, status=%d)", (long)socketId, (int)status);
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGI("NBIOT", "NSOCL response not OK (sock=%ld, parse=%s)",
                 (long)socketId, App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGD("NBIOT", "UDP socket closed: %ld", (long)socketId);
    return APP_STATUS_OK;
}

/* ============================================================
 *  App_Bc95AtUdpSendOnce()
 *    호스트 이름으로 한 번의 UDP 데이터그램을 송신한다.
 *    DNS -> 소켓 생성 -> 송신 -> 닫기 전체 흐름 보장.
 *    통신 환경이 불안정할 수 있으므로 단계별 재시도 적용.
 *
 *    실패 시에도 생성된 소켓은 반드시 닫는다.
 * ============================================================ */
AppStatus_t App_Bc95AtUdpSendOnce(const char *p_hostname, uint16_t port, const uint8_t *p_data, uint16_t length, AppBc95UdpResult_t *p_result)
{
    AppStatus_t status;
    int32_t socketId = -1;
    char ipBuf[APP_BC95_IP_STR_SIZE];
    uint32_t attempt;
    uint8_t  seq;
    AppBc95UdpResult_t result;

    (void)memset(&result, 0, sizeof(result));
    result.socketId = -1;
    result.lastStage = APP_BC95_UDP_STAGE_NONE;
    ipBuf[0] = '\0';

    APP_RETURN_IF_FALSE((p_hostname != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);

    /* ---- 1) DNS 해석 (재시도) ---- */
    result.lastStage = APP_BC95_UDP_STAGE_RESOLVE;
    status = App_Bc95AtResolveHostRobust(p_hostname, ipBuf, sizeof(ipBuf));
    if ((status != APP_STATUS_OK) || (ipBuf[0] == '\0'))
    {
        APP_LOGE("NBIOT", "DNS resolve failed for %s", p_hostname);
        if (p_result != NULL)
        {
            (void)memcpy(p_result, &result, sizeof(*p_result));
        }
        return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
    }
    (void)strncpy(result.resolvedIp, ipBuf, sizeof(result.resolvedIp) - 1u);

    /* ---- 2) UDP 소켓 생성 (재시도) ---- */
    result.lastStage = APP_BC95_UDP_STAGE_CREATE;
    for (attempt = 1u; attempt <= APP_BC95_UDP_SEND_RETRY_MAX; attempt++)
    {
        APP_WWDGFeed();
        status = App_Bc95AtCreateUdpSocket(APP_BC95_UDP_LOCAL_PORT, &socketId);
        if (status == APP_STATUS_OK)
        {
            break;
        }
        APP_LOGI("NBIOT", "NSOCR retry %lu/%lu (status=%d)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_UDP_SEND_RETRY_MAX,
                 (int)status);
        if (attempt < APP_BC95_UDP_SEND_RETRY_MAX)
        {
            HAL_Delay(APP_BC95_UDP_SEND_RETRY_DELAY_MS);
        }
        APP_WWDGFeed();
    }
    if ((status != APP_STATUS_OK) || (socketId < 0))
    {
        APP_LOGE("NBIOT", "UDP socket create failed");
        if (p_result != NULL)
        {
            (void)memcpy(p_result, &result, sizeof(*p_result));
        }
        return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
    }
    result.socketId = socketId;

    /* ---- 3) 송신 + RF 송출 확인 (재시도) ----
     * seq 1..N 으로 매 시도마다 다른 번호 사용.
     */
    result.lastStage = APP_BC95_UDP_STAGE_SEND;
    seq = 0u;
    status = APP_STATUS_FATAL;
    for (attempt = 1u; attempt <= APP_BC95_UDP_SEND_RETRY_MAX; attempt++)
    {
        seq = (uint8_t)attempt;
        result.seqNumber = seq;

        result.lastStage = APP_BC95_UDP_STAGE_SEND;
        APP_WWDGFeed();
        status = App_Bc95AtUdpSendAndConfirm(socketId, ipBuf, port,
                                             p_data, length, seq,
                                             APP_BC95_NSOST_CONFIRM_TIMEOUT_MS);
        if (status == APP_STATUS_OK)
        {
            result.lastStage = APP_BC95_UDP_STAGE_CONFIRM;
            result.sentBytes = length;
            result.sendConfirmed = APP_TRUE;
            break;
        }

        APP_LOGI("NBIOT", "UDP send retry %lu/%lu (status=%d)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_UDP_SEND_RETRY_MAX,
                 (int)status);
        if (attempt < APP_BC95_UDP_SEND_RETRY_MAX)
        {
            HAL_Delay(APP_BC95_UDP_SEND_RETRY_DELAY_MS);
        }
        APP_WWDGFeed();
    }

    /* ---- 4) 소켓 닫기 (성공/실패 무관) ---- */
    result.lastStage = APP_BC95_UDP_STAGE_CLOSE;
    (void)App_Bc95AtCloseSocket(socketId);

    if (status == APP_STATUS_OK)
    {
        result.lastStage = APP_BC95_UDP_STAGE_DONE;
        APP_LOGI("NBIOT", "UDP send done: %s(%s):%u, %u bytes, seq=%u",
                 p_hostname, ipBuf, (unsigned)port, (unsigned)length, (unsigned)seq);
    }
    else
    {
        APP_LOGE("NBIOT", "UDP send failed at stage=%d, status=%d",
                 (int)result.lastStage, (int)status);
    }

    if (p_result != NULL)
    {
        (void)memcpy(p_result, &result, sizeof(*p_result));
    }
    return status;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////

AppStatus_t App_NBIoTAtInit(void)
{
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(&g_appBc95AtRxContext, 0, sizeof(g_appBc95AtRxContext));
    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));

    (void)memset(g_appBc95AtImeiBcd, 0, sizeof(g_appBc95AtImeiBcd));
    (void)memset(g_appBc95AtImsiBcd, 0, sizeof(g_appBc95AtImsiBcd));
    (void)memset(&g_appBc95AtQuality, 0, sizeof(g_appBc95AtQuality));
    (void)memset(g_appBc95AtQualityBcd, 0, sizeof(g_appBc95AtQualityBcd));

    g_appBc95AtInitialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTBringUp(void)
{
    AppStatus_t status;

    App_NBIoTAtInit();

    /* 부팅 완료 대기 (최대 15초) */
    status = App_Bc95AtWaitUntilReady(APP_SELFTEST_NBIOT_BOOT_DELAY_MS);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Module boot timeout");
        return(status);
    }

    /* USIM 준비 대기 */
    status = App_Bc95AtWaitForUsim(APP_BC95_USIM_READY_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Module usim timeout");
        return(status);
    }

    APP_LOGI("NBIOT", "Module is ready, proceed with identity read");
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTNetworkBringUp(void)
{
    AppStatus_t status;
    AppBc95NetStatus_t netStatus;

    /* 네트워크 등록 대기 (셀 캠프 + CEREG=1/5 + IP 할당까지) */
    status = App_Bc95AtWaitForNetwork(APP_BC95_NET_DEFAULT_TIMEOUT_MS, &netStatus);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Network not ready (phase=%s)",
                 App_Bc95AtGetNetPhaseString(netStatus.phase));
        return(status);
    }

    App_Bc95AtWarmupDns(WARMUPDNS_SERVER_DOMAIN);

    APP_LOGI("NBIOT", "Network is ready, proceed with communication");
    return APP_STATUS_OK;
}


AppStatus_t App_NBIoTReadIdentity(void)
{
    AppStatus_t status;
    uint8_t appImeiBcd[APP_BC95_IMEI_BCD_BYTES];
    uint8_t appImsiBcd[APP_BC95_IMSI_BCD_BYTES];

    /* --- IMEI --- */
    status = App_Bc95AtFetchImei(appImeiBcd, sizeof(appImeiBcd));
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "IMEI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMEI", appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);
    App_Bc95PrintDecoded("IMEI", appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);

    /* --- IMSI --- */
    status = App_Bc95AtFetchImsi(appImsiBcd, sizeof(appImsiBcd));
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "IMSI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMSI", appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);
    App_Bc95PrintDecoded("IMSI", appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);

    return APP_STATUS_OK;
}

/*
    AT+NUESTATS=CELL

    NUESTATS:CELL,2554,74,1,-613,-110,-529,48

    OK

    Response
    NUESTATS:CELL,<earfcn>,<physical cell id>,<primarycell>,<rsrp>,<rsrq>,<rssi>,<snr>
*/
AppStatus_t App_NBIoTReadQuality(void)
{
    AppStatus_t      status;
    AppBc95Quality_t quality;

    /* --- Quality --- */
    status = App_Bc95AtFetchQuality(&quality, g_appBc95AtQualityBcd, sizeof(g_appBc95AtQualityBcd));
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "Quality fetch failed (status=%d)", (int)status);
        return status;
    }
    App_NBIoTPrintQuality(&quality);
    App_Bc95PrintBcd("QUALITY", g_appBc95AtQualityBcd, (uint32_t)APP_BC95_QUALITY_BCD_BYTES);

    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTTransmitUdp(void)
{
    AppStatus_t status;
    AppBc95UdpResult_t result;

    /* 1) 송신할 데이터 (예시) */
    const uint8_t payload[] = {
        0x01, 0x02, 0x03, 0x04, 0x05,
        0x86, 0x19, 0x21, 0x03, 0x12, 0x29, 0x50, 0x8F   /* 예: IMEI BCD */
    };

    uint16_t port = 6001u;
    status = App_Bc95AtUdpSendOnce(MY_SERVER_DOMAIN,
                                   port,
                                   payload,
                                   (uint16_t)sizeof(payload),
                                   &result);

    APP_LOGI("NBIOT", "Result: stage=%d, ip=%s, port=%u, sent=%u, seq=%u, confirmed=%u",
             (int)result.lastStage,
             result.resolvedIp,
             port,
             (unsigned)result.sentBytes,
             (unsigned)result.seqNumber,
             (unsigned)result.sendConfirmed);

    return status;
}

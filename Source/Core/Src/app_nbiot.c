#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "app_build_config.h"
#include "main.h"
#include "app_hw.h"
#include "app_nbiot.h"
#include "app_gpio_lp.h"
#include "app_log.h"
#include "app_clock.h"
#include "app_fsm.h"
#include "app_meter_storage.h"
#include "app_meter_server_format.h"

#define APP_BC95_AT_CMD_PING          "AT\r\n"
#define APP_BC95_AT_CMD_IMEI          "AT+CGSN=1\r\n"
#define APP_BC95_AT_CMD_IMSI          "AT+CIMI\r\n"
#define APP_BC95_AT_CMD_NUESTATS_CELL "AT+NUESTATS=CELL\r\n"

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

#define APP_BC95_AT_CMD_TX_BUF_SIZE          (1400u)
#define APP_BC95_AT_QDNS_PREFIX              "+QDNS:"
#define APP_BC95_AT_QDNS_PREFIX_LEN          (6u)
#define APP_BC95_AT_NSOSTR_PREFIX            "+NSOSTR:"
#define APP_BC95_AT_NSOSTR_PREFIX_LEN        (8u)
#define APP_BC95_AT_NSORF_PREFIX             "+NSORF:"
#define APP_BC95_AT_NSORF_PREFIX_LEN         (7u)

#define APP_BC95_AT_CMD_NSOCR_UDP_FMT        "AT+NSOCR=DGRAM,17,%u,1,AF_INET\r\n"
#define APP_BC95_AT_CMD_NSOCL_FMT            "AT+NSOCL=%ld\r\n"
#define APP_BC95_AT_CMD_NSORF_FMT            "AT+NSORF=%ld,%u\r\n"

#define APP_BC95_AT_CMD_CFUN_QUERY           "AT+CFUN?\r\n"
#define APP_BC95_AT_CMD_CFUN_SET_FULL        "AT+CFUN=1\r\n"
#define APP_BC95_AT_CMD_CFUN_SET_MIN         "AT+CFUN=0\r\n"
#define APP_BC95_AT_CMD_CGATT_QUERY          "AT+CGATT?\r\n"
#define APP_BC95_AT_CMD_CGATT_DETACH         "AT+CGATT=0\r\n"
#define APP_BC95_AT_CMD_CMEE_ON              "AT+CMEE=1\r\n"
#define APP_BC95_AT_CMD_CEREG_SET_3          "AT+CEREG=3\r\n"
#define APP_BC95_AT_CMD_CEREG_QUERY          "AT+CEREG?\r\n"
#define APP_BC95_AT_CMD_CGPADDR_QUERY        "AT+CGPADDR\r\n"

#define APP_BC95_AT_CFUN_PREFIX              "+CFUN:"
#define APP_BC95_AT_CFUN_PREFIX_LEN          (6u)
#define APP_BC95_AT_CGATT_PREFIX             "+CGATT:"
#define APP_BC95_AT_CGATT_PREFIX_LEN         (7u)
#define APP_BC95_AT_CEREG_PREFIX             "+CEREG:"
#define APP_BC95_AT_CEREG_PREFIX_LEN         (7u)
#define APP_BC95_AT_CGPADDR_PREFIX           "+CGPADDR:"
#define APP_BC95_AT_CGPADDR_PREFIX_LEN       (9u)

#define APP_BC95_AT_RX_IDLE_GAP_MS           (40u)
#define APP_BC95_AT_RX_POST_TERM_WAIT_MS     (20u)
#define APP_BC95_AT_DRAIN_GUARD_MS           (5u)
#define APP_BC95_AT_LINE_QUEUE_DEPTH         (12u)
#define APP_BC95_AT_LINE_MAX_LEN             (96u)
#define APP_BC95_SERVICE_READY_SETTLE_MS     (500u)

#define APP_NBIOT_REPORT_LOG_ATTACH         "[[Attach]]"
#define APP_NBIOT_REPORT_LOG_RESET          "[[Reset]]"
#define APP_NBIOT_REPORT_LOG_DETACH         "[[Detach]]"
#define APP_NBIOT_REPORT_LOG_POWEROFF       "[[PowerOff]]"

#define APP_NBIOT_REJECT_CAUSE_SUSPEND        (19)
#define APP_NBIOT_REJECT_CAUSE_TERMINATED_1   (8)
#define APP_NBIOT_REJECT_CAUSE_TERMINATED_2   (2)
#define APP_NBIOT_REJECT_CAUSE_TERMINATED_3   (26)

/* ============================================================
 *  내부 상태
 * ============================================================ */
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

typedef struct
{
    char     lines[APP_BC95_AT_LINE_QUEUE_DEPTH][APP_BC95_AT_LINE_MAX_LEN];
    char     partial[APP_BC95_AT_LINE_MAX_LEN];
    uint8_t  head;
    uint8_t  tail;
    uint8_t  count;
    uint16_t partialLen;
    uint16_t parsedLen;
} AppBc95AtLineQueue_t;

static AppBc95AtRxContext_t g_appBc95AtRxContext;
static uint8_t  g_appBc95AtRxBuf[APP_BC95_AT_RX_BUF_SIZE];

static uint8_t  g_appBc95AtInitialized;
static uint8_t  g_appBc95AtImeiBcd[APP_BC95_IMEI_BCD_BYTES];
static uint8_t  g_appBc95AtImsiBcd[APP_BC95_IMSI_BCD_BYTES];
static uint8_t  g_appBc95AtQualityBcd[APP_BC95_QUALITY_BCD_BYTES];
static AppBc95Quality_t g_appBc95AtQuality;
static AppBc95NetStatus_t g_appBc95AtNetStatus;
static AppNbiotCarrierContext_t g_appNbiotCarrierContext;
static uint8_t g_appBc95UsimFatal = APP_FALSE;

/* UDP seq 단조 증가 카운터 (stale URC 매칭 방지) */
static uint8_t g_appBc95UdpSeqCounter = 0u;

#ifdef DEBUG
typedef struct
{
    volatile uint32_t errorCode;
    volatile uint16_t errorAtIndex;
    volatile uint8_t  errorCount;
} AppBc95AtRxErrorInfo_t;
static AppBc95AtRxErrorInfo_t g_appBc95AtRxErrorInfo;
#endif

static AppStatus_t App_Bc95AtSendSimpleOkCommand(const char *p_cmd,
                                                 uint32_t rxTimeoutMs,
                                                 const char *p_cmdLabel,
                                                 const char *p_logPrefix);
static AppStatus_t App_Bc95AtWaitForServiceReady(uint32_t totalTimeoutMs);

static void App_HwNbiotPowerCycle(void)
{
    APP_LOGD("NBIOT", "App_HwNbiotPowerCycle");
    App_GpioLpSetNbiotPowered(APP_FALSE); /* off */
    /*
     * Hold power-off briefly so the modem performs a clean reboot, but do not
     * wait after power-on here; the next boot-wait path must start immediately
     * so the boot banner can be captured.
     */
    HAL_Delay(200u);
    App_GpioLpSetNbiotPowered(APP_TRUE);  /* on */
}

/* ============================================================
 *  유틸: WDT-feed 분할 지연
 * ============================================================ */
static void App_Bc95AtDelayWithFeed(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t step  = 100u;

    while ((HAL_GetTick() - start) < ms)
    {
        APP_WWDGFeed();
        if ((ms - (HAL_GetTick() - start)) < step)
        {
            step = ms - (HAL_GetTick() - start);
            if (step == 0u) break;
        }
        HAL_Delay(step);
    }
    APP_WWDGFeed();
}

static uint16_t App_Bc95AtCopySnapshot(const uint8_t *p_src, uint16_t srcLen,
                                       char *p_dst, uint16_t dstSize)
{
    uint16_t copyLen = srcLen;

    if ((p_src == NULL) || (p_dst == NULL) || (dstSize == 0u))
    {
        return 0u;
    }

    if (copyLen >= dstSize)
    {
        copyLen = (uint16_t)(dstSize - 1u);
    }

    if (copyLen > 0u)
    {
        (void)memcpy(p_dst, p_src, copyLen);
    }
    p_dst[copyLen] = '\0';
    return copyLen;
}

static void App_Bc95AtLineQueueReset(AppBc95AtLineQueue_t *p_queue)
{
    if (p_queue == NULL) return;
    (void)memset(p_queue, 0, sizeof(*p_queue));
}

static void App_Bc95AtLineQueuePush(AppBc95AtLineQueue_t *p_queue, const char *p_line)
{
    uint8_t index;

    if ((p_queue == NULL) || (p_line == NULL) || (p_line[0] == '\0'))
    {
        return;
    }

    index = p_queue->tail;
    (void)strncpy(p_queue->lines[index], p_line, (size_t)(APP_BC95_AT_LINE_MAX_LEN - 1u));
    p_queue->lines[index][APP_BC95_AT_LINE_MAX_LEN - 1u] = '\0';

    p_queue->tail = (uint8_t)((p_queue->tail + 1u) % APP_BC95_AT_LINE_QUEUE_DEPTH);
    if (p_queue->count < APP_BC95_AT_LINE_QUEUE_DEPTH)
    {
        p_queue->count++;
    }
    else
    {
        p_queue->head = (uint8_t)((p_queue->head + 1u) % APP_BC95_AT_LINE_QUEUE_DEPTH);
    }
}

static void App_Bc95AtLineQueueFeed(AppBc95AtLineQueue_t *p_queue,
                                    const char *p_snapshot,
                                    uint16_t snapshotLen)
{
    uint16_t i;
    char ch;

    if ((p_queue == NULL) || (p_snapshot == NULL))
    {
        return;
    }

    if (snapshotLen < p_queue->parsedLen)
    {
        p_queue->parsedLen = 0u;
        p_queue->partialLen = 0u;
        p_queue->partial[0] = '\0';
    }

    for (i = p_queue->parsedLen; i < snapshotLen; i++)
    {
        ch = p_snapshot[i];
        if ((ch == '\r') || (ch == '\n'))
        {
            if (p_queue->partialLen > 0u)
            {
                p_queue->partial[p_queue->partialLen] = '\0';
                App_Bc95AtLineQueuePush(p_queue, p_queue->partial);
                p_queue->partialLen = 0u;
                p_queue->partial[0] = '\0';
            }
            continue;
        }

        if (p_queue->partialLen < (APP_BC95_AT_LINE_MAX_LEN - 1u))
        {
            p_queue->partial[p_queue->partialLen++] = ch;
            p_queue->partial[p_queue->partialLen] = '\0';
        }
    }

    p_queue->parsedLen = snapshotLen;
}

static uint8_t App_Bc95AtLineQueueContainsExact(const AppBc95AtLineQueue_t *p_queue,
                                                const char *p_line)
{
    uint8_t i;
    uint8_t index;

    if ((p_queue == NULL) || (p_line == NULL))
    {
        return APP_FALSE;
    }

    for (i = 0u; i < p_queue->count; i++)
    {
        index = (uint8_t)((p_queue->head + i) % APP_BC95_AT_LINE_QUEUE_DEPTH);
        if (strcmp(p_queue->lines[index], p_line) == 0)
        {
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

static uint8_t App_Bc95AtLineQueueContainsPrefix(const AppBc95AtLineQueue_t *p_queue,
                                                 const char *p_prefix)
{
    uint8_t i;
    uint8_t index;
    size_t prefixLen;

    if ((p_queue == NULL) || (p_prefix == NULL))
    {
        return APP_FALSE;
    }

    prefixLen = strlen(p_prefix);
    for (i = 0u; i < p_queue->count; i++)
    {
        index = (uint8_t)((p_queue->head + i) % APP_BC95_AT_LINE_QUEUE_DEPTH);
        if (strncmp(p_queue->lines[index], p_prefix, prefixLen) == 0)
        {
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

static uint8_t App_Bc95AtLineQueueContainsSubstring(const AppBc95AtLineQueue_t *p_queue,
                                                    const char *p_token)
{
    uint8_t i;
    uint8_t index;

    if ((p_queue == NULL) || (p_token == NULL) || (p_token[0] == '\0'))
    {
        return APP_FALSE;
    }

    for (i = 0u; i < p_queue->count; i++)
    {
        index = (uint8_t)((p_queue->head + i) % APP_BC95_AT_LINE_QUEUE_DEPTH);
        if (strstr(p_queue->lines[index], p_token) != NULL)
        {
            return APP_TRUE;
        }
    }

    if (strstr(p_queue->partial, p_token) != NULL)
    {
        return APP_TRUE;
    }
    return APP_FALSE;
}

static uint8_t App_Bc95AtLineQueueHasFinalResponse(const AppBc95AtLineQueue_t *p_queue)
{
    if (p_queue == NULL)
    {
        return APP_FALSE;
    }

    if (App_Bc95AtLineQueueContainsExact(p_queue, APP_BC95_AT_OK_TOKEN) == APP_TRUE)
    {
        return APP_TRUE;
    }
    if (App_Bc95AtLineQueueContainsExact(p_queue, APP_BC95_AT_ERROR_TOKEN) == APP_TRUE)
    {
        return APP_TRUE;
    }
    if (App_Bc95AtLineQueueContainsPrefix(p_queue, APP_BC95_AT_CME_ERROR_PREFIX) == APP_TRUE)
    {
        return APP_TRUE;
    }
    return APP_FALSE;
}

/* ============================================================
 *  RX 컨텍스트 종료 (경쟁 조건 방지를 위한 통합 헬퍼)
 *    - active 를 먼저 false 로 만든 뒤 abort 호출 → ISR 이
 *      추가 IT 등록을 못 하도록 보장
 * ============================================================ */
static void App_Bc95AtFinishRxIt(UART_HandleTypeDef *p_huart)
{
    g_appBc95AtRxContext.active = APP_FALSE;
    if (p_huart != NULL)
    {
        (void)HAL_UART_AbortReceive_IT(p_huart);
    }
}

/* ============================================================
 *  RX 라인 드레인 (송신 직전 호출)
 * ============================================================ */
static void App_Bc95AtDrainRxLine(UART_HandleTypeDef *p_huart, uint32_t drainMs)
{
    uint32_t guardMs;

    if (p_huart == NULL) return;

    App_Bc95AtFinishRxIt(p_huart);

    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    g_appBc95AtRxContext.error = APP_FALSE;
    guardMs = drainMs;
    if (guardMs > APP_BC95_AT_DRAIN_GUARD_MS)
    {
        guardMs = APP_BC95_AT_DRAIN_GUARD_MS;
    }
    if (guardMs > 0u)
    {
        App_Bc95AtDelayWithFeed(guardMs);
    }
}

/* ============================================================
 *  AT 응답 토큰 검출 헬퍼
 * ============================================================ */
static uint8_t App_Bc95AtIsLineStart(const char *p_buf, const char *p_pos)
{
    if (p_pos == p_buf) return APP_TRUE;
    if ((*(p_pos - 1) == '\r') || (*(p_pos - 1) == '\n')) return APP_TRUE;
    return APP_FALSE;
}

static uint8_t App_Bc95AtIsLineEnd(char ch)
{
    if ((ch == '\0') || (ch == '\r') || (ch == '\n')) return APP_TRUE;
    return APP_FALSE;
}

static const char *App_Bc95AtFindOkBoundary(const char *p_resp)
{
    const char *p_scan = p_resp;
    const char *p_token;

    while (*p_scan != '\0')
    {
        p_token = strstr(p_scan, APP_BC95_AT_OK_TOKEN);
        if (p_token == NULL) return NULL;

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
        if (p_token == NULL) return APP_FALSE;

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
    const char *p_str;

    if ((p_buf == NULL) || (length < 6u)) return APP_FALSE;

    p_str = (const char *)p_buf;

    if (App_Bc95AtFindOkBoundary(p_str) != NULL)    return APP_TRUE;
    if (App_Bc95AtHasErrorToken(p_str) == APP_TRUE) return APP_TRUE;

    if (strstr(p_str, APP_BC95_AT_CME_ERROR_PREFIX) != NULL)
    {
        if ((strchr(p_str, '\n') != NULL) || (strchr(p_str, '\r') != NULL))
        {
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

/* ============================================================
 *  BCD 변환 (IMEI/IMSI 공통 로직, 길이 파라미터로 분리)
 * ============================================================ */
static AppBc95AtStatus_t App_Bc95AtDigitsToBcd(const char *p_digits, uint32_t digitCount,
                                                uint8_t *p_bcdOut, uint32_t bcdSize,
                                                uint32_t requiredDigits, uint32_t requiredBcdBytes)
{
    uint8_t nibble[16];
    uint32_t index;
    uint32_t pairIndex;

    if ((p_digits == NULL) || (p_bcdOut == NULL))   return APP_BC95_AT_ERR_PARAM;
    if (digitCount != requiredDigits)               return APP_BC95_AT_ERR_RANGE;
    if (bcdSize < requiredBcdBytes)                 return APP_BC95_AT_ERR_PARAM;
    if (requiredBcdBytes > sizeof(nibble) / 2u)     return APP_BC95_AT_ERR_PARAM;

    for (index = 0u; index < digitCount; index++)
    {
        if ((p_digits[index] < '0') || (p_digits[index] > '9'))
            return APP_BC95_AT_ERR_FORMAT;
        nibble[index] = (uint8_t)(p_digits[index] - '0');
    }
    nibble[(requiredBcdBytes * 2u) - 1u] = APP_BC95_AT_BCD_PAD_NIBBLE;

    for (pairIndex = 0u; pairIndex < requiredBcdBytes; pairIndex++)
    {
        p_bcdOut[pairIndex] = (uint8_t)((nibble[2u * pairIndex] << 4u) |
                                        (nibble[(2u * pairIndex) + 1u] & 0x0Fu));
    }
    return APP_BC95_AT_OK;
}

/* ============================================================
 *  AT 응답 1차 분류
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtCheckResponse(const char *p_resp, int32_t *p_cmeErrOut)
{
    const char *p_cme;
    const char *p_ok;

    if (p_cmeErrOut != NULL) *p_cmeErrOut = 0;

    if ((p_resp == NULL) || (p_resp[0] == '\0'))
        return APP_BC95_AT_ERR_TIMEOUT;

    p_cme = strstr(p_resp, APP_BC95_AT_CME_ERROR_PREFIX);
    if (p_cme != NULL)
    {
        if (p_cmeErrOut != NULL)
        {
            const char *p_num = p_cme + APP_BC95_AT_CME_ERROR_PREFIX_LEN;
            while ((*p_num == ' ') || (*p_num == '\t')) p_num++;
            *p_cmeErrOut = (int32_t)atoi(p_num);
        }
        return APP_BC95_AT_ERR_CME_ERROR;
    }

    if (App_Bc95AtHasErrorToken(p_resp) == APP_TRUE)
        return APP_BC95_AT_ERR_AT_ERROR;

    p_ok = App_Bc95AtFindOkBoundary(p_resp);
    if (p_ok == NULL) return APP_BC95_AT_ERR_NO_OK;

    return APP_BC95_AT_OK;
}

/* ============================================================
 *  UART IT 기반 가변 길이 AT 응답 수신
 * ============================================================ */
static AppStatus_t App_Bc95AtUartReceiveResponse(UART_HandleTypeDef *p_huart,
                                                 uint8_t *p_buffer,
                                                 uint16_t bufferSize,
                                                 uint32_t timeoutMs,
                                                 uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint32_t lastRxTick;
    uint32_t termTick = 0u;
    uint16_t curLen;
    uint16_t lastCheckedLen = 0u;
    uint16_t safeLen;
    char     rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    AppBc95AtLineQueue_t lineQueue;
    uint8_t  termSeen = APP_FALSE;

    APP_RETURN_IF_FALSE((p_huart != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buffer != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufferSize > 1u), APP_STATUS_INVALID_PARAM);

    (void)memset(p_buffer, 0, bufferSize);
    if (p_rxLengthOut != NULL) *p_rxLengthOut = 0u;

    g_appBc95AtRxContext.p_huart        = p_huart;
    g_appBc95AtRxContext.p_buffer       = p_buffer;
    g_appBc95AtRxContext.bufferSize     = (uint16_t)(bufferSize - 1u);
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    __HAL_UART_CLEAR_FLAG(p_huart,
        UART_CLEAR_OREF | UART_CLEAR_FEF |
        UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(p_huart, &p_buffer[0], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(p_huart);
        return APP_STATUS_UART_RX_FAILED;
    }

    App_Bc95AtLineQueueReset(&lineQueue);
    startTick  = HAL_GetTick();
    lastRxTick = startTick;
    while (1)
    {
        APP_WWDGFeed();

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            curLen = g_appBc95AtRxContext.receivedLength;
            if (curLen >= g_appBc95AtRxContext.bufferSize)
            {
                curLen = g_appBc95AtRxContext.bufferSize;
            }
            if (curLen < bufferSize)
            {
                p_buffer[curLen] = (uint8_t)'\0';
            }
            App_Bc95AtFinishRxIt(p_huart);
            if (p_rxLengthOut != NULL) *p_rxLengthOut = curLen;
            return APP_STATUS_UART_RX_FAILED;
        }

        curLen = g_appBc95AtRxContext.receivedLength;
        if (curLen != lastCheckedLen)
        {
            lastCheckedLen = curLen;
            lastRxTick = HAL_GetTick();
            termSeen = APP_FALSE;
        }

        safeLen = curLen;
        if (safeLen >= g_appBc95AtRxContext.bufferSize)
        {
            safeLen = g_appBc95AtRxContext.bufferSize;
        }
        safeLen = App_Bc95AtCopySnapshot(p_buffer, safeLen, rxSnapshot, (uint16_t)sizeof(rxSnapshot));
        App_Bc95AtLineQueueFeed(&lineQueue, rxSnapshot, safeLen);

        if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
            (App_Bc95AtIsResponseTerminated((const uint8_t *)rxSnapshot, safeLen) == APP_TRUE))
        {
            if (termSeen != APP_TRUE)
            {
                termSeen = APP_TRUE;
                termTick = HAL_GetTick();
            }

            if (((HAL_GetTick() - lastRxTick) >= APP_BC95_AT_RX_IDLE_GAP_MS) &&
                ((HAL_GetTick() - termTick) >= APP_BC95_AT_RX_POST_TERM_WAIT_MS))
            {
                App_Bc95AtFinishRxIt(p_huart);
                if (safeLen < bufferSize)
                {
                    p_buffer[safeLen] = (uint8_t)'\0';
                }
                if (p_rxLengthOut != NULL) *p_rxLengthOut = safeLen;
                return APP_STATUS_OK;
            }
        }

        if (curLen >= g_appBc95AtRxContext.bufferSize)
        {
            App_Bc95AtFinishRxIt(p_huart);
            if (safeLen < bufferSize)
            {
                p_buffer[safeLen] = (uint8_t)'\0';
            }
            if (p_rxLengthOut != NULL) *p_rxLengthOut = safeLen;
            if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
                (App_Bc95AtIsResponseTerminated((const uint8_t *)rxSnapshot, safeLen) == APP_TRUE))
            {
                APP_LOGI("NBIOT", "Response completed at full buffer (rxLen=%u)",
                         (unsigned)safeLen);
                return APP_STATUS_OK;
            }
            return APP_STATUS_UART_TIMEOUT;
        }

        if ((HAL_GetTick() - startTick) >= timeoutMs)
        {
            App_Bc95AtFinishRxIt(p_huart);
            __HAL_UART_CLEAR_FLAG(p_huart,
                UART_CLEAR_OREF | UART_CLEAR_FEF |
                UART_CLEAR_NEF  | UART_CLEAR_PEF);
            if (safeLen < bufferSize)
            {
                p_buffer[safeLen] = (uint8_t)'\0';
            }
            if (p_rxLengthOut != NULL) *p_rxLengthOut = safeLen;

            if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
                (App_Bc95AtIsResponseTerminated((const uint8_t *)rxSnapshot, safeLen) == APP_TRUE))
            {
                APP_LOGI("NBIOT", "Late-detected response on timeout (rxLen=%u)",
                         (unsigned)safeLen);
                return APP_STATUS_OK;
            }
            return APP_STATUS_UART_TIMEOUT;
        }

        HAL_Delay(1u);
    }
}

/* ============================================================
 *  명령 송신 + 응답 수신
 * ============================================================ */
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

    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)p_cmd, cmdLen,
                                  APP_BC95_AT_TX_TIMEOUT_MS);
    APP_WWDGFeed();
    APP_RETURN_IF_FALSE((halStatus == HAL_OK), APP_STATUS_UART_TX_FAILED);

    status = App_Bc95AtUartReceiveResponse(APP_UART_NBIOT_HANDLE,
                                           p_rxBuf, rxBufSize,
                                           rxTimeoutMs, p_rxLengthOut);
    APP_WWDGFeed();
    return status;
}

/* ============================================================
 *  Ping / Boot wait
 * ============================================================ */
AppStatus_t App_Bc95AtPing(uint32_t timeoutMs)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int32_t cmeErr;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_PING,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   timeoutMs, &rxLen);
    if (status != APP_STATUS_OK) return status;

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK) return APP_STATUS_FATAL;
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtWaitForBoot(uint32_t bannerTimeoutMs)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint16_t curLen;
    uint16_t lastCheckedLen = 0u;
    uint16_t bufferCap;
    char     rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    AppBc95AtLineQueue_t lineQueue;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));
    bufferCap = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = bufferCap;
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE, &g_appBc95AtRxBuf[0], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    App_Bc95AtLineQueueReset(&lineQueue);
    startTick = HAL_GetTick();
    while (1)
    {
        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_RX_FAILED;
        }

        curLen = g_appBc95AtRxContext.receivedLength;
        if (curLen != lastCheckedLen)
        {
            if (curLen >= bufferCap)
            {
                curLen = bufferCap;
            }
            (void)App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, curLen, rxSnapshot, (uint16_t)sizeof(rxSnapshot));
            App_Bc95AtLineQueueFeed(&lineQueue, rxSnapshot, curLen);

            if ((App_Bc95AtLineQueueContainsSubstring(&lineQueue, APP_BC95_BOOT_BANNER) == APP_TRUE) ||
                (strstr(rxSnapshot, APP_BC95_BOOT_BANNER) != NULL))
            {
                uint32_t completeWaitStart = HAL_GetTick();
                while ((HAL_GetTick() - completeWaitStart) < 500u)
                {
                    curLen = g_appBc95AtRxContext.receivedLength;
                    if (curLen >= bufferCap)
                    {
                        curLen = bufferCap;
                    }
                    (void)App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, curLen, rxSnapshot, (uint16_t)sizeof(rxSnapshot));
                    App_Bc95AtLineQueueFeed(&lineQueue, rxSnapshot, curLen);
                    if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
                        (App_Bc95AtFindOkBoundary(rxSnapshot) != NULL)) break;
                    APP_WWDGFeed();
                    HAL_Delay(10u);
                }
                App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
                if (curLen < sizeof(g_appBc95AtRxBuf))
                {
                    g_appBc95AtRxBuf[curLen] = (uint8_t)'\0';
                }
                APP_LOGD("NBIOT", "Boot banner detected (len=%u)", (unsigned)curLen);
                return APP_STATUS_OK;
            }
            lastCheckedLen = curLen;
        }

        if (curLen >= bufferCap)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_TIMEOUT;
        }

        if ((HAL_GetTick() - startTick) >= bannerTimeoutMs)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_PEF);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_WWDGFeed();
        HAL_Delay(1u);
    }
}


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

    bannerWaitMs = (totalTimeoutMs > APP_BC95_BOOT_WAIT_BANNER_MS)
                       ? APP_BC95_BOOT_WAIT_BANNER_MS
                       : totalTimeoutMs;

    if (g_appNbiotCarrierContext.lastResetType == APP_NBIOT_CARRIER_RESET_SW)
    {
        APP_LOGI("NBIOT", "Skip boot banner wait after SW reset, probe with AT");
    }
    else
    {
        APP_LOGI("NBIOT", "Wait for boot banner up to %lums...", (unsigned long)bannerWaitMs);
        status = App_Bc95AtWaitForBoot(bannerWaitMs);
        if (status == APP_STATUS_OK)
        {
            bannerSeen = APP_TRUE;
            APP_LOGD("NBIOT", "Boot banner received");
        }
        else
        {
            APP_LOGI("NBIOT", "Boot banner not received, probe with AT");
        }
    }

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
            APP_LOGE("NBIOT", "Ready check exhausted retries (last=%d)", (int)status);
            return APP_STATUS_UART_TIMEOUT;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_BOOT_PING_INTERVAL_MS);
    }
}

/* ============================================================
 *  USIM 준비 대기
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

    g_appBc95UsimFatal = APP_FALSE;
    startTick = HAL_GetTick();
    APP_LOGI("NBIOT", "Wait for USIM ready (timeout=%lums)...", (unsigned long)timeoutMs);

    while (1)
    {
        attempt++;
        rxLen = 0u;
        st = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);

        if (st == APP_STATUS_OK)
        {
            atSt = App_Bc95AtExtractImsiString((const char *)g_appBc95AtRxBuf,
                                               imsiStr, sizeof(imsiStr));
            if (atSt == APP_BC95_AT_OK)
            {
                APP_LOGI("NBIOT", "USIM ready (attempt=%lu, elapsed=%lums)",
                         (unsigned long)attempt,
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }

            if (atSt == APP_BC95_AT_ERR_CME_ERROR)
            {
                cmeErr = 0;
                (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
                if ((cmeErr == 10) || (cmeErr == 13) || (cmeErr == 15) ||
                    (cmeErr == 16) || (cmeErr == 311) || (cmeErr == 313) ||
                    (cmeErr == 315) || (cmeErr == 317) || (cmeErr == 318))
                {
                    g_appBc95UsimFatal = APP_TRUE;
                    APP_LOGE("NBIOT", "USIM fatal (CME=%ld)", (long)cmeErr);
                    return APP_STATUS_FATAL;
                }
                APP_LOGI("NBIOT", "USIM not ready (CME=%ld, attempt=%lu)",
                         (long)cmeErr, (unsigned long)attempt);
            }
            else
            {
                APP_LOGD("NBIOT", "USIM not ready (parse=%s, attempt=%lu)",
                         App_Bc95AtGetStatusString(atSt), (unsigned long)attempt);
            }
        }
        else
        {
            APP_LOGI("NBIOT", "USIM probe UART issue (status=%d, attempt=%lu)",
                     (int)st, (unsigned long)attempt);
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= timeoutMs)
        {
            g_appBc95UsimFatal = APP_TRUE;
            APP_LOGE("NBIOT", "USIM fatal (timeout/no response, attempt=%lu, elapsed=%lums)",
                     (unsigned long)attempt, (unsigned long)elapsed);
            return APP_STATUS_FATAL;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_USIM_READY_POLL_MS);
    }
}

AppStatus_t App_Bc95AtProbeServiceReady(AppBc95ServiceReadyProbe_t *p_probe)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int32_t cmeErr = 0;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_probe != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(p_probe, 0, sizeof(*p_probe));
    p_probe->lastStatus = APP_STATUS_INVALID_PARAM;

    status = App_Bc95AtPing(APP_BC95_BOOT_PING_TIMEOUT_MS);
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "[[ReadyProbe]] AT ping fail (status=%d)", (int)status);
        return status;
    }
    p_probe->atReady = APP_TRUE;

    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CMEE_ON,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CMEE=1",
                                           "[[ReadyProbe]]");
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "[[ReadyProbe]] CMEE enable fail (status=%d)", (int)status);
        return status;
    }
    p_probe->cmeeReady = APP_TRUE;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "[[ReadyProbe]] IMSI probe UART issue (status=%d)", (int)status);
        return status;
    }

    atStatus = App_Bc95AtExtractImsiString((const char *)g_appBc95AtRxBuf,
                                           imsiStr,
                                           sizeof(imsiStr));
    if (atStatus == APP_BC95_AT_OK)
    {
        p_probe->usimReady = APP_TRUE;
        p_probe->lastStatus = APP_STATUS_OK;
        APP_LOGI("NBIOT", "[[ReadyProbe]] service ready (AT=1, CMEE=1, USIM=1)");
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_CME_ERROR)
    {
        (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
        p_probe->lastCmeError = cmeErr;
        p_probe->lastStatus = APP_STATUS_UART_TIMEOUT;
        APP_LOGI("NBIOT", "[[ReadyProbe]] service not ready yet (AT=1, CMEE=1, USIM=0, CME=%ld)",
                 (long)cmeErr);
        return APP_STATUS_UART_TIMEOUT;
    }

    p_probe->lastStatus = APP_STATUS_FATAL;
    APP_LOGI("NBIOT", "[[ReadyProbe]] IMSI parse fail (status=%s)",
             App_Bc95AtGetStatusString(atStatus));
    return APP_STATUS_FATAL;
}

static AppStatus_t App_Bc95AtWaitForServiceReady(uint32_t totalTimeoutMs)
{
    AppStatus_t status;
    uint32_t startTick;
    uint32_t elapsedMs;
    uint32_t remainingMs;
    AppBc95ServiceReadyProbe_t probe;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    startTick = HAL_GetTick();
    APP_LOGI("NBIOT", "Wait for service ready after boot banner (timeout=%lums, settle=%lums)...",
             (unsigned long)totalTimeoutMs,
             (unsigned long)APP_BC95_SERVICE_READY_SETTLE_MS);

    if (APP_BC95_SERVICE_READY_SETTLE_MS != 0u)
    {
        App_Bc95AtDelayWithFeed(APP_BC95_SERVICE_READY_SETTLE_MS);
    }

    elapsedMs = HAL_GetTick() - startTick;
    if (elapsedMs >= totalTimeoutMs)
    {
        APP_LOGE("NBIOT", "Service ready timeout before USIM probe (elapsed=%lums)",
                 (unsigned long)elapsedMs);
        g_appBc95UsimFatal = APP_TRUE;
        return APP_STATUS_FATAL;
    }

    while (1)
    {
        status = App_Bc95AtProbeServiceReady(&probe);
        if (status == APP_STATUS_OK)
        {
            APP_LOGI("NBIOT", "Service ready confirmed (elapsed=%lums)",
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }

        if ((probe.lastStatus == APP_STATUS_FATAL) ||
            ((probe.lastCmeError == 10) || (probe.lastCmeError == 13) ||
             (probe.lastCmeError == 15) || (probe.lastCmeError == 16) ||
             (probe.lastCmeError == 311) || (probe.lastCmeError == 313) ||
             (probe.lastCmeError == 315) || (probe.lastCmeError == 317) ||
             (probe.lastCmeError == 318)))
        {
            g_appBc95UsimFatal = APP_TRUE;
            APP_LOGE("NBIOT", "Service ready fatal (status=%d, cme=%ld)",
                     (int)probe.lastStatus,
                     (long)probe.lastCmeError);
            return APP_STATUS_FATAL;
        }

        elapsedMs = HAL_GetTick() - startTick;
        if (elapsedMs >= totalTimeoutMs)
        {
            g_appBc95UsimFatal = APP_TRUE;
            APP_LOGE("NBIOT", "Service ready timeout (elapsed=%lums, lastStatus=%d, cme=%ld)",
                     (unsigned long)elapsedMs,
                     (int)probe.lastStatus,
                     (long)probe.lastCmeError);
            return APP_STATUS_FATAL;
        }

        remainingMs = totalTimeoutMs - elapsedMs;
        if (remainingMs > APP_BC95_USIM_READY_POLL_MS)
        {
            App_Bc95AtDelayWithFeed(APP_BC95_USIM_READY_POLL_MS);
        }
        else
        {
            App_Bc95AtDelayWithFeed(remainingMs);
        }
    }
}

/* ============================================================
 *  IMEI / IMSI 추출
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtExtractImeiString(const char *p_resp, char *p_out, uint32_t outSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    uint32_t digitCount;

    if ((p_resp == NULL) || (p_out == NULL) || (outSize <= (uint32_t)APP_BC95_IMEI_DIGITS))
        return APP_BC95_AT_ERR_PARAM;
    p_out[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = App_Bc95AtFindCgsnPrefix(p_resp);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;

    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;

    digitCount = 0u;
    while ((*p_payload != '\0') && (digitCount <= (uint32_t)APP_BC95_IMEI_DIGITS))
    {
        if ((*p_payload >= '0') && (*p_payload <= '9'))
        {
            if (digitCount < (uint32_t)APP_BC95_IMEI_DIGITS)
                p_out[digitCount] = *p_payload;
            digitCount++;
        }
        else if ((*p_payload == '\r') || (*p_payload == '\n'))
        {
            break;
        }
        p_payload++;
    }

    if (digitCount > (uint32_t)APP_BC95_IMEI_DIGITS)
    {
        p_out[APP_BC95_IMEI_DIGITS] = '\0';
        return APP_BC95_AT_ERR_RANGE;
    }
    p_out[digitCount] = '\0';
    if (digitCount != (uint32_t)APP_BC95_IMEI_DIGITS) return APP_BC95_AT_ERR_RANGE;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtImeiToBcd(const char *p_imeiStr, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    if ((p_imeiStr == NULL) || (p_bcdOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    if (strlen(p_imeiStr) != (size_t)APP_BC95_IMEI_DIGITS) return APP_BC95_AT_ERR_RANGE;
    return App_Bc95AtDigitsToBcd(p_imeiStr, (uint32_t)APP_BC95_IMEI_DIGITS,
                                 p_bcdOut, bcdSize,
                                 (uint32_t)APP_BC95_IMEI_DIGITS,
                                 (uint32_t)APP_BC95_IMEI_BCD_BYTES);
}

AppBc95AtStatus_t App_Bc95AtParseImeiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    AppBc95AtStatus_t status;
    char imeiStr[APP_BC95_IMEI_DIGITS + 1u];

    if ((p_bcdOut == NULL) || (bcdSize < (uint32_t)APP_BC95_IMEI_BCD_BYTES))
        return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_bcdOut, 0, APP_BC95_IMEI_BCD_BYTES);

    status = App_Bc95AtExtractImeiString(p_resp, imeiStr, sizeof(imeiStr));
    if (status != APP_BC95_AT_OK) return status;

    status = App_Bc95AtImeiToBcd(imeiStr, p_bcdOut, bcdSize);
    if (status == APP_BC95_AT_OK)
        (void)memcpy(g_appBc95AtImeiBcd, p_bcdOut, APP_BC95_IMEI_BCD_BYTES);
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
        return APP_BC95_AT_ERR_PARAM;
    p_out[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

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
                p_lineStart++;

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

    if (p_digitStart == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    if (digitLen != (uint32_t)APP_BC95_IMSI_DIGITS) return APP_BC95_AT_ERR_RANGE;

    (void)memcpy(p_out, p_digitStart, APP_BC95_IMSI_DIGITS);
    p_out[APP_BC95_IMSI_DIGITS] = '\0';
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtImsiToBcd(const char *p_imsiStr, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    if ((p_imsiStr == NULL) || (p_bcdOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    if (strlen(p_imsiStr) != (size_t)APP_BC95_IMSI_DIGITS) return APP_BC95_AT_ERR_RANGE;
    return App_Bc95AtDigitsToBcd(p_imsiStr, (uint32_t)APP_BC95_IMSI_DIGITS,
                                 p_bcdOut, bcdSize,
                                 (uint32_t)APP_BC95_IMSI_DIGITS,
                                 (uint32_t)APP_BC95_IMSI_BCD_BYTES);
}

AppBc95AtStatus_t App_Bc95AtParseImsiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    AppBc95AtStatus_t status;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    if ((p_bcdOut == NULL) || (bcdSize < (uint32_t)APP_BC95_IMSI_BCD_BYTES))
        return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_bcdOut, 0, APP_BC95_IMSI_BCD_BYTES);

    status = App_Bc95AtExtractImsiString(p_resp, imsiStr, sizeof(imsiStr));
    if (status != APP_BC95_AT_OK) return status;

    status = App_Bc95AtImsiToBcd(imsiStr, p_bcdOut, bcdSize);
    if (status == APP_BC95_AT_OK)
        (void)memcpy(g_appBc95AtImsiBcd, p_bcdOut, APP_BC95_IMSI_BCD_BYTES);
    return status;
}

/* ============================================================
 *  Fetch IMEI/IMSI (단일/재시도)
 * ============================================================ */
AppStatus_t App_Bc95AtFetchImei(uint8_t *p_imeiBcd, uint32_t bcdSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_imeiBcd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bcdSize >= (uint32_t)APP_BC95_IMEI_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMEI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK) return status;

    atStatus = App_Bc95AtParseImeiToBcd((const char *)g_appBc95AtRxBuf, p_imeiBcd, bcdSize);
    if (atStatus != APP_BC95_AT_OK) return APP_STATUS_FATAL;
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchImsi(uint8_t *p_imsiBcd, uint32_t bcdSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_imsiBcd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bcdSize >= (uint32_t)APP_BC95_IMSI_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK) return status;

    atStatus = App_Bc95AtParseImsiToBcd((const char *)g_appBc95AtRxBuf, p_imsiBcd, bcdSize);
    if (atStatus != APP_BC95_AT_OK) return APP_STATUS_FATAL;
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchImeiWithRetry(uint8_t *p_imeiBcd, uint32_t bcdSize, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchImei(p_imeiBcd, bcdSize);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;

        APP_LOGI("NBIOT", "IMEI retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);

        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_FETCH_RETRY_DELAY_MS);
    }
    return st;
}

AppStatus_t App_Bc95AtFetchImsiWithRetry(uint8_t *p_imsiBcd, uint32_t bcdSize, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchImsi(p_imsiBcd, bcdSize);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;

        APP_LOGI("NBIOT", "IMSI retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);

        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_FETCH_RETRY_DELAY_MS);
    }
    return st;
}

/* ============================================================
 *  Getter / 문자열 변환
 * ============================================================ */
const uint8_t          *App_Bc95AtGetImeiBcd(void)     { return g_appBc95AtImeiBcd; }
const uint8_t          *App_Bc95AtGetImsiBcd(void)     { return g_appBc95AtImsiBcd; }
const AppBc95Quality_t *App_Bc95AtGetQuality(void)     { return &g_appBc95AtQuality; }
const uint8_t          *App_Bc95AtGetQualityBcd(void)  { return g_appBc95AtQualityBcd; }

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

/* ============================================================
 *  UART ISR Callbacks
 * ============================================================ */
void App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
    HAL_StatusTypeDef halStatus;
    uint16_t nextIndex;

    if ((p_huart == NULL) || (g_appBc95AtRxContext.active != APP_TRUE)) return;
    if (p_huart != g_appBc95AtRxContext.p_huart) return;

    nextIndex = (uint16_t)(g_appBc95AtRxContext.receivedLength + 1u);
    g_appBc95AtRxContext.receivedLength = nextIndex;

    if (nextIndex >= g_appBc95AtRxContext.bufferSize) return;

    /* active 한 번 더 확인 (메인 루프에서 finish 한 경우 IT 재등록 금지) */
    if (g_appBc95AtRxContext.active != APP_TRUE) return;

    halStatus = HAL_UART_Receive_IT(p_huart,
                                    &g_appBc95AtRxContext.p_buffer[nextIndex], 1u);
    if (halStatus != HAL_OK)
    {
        g_appBc95AtRxContext.error = APP_TRUE;
    }
}

void App_Bc95AtOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
    if ((p_huart == NULL) || (g_appBc95AtRxContext.active != APP_TRUE)) return;
    if (p_huart != g_appBc95AtRxContext.p_huart) return;

#ifdef DEBUG
    g_appBc95AtRxErrorInfo.errorCode    = p_huart->ErrorCode;
    g_appBc95AtRxErrorInfo.errorAtIndex = g_appBc95AtRxContext.receivedLength;
    g_appBc95AtRxErrorInfo.errorCount++;
#endif

    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
    g_appBc95AtRxContext.error = APP_TRUE;
}

/* ============================================================
 *  Quality (NUESTATS=CELL)
 * ============================================================ */
static int16_t App_Bc95AtCentibelToDbm(int32_t centibel)
{
    int32_t result;
    if (centibel >= 0) result = (centibel + 5) / 10;
    else               result = (centibel - 5) / 10;
    return (int16_t)result;
}

static uint16_t App_Bc95AtDbmToAbsU16(int16_t dbm)
{
    int32_t absVal = (dbm >= 0) ? 0 : (int32_t)(-dbm);
    if (absVal > APP_BC95_AT_DBM_ABS_MAX) absVal = APP_BC95_AT_DBM_ABS_MAX;
    return (uint16_t)absVal;
}

static uint8_t App_Bc95AtDbmToAbsByte(int16_t dbm)
{
    return (uint8_t)App_Bc95AtDbmToAbsU16(dbm);
}

AppBc95AtStatus_t App_Bc95AtParseNuestatsCell(const char *p_resp, AppBc95Quality_t *p_quality)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_scan;
    const char *p_lineStart;
    const char *p_picked = NULL;
    int parsedFields;
    int earfcn = 0, pci = 0, primary = 0;
    int rsrpCb = 0, rsrqCb = 0, rssiCb = 0, snrCb = 0;

    if ((p_resp == NULL) || (p_quality == NULL)) return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_quality, 0, sizeof(*p_quality));

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if ((status != APP_BC95_AT_OK) && (status != APP_BC95_AT_ERR_NO_OK)) return status;

    p_scan = p_resp;
    while ((p_lineStart = strstr(p_scan, APP_BC95_AT_NUESTATS_CELL_PREFIX)) != NULL)
    {
        const char *p_payload = p_lineStart + APP_BC95_AT_NUESTATS_CELL_PREFIX_LEN;
        parsedFields = sscanf(p_payload, "%d,%d,%d,%d,%d,%d,%d",
                              &earfcn, &pci, &primary,
                              &rsrpCb, &rsrqCb, &rssiCb, &snrCb);
        if (parsedFields == (int)APP_BC95_AT_NUESTATS_CELL_FIELDS)
        {
            if (p_picked == NULL) p_picked = p_payload;
            if (primary == 1) { p_picked = p_payload; break; }
        }
        p_scan = p_payload;
    }

    if (p_picked == NULL) return APP_BC95_AT_ERR_NO_DATA;

    parsedFields = sscanf(p_picked, "%d,%d,%d,%d,%d,%d,%d",
                          &earfcn, &pci, &primary,
                          &rsrpCb, &rsrqCb, &rssiCb, &snrCb);
    if (parsedFields != (int)APP_BC95_AT_NUESTATS_CELL_FIELDS) return APP_BC95_AT_ERR_FORMAT;

    p_quality->earfcn      = (uint16_t)((earfcn < 0) ? 0 : (earfcn & 0xFFFF));
    p_quality->pci         = (uint16_t)((pci    < 0) ? 0 : (pci    & 0xFFFF));
    p_quality->primaryCell = (uint8_t)((primary == 1) ? 1u : 0u);
    p_quality->rsrpDbm     = App_Bc95AtCentibelToDbm((int32_t)rsrpCb);
    p_quality->rsrqDbm     = App_Bc95AtCentibelToDbm((int32_t)rsrqCb);
    p_quality->rssiDbm     = App_Bc95AtCentibelToDbm((int32_t)rssiCb);
    p_quality->snrDb       = App_Bc95AtCentibelToDbm((int32_t)snrCb);
    p_quality->ber         = 0u;
    p_quality->valid       = APP_TRUE;
    return APP_BC95_AT_OK;
}

AppStatus_t App_Bc95AtQualityToBcd(AppBc95Quality_t *pQuality, uint8_t *p_buf, uint32_t bufSize)
{
    uint16_t rsrpAbs, rsrqAbs;
    int16_t  snrVal;

    APP_RETURN_IF_FALSE((pQuality != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufSize >= (uint32_t)APP_BC95_QUALITY_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    (void)memset(p_buf, 0, APP_BC95_QUALITY_BCD_BYTES);
    p_buf[0] = App_Bc95AtDbmToAbsByte(pQuality->rssiDbm);
    p_buf[1] = pQuality->ber;
    p_buf[2] = (uint8_t)(pQuality->pci & 0xFFu);
    p_buf[3] = (uint8_t)((pQuality->pci >> 8u) & 0xFFu);
    rsrpAbs = App_Bc95AtDbmToAbsU16(pQuality->rsrpDbm);
    p_buf[4] = (uint8_t)(rsrpAbs & 0xFFu);
    p_buf[5] = (uint8_t)((rsrpAbs >> 8u) & 0xFFu);
    rsrqAbs = App_Bc95AtDbmToAbsU16(pQuality->rsrqDbm);
    p_buf[6] = (uint8_t)(rsrqAbs & 0xFFu);
    p_buf[7] = (uint8_t)((rsrqAbs >> 8u) & 0xFFu);
    snrVal = pQuality->snrDb;
    p_buf[8] = (uint8_t)((uint16_t)snrVal & 0xFFu);
    p_buf[9] = (uint8_t)(((uint16_t)snrVal >> 8u) & 0xFFu);
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf, uint32_t bcdBufSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_quality != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_quality, 0, sizeof(*p_quality));

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NUESTATS_CELL,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_UART_TIMEOUT))
    {
        APP_LOGE("NBIOT", "NUESTATS send fail (status=%d, rxLen=%u)",
                 (int)status, (unsigned)rxLen);
        return status;
    }

    if (status == APP_STATUS_UART_TIMEOUT)
    {
        /* 부분 데이터라도 NUESTATS:CELL 라인이 있으면 파싱 가능 */
        if (rxLen < 30u)
        {
            /* 너무 짧으면 파싱 불가 */
            APP_LOGE("NBIOT", "NUESTATS timeout, too short (rxLen=%u)",
                     (unsigned)rxLen);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_LOGI("NBIOT", "NUESTATS timeout but data present (rxLen=%u), try parse",
                 (unsigned)rxLen);
        /* OK 토큰이 없을 수 있으므로 ParseNuestatsCell 가 통과할지 확인 */
    }

    atStatus = App_Bc95AtParseNuestatsCell((const char *)g_appBc95AtRxBuf, p_quality);
    if (atStatus == APP_BC95_AT_OK)
    {
        (void)memcpy(&g_appBc95AtQuality, p_quality, sizeof(g_appBc95AtQuality));

        if (p_bcdBuf != NULL)
        {
            status = App_Bc95AtQualityToBcd(p_quality, p_bcdBuf, bcdBufSize);
            if (status != APP_STATUS_OK)
            {
                APP_LOGE("NBIOT", "quality to bcd failed");
                return APP_STATUS_FATAL;
            }
        }
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_NO_DATA)
    {
        APP_LOGI("NBIOT", "NUESTATS: no cell info yet (not camped)");
        return APP_STATUS_UART_TIMEOUT;   /* 일시 실패 */
    }

    APP_LOGE("NBIOT", "NUESTATS parse fail (status=%d, rxLen=%u)",
             (int)atStatus, (unsigned)rxLen);
    return APP_STATUS_FATAL;
}

AppStatus_t App_Bc95AtFetchQualityWithRetry(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf,
                                            uint32_t bcdBufSize, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchQuality(p_quality, p_bcdBuf, bcdBufSize);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;
        if (st == APP_STATUS_FATAL) return st;   /* 영구 실패는 즉시 */

        APP_LOGW("NBIOT", "Quality retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);

        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_FETCH_RETRY_DELAY_MS);
    }
    return st;
}

/* ============================================================
 *  Print helpers
 * ============================================================ */
#define APP_BC95_PRINT_TEMP_LEN     (64u)

static void App_Bc95PrintBcd(const char *p_label, const uint8_t *p_bcd, uint32_t length)
{
    char tempChar[APP_BC95_PRINT_TEMP_LEN];
    uint32_t index, offset = 0u, remaining;
    int written;

    if ((p_label == NULL) || (p_bcd == NULL) || (length == 0u)) return;
    tempChar[0] = '\0';

    for (index = 0u; index < length; index++)
    {
        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u)) break;
        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        written = snprintf(&tempChar[offset], remaining, "%02X%s",
                           p_bcd[index],
                           (index < (length - 1u)) ? "-" : "");
        if (written <= 0) break;
        if ((uint32_t)written >= remaining)
        {
            offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u;
            break;
        }
        offset += (uint32_t)written;
    }
    APP_LOGI("NBIOT", "%s BCD: %s", p_label, tempChar);
}

static void App_Bc95PrintDecoded(const char *p_label, const uint8_t *p_bcd, uint32_t length)
{
    char tempChar[APP_BC95_PRINT_TEMP_LEN];
    uint32_t index, offset = 0u, remaining;
    uint8_t highNibble, lowNibble;
    int written;

    if ((p_label == NULL) || (p_bcd == NULL) || (length == 0u)) return;
    tempChar[0] = '\0';

    for (index = 0u; index < length; index++)
    {
        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u)) break;

        highNibble = (uint8_t)((p_bcd[index] >> 4u) & 0x0Fu);
        lowNibble  = (uint8_t)(p_bcd[index] & 0x0Fu);

        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        if (highNibble <= 9u)
            written = snprintf(&tempChar[offset], remaining, "%u", (unsigned)highNibble);
        else if (highNibble != 0x0Fu)
            written = snprintf(&tempChar[offset], remaining, "?");
        else
            written = 0;
        if (written < 0) break;
        if ((uint32_t)written >= remaining)
        { offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u; break; }
        offset += (uint32_t)written;

        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u)) break;
        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        if (lowNibble <= 9u)
            written = snprintf(&tempChar[offset], remaining, "%u", (unsigned)lowNibble);
        else if (lowNibble != 0x0Fu)
            written = snprintf(&tempChar[offset], remaining, "?");
        else
            written = 0;
        if (written < 0) break;
        if ((uint32_t)written >= remaining)
        { offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u; break; }
        offset += (uint32_t)written;
    }
    APP_LOGI("NBIOT", "%s     : %s", p_label, tempChar);
}

static void App_NBIoTPrintQuality(const AppBc95Quality_t *p_q)
{
    if (p_q == NULL) return;
    APP_LOGI("NBIOT", "EARFCN: %u, PCI: %u, Primary: %u",
             (unsigned)p_q->earfcn, (unsigned)p_q->pci, (unsigned)p_q->primaryCell);
    APP_LOGI("NBIOT", "RSSI : %d dBm", (int)p_q->rssiDbm);
    APP_LOGI("NBIOT", "RSRP : %d dBm", (int)p_q->rsrpDbm);
    APP_LOGI("NBIOT", "RSRQ : %d dB",  (int)p_q->rsrqDbm);
    APP_LOGI("NBIOT", "SNR  : %d dB",  (int)p_q->snrDb);
    APP_LOGI("NBIOT", "BER  : %u (reserved)", (unsigned)p_q->ber);
}

/* ============================================================
 *  네트워크 응답 파서
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtParseCfun(const char *p_resp, int32_t *p_funOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedFun;

    if ((p_resp == NULL) || (p_funOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    *p_funOut = 0;
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CFUN_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CFUN_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;
    if (sscanf(p_payload, "%d", &parsedFun) != 1) return APP_BC95_AT_ERR_FORMAT;
    *p_funOut = (int32_t)parsedFun;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCgatt(const char *p_resp, int32_t *p_stateOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedState;

    if ((p_resp == NULL) || (p_stateOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    *p_stateOut = 0;
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CGATT_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CGATT_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;
    if (sscanf(p_payload, "%d", &parsedState) != 1) return APP_BC95_AT_ERR_FORMAT;
    *p_stateOut = (int32_t)parsedState;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCereg(const char *p_resp, int32_t *p_nOut, int32_t *p_statOut, int32_t *p_rejectCauseOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    const char *p_lineEnd;
    const char *p_lastComma;
    int parsedN, parsedStat;

    if ((p_resp == NULL) || (p_statOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    if (p_nOut != NULL) *p_nOut = 0;
    if (p_rejectCauseOut != NULL) *p_rejectCauseOut = -1;
    *p_statOut = 0;
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CEREG_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CEREG_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '	')) p_payload++;
    if (sscanf(p_payload, "%d,%d", &parsedN, &parsedStat) != 2) return APP_BC95_AT_ERR_FORMAT;
    if (p_nOut != NULL) *p_nOut = (int32_t)parsedN;
    *p_statOut = (int32_t)parsedStat;

    if ((p_rejectCauseOut != NULL) && (parsedStat == APP_BC95_CEREG_DENIED))
    {
        p_lineEnd = p_payload;
        while ((*p_lineEnd != '\0') && (*p_lineEnd != ' ') && (*p_lineEnd != ' '))
        {
            p_lineEnd++;
        }

        p_lastComma = p_lineEnd;
        while ((p_lastComma > p_payload) && (*p_lastComma != ','))
        {
            p_lastComma--;
        }

        if ((*p_lastComma == ',') && ((p_lastComma + 1) < p_lineEnd))
        {
            char *p_endptr = NULL;
            long parsedReject = strtol(p_lastComma + 1, &p_endptr, 10);
            if ((p_endptr != (p_lastComma + 1)) && (parsedReject >= 0L) && (parsedReject <= 65535L))
            {
                *p_rejectCauseOut = (int32_t)parsedReject;
            }
        }
    }

    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCgpaddr(const char *p_resp, char *p_ipOut, uint32_t ipBufSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    const char *p_comma;
    uint32_t i;

    if ((p_resp == NULL) || (p_ipOut == NULL) || (ipBufSize == 0u)) return APP_BC95_AT_ERR_PARAM;
    p_ipOut[0] = '\0';
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CGPADDR_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CGPADDR_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;

    p_comma = strchr(p_payload, ',');
    if (p_comma == NULL) return APP_BC95_AT_OK;  /* IP 미할당 */
    p_comma++;
    while ((*p_comma == ' ') || (*p_comma == '\t') || (*p_comma == '"')) p_comma++;

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
 *  명령 송신 (UART 일시 오류 재시도)
 * ============================================================ */
static AppStatus_t App_Bc95AtSendWithRetry(const char *p_cmd, uint8_t *p_rxBuf,
                                           uint16_t rxBufSize, uint32_t rxTimeoutMs,
                                           uint16_t *p_rxLenOut, uint32_t maxRetry)
{
    AppStatus_t status = APP_STATUS_FATAL;
    uint32_t attempt;

    for (attempt = 0u; attempt < maxRetry; attempt++)
    {
        status = App_Bc95AtSendCommand(p_cmd, p_rxBuf, rxBufSize, rxTimeoutMs, p_rxLenOut);
        if (status == APP_STATUS_OK) return APP_STATUS_OK;

        if ((status == APP_STATUS_UART_RX_FAILED) ||
            (status == APP_STATUS_UART_TX_FAILED) ||
            (status == APP_STATUS_UART_TIMEOUT))
        {
            APP_LOGW("NBIOT", "cmd(%s) retry %lu/%lu (status=%d)",
                     p_cmd,
                     (unsigned long)(attempt + 1u),
                     (unsigned long)maxRetry, (int)status);
            App_Bc95AtDelayWithFeed(APP_BC95_NET_CMD_RETRY_DELAY_MS);
            continue;
        }
        break;
    }
    return status;
}

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
                                   APP_BC95_AT_CFUN_RESP_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "CFUN=1 send fail (status=%d)", (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
#ifdef NBIoT_SIMULATION_CODE
    APP_LOGI("NBIOT", "Forcely err timeout CFUN=1 test..."); // NBIoT simulation test
    atStatus = APP_BC95_AT_ERR_TIMEOUT;
#endif // NBIoT_SIMULATION_CODE
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "CFUN=1 not OK (parse=%s)",
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }
    APP_LOGI("NBIOT", "CFUN=1 done");
    return APP_STATUS_OK;
}

static uint8_t App_Bc95AtIsSuspendRejectCause(int32_t rejectCause)
{
    return (rejectCause == APP_NBIOT_REJECT_CAUSE_SUSPEND) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_Bc95AtIsTerminatedRejectCause(int32_t rejectCause)
{
    return ((rejectCause == APP_NBIOT_REJECT_CAUSE_TERMINATED_1) ||
            (rejectCause == APP_NBIOT_REJECT_CAUSE_TERMINATED_2) ||
            (rejectCause == APP_NBIOT_REJECT_CAUSE_TERMINATED_3)) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_Bc95AtIsUsimFatalCme(int32_t cmeErr)
{
    switch (cmeErr)
    {
        case 10: case 13: case 15: case 16:
        case 311: case 313: case 315: case 317: case 318:
            return APP_TRUE;
        default:
            return APP_FALSE;
    }
}

AppStatus_t App_Bc95AtQueryNetStatus(AppBc95NetStatus_t *p_status)
{
    AppStatus_t st;
    AppBc95AtStatus_t atSt;
    uint16_t rxLen;
    int32_t funVal = -1, cgattVal = -1;
    int32_t ceregN = -1, ceregStat = -1;
    int32_t rejectCause = -1;
    int32_t cmeErr = 0;

    APP_RETURN_IF_FALSE((p_status != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_status, 0, sizeof(*p_status));
    p_status->phase      = APP_BC95_NET_PHASE_INIT;
    p_status->cfunValue  = 0xFFu;
    p_status->cgattState = 0xFFu;
    p_status->ceregStat  = APP_BC95_CEREG_NOT_REGISTERED;
    p_status->rejectCause = 0u;
    p_status->rejectCauseValid = APP_FALSE;

    /* CFUN? */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CFUN_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCfun((const char *)g_appBc95AtRxBuf, &funVal);
        if (atSt == APP_BC95_AT_OK) p_status->cfunValue = (uint8_t)funVal;
        else if (atSt == APP_BC95_AT_ERR_CME_ERROR)
        {
            cmeErr = 0;
            (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
            if (App_Bc95AtIsUsimFatalCme(cmeErr) == APP_TRUE)
            {
                APP_LOGE("NBIOT", "CFUN? USIM fatal (CME=%ld)", (long)cmeErr);
                p_status->phase = APP_BC95_NET_PHASE_USIM_ERROR;
                p_status->lastUpdateTick = HAL_GetTick();
                return APP_STATUS_FATAL;
            }
        }
    }

    if ((funVal >= 0) && (funVal != 1))
    {
        p_status->phase = APP_BC95_NET_PHASE_CFUN_OFF;
        p_status->lastUpdateTick = HAL_GetTick();
        return APP_STATUS_OK;
    }

    /* CEREG? */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CEREG_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCereg((const char *)g_appBc95AtRxBuf, &ceregN, &ceregStat, &rejectCause);
#ifdef NBIoT_SIMULATION_CODE
        APP_LOGI("NBIOT", "Forcely err timeout CEREG:0 test..."); // NBIoT simulation test
        atSt = APP_BC95_AT_ERR_FORMAT;
#endif // NBIoT_SIMULATION_CODE
        if (atSt == APP_BC95_AT_OK)
        {
            p_status->ceregStat = (AppBc95CeregStat_t)ceregStat;
            if ((rejectCause >= 0) && (rejectCause <= 65535))
            {
                p_status->rejectCause = (uint16_t)rejectCause;
                p_status->rejectCauseValid = APP_TRUE;
            }
        }
    }

    /* CGATT? */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CGATT_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCgatt((const char *)g_appBc95AtRxBuf, &cgattVal);
#ifdef NBIoT_SIMULATION_CODE
        APP_LOGI("NBIOT", "Forcely err timeout CGATT:0 test..."); // NBIoT simulation test
        atSt = APP_BC95_AT_ERR_FORMAT;
#endif // NBIoT_SIMULATION_CODE
        if (atSt == APP_BC95_AT_OK)
            p_status->cgattState = (uint8_t)cgattVal;
    }

    /* CGPADDR (등록 시에만) */
    if ((p_status->ceregStat == APP_BC95_CEREG_REGISTERED_HOME) ||
        (p_status->ceregStat == APP_BC95_CEREG_REGISTERED_ROAM))
    {
        rxLen = 0u;
        st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CGPADDR_QUERY,
                                     g_appBc95AtRxBuf,
                                     (uint16_t)sizeof(g_appBc95AtRxBuf),
                                     APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                     APP_BC95_NET_CMD_RETRY_MAX);
        if (st == APP_STATUS_OK)
        {
            atSt = App_Bc95AtParseCgpaddr((const char *)g_appBc95AtRxBuf,
                                          p_status->ipAddr,
                                          (uint32_t)sizeof(p_status->ipAddr));
            /* 패치: "0.0.0.0" 거르기 */
            if ((atSt == APP_BC95_AT_OK) &&
                (p_status->ipAddr[0] != '\0') &&
                (strcmp(p_status->ipAddr, "0.0.0.0") != 0))
            {
                p_status->hasIp = APP_TRUE;
            }
        }
    }

    /* phase 종합 */
    if ((p_status->cfunValue != 0xFFu) && (p_status->cfunValue != 1u))
        p_status->phase = APP_BC95_NET_PHASE_CFUN_OFF;
    else if ((p_status->ceregStat == APP_BC95_CEREG_NOT_REGISTERED) ||
             (p_status->ceregStat == APP_BC95_CEREG_SEARCHING) ||
             (p_status->ceregStat == APP_BC95_CEREG_UNKNOWN))
        p_status->phase = APP_BC95_NET_PHASE_REGISTERING;
    else if (p_status->ceregStat == APP_BC95_CEREG_DENIED)
    {
        if ((p_status->rejectCauseValid == APP_TRUE) &&
            (App_Bc95AtIsSuspendRejectCause((int32_t)p_status->rejectCause) == APP_TRUE))
        {
            p_status->phase = APP_BC95_NET_PHASE_USIM_SUSPEND;
        }
        else if ((p_status->rejectCauseValid == APP_TRUE) &&
                 (App_Bc95AtIsTerminatedRejectCause((int32_t)p_status->rejectCause) == APP_TRUE))
        {
            p_status->phase = APP_BC95_NET_PHASE_USIM_TERMINATED;
        }
        else
        {
            p_status->phase = APP_BC95_NET_PHASE_DENIED;
        }
    }
    else if ((p_status->ceregStat == APP_BC95_CEREG_REGISTERED_HOME) ||
             (p_status->ceregStat == APP_BC95_CEREG_REGISTERED_ROAM))
    {
        if (p_status->cgattState != 1u)        p_status->phase = APP_BC95_NET_PHASE_ATTACHING;
        else if (p_status->hasIp != APP_TRUE)  p_status->phase = APP_BC95_NET_PHASE_WAITING_IP;
        else { p_status->phase = APP_BC95_NET_PHASE_READY; p_status->ready = APP_TRUE; }
    }
    else
        p_status->phase = APP_BC95_NET_PHASE_REGISTERING;

    p_status->lastUpdateTick = HAL_GetTick();
    (void)memcpy(&g_appBc95AtNetStatus, p_status, sizeof(g_appBc95AtNetStatus));
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtWaitForNetwork(uint32_t totalTimeoutMs, AppBc95NetStatus_t *p_status)
{
    AppStatus_t st;
    AppBc95NetStatus_t snapshot;
    uint32_t startTick;
    uint32_t elapsed;
    uint32_t pollCount   = 0u;
    uint32_t deniedCount = 0u;
    uint8_t  cfunFixDone = APP_FALSE;
    AppBc95NetPhase_t lastPhase = APP_BC95_NET_PHASE_INIT;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    if (p_status != NULL) (void)memset(p_status, 0, sizeof(*p_status));
    (void)memset(&snapshot, 0, sizeof(snapshot));

    startTick = HAL_GetTick();
    APP_LOGI("NBIOT", "Wait for network (timeout=%lums)...", (unsigned long)totalTimeoutMs);

    while (1)
    {
        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= totalTimeoutMs)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " Network wait timeout (poll=%lu, lastPhase=%s, elapsed=%lums)",
                     (unsigned long)pollCount,
                     App_Bc95AtGetNetPhaseString(lastPhase),
                     (unsigned long)elapsed);
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_UART_TIMEOUT;
        }

        st = App_Bc95AtQueryNetStatus(&snapshot);
        snapshot.pollCount   = ++pollCount;
        snapshot.deniedCount = deniedCount;

        if (st == APP_STATUS_FATAL)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " Net query fatal (phase=%s)",
                     App_Bc95AtGetNetPhaseString(snapshot.phase));
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_FATAL;
        }

        if (st != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "poll #%lu: query failed (%d)",
                     (unsigned long)pollCount, (int)st);
            App_Bc95AtDelayWithFeed(APP_BC95_NET_POLL_INTERVAL_MS);
            continue;
        }

        if (snapshot.phase != lastPhase)
        {
            APP_LOGD("NBIOT", "poll #%lu: phase=%s, CFUN=%u, CEREG=%s, CGATT=%u, rejectCause=%u, IP='%s'",
                     (unsigned long)pollCount,
                     App_Bc95AtGetNetPhaseString(snapshot.phase),
                     (unsigned)snapshot.cfunValue,
                     App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                     (unsigned)snapshot.cgattState,
                     (unsigned)snapshot.rejectCause,
                     snapshot.ipAddr);
            lastPhase = snapshot.phase;
        }

        if (snapshot.phase == APP_BC95_NET_PHASE_READY)
        {
            APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " Network ready (elapsed=%lums, ip=%s, poll=%lu)",
                     (unsigned long)elapsed, snapshot.ipAddr, (unsigned long)pollCount);
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_OK;
        }

        if (snapshot.phase == APP_BC95_NET_PHASE_USIM_ERROR)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " USIM permanent error, give up");
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_FATAL;
        }

        if (snapshot.phase == APP_BC95_NET_PHASE_USIM_SUSPEND)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " USIM suspended by network (rejectCause=%u)",
                     (unsigned)snapshot.rejectCause);
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_FATAL;
        }

        if (snapshot.phase == APP_BC95_NET_PHASE_USIM_TERMINATED)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " USIM terminated by network (rejectCause=%u)",
                     (unsigned)snapshot.rejectCause);
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_FATAL;
        }

        if (snapshot.phase == APP_BC95_NET_PHASE_CFUN_OFF)
        {
            if (cfunFixDone != APP_TRUE)
            {
                cfunFixDone = APP_TRUE;
                APP_LOGD("NBIOT", "CFUN=0, send AT+CFUN=1");
                (void)App_Bc95AtSetFullFunction();
            }
        }
        else if (snapshot.phase == APP_BC95_NET_PHASE_DENIED)
        {
            deniedCount++;
            if (deniedCount >= APP_BC95_NET_DENIED_RETRY_MAX)
            {
                APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                    " Registration denied %lu times, give up",
                         (unsigned long)deniedCount);
                if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                return APP_STATUS_FATAL;
            }
            APP_LOGD("NBIOT", "Denied (%lu/%lu), keep polling",
                     (unsigned long)deniedCount,
                     (unsigned long)APP_BC95_NET_DENIED_RETRY_MAX);
        }

        App_Bc95AtDelayWithFeed(APP_BC95_NET_POLL_INTERVAL_MS);
    }
}

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
        case APP_BC95_NET_PHASE_READY:           return "ready";
        case APP_BC95_NET_PHASE_DENIED:          return "denied";
        case APP_BC95_NET_PHASE_USIM_SUSPEND:    return "usim_suspend";
        case APP_BC95_NET_PHASE_USIM_TERMINATED: return "usim_terminated";
        default:                                 return "invalid";
    }
}

const AppBc95NetStatus_t *App_Bc95AtGetLastNetStatus(void) { return &g_appBc95AtNetStatus; }

static const char *App_NbiotCarrierAttachStateString(AppNbiotCarrierAttachState_t state)
{
    switch (state)
    {
        case APP_NBIOT_ATTACH_STATE_IDLE:         return "idle";
        case APP_NBIOT_ATTACH_STATE_BOOT_WAIT:    return "boot_wait";
        case APP_NBIOT_ATTACH_STATE_NETWORK_WAIT: return "network_wait";
        case APP_NBIOT_ATTACH_STATE_READY:        return "ready";
        case APP_NBIOT_ATTACH_STATE_SW_RESET:     return "sw_reset";
        case APP_NBIOT_ATTACH_STATE_HW_RESET:     return "hw_reset";
        case APP_NBIOT_ATTACH_STATE_ABORT:        return "abort";
        default:                                  return "invalid";
    }
}

static const char *App_NbiotCarrierPowerOffStateString(AppNbiotCarrierPowerOffState_t state)
{
    switch (state)
    {
        case APP_NBIOT_POWEROFF_STATE_IDLE:        return "idle";
        case APP_NBIOT_POWEROFF_STATE_DETACH_REQ:  return "detach_req";
        case APP_NBIOT_POWEROFF_STATE_WAIT_CEREG0: return "wait_cereg0";
        case APP_NBIOT_POWEROFF_STATE_FORCE_OFF:   return "force_off";
        case APP_NBIOT_POWEROFF_STATE_DONE:        return "done";
        default:                                   return "invalid";
    }
}

static const char *App_NbiotCarrierResetTypeString(AppNbiotCarrierResetType_t type)
{
    switch (type)
    {
        case APP_NBIOT_CARRIER_RESET_NONE: return "none";
        case APP_NBIOT_CARRIER_RESET_SW:   return "sw";
        case APP_NBIOT_CARRIER_RESET_HW:   return "hw";
        default:                           return "invalid";
    }
}

static AppStatus_t App_Bc95AtSendSimpleOkCommand(const char *p_cmd,
                                                 uint32_t rxTimeoutMs,
                                                 const char *p_cmdLabel,
                                                 const char *p_logPrefix)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_cmd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_cmdLabel != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_logPrefix != NULL), APP_STATUS_INVALID_PARAM);

    APP_LOGI("NBIOT", "%s TX %s", p_logPrefix, p_cmdLabel);
    status = App_Bc95AtSendWithRetry(p_cmd,
                                     g_appBc95AtRxBuf,
                                     (uint16_t)sizeof(g_appBc95AtRxBuf),
                                     rxTimeoutMs,
                                     &rxLen,
                                     APP_NBIOT_AT_NORESP_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "%s %s send fail (status=%d)", p_logPrefix, p_cmdLabel, (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "%s %s not OK (parse=%s)",
                 p_logPrefix, p_cmdLabel, App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGI("NBIOT", "%s %s OK", p_logPrefix, p_cmdLabel);
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotCarrierPerformSwReset(void)
{
    AppStatus_t status;

    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET " SW reset sequence start (AT+CFUN=0 -> AT+CFUN=1)");

    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CFUN_SET_MIN,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CFUN=0",
                                           APP_NBIOT_REPORT_LOG_RESET);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    App_Bc95AtDelayWithFeed(APP_NBIOT_SW_RESET_SETTLE_MS);

    status = App_Bc95AtSetFullFunction();
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_RESET " AT+CFUN=1 failed (status=%d)", (int)status);
        return status;
    }

    App_Bc95AtDelayWithFeed(APP_NBIOT_SW_RESET_SETTLE_MS);
    (void)App_NBIoTAtInit();
    APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_RESET " SW reset sequence complete");
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotCarrierPerformHwReset(void)
{
    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET " HW reset sequence start (power cycle)");
    App_HwNbiotPowerCycle();
    /*
     * Do not add a post-power-on settle delay here.
     *
     * The next attach attempt enters App_Bc95AtWaitUntilReady() and starts the
     * boot-banner wait. A long delay here causes the modem's early boot banner
     * to be emitted before UART RX is armed, so banner capture fails after HW
     * reset even though the modem is actually alive.
     */
    (void)App_NBIoTAtInit();
    APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_RESET " HW reset sequence complete");
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotCarrierWaitForCereg0(uint32_t timeoutMs)
{
    AppStatus_t status;
    AppBc95NetStatus_t snapshot;
    uint32_t startTick = HAL_GetTick();

    while ((HAL_GetTick() - startTick) < timeoutMs)
    {
        (void)memset(&snapshot, 0, sizeof(snapshot));
        status = App_Bc95AtQueryNetStatus(&snapshot);
        if (status == APP_STATUS_OK)
        {
            g_appNbiotCarrierContext.lastNetStatus = snapshot;
            APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " poll: phase=%s CEREG=%s CGATT=%u",
                     App_Bc95AtGetNetPhaseString(snapshot.phase),
                     App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                     (unsigned)snapshot.cgattState);

            if ((snapshot.ceregStat == APP_BC95_CEREG_NOT_REGISTERED) || (snapshot.cfunValue != 1u))
            {
                APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " complete: CEREG=%s CGATT=%u",
                         App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                         (unsigned)snapshot.cgattState);
                return APP_STATUS_OK;
            }
        }
        else
        {
            APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " poll query fail (status=%d)", (int)status);
        }

        App_Bc95AtDelayWithFeed(APP_NBIOT_DETACH_POLL_INTERVAL_MS);
    }

    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " timeout waiting CEREG:0");
    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_NbiotCarrierRecoverAfterAttachFailure(AppStatus_t lastStatus)
{
    uint32_t elapsedMs = HAL_GetTick() - g_appNbiotCarrierContext.attachStartTick;

    if (g_appBc95UsimFatal == APP_TRUE)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " no-USIM fatal -> abort without reset (elapsed=%lums)",
                 (unsigned long)elapsedMs);
        return APP_STATUS_FATAL;
    }

    if (g_appNbiotCarrierContext.lastNetStatus.phase == APP_BC95_NET_PHASE_USIM_SUSPEND)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " suspended USIM detected (rejectCause=%u) -> abort without reset/retry",
                 (unsigned)g_appNbiotCarrierContext.lastNetStatus.rejectCause);
        return APP_STATUS_FATAL;
    }

    if (g_appNbiotCarrierContext.lastNetStatus.phase == APP_BC95_NET_PHASE_USIM_TERMINATED)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " terminated USIM detected (rejectCause=%u) -> abort without reset/retry",
                 (unsigned)g_appNbiotCarrierContext.lastNetStatus.rejectCause);
        return APP_STATUS_FATAL;
    }

    if (elapsedMs >= APP_NBIOT_ATTACH_TOTAL_FAIL_LIMIT_MS)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " fail limit reached elapsed=%lums sw=%u hw=%u last=%d",
                 (unsigned long)elapsedMs,
                 (unsigned)g_appNbiotCarrierContext.swResetCount,
                 (unsigned)g_appNbiotCarrierContext.hwResetCount,
                 (int)lastStatus);
        return (lastStatus != APP_STATUS_OK) ? lastStatus : APP_STATUS_FATAL;
    }

    if (g_appNbiotCarrierContext.swResetCount < APP_NBIOT_ATTACH_SW_RESET_MAX)
    {
        g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_SW_RESET;
        g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_SW;
        g_appNbiotCarrierContext.swResetCount++;
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET
                 " escalate to SW reset %u/%u (elapsed=%lums)",
                 (unsigned)g_appNbiotCarrierContext.swResetCount,
                 (unsigned)APP_NBIOT_ATTACH_SW_RESET_MAX,
                 (unsigned long)elapsedMs);
        return App_NbiotCarrierPerformSwReset();
    }

    if (g_appNbiotCarrierContext.hwResetCount < APP_NBIOT_ATTACH_HW_RESET_MAX)
    {
        g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_HW_RESET;
        g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_HW;
        g_appNbiotCarrierContext.hwResetCount++;
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET
                 " escalate to HW reset %u/%u (elapsed=%lums)",
                 (unsigned)g_appNbiotCarrierContext.hwResetCount,
                 (unsigned)APP_NBIOT_ATTACH_HW_RESET_MAX,
                 (unsigned long)elapsedMs);
        return App_NbiotCarrierPerformHwReset();
    }

    APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_RESET
             " reset recovery exhausted sw=%u hw=%u elapsed=%lums last=%d",
             (unsigned)g_appNbiotCarrierContext.swResetCount,
             (unsigned)g_appNbiotCarrierContext.hwResetCount,
             (unsigned long)elapsedMs,
             (int)lastStatus);
    return (lastStatus != APP_STATUS_OK) ? lastStatus : APP_STATUS_FATAL;
}

/* ============================================================
 *  UDP / DNS
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

static uint8_t App_Bc95AtNextUdpSeq(void)
{
    g_appBc95UdpSeqCounter++;
    if (g_appBc95UdpSeqCounter == 0u) g_appBc95UdpSeqCounter = 1u;
    return g_appBc95UdpSeqCounter;
}

static AppBc95AtStatus_t App_Bc95AtParseQdnsResult(const char *p_resp,
                                                   char *p_ipOut,
                                                   uint32_t ipBufSize,
                                                   uint8_t *p_found,
                                                   uint8_t *p_isFail)
{
    const char *p_scan;
    const char *p_pfx;
    const char *p_line;
    const char *p_end;
    uint32_t    lineLen;
    uint32_t    i;

    if ((p_resp == NULL) || (p_ipOut == NULL) || (ipBufSize < APP_BC95_IP_STR_SIZE) ||
        (p_found == NULL) || (p_isFail == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    *p_found  = APP_FALSE;
    *p_isFail = APP_FALSE;
    p_scan = p_resp;

    while ((p_pfx = strstr(p_scan, APP_BC95_AT_QDNS_PREFIX)) != NULL)
    {
        p_line = p_pfx + APP_BC95_AT_QDNS_PREFIX_LEN;
        while ((*p_line == ' ') || (*p_line == '\t')) p_line++;

        p_end = p_line;
        while ((*p_end != '\0') && (*p_end != '\r') && (*p_end != '\n')) p_end++;
        if (*p_end == '\0')
        {
            return APP_BC95_AT_ERR_NO_DATA;
        }

        lineLen = (uint32_t)(p_end - p_line);
        if ((lineLen == 4u) && (strncmp(p_line, "FAIL", 4u) == 0))
        {
            *p_found  = APP_TRUE;
            *p_isFail = APP_TRUE;
            return APP_BC95_AT_OK;
        }

        if ((lineLen < 7u) || (lineLen >= ipBufSize))
        {
            return APP_BC95_AT_ERR_FORMAT;
        }

        for (i = 0u; i < lineLen; i++)
        {
            char c = p_line[i];
            if (((c < '0') || (c > '9')) && (c != '.'))
            {
                return APP_BC95_AT_ERR_FORMAT;
            }
        }

        (void)memcpy(p_ipOut, p_line, lineLen);
        p_ipOut[lineLen] = '\0';
        *p_found = APP_TRUE;
        return APP_BC95_AT_OK;
    }

    return APP_BC95_AT_ERR_NO_PREFIX;
}

AppStatus_t App_Bc95AtResolveHost(const char *p_hostname, char *p_ipOut, uint32_t ipBufSize)
{
    AppStatus_t       status;
    HAL_StatusTypeDef halStatus;
    AppBc95AtStatus_t atStatus;
    char              atCmd[96];
    char              rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    int               cmdLen;
    int32_t           cmeErr;
    uint32_t          rxLen;
    uint32_t          snapLen;
    uint32_t          startTick;
    uint32_t          elapsed;
    uint32_t          hostLen;
    uint8_t           qdnsFound;
    uint8_t           qdnsFail;

    if ((p_hostname == NULL) || (p_ipOut == NULL) || (ipBufSize < APP_BC95_IP_STR_SIZE))
    {
        APP_LOGE("NBIOT", "ResolveHost: invalid param");
        return APP_STATUS_FATAL;
    }
    hostLen = (uint32_t)strlen(p_hostname);
    if ((hostLen == 0u) || (hostLen > APP_BC95_HOSTNAME_MAX_LEN))
    {
        APP_LOGE("NBIOT", "ResolveHost: bad hostname len=%lu", (unsigned long)hostLen);
        return APP_STATUS_FATAL;
    }
    (void)memset(p_ipOut, 0, ipBufSize);

    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);
    App_Bc95AtDelayWithFeed(APP_BC95_DNS_INTERCMD_DELAY_MS);

    cmdLen = snprintf(atCmd, sizeof(atCmd), "AT+QDNS=0,%s\r\n", p_hostname);
    if ((cmdLen <= 0) || (cmdLen >= (int)sizeof(atCmd)))
    {
        APP_LOGE("NBIOT", "ResolveHost: cmd build fail");
        return APP_STATUS_FATAL;
    }
    APP_LOGD("NBIOT", "DNS query: %s", p_hostname);

    (void)memset(g_appBc95AtRxBuf, 0, APP_BC95_AT_RX_BUF_SIZE);
    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = (uint16_t)(APP_BC95_AT_RX_BUF_SIZE - 1u);
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE, &g_appBc95AtRxBuf[0], 1u);
    if (halStatus != HAL_OK)
    {
        APP_LOGE("NBIOT", "ResolveHost: RX_IT fail (%d)", halStatus);
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)atCmd, (uint16_t)cmdLen,
                                  APP_BC95_DNS_CMD_TIMEOUT_MS);
    if (halStatus != HAL_OK)
    {
        APP_LOGE("NBIOT", "ResolveHost: TX fail (%d)", halStatus);
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen >= (APP_BC95_AT_RX_BUF_SIZE - 1u))
        {
            rxLen = (APP_BC95_AT_RX_BUF_SIZE - 1u);
        }
        snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, (uint16_t)rxLen,
                                         rxSnapshot, (uint16_t)sizeof(rxSnapshot));

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            APP_LOGE("NBIOT", "ResolveHost: UART err (rxLen=%lu)", (unsigned long)rxLen);
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_RX_FAILED;
        }

        cmeErr = 0;
        atStatus = App_Bc95AtCheckResponse(rxSnapshot, &cmeErr);
        if (atStatus == APP_BC95_AT_OK)
        {
            break;
        }
        if ((atStatus == APP_BC95_AT_ERR_AT_ERROR) || (atStatus == APP_BC95_AT_ERR_CME_ERROR))
        {
            APP_LOGE("NBIOT", "ResolveHost: response error parse=%s cme=%ld raw=[%s]",
                     App_Bc95AtGetStatusString(atStatus), (long)cmeErr, rxSnapshot);
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_TIMEOUT;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= APP_BC95_DNS_CMD_TIMEOUT_MS)
        {
            cmeErr = 0;
            atStatus = App_Bc95AtCheckResponse(rxSnapshot, &cmeErr);
            if (atStatus == APP_BC95_AT_OK)
            {
                APP_LOGI("NBIOT", "ResolveHost: late OK detected (rxLen=%lu)", (unsigned long)snapLen);
                break;
            }
            APP_LOGE("NBIOT", "ResolveHost: OK timeout (rxLen=%lu, raw=[%s])",
                     (unsigned long)rxLen, rxSnapshot);
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_WWDGFeed();
        HAL_Delay(5u);
    }

    startTick = HAL_GetTick();
    status    = APP_STATUS_UART_TIMEOUT;

    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen >= (APP_BC95_AT_RX_BUF_SIZE - 1u))
        {
            rxLen = (APP_BC95_AT_RX_BUF_SIZE - 1u);
        }
        snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, (uint16_t)rxLen,
                                         rxSnapshot, (uint16_t)sizeof(rxSnapshot));

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            APP_LOGE("NBIOT", "ResolveHost: UART err in URC wait raw=[%s]", rxSnapshot);
            status = APP_STATUS_UART_RX_FAILED;
            break;
        }

        qdnsFound = APP_FALSE;
        qdnsFail  = APP_FALSE;
        atStatus = App_Bc95AtParseQdnsResult(rxSnapshot, p_ipOut, ipBufSize, &qdnsFound, &qdnsFail);
        if ((atStatus == APP_BC95_AT_OK) && (qdnsFound == APP_TRUE))
        {
            if (qdnsFail == APP_TRUE)
            {
                APP_LOGI("NBIOT", "DNS FAIL for %s (transient)", p_hostname);
                status = APP_STATUS_UART_TIMEOUT;
            }
            else
            {
                APP_LOGD("NBIOT", "DNS OK: %s -> %s", p_hostname, p_ipOut);
                status = APP_STATUS_OK;
            }
            break;
        }
        if ((atStatus != APP_BC95_AT_ERR_NO_PREFIX) && (atStatus != APP_BC95_AT_ERR_NO_DATA))
        {
            APP_LOGE("NBIOT", "ResolveHost: invalid QDNS URC raw=[%s]", rxSnapshot);
            (void)memset(p_ipOut, 0, ipBufSize);
            status = APP_STATUS_UART_TIMEOUT;
            break;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= APP_BC95_DNS_URC_TIMEOUT_MS)
        {
            qdnsFound = APP_FALSE;
            qdnsFail  = APP_FALSE;
            atStatus = App_Bc95AtParseQdnsResult(rxSnapshot, p_ipOut, ipBufSize, &qdnsFound, &qdnsFail);
            if ((atStatus == APP_BC95_AT_OK) && (qdnsFound == APP_TRUE) && (qdnsFail == APP_FALSE))
            {
                APP_LOGI("NBIOT", "ResolveHost: late QDNS detected %s -> %s", p_hostname, p_ipOut);
                status = APP_STATUS_OK;
            }
            else
            {
                APP_LOGE("NBIOT", "ResolveHost: URC timeout (rxLen=%lu, raw=[%s])",
                         (unsigned long)rxLen, rxSnapshot);
                status = APP_STATUS_UART_TIMEOUT;
            }
            break;
        }
        APP_WWDGFeed();
        HAL_Delay(10u);
    }

    App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
    return status;
}

AppStatus_t App_Bc95AtResolveHostRobust(const char *p_hostname, char *p_ipOut, uint32_t ipBufSize)
{
    AppStatus_t status;
    uint32_t attempt;
    uint32_t totalAttempts = 0u;   /* 패치: 초기화 */
    uint32_t uartErrCount  = 0u;   /* 패치: 초기화 */
    uint32_t startTick;

    if ((p_hostname == NULL) || (p_ipOut == NULL) || (ipBufSize < APP_BC95_IP_STR_SIZE))
    {
        APP_LOGE("NBIOT", "ResolveHostRobust: invalid param");
        return APP_STATUS_FATAL;
    }
    (void)memset(p_ipOut, 0, ipBufSize);
    APP_LOGI("NBIOT", "==== DNS resolve start: %s ====", p_hostname);

    startTick = HAL_GetTick();

    for (attempt = 1u; attempt <= APP_BC95_DNS_FAIL_RETRY_MAX; attempt++)
    {
        totalAttempts++;

        APP_LOGD("NBIOT", "DNS attempt %lu (try=%lu/%lu)",
                 (unsigned long)totalAttempts,
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_DNS_FAIL_RETRY_MAX);

        status = App_Bc95AtResolveHost(p_hostname, p_ipOut, ipBufSize);

        if (status == APP_STATUS_OK)
        {
            APP_LOGI("NBIOT", "==== DNS resolved: %s -> %s (attempts=%lu, elapsed=%lums) ====",
                     p_hostname, p_ipOut,
                     (unsigned long)totalAttempts,
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }
        if (status == APP_STATUS_FATAL)
        {
            APP_LOGE("NBIOT", "DNS fatal (attempt=%lu)", (unsigned long)totalAttempts);
            return APP_STATUS_FATAL;
        }
        if (status == APP_STATUS_UART_RX_FAILED)
        {
            uartErrCount++;
            APP_LOGE("NBIOT", "DNS UART err count=%lu", (unsigned long)uartErrCount);
            if (uartErrCount >= 3u)
            {
                APP_LOGE("NBIOT", "DNS abort: UART persistent");
                return APP_STATUS_UART_RX_FAILED;
            }
        }
        else
        {
            uartErrCount = 0u;
        }

        if (attempt < APP_BC95_DNS_FAIL_RETRY_MAX)
        {
            APP_LOGI("NBIOT", "DNS retry after %lums (status=%d)",
                     (unsigned long)APP_BC95_DNS_FAIL_RETRY_DELAY_MS, (int)status);
            App_Bc95AtDelayWithFeed(APP_BC95_DNS_FAIL_RETRY_DELAY_MS);
        }
    }

    APP_LOGE("NBIOT", "==== DNS FAILED: %s (totalAttempts=%lu, elapsed=%lums) ====",
             p_hostname,
             (unsigned long)totalAttempts,
             (unsigned long)(HAL_GetTick() - startTick));
    (void)memset(p_ipOut, 0, ipBufSize);
    return APP_STATUS_UART_TIMEOUT;
}

#ifdef NBIOT_SUPPORT_DNS
static AppStatus_t App_Bc95AtWarmupDns(const char *p_hostname)
{
    AppStatus_t status;
    char ipBuf[APP_BC95_IP_STR_SIZE];
    uint32_t attempt;

    APP_RETURN_IF_FALSE((p_hostname != NULL), APP_STATUS_INVALID_PARAM);
    APP_LOGI("NBIOT", "[DNS] warmup with %s", p_hostname);

    for (attempt = 1u; attempt <= APP_BC95_DNS_FAIL_RETRY_MAX; attempt++)
    {
        status = App_Bc95AtResolveHostRobust(p_hostname, ipBuf, sizeof(ipBuf));
        if (status == APP_STATUS_OK)
        {
            APP_LOGI("NBIOT", "[DNS] warmup OK on %lu (%s:ip=%s)",
                     (unsigned long)attempt, p_hostname, ipBuf);
            return APP_STATUS_OK;
        }
        APP_LOGI("NBIOT", "[DNS] warmup %lu/%lu failed",
                 (unsigned long)attempt, (unsigned long)APP_BC95_DNS_FAIL_RETRY_MAX);
        if (attempt < APP_BC95_DNS_FAIL_RETRY_MAX)
            App_Bc95AtDelayWithFeed(APP_BC95_DNS_FAIL_RETRY_DELAY_MS);
    }
    APP_LOGI("NBIOT", "[DNS] warmup skipped");
    return APP_STATUS_UART_TIMEOUT;
}
#endif // NBIOT_SUPPORT_DNS

/* ============================================================
 *  UDP socket / send
 * ============================================================ */
AppStatus_t App_Bc95AtCreateUdpSocket(uint16_t localPort, int32_t *p_socketOut)
{
    AppStatus_t  status;
    AppBc95AtStatus_t atStatus;
    uint16_t  rxLen;
    int  printed;
    int  parsedSock;
    const char *p_line;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_socketOut != NULL), APP_STATUS_INVALID_PARAM);
    *p_socketOut = -1;

    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       APP_BC95_AT_CMD_NSOCR_UDP_FMT, (unsigned)localPort);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
        return APP_STATUS_INVALID_PARAM;

    rxLen = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "NSOCR send fail (status=%d)", (int)status);
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "NSOCR resp err (parse=%s, CME=%ld)",
                 App_Bc95AtGetStatusString(atStatus), (long)cmeErr);
        return APP_STATUS_FATAL;
    }

    parsedSock = -1;
    p_line = (const char *)g_appBc95AtRxBuf;
    while (*p_line != '\0')
    {
        const char *p_lineStart = p_line;
        while ((*p_lineStart == '\r') || (*p_lineStart == '\n')) p_lineStart++;
        if ((*p_lineStart >= '0') && (*p_lineStart <= '9'))
        {
            if (sscanf(p_lineStart, "%d", &parsedSock) == 1) break;
        }
        while ((*p_line != '\0') && (*p_line != '\n')) p_line++;
        if (*p_line == '\n') p_line++;
    }

    if (parsedSock < 0)
    {
        APP_LOGE("NBIOT", "NSOCR socket id parse fail. raw=[%s]", g_appBc95AtRxBuf);
        /* 패치: 알 수 없는 소켓 누수 방지 */
        App_Bc95AtCloseAllSockets();
        return APP_STATUS_FATAL;
    }

    *p_socketOut = (int32_t)parsedSock;
    APP_LOGD("NBIOT", "UDP socket created: %ld", (long)parsedSock);
    return APP_STATUS_OK;
}

static AppBc95AtStatus_t App_Bc95AtParseNsostr(const char *p_resp,
                                               int *p_sock, int *p_seq, int *p_status)
{
    const char *p_pfx;
    if ((p_resp == NULL) || (p_sock == NULL) || (p_seq == NULL) || (p_status == NULL))
        return APP_BC95_AT_ERR_PARAM;

    p_pfx = strstr(p_resp, APP_BC95_AT_NSOSTR_PREFIX);
    if (p_pfx == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_pfx += APP_BC95_AT_NSOSTR_PREFIX_LEN;
    while ((*p_pfx == ' ') || (*p_pfx == '\t')) p_pfx++;
    if (sscanf(p_pfx, "%d,%d,%d", p_sock, p_seq, p_status) != 3) return APP_BC95_AT_ERR_FORMAT;
    return APP_BC95_AT_OK;
}

static AppBc95AtStatus_t App_Bc95AtFindNsostrForSocketSeq(const char *p_resp,
                                                          int targetSock,
                                                          int targetSeq,
                                                          int *p_statusOut)
{
    const char *p_scan;
    const char *p_pfx;
    const char *p_line;
    const char *p_end;
    int parsedSock;
    int parsedSeq;
    int parsedStatus;

    if ((p_resp == NULL) || (p_statusOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    p_scan = p_resp;
    while ((p_pfx = strstr(p_scan, APP_BC95_AT_NSOSTR_PREFIX)) != NULL)
    {
        p_line = p_pfx + APP_BC95_AT_NSOSTR_PREFIX_LEN;
        while ((*p_line == ' ') || (*p_line == '\t')) p_line++;

        p_end = p_line;
        while ((*p_end != '\0') && (*p_end != '\r') && (*p_end != '\n')) p_end++;
        if (*p_end == '\0')
        {
            return APP_BC95_AT_ERR_NO_DATA;
        }

        if (sscanf(p_line, "%d,%d,%d", &parsedSock, &parsedSeq, &parsedStatus) == 3)
        {
            if ((parsedSock == targetSock) && (parsedSeq == targetSeq))
            {
                *p_statusOut = parsedStatus;
                return APP_BC95_AT_OK;
            }
        }

        p_scan = (*p_end == '\0') ? p_end : (p_end + 1);
    }

    return APP_BC95_AT_ERR_NO_PREFIX;
}

static AppBc95AtStatus_t App_Bc95AtParseNsorfForSocket(const char *p_resp,
                                                       int targetSock,
                                                       uint16_t *p_payloadLen)
{
    const char *p_scan;
    const char *p_pfx;
    int parsedSock;
    char parsedIp[APP_BC95_IP_STR_SIZE];
    unsigned parsedPort;
    unsigned parsedLen;
    char parsedHex[2u * APP_BC95_UDP_MAX_PAYLOAD + 1u];
    unsigned parsedRemain;

    APP_RETURN_IF_FALSE((p_resp != NULL) && (p_payloadLen != NULL), APP_BC95_AT_ERR_PARAM);

    p_scan = p_resp;
    while ((p_pfx = strstr(p_scan, APP_BC95_AT_NSORF_PREFIX)) != NULL)
    {
        parsedSock = -1;
        parsedIp[0] = '\0';
        parsedPort = 0u;
        parsedLen = 0u;
        parsedHex[0] = '\0';
        parsedRemain = 0u;

        if (sscanf(p_pfx + APP_BC95_AT_NSORF_PREFIX_LEN,
                   "%d,%63[^,],%u,%u,%1024[^,],%u",
                   &parsedSock,
                   parsedIp,
                   &parsedPort,
                   &parsedLen,
                   parsedHex,
                   &parsedRemain) >= 5)
        {
            if ((parsedSock == targetSock) && (parsedLen > 0u))
            {
                *p_payloadLen = (uint16_t)parsedLen;
                return APP_BC95_AT_OK;
            }
        }

        p_scan = p_pfx + APP_BC95_AT_NSORF_PREFIX_LEN;
    }

    return APP_BC95_AT_ERR_NO_DATA;
}

static AppStatus_t App_Bc95AtWaitUdpAck(int32_t socketId, uint32_t timeoutMs)
{
#if (APP_POLICY_WAIT_SERVER_ACK_ENABLE != APP_TRUE)
    (void)socketId;
    (void)timeoutMs;
    return APP_STATUS_OK;
#else
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint32_t startTick;
    uint16_t rxLen;
    uint16_t ackPayloadLen;
    int printed;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE(socketId >= 0, APP_STATUS_INVALID_PARAM);

    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < timeoutMs)
    {
        APP_WWDGFeed();

        printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                           APP_BC95_AT_CMD_NSORF_FMT,
                           (long)socketId,
                           (unsigned)APP_POLICY_SERVER_ACK_READ_BYTES);
        APP_RETURN_IF_FALSE((printed > 0) && ((uint32_t)printed < sizeof(appBc95AtCmdTxBuf)), APP_STATUS_INVALID_PARAM);

        rxLen = 0u;
        status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                       g_appBc95AtRxBuf,
                                       (uint16_t)sizeof(g_appBc95AtRxBuf),
                                       APP_BC95_SOCKET_TIMEOUT_MS,
                                       &rxLen);
        if (status == APP_STATUS_OK)
        {
            cmeErr = 0;
            atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
            if (atStatus == APP_BC95_AT_OK)
            {
                ackPayloadLen = 0u;
                if (App_Bc95AtParseNsorfForSocket((const char *)g_appBc95AtRxBuf,
                                                  (int)socketId,
                                                  &ackPayloadLen) == APP_BC95_AT_OK)
                {
                    APP_LOGI("NBIOT", "Server ACK received (sock=%ld, bytes=%u)",
                             (long)socketId,
                             (unsigned)ackPayloadLen);
                    return APP_STATUS_OK;
                }
            }
        }

        App_Bc95AtDelayWithFeed(APP_POLICY_SERVER_ACK_POLL_MS);
    }

    APP_LOGW("NBIOT", "Server ACK timeout (sock=%ld, timeout=%lums)",
             (long)socketId,
             (unsigned long)timeoutMs);
    return APP_STATUS_UART_TIMEOUT;
#endif
}

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
    uint16_t          snapLen;
    uint32_t          startTick;
    uint32_t          elapsed;
    int               parsedStatus = -1;
    int32_t           cmeErr;
    char              appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];
    char              rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];

    APP_RETURN_IF_FALSE(seqNum > 0u, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_ip != NULL) && (p_ip[0] != '\0'), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(socketId >= 0, APP_STATUS_INVALID_PARAM);

    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       "AT+NSOST=%ld,%s,%u,%u,",
                       (long)socketId, p_ip, (unsigned)port, (unsigned)length);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
    {
        APP_LOGE("NBIOT", "NSOST: header build fail");
        return APP_STATUS_INVALID_PARAM;
    }
    headerLen = (uint32_t)printed;

    if ((headerLen + (2u * (uint32_t)length) + 16u) >= sizeof(appBc95AtCmdTxBuf))
    {
        APP_LOGE("NBIOT", "NSOST: buf too small");
        return APP_STATUS_INVALID_PARAM;
    }
    App_Bc95AtBytesToHex(p_data, length, &appBc95AtCmdTxBuf[headerLen]);
    headerLen += (2u * (uint32_t)length);

    printed = snprintf(&appBc95AtCmdTxBuf[headerLen],
                       sizeof(appBc95AtCmdTxBuf) - headerLen,
                       ",%u\r\n", (unsigned)seqNum);
    if ((printed <= 0) || ((headerLen + (uint32_t)printed) >= sizeof(appBc95AtCmdTxBuf)))
    {
        APP_LOGE("NBIOT", "NSOST: tail build fail");
        return APP_STATUS_INVALID_PARAM;
    }

    APP_LOGI("NBIOT", "NSOST send: sock=%ld, ip=%s, port=%u, len=%u, seq=%u",
             (long)socketId, p_ip, (unsigned)port, (unsigned)length, (unsigned)seqNum);

    rxLen  = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "NSOST send fail (status=%d, rxLen=%u)", (int)status, (unsigned)rxLen);
        if (status == APP_STATUS_UART_RX_FAILED) return APP_STATUS_UART_RX_FAILED;
        return APP_STATUS_UART_TIMEOUT;
    }

    snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, rxLen,
                                     rxSnapshot, (uint16_t)sizeof(rxSnapshot));
    cmeErr   = 0;
    atStatus = App_Bc95AtCheckResponse(rxSnapshot, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGE("NBIOT", "NSOST no OK (parse=%s, cme=%ld, raw=[%s])",
                 App_Bc95AtGetStatusString(atStatus), (long)cmeErr, rxSnapshot);
        return APP_STATUS_UART_TIMEOUT;
    }

    if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                         (int)socketId,
                                         (int)seqNum,
                                         &parsedStatus) == APP_BC95_AT_OK)
    {
        if (parsedStatus == 1)
        {
            APP_LOGI("NBIOT", "NSOSTR confirmed (fast): sock=%ld, seq=%u",
                     (long)socketId, (unsigned)seqNum);
            return APP_STATUS_OK;
        }
        APP_LOGE("NBIOT", "NSOSTR fail (fast): sock=%ld, seq=%u, status=%d (transient)",
                 (long)socketId, (unsigned)seqNum, parsedStatus);
        return APP_STATUS_UART_TIMEOUT;
    }

    bufCap = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);

    if (rxLen >= bufCap)
    {
        APP_LOGE("NBIOT", "NSOSTR wait: buf full after OK (rxLen=%u)", (unsigned)rxLen);
        if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                             (int)socketId,
                                             (int)seqNum,
                                             &parsedStatus) == APP_BC95_AT_OK)
        {
            return (parsedStatus == 1) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
        }
        return APP_STATUS_UART_TIMEOUT;
    }

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = bufCap;
    g_appBc95AtRxContext.receivedLength = rxLen;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);

    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE,
                                    &g_appBc95AtRxBuf[rxLen], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        APP_LOGE("NBIOT", "NSOSTR wait: RX_IT start fail (%d)", (int)halStatus);
        return APP_STATUS_UART_RX_FAILED;
    }

    if (g_appBc95AtRxContext.error == APP_TRUE)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        APP_LOGE("NBIOT", "NSOSTR wait: error right after start");
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen >= bufCap)
        {
            rxLen = bufCap;
        }
        snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, rxLen,
                                         rxSnapshot, (uint16_t)sizeof(rxSnapshot));

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF  | UART_CLEAR_PEF);
            APP_LOGE("NBIOT", "NSOSTR wait: UART err (rxLen=%u, raw=[%s])",
                     (unsigned)g_appBc95AtRxContext.receivedLength, rxSnapshot);
            return APP_STATUS_UART_RX_FAILED;
        }

        atStatus = App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                                    (int)socketId,
                                                    (int)seqNum,
                                                    &parsedStatus);
        if (atStatus == APP_BC95_AT_OK)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            if (parsedStatus == 1)
            {
                APP_LOGI("NBIOT", "NSOSTR confirmed: sock=%ld, seq=%u (elapsed=%lums)",
                         (long)socketId, (unsigned)seqNum,
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }
            APP_LOGE("NBIOT", "NSOSTR fail: sock=%ld, seq=%u, status=%d (transient)",
                     (long)socketId, (unsigned)seqNum, parsedStatus);
            return APP_STATUS_UART_TIMEOUT;
        }

        if (g_appBc95AtRxContext.receivedLength >= bufCap)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            APP_LOGE("NBIOT", "NSOSTR wait: buffer full (rxLen=%u, raw=[%s])",
                     (unsigned)g_appBc95AtRxContext.receivedLength, rxSnapshot);
            if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                                 (int)socketId,
                                                 (int)seqNum,
                                                 &parsedStatus) == APP_BC95_AT_OK)
            {
                return (parsedStatus == 1) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
            }
            return APP_STATUS_UART_TIMEOUT;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= confirmTimeoutMs)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF  | UART_CLEAR_PEF);
            if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                                 (int)socketId,
                                                 (int)seqNum,
                                                 &parsedStatus) == APP_BC95_AT_OK)
            {
                APP_LOGI("NBIOT", "NSOSTR late-detected: sock=%ld, seq=%u, status=%d",
                         (long)socketId, (unsigned)seqNum, parsedStatus);
                return (parsedStatus == 1) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
            }
            APP_LOGE("NBIOT", "NSOSTR timeout (sock=%ld, seq=%u, rxLen=%u, raw=[%s])",
                     (long)socketId, (unsigned)seqNum, (unsigned)rxLen, rxSnapshot);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_WWDGFeed();
        HAL_Delay(10u);
    }
}

static AppStatus_t App_Bc95AtCloseSocketInternal(int32_t socketId,
                                                    uint8_t quietNoSocket,
                                                    AppBc95AtStatus_t *p_atStatusOut)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int printed;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((socketId >= 0), APP_STATUS_INVALID_PARAM);

    if (p_atStatusOut != NULL)
    {
        *p_atStatusOut = APP_BC95_AT_OK;
    }

    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       APP_BC95_AT_CMD_NSOCL_FMT, (long)socketId);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
        return APP_STATUS_INVALID_PARAM;

    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS, &rxLen);

#ifdef NBIoT_SIMULATION_CODE
    APP_LOGI("NBIOT", "Forcely err NSOCL send fail test..."); // NBIoT simulation test
    status = APP_STATUS_INVALID_PARAM;
    quietNoSocket = APP_FALSE;
#endif // NBIoT_SIMULATION_CODE
    if (status != APP_STATUS_OK)
    {
        if (quietNoSocket != APP_TRUE)
        {
            APP_LOGI("NBIOT", "NSOCL send fail (sock=%ld, status=%d)", (long)socketId, (int)status);
        }
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (p_atStatusOut != NULL)
    {
        *p_atStatusOut = atStatus;
    }
#ifdef NBIoT_SIMULATION_CODE
    APP_LOGI("NBIOT", "Forcely err NSOCL not OK test..."); // NBIoT simulation test
    atStatus = APP_BC95_AT_ERR_AT_ERROR;
#endif // NBIoT_SIMULATION_CODE
    if (atStatus != APP_BC95_AT_OK)
    {
        if (quietNoSocket != APP_TRUE)
        {
            APP_LOGI("NBIOT", "NSOCL not OK (sock=%ld, parse=%s)",
                     (long)socketId, App_Bc95AtGetStatusString(atStatus));
        }
        return APP_STATUS_FATAL;
    }
    APP_LOGD("NBIOT", "UDP socket closed: %ld", (long)socketId);
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtCloseSocket(int32_t socketId)
{
    return App_Bc95AtCloseSocketInternal(socketId, APP_FALSE, NULL);
}

void App_Bc95AtCloseAllSockets(void)
{
    int sid;
    uint32_t closedCount = 0u;
    uint32_t alreadyClosedCount = 0u;
    uint32_t sendFailCount = 0u;
    uint32_t otherFailCount = 0u;

    APP_LOGI("NBIOT", "Closing all possible sockets (0..%d)", APP_BC95_UDP_SOCKET_ID_MAX);
    for (sid = 0; sid <= APP_BC95_UDP_SOCKET_ID_MAX; sid++)
    {
        AppStatus_t closeStatus;
        AppBc95AtStatus_t atStatus = APP_BC95_AT_OK;

        closeStatus = App_Bc95AtCloseSocketInternal((int32_t)sid, APP_TRUE, &atStatus);
        if (closeStatus == APP_STATUS_OK)
        {
            closedCount++;
        }
        else if ((closeStatus == APP_STATUS_FATAL) &&
                 ((atStatus == APP_BC95_AT_ERR_AT_ERROR) ||
                  (atStatus == APP_BC95_AT_ERR_NO_OK)))
        {
            alreadyClosedCount++;
        }
        else if ((closeStatus == APP_STATUS_UART_RX_FAILED) ||
                 (closeStatus == APP_STATUS_UART_TX_FAILED) ||
                 (closeStatus == APP_STATUS_UART_TIMEOUT))
        {
            sendFailCount++;
        }
        else
        {
            otherFailCount++;
        }
        APP_WWDGFeed();
    }

    APP_LOGI("NBIOT",
             "Socket cleanup summary: closed=%lu already_closed=%lu send_fail=%lu other_fail=%lu",
             (unsigned long)closedCount,
             (unsigned long)alreadyClosedCount,
             (unsigned long)sendFailCount,
             (unsigned long)otherFailCount);
}

AppStatus_t App_Bc95AtUdpSendOnce(const char *p_logTag,
                                  const char *p_host, uint16_t port,
                                  const uint8_t *p_data, uint16_t length,
                                  AppBc95UdpResult_t *p_result)
{
    AppStatus_t status;
    int32_t socketId = -1;
    char ipBuf[APP_BC95_IP_STR_SIZE];
    uint32_t attempt;
    uint8_t  seq = 0u;
    AppBc95UdpResult_t result;

    (void)memset(&result, 0, sizeof(result));
    result.socketId  = -1;
    result.lastStage = APP_BC95_UDP_STAGE_NONE;
    ipBuf[0] = '\0';

    APP_RETURN_IF_FALSE((p_logTag != NULL) && (p_host != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);

    /* 1) DNS */
    result.lastStage = APP_BC95_UDP_STAGE_RESOLVE;
#ifdef NBIOT_SUPPORT_DNS
    status = App_Bc95AtResolveHostRobust(p_host, ipBuf, sizeof(ipBuf));
    if ((status != APP_STATUS_OK) || (ipBuf[0] == '\0'))
    {
        APP_LOGE("NBIOT", "DNS resolve failed for %s", p_host);
        if (p_result != NULL)
            (void)memcpy(p_result, &result, sizeof(*p_result));
        return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
    }
    (void)strncpy(result.resolvedIp, ipBuf, sizeof(result.resolvedIp) - 1u);
#else
    (void)strcpy(ipBuf, p_host);
    (void)strcpy(result.resolvedIp, ipBuf);
#endif


    /* 2) Create socket (재시도) */
    result.lastStage = APP_BC95_UDP_STAGE_CREATE;
    for (attempt = 1u; attempt <= APP_BC95_UDP_SEND_RETRY_MAX; attempt++)
    {
        APP_WWDGFeed();
        status = App_Bc95AtCreateUdpSocket(APP_BC95_UDP_LOCAL_PORT, &socketId);
        if (status == APP_STATUS_OK) break;

        APP_LOGI("NBIOT", "NSOCR retry %lu/%lu (status=%d)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_UDP_SEND_RETRY_MAX, (int)status);
        if (attempt < APP_BC95_UDP_SEND_RETRY_MAX)
            App_Bc95AtDelayWithFeed(APP_BC95_UDP_SEND_RETRY_DELAY_MS);
    }
    if ((status != APP_STATUS_OK) || (socketId < 0))
    {
        APP_LOGE("NBIOT", "UDP socket create failed");
        if (p_result != NULL) (void)memcpy(p_result, &result, sizeof(*p_result));
        return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
    }
    result.socketId = socketId;

    /* 3) Send (재시도, 매 시도마다 새 seq) */
    result.lastStage = APP_BC95_UDP_STAGE_SEND;
    status = APP_STATUS_FATAL;
    for (attempt = 1u; attempt <= APP_BC95_UDP_SEND_RETRY_MAX; attempt++)
    {
        seq = App_Bc95AtNextUdpSeq();   /* 패치: 전역 단조 증가 seq */
        result.seqNumber = seq;

        APP_WWDGFeed();
        status = App_Bc95AtUdpSendAndConfirm(socketId, ipBuf, port,
                                             p_data, length, seq,
                                             APP_BC95_NSOST_CONFIRM_TIMEOUT_MS);
        if (status == APP_STATUS_OK)
        {
            result.lastStage     = APP_BC95_UDP_STAGE_CONFIRM;
            result.sentBytes     = length;
            result.sendConfirmed = APP_TRUE;
            break;
        }
        if (status == APP_STATUS_INVALID_PARAM)
        {
            APP_LOGE("NBIOT", "UDP send invalid param");
            break;
        }

        APP_LOGI("NBIOT", "UDP send retry %lu/%lu (status=%d)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_UDP_SEND_RETRY_MAX, (int)status);
        if (attempt < APP_BC95_UDP_SEND_RETRY_MAX)
            App_Bc95AtDelayWithFeed(APP_BC95_UDP_SEND_RETRY_DELAY_MS);
    }

#if (APP_POLICY_WAIT_SERVER_ACK_ENABLE == APP_TRUE)
    if (status == APP_STATUS_OK)
    {
#if (APP_POLICY_TEST_MODE_SKIP_SERVER_ACK_WAIT == APP_TRUE)
        APP_LOGW("NBIOT", "%s Test mode: skip server ACK wait/timeout", p_logTag);
#else
        APP_LOGI("NBIOT", "%s wait server ACK timeout=%lu ms", p_logTag, (unsigned long)APP_POLICY_SERVER_ACK_TIMEOUT_MS);
        status = App_Bc95AtWaitUdpAck(socketId, APP_POLICY_SERVER_ACK_TIMEOUT_MS);
#endif
    }
#endif

    /* 4) Close (성공/실패 무관) */
    result.lastStage = APP_BC95_UDP_STAGE_CLOSE;
    (void)App_Bc95AtCloseSocket(socketId);

    if (status == APP_STATUS_OK)
    {
        result.lastStage = APP_BC95_UDP_STAGE_DONE;
        APP_LOGI("NBIOT", "UDP send done: %s:%u, %u bytes, seq=%u",
                 ipBuf, (unsigned)port, (unsigned)length, (unsigned)seq);
    }
    else
    {
        APP_LOGE("NBIOT", "UDP send/ack failed (stage=%d, status=%d)",
                 (int)result.lastStage, (int)status);
    }

    if (p_result != NULL) (void)memcpy(p_result, &result, sizeof(*p_result));
    return status;
}

/* 전역 - 마지막 동기화된 시간 */
static AppBc95Time_t g_appBc95AtLastTime;

/* ============================================================
 *  AT+CCLK? 응답 파싱
 *
 *  응답 형식:
 *    +CCLK:25/05/22,14:30:45+36
 *    OK
 *
 *  필드:
 *    YY/MM/DD : 연/월/일 (실제 연도 = 2000 + YY)
 *    HH:MM:SS : 시:분:초
 *    ±TZ      : 타임존, 15분 단위 (선택, 예: +36 = UTC+9h)
 *
 *  BC95-GV 는 네트워크 시간을 받기 전에는
 *  "+CCLK:70/01/01,00:00:00+00" 같은 값을 반환할 수 있다.
 *  연도가 APP_BC95_TIME_MIN_VALID_YEAR 미만이면 NO_DATA 로 처리.
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtParseCclk(const char *p_resp, AppBc95Time_t *p_time)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedYy = 0, parsedMo = 0, parsedDd = 0;
    int parsedHh = 0, parsedMi = 0, parsedSs = 0;
    int parsedTz = 0;
    char tzSign  = '+';
    int parsedFields;

    if ((p_resp == NULL) || (p_time == NULL)) return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_time, 0, sizeof(*p_time));

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CCLK_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CCLK_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t') || (*p_payload == '"'))
        p_payload++;

    /* "YY/MM/DD,HH:MM:SS±TZ" (TZ 있는 경우) */
    parsedFields = sscanf(p_payload, "%d/%d/%d,%d:%d:%d%c%d",
                          &parsedYy, &parsedMo, &parsedDd,
                          &parsedHh, &parsedMi, &parsedSs,
                          &tzSign, &parsedTz);

    if (parsedFields < 6)
    {
        APP_LOGE("NBIOT", "CCLK format error: [%s]", p_payload);
        return APP_BC95_AT_ERR_FORMAT;
    }

    /* TZ 부분이 부족하면 0 으로 */
    if (parsedFields < 8)
    {
        parsedTz = 0;
        tzSign   = '+';
    }

    /* 범위 검증 */
    if ((parsedYy < 0)  || (parsedYy > 99)  ||
        (parsedMo < 1)  || (parsedMo > 12)  ||
        (parsedDd < 1)  || (parsedDd > 31)  ||
        (parsedHh < 0)  || (parsedHh > 23)  ||
        (parsedMi < 0)  || (parsedMi > 59)  ||
        (parsedSs < 0)  || (parsedSs > 59)  ||
        (parsedTz < 0)  || (parsedTz > 96))
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    /* 네트워크 시간을 못 받은 상태 거르기 */
    if ((uint16_t)(2000 + parsedYy) < APP_BC95_TIME_MIN_VALID_YEAR)
    {
        APP_LOGI("NBIOT", "CCLK: network time not synced yet (year=20%02d)", parsedYy);
        return APP_BC95_AT_ERR_NO_DATA;
    }

    p_time->dateTime.year   = (uint16_t)(2000 + parsedYy);
    p_time->dateTime.month  = (uint8_t)parsedMo;
    p_time->dateTime.day    = (uint8_t)parsedDd;
    p_time->dateTime.hour   = (uint8_t)parsedHh;
    p_time->dateTime.minute = (uint8_t)parsedMi;
    p_time->dateTime.second = (uint8_t)parsedSs;

    if (tzSign == '-')
        p_time->tzQuarterHour = (int8_t)(-parsedTz);
    else
        p_time->tzQuarterHour = (int8_t)parsedTz;

    p_time->valid = APP_TRUE;
    return APP_BC95_AT_OK;
}

/* ============================================================
 *  AT+CCLK? 송신 + 파싱
 * ============================================================ */
AppStatus_t App_Bc95AtFetchTime(AppBc95Time_t *p_time)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_time != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_time, 0, sizeof(*p_time));

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CCLK_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "CCLK send fail (status=%d, rxLen=%u)",
                 (int)status, (unsigned)rxLen);
        return status;
    }

    atStatus = App_Bc95AtParseCclk((const char *)g_appBc95AtRxBuf, p_time);
    if (atStatus == APP_BC95_AT_OK)
    {
        (void)memcpy(&g_appBc95AtLastTime, p_time, sizeof(g_appBc95AtLastTime));
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_NO_DATA)
    {
        /* 아직 네트워크 시간 미수신 - 일시 실패 */
        return APP_STATUS_UART_TIMEOUT;
    }

    APP_LOGE("NBIOT", "CCLK parse fail (atStatus=%s, raw=[%s])",
             App_Bc95AtGetStatusString(atStatus), g_appBc95AtRxBuf);
    return APP_STATUS_FATAL;
}

AppStatus_t App_Bc95AtFetchTimeWithRetry(AppBc95Time_t *p_time, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchTime(p_time);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;
        if (st == APP_STATUS_FATAL) return st;

        APP_LOGI("NBIOT", "CCLK retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);
        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_TIME_SYNC_RETRY_DELAY_MS);
    }
    return st;
}

/* ============================================================
 *  AT+CTZU=1 (자동 타임존 동기화 활성화)
 *    부팅 시 1회 호출하면 이후 네트워크 시간이 자동 갱신된다.
 *    실패해도 치명적이지 않음 (수동 CCLK 로 fallback 가능).
 * ============================================================ */
AppStatus_t App_Bc95AtEnableAutoTimezone(void)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CTZU_ENABLE,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "CTZU=1 send fail (status=%d) - ignored",
                 (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "CTZU=1 not OK (parse=%s) - ignored(optional feature not supported)",
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGI("NBIOT", "Auto-timezone (CTZU=1) enabled");
    return APP_STATUS_OK;
}

/* ============================================================
 *  통합: 모듈에서 시간 가져와 RTC 에 적용 (기존 RTC_SetTime 사용)
 *
 *  주의: RTC_SetTime() 의 year 인자가 2자리 ('25' 같은 BCD)인지
 *        4자리 ('2025')인지에 따라 호출 방식이 달라진다.
 *        STM32 HAL 의 RTC_DateTypeDef.Year 는 0~99 (2000 기준) 이므로
 *        2자리(YY) 전달이 표준이다. 여기서는 YY 만 전달.
 * ============================================================ */
AppStatus_t App_Bc95AtSyncTimeToRtc(void)
{
    AppBc95Time_t timeInfo;

  #if 0 //kiki TODO del
    AppStatus_t status;
    status = App_Bc95AtFetchTimeWithRetry(&timeInfo, APP_BC95_TIME_SYNC_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Time fetch failed (status=%d)", (int)status);
        return status;
    }

    APP_LOGI("NBIOT", "Module time: %04u-%02u-%02u %02u:%02u:%02u (TZ=%+d/15min)",
             (unsigned)timeInfo.dateTime.year, (unsigned)timeInfo.dateTime.month, (unsigned)timeInfo.dateTime.day,
             (unsigned)timeInfo.dateTime.hour, (unsigned)timeInfo.dateTime.minute, (unsigned)timeInfo.dateTime.second,
             (int)timeInfo.tzQuarterHour);

    /* 기존 RTC_SetTime() 사용
     *   year   : 2자리(YY, 2000 기준) - HAL_RTC_SetDate 가 요구하는 형식
     *   month  : 1~12
     *   date   : 1~31
     *   hour/min/sec : 0~23 / 0~59 / 0~59
     */
    RTC_SetTime((int)(timeInfo.dateTime.year - 2000u),
                (int)timeInfo.dateTime.month,
                (int)timeInfo.dateTime.day,
                (int)timeInfo.dateTime.hour,
                (int)timeInfo.dateTime.minute,
                (int)timeInfo.dateTime.second);

    APP_LOGI("NBIOT", "RTC set to 20%02u-%02u-%02u %02u:%02u:%02u",
             (unsigned)(timeInfo.dateTime.year - 2000u),
             (unsigned)timeInfo.dateTime.month, (unsigned)timeInfo.dateTime.day,
             (unsigned)timeInfo.dateTime.hour, (unsigned)timeInfo.dateTime.minute, (unsigned)timeInfo.dateTime.second);

    return APP_STATUS_OK;

  #else
if ((App_Bc95AtFetchTimeWithRetry(&timeInfo, APP_BC95_TIME_SYNC_RETRY_MAX) == APP_STATUS_OK) &&
    (timeInfo.valid != 0u))
{
    return App_ClockSyncFromNbiot(&timeInfo);   /* INITS/오차 판단 + 타임존 반영 */
}
return APP_STATUS_FATAL;
#endif

}

#ifndef APP_RTC_SYNC_THRESHOLD_SEC
#define APP_RTC_SYNC_THRESHOLD_SEC   (30)   /* 이 값(초) 초과 시 RTC 재설정 */
#endif

/* --- 시각 <-> epoch 변환  */
/* AppDateTime_t → 2000-01-01 기준 경과 초. 시각 비교 전용(윤년 반영) */
static int64_t App_DateTimeToEpoch(const AppDateTime_t *dt)
{
    static const uint16_t daysBeforeMonth[12] =
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int64_t days = 0;
    uint16_t y;

    for (y = 2000u; y < dt->year; y++)
    {
        uint8_t leap = (((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u))) ? 1u : 0u;
        days += (365 + leap);
    }
    if ((dt->month >= 1u) && (dt->month <= 12u))
    {
        days += daysBeforeMonth[dt->month - 1u];
        if (dt->month > 2u)
        {
            uint8_t leap = (((dt->year % 4u) == 0u) &&
                            (((dt->year % 100u) != 0u) || ((dt->year % 400u) == 0u))) ? 1u : 0u;
            days += leap;
        }
    }
    days += (dt->day - 1);

    return (days * 86400LL) + ((int64_t)dt->hour * 3600LL)
         + ((int64_t)dt->minute * 60LL) + (int64_t)dt->second;
}

/* epoch 초 → AppDateTime_t (역변환). 타임존 보정 결과를 다시 년/월/일로 */
static void App_EpochToDateTime(int64_t epoch, AppDateTime_t *dt)
{
    static const uint8_t mdaysNorm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int64_t days = epoch / 86400LL;
    int64_t rem  = epoch % 86400LL;
    uint16_t y = 2000u;

    if (rem < 0) { rem += 86400LL; days -= 1; }

    dt->hour   = (uint8_t)(rem / 3600LL);
    dt->minute = (uint8_t)((rem % 3600LL) / 60LL);
    dt->second = (uint8_t)(rem % 60LL);

    for (;;)
    {
        uint8_t leap = (((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u))) ? 1u : 0u;
        int64_t yearDays = 365 + leap;
        if (days < yearDays) break;
        days -= yearDays;
        y++;
    }
    dt->year = y;

    {
        uint8_t m;
        uint8_t leap = (((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u))) ? 1u : 0u;
        for (m = 0u; m < 12u; m++)
        {
            uint8_t dmax = mdaysNorm[m] + (((m == 1u) && leap) ? 1u : 0u);
            if (days < dmax) break;
            days -= dmax;
        }
        dt->month = (uint8_t)(m + 1u);
        dt->day   = (uint8_t)(days + 1);
    }
}

/**
 * @brief NB-IoT CCLK 시각을 타임존 보정 후 RTC와 비교하여,
 *        RTC가 미설정(INITS=0)이거나 오차가 임계값을 넘으면 RTC를 설정한다.
 */
AppStatus_t App_ClockSyncFromNbiot(const AppBc95Time_t *nbTime)
{
    AppDateTime_t rtcTime;
    int64_t nbEpoch;
    int64_t diff;
    uint8_t needSet = 0u;

    APP_RETURN_IF_FALSE((nbTime != NULL) && (nbTime->valid != 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(nbTime->dateTime.year >= APP_BC95_TIME_MIN_VALID_YEAR, APP_STATUS_INVALID_PARAM);

    APP_LOGI("NBIOT", "Get Module time: %04u-%02u-%02u %02u:%02u:%02u (TZ=%+d/15min)",
             (unsigned)nbTime->dateTime.year, (unsigned)nbTime->dateTime.month, (unsigned)nbTime->dateTime.day,
             (unsigned)nbTime->dateTime.hour, (unsigned)nbTime->dateTime.minute, (unsigned)nbTime->dateTime.second,
             (int)nbTime->tzQuarterHour);

    nbEpoch = App_DateTimeToEpoch(&nbTime->dateTime);

    if(!IsUpdatedRTC())
    {
        /* 백업 도메인이 초기 상태 → 아직 유효 시각 없음 */
        APP_LOGI("RTC", "INITS=0 (uninitialized) -> set from NBIoT");
        needSet = 1u;
    }
    else if (RTC_GetTime(&rtcTime) != APP_STATUS_OK)
    {
        APP_LOGW("RTC", "RTC read failed -> set from NBIoT");
        needSet = 1u;
    }
    else
    {
        diff = nbEpoch - App_DateTimeToEpoch(&rtcTime);
        if (diff < 0) { diff = -diff; }

        APP_LOGI("RTC", "NBIoT vs RTC diff=%ld sec", (long)diff);

        if (diff > (int64_t)APP_RTC_SYNC_THRESHOLD_SEC)
        {
            APP_LOGI("RTC", "diff exceeds %d sec -> resync", APP_RTC_SYNC_THRESHOLD_SEC);
            needSet = 1u;
        }
        else
        {
            APP_LOGI("RTC", "within tolerance -> keep RTC");
        }
    }

    if (needSet)
    {
        RTC_SetTime(nbTime->dateTime.year, nbTime->dateTime.month, nbTime->dateTime.day,
                    nbTime->dateTime.hour, nbTime->dateTime.minute, nbTime->dateTime.second);
        /* RTC_SetTime → HAL_RTC_SetTime/SetDate 가 INIT 진입/해제하며 INITS=1로 만듦 */

        APP_LOGI("NBIOT", "RTC set to %04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)nbTime->dateTime.year,
                 (unsigned)nbTime->dateTime.month, (unsigned)nbTime->dateTime.day,
                 (unsigned)nbTime->dateTime.hour, (unsigned)nbTime->dateTime.minute, (unsigned)nbTime->dateTime.second);

        App_FsmInvalidateRtcSchedules();
    }

    return APP_STATUS_OK;
}

const AppBc95Time_t *App_Bc95AtGetLastTime(void)
{
    return &g_appBc95AtLastTime;
}

/* ============================================================
 *  Application level wrapper
 * ============================================================ */
AppStatus_t App_NBIoTSyncTime(void)
{
    AppStatus_t status;

    APP_LOGI("NBIOT", "Synchronizing time from network...");

    status = App_Bc95AtSyncTimeToRtc();
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Time sync failed (status=%d)", (int)status);
        return status;
    }

    APP_LOGI("NBIOT", "Time sync done");
    return APP_STATUS_OK;
}

/* ============================================================
 *  Application level
 * ============================================================ */
AppStatus_t App_NBIoTAtInit(void)
{
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(&g_appBc95AtRxContext, 0, sizeof(g_appBc95AtRxContext));
    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));
    (void)memset(g_appBc95AtImeiBcd, 0, sizeof(g_appBc95AtImeiBcd));
    (void)memset(g_appBc95AtImsiBcd, 0, sizeof(g_appBc95AtImsiBcd));
    (void)memset(&g_appBc95AtQuality, 0, sizeof(g_appBc95AtQuality));
    (void)memset(g_appBc95AtQualityBcd, 0, sizeof(g_appBc95AtQualityBcd));
    (void)memset(&g_appBc95AtNetStatus, 0, sizeof(g_appBc95AtNetStatus));
    /*
     * ATT-05/06/07: do NOT clear g_appNbiotCarrierContext here.
     *
     * App_NBIoTAtInit() is called after SW/HW reset recovery inside one mandatory
     * attach session. Clearing the carrier context here resets attachAttemptCount,
     * swResetCount, hwResetCount, and lastResetType back to zero, which makes the
     * logs repeat as attempt=1 and SW reset 1/3 forever even though elapsed time is
     * still cumulative.
     *
     * The carrier context is intentionally initialized once at the start of
     * App_NBIoTCarrierAttachMandatory().
     */

    g_appBc95UdpSeqCounter = 0u;
    g_appBc95UsimFatal = APP_FALSE;
    g_appBc95AtInitialized = APP_TRUE;
#ifdef NBIoT_SIMULATION_CODE
    APP_LOGI("NBIOT", "Forcely err not initializedBC95 test..."); // NBIoT simulation test
    g_appBc95AtInitialized = APP_FALSE;
#endif // NBIoT_SIMULATION_CODE

    APP_LOGI("NBIOT", "NBIoT Init");

    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTBringUp(void)
{
    AppStatus_t status;

    status = App_Bc95AtWaitUntilReady(APP_BC95_BOOT_WAIT_BANNER_MS + 6000u);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Module boot timeout");
        return status;
    }

    status = App_Bc95AtWaitForServiceReady(APP_BC95_USIM_READY_TIMEOUT_MS +
                                           APP_BC95_SERVICE_READY_SETTLE_MS +
                                           APP_BC95_AT_RX_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        if (g_appBc95UsimFatal == APP_TRUE)
        {
            APP_LOGE("NBIOT", "Module service ready fatal");
        }
        else
        {
            APP_LOGE("NBIOT", "Module service ready fail (status=%d)", (int)status);
        }
        return status;
    }

    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CEREG_SET_3,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CEREG=3",
                                           APP_NBIOT_REPORT_LOG_ATTACH);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " failed to set CEREG=3 (status=%d)", (int)status);
        return status;
    }

    APP_LOGI("NBIOT", "Module ready");
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTBringUpWithReset(uint8_t maxResetRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint8_t i;

    if (maxResetRetry == 0u) maxResetRetry = 1u;

    if (g_appBc95AtInitialized != APP_TRUE)
    {
        (void)App_NBIoTAtInit();
    }

    for (i = 0u; i < maxResetRetry; i++)
    {
        st = App_NBIoTBringUp();
        if (st == APP_STATUS_OK) return APP_STATUS_OK;

        APP_LOGE("NBIOT", "BringUp failed (%d), HW reset %u/%u",
                 (int)st, (unsigned)(i + 1u), (unsigned)maxResetRetry);

        App_HwNbiotPowerCycle();   /* weak default: no-op */
        App_Bc95AtDelayWithFeed(2000u);
        (void)App_NBIoTAtInit();   /* 리셋 후 컨텍스트 재초기화 */
    }
    return st;
}

AppStatus_t App_NBIoTNetworkBringUp(void)
{
    AppStatus_t status;
    AppBc95NetStatus_t netStatus;

    status = App_Bc95AtWaitForNetwork(APP_BC95_NET_DEFAULT_TIMEOUT_MS, &netStatus);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Network not ready (phase=%s)",
                 App_Bc95AtGetNetPhaseString(netStatus.phase));
        return status;
    }

    /* 자동 타임존 활성화 (실패해도 무시) */
    (void)App_Bc95AtEnableAutoTimezone();

    /* 네트워크 시간 → RTC 동기화 */
    (void)App_NBIoTSyncTime();

#ifdef NBIOT_SUPPORT_DNS
    (void)App_Bc95AtWarmupDns(WARMUPDNS_SERVER_DOMAIN);
#endif // NBIOT_SUPPORT_DNS

    APP_LOGI("NBIOT", "Network ready");
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTCarrierAttachMandatory(void)
{
#if (APP_NBIOT_MANDATORY_LAYER_ENABLE != APP_TRUE)
    return App_NBIoTNetworkBringUp();
#else
    AppStatus_t status;
    AppStatus_t recoverStatus;
    AppBc95NetStatus_t netStatus;

    if (g_appBc95AtInitialized != APP_TRUE)
    {
        (void)App_NBIoTAtInit();
    }

    (void)memset(&g_appNbiotCarrierContext, 0, sizeof(g_appNbiotCarrierContext));
    g_appNbiotCarrierContext.attachStartTick = HAL_GetTick();
    g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_BOOT_WAIT;
    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_IDLE;
    g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_NONE;

    APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
             " mandatory start (window=%lums total=%lums swMax=%u hwMax=%u)",
             (unsigned long)APP_NBIOT_ATTACH_TIMEOUT_MS,
             (unsigned long)APP_NBIOT_ATTACH_TOTAL_FAIL_LIMIT_MS,
             (unsigned)APP_NBIOT_ATTACH_SW_RESET_MAX,
             (unsigned)APP_NBIOT_ATTACH_HW_RESET_MAX);

    while (1)
    {
        (void)memset(&netStatus, 0, sizeof(netStatus));
        g_appNbiotCarrierContext.attachAttemptCount++;
        g_appNbiotCarrierContext.lastAttemptTick = HAL_GetTick();
        g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_BOOT_WAIT;

        APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " attempt=%u state=%s",
                 (unsigned)g_appNbiotCarrierContext.attachAttemptCount,
                 App_NbiotCarrierAttachStateString(g_appNbiotCarrierContext.attachState));

        status = App_NBIoTBringUp();
        if (status == APP_STATUS_OK)
        {
            g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_NETWORK_WAIT;
            status = App_Bc95AtWaitForNetwork(APP_NBIOT_ATTACH_TIMEOUT_MS, &netStatus);
            g_appNbiotCarrierContext.lastNetStatus = netStatus;
        }

        g_appNbiotCarrierContext.lastStatus = status;

        if (status == APP_STATUS_OK)
        {
            g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_READY;
            (void)App_Bc95AtEnableAutoTimezone();
            (void)App_NBIoTSyncTime();
#ifdef NBIOT_SUPPORT_DNS
            (void)App_Bc95AtWarmupDns(WARMUPDNS_SERVER_DOMAIN);
#endif // NBIOT_SUPPORT_DNS
            APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                     " success attempt=%u elapsed=%lums phase=%s ip=%s",
                     (unsigned)g_appNbiotCarrierContext.attachAttemptCount,
                     (unsigned long)(HAL_GetTick() - g_appNbiotCarrierContext.attachStartTick),
                     App_Bc95AtGetNetPhaseString(netStatus.phase),
                     netStatus.ipAddr);
            return APP_STATUS_OK;
        }

        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " fail attempt=%u status=%d phase=%s CEREG=%s CGATT=%u ip=%s",
                 (unsigned)g_appNbiotCarrierContext.attachAttemptCount,
                 (int)status,
                 App_Bc95AtGetNetPhaseString(netStatus.phase),
                 App_Bc95AtGetCeregStatString(netStatus.ceregStat),
                 (unsigned)netStatus.cgattState,
                 netStatus.ipAddr);

        recoverStatus = App_NbiotCarrierRecoverAfterAttachFailure(status);
        if (recoverStatus != APP_STATUS_OK)
        {
            g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_ABORT;
            g_appNbiotCarrierContext.lastStatus = recoverStatus;
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                     " abort state=%s reset=%s status=%d",
                     App_NbiotCarrierAttachStateString(g_appNbiotCarrierContext.attachState),
                     App_NbiotCarrierResetTypeString(g_appNbiotCarrierContext.lastResetType),
                     (int)recoverStatus);
            return recoverStatus;
        }
    }
#endif
}

AppStatus_t App_NBIoTReadIdentity(uint8_t bSaveInfo)
{
    AppStatus_t status;
    uint8_t appImeiBcd[APP_BC95_IMEI_BCD_BYTES];
    uint8_t appImsiBcd[APP_BC95_IMSI_BCD_BYTES];

    status = App_Bc95AtFetchImeiWithRetry(appImeiBcd, sizeof(appImeiBcd),
                                          APP_BC95_IMEI_FETCH_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "IMEI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMEI", appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);
    App_Bc95PrintDecoded("IMEI", appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);

    status = App_Bc95AtFetchImsiWithRetry(appImsiBcd, sizeof(appImsiBcd),
                                          APP_BC95_IMSI_FETCH_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "IMSI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMSI", appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);
    App_Bc95PrintDecoded("IMSI", appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);

    /* ----- Options에 반영 (IMEI 8B + IMSI 8B = 16B) ----- */
    if (bSaveInfo == APP_TRUE)
    {
        AppMeterServerFormatOptions_t opt;

        (void)App_MeterServerOptionsLoad(&opt); /* 기존 값 유지하면서 일부만 갱신 */
        App_MeterServerOptionsSetMobileId(&opt, appImeiBcd, appImsiBcd);
        (void)App_MeterServerOptionsUpdate(&opt); /* 변경 시에만 EEPROM write */
    }

    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTReadQuality(uint8_t bSaveInfo)
{
    AppStatus_t status;
    AppBc95Quality_t quality;

    status = App_Bc95AtFetchQualityWithRetry(&quality, g_appBc95AtQualityBcd,
                                             sizeof(g_appBc95AtQualityBcd),
                                             APP_BC95_QUALITY_FETCH_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "Quality fetch failed (status=%d)", (int)status);
        return status;
    }
    App_NBIoTPrintQuality(&quality);
    App_Bc95PrintBcd("QUALITY", g_appBc95AtQualityBcd, (uint32_t)APP_BC95_QUALITY_BCD_BYTES);

    /* ----- Options에 반영 (NB-IoT 10B) ----- */
    if (bSaveInfo == APP_TRUE)
    {
        AppMeterServerFormatOptions_t opt;

        (void)App_MeterServerOptionsLoad(&opt);
        App_MeterServerOptionsSetWirelessQuality(&opt,
                                                 g_appBc95AtQualityBcd,
                                                 (uint8_t)APP_BC95_QUALITY_BCD_BYTES);
        (void)App_MeterServerOptionsUpdate(&opt);
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_NBIoTTransmitUdpInternal(const char *p_logTag,
                                                const char *p_host,
                                                uint16_t port,
                                                uint8_t deleteStorage,
                                                uint8_t unsentOnly,
                                                const AppMeterStorageRecord_t *p_liveRecord)
{
    AppStatus_t status;
    AppBc95UdpResult_t sendResult;
    AppMeterServerFormatOptions_t opt;
    AppMeterServerFormatResult_t buildResult;
    uint8_t packet[APP_METER_SERVER_FORMAT_MAX_PACKET_SIZE];

    APP_RETURN_IF_FALSE((p_logTag != NULL) && (p_host != NULL), APP_STATUS_INVALID_PARAM);

    App_MeterServerOptionsLoad(&opt);
    App_MeterServerOptionsDump(&opt);
    if (p_liveRecord != NULL)
    {
        status = App_MeterServerFormatBuildFromRecord(&opt, p_liveRecord, packet, sizeof(packet), &buildResult);
    }
    else
    {
        status = (unsentOnly == APP_TRUE)
                 ? App_MeterServerFormatBuildFromUnsentStorage(&opt, packet, sizeof(packet), &buildResult)
                 : App_MeterServerFormatBuildFromStorage(&opt, packet, sizeof(packet), &buildResult);
    }
    if (status == APP_STATUS_OK)
    {
        status = App_Bc95AtUdpSendOnce(p_logTag, p_host, port, packet, buildResult.packetLength, &sendResult);
        if ((status == APP_STATUS_OK) && (buildResult.recordCount != 0u))
        {
            if (p_liveRecord != NULL)
            {
                APP_LOGI("NBIOT", "%s live meter record sent without storage (records=%u)",
                         p_logTag,
                         (unsigned int)buildResult.recordCount);
            }
            else if (unsentOnly == APP_TRUE)
            {
                AppStatus_t markStatus = App_MeterStorageMarkOldestUnsentSent(buildResult.recordCount);
                if (markStatus != APP_STATUS_OK)
                {
                    APP_LOGE("NBIOT", "%s storage sent-mark failed (status=%ld, records=%u)",
                             p_logTag,
                             (long)markStatus,
                             (unsigned int)buildResult.recordCount);
                    return markStatus;
                }

                if (deleteStorage == APP_TRUE)
                {
                    uint8_t deletedCount = 0u;
                    AppStatus_t clearStatus = App_MeterStorageDeleteOldestSent(&deletedCount);
                    if (clearStatus != APP_STATUS_OK)
                    {
                        APP_LOGE("NBIOT", "%s storage delete failed (status=%ld, records=%u)",
                                 p_logTag,
                                 (long)clearStatus,
                                 (unsigned int)buildResult.recordCount);
                        return clearStatus;
                    }
                    buildResult.cleared = (deletedCount != 0u) ? APP_TRUE : APP_FALSE;
                    if (deletedCount != 0u)
                    {
                        APP_LOGI("NBIOT", "%s storage deleted (records=%u, remain=%u)",
                                 p_logTag,
                                 (unsigned int)deletedCount,
                                 (unsigned int)App_MeterStorageCount());
                    }
                }
                else
                {
                    APP_LOGI("NBIOT", "%s storage marked sent (records=%u, remain=%u)",
                             p_logTag,
                             (unsigned int)buildResult.recordCount,
                             (unsigned int)App_MeterStorageCount());
                }
            }
            else if (deleteStorage == APP_TRUE)
            {
                AppStatus_t clearStatus = App_MeterStorageDeleteOldest(buildResult.recordCount);
                if (clearStatus != APP_STATUS_OK)
                {
                    APP_LOGE("NBIOT", "%s storage delete failed (status=%ld, records=%u)",
                             p_logTag,
                             (long)clearStatus,
                             (unsigned int)buildResult.recordCount);
                    return clearStatus;
                }
                buildResult.cleared = APP_TRUE;
                APP_LOGI("NBIOT", "%s storage deleted (records=%u, remain=%u)",
                         p_logTag,
                         (unsigned int)buildResult.recordCount,
                         (unsigned int)App_MeterStorageCount());
            }
        }
    }
    else
    {
        APP_LOGI("NBIOT", "%s skip send", p_logTag);
    }

    APP_LOGD("NBIOT", "%s build len=%u payload=%u rec=%u checksum=0x%02X cleared=%u delete=%u live=%u",
             p_logTag,
             (unsigned int)buildResult.packetLength,
             (unsigned int)buildResult.payloadLength,
             (unsigned int)buildResult.recordCount,
             (unsigned int)buildResult.checksum,
             (unsigned int)buildResult.cleared,
             (unsigned int)deleteStorage,
             (unsigned int)((p_liveRecord != NULL) ? APP_TRUE : APP_FALSE));

    if (status == APP_STATUS_OK)
    {
        APP_LOGI("NBIOT", "%s SendResult: stage=%d, ip=%s, port=%u, sent=%u, seq=%u, confirmed=%u",
                 p_logTag,
                 (int)sendResult.lastStage,
                 sendResult.resolvedIp,
                 port,
                 (unsigned)sendResult.sentBytes,
                 (unsigned)sendResult.seqNumber,
                 (unsigned)sendResult.sendConfirmed);
    }

    return status;
}

AppStatus_t App_NBIoTTransmitServiceUdp(uint8_t deleteStorage)
{
#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_DOMAIN, APP_SERVICE_SERVER_PORT, deleteStorage, APP_TRUE, NULL);
#else
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_IP, APP_SERVICE_SERVER_PORT, deleteStorage, APP_TRUE, NULL);
#endif
}

AppStatus_t App_NBIoTTransmitMgmtUdp(uint8_t deleteStorage)
{
#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_DOMAIN, APP_MGMT_SERVER_PORT, deleteStorage, APP_FALSE, NULL);
#else
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_IP, APP_MGMT_SERVER_PORT, deleteStorage, APP_FALSE, NULL);
#endif
}

AppStatus_t App_NBIoTTransmitServiceLiveRecord(const AppMeterStorageRecord_t *p_record)
{
#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_DOMAIN, APP_SERVICE_SERVER_PORT, APP_FALSE, APP_FALSE, p_record);
#else
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_IP, APP_SERVICE_SERVER_PORT, APP_FALSE, APP_FALSE, p_record);
#endif
}

AppStatus_t App_NBIoTTransmitMgmtLiveRecord(const AppMeterStorageRecord_t *p_record)
{
#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_DOMAIN, APP_MGMT_SERVER_PORT, APP_FALSE, APP_FALSE, p_record);
#else
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_IP, APP_MGMT_SERVER_PORT, APP_FALSE, APP_FALSE, p_record);
#endif
}

AppStatus_t App_NBIoTTransmitUdp(void)
{
    return App_NBIoTTransmitServiceUdp(APP_TRUE);
}

AppStatus_t App_NBIoTCarrierPowerOffMandatory(void)
{
#if (APP_NBIOT_MANDATORY_LAYER_ENABLE != APP_TRUE)
    return App_GpioLpSetNbiotPowered(APP_FALSE);
#else
    AppStatus_t status = APP_STATUS_OK;
    AppStatus_t detachWaitStatus = APP_STATUS_OK;
    AppStatus_t cfunMinStatus = APP_STATUS_OK;

    g_appNbiotCarrierContext.lastPowerOffCfun0Status = APP_STATUS_OK;
    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_DETACH_REQ;
    g_appNbiotCarrierContext.lastDetachTick = HAL_GetTick();

    APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
             " mandatory start (close sockets + AT+CGATT=0 + wait CEREG:0 + AT+CFUN=0)");

    App_Bc95AtCloseAllSockets();

    if (g_appBc95AtInitialized == APP_TRUE)
    {
        if (g_appNbiotCarrierContext.attachState == APP_NBIOT_ATTACH_STATE_READY)
        {
            status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CGATT_DETACH,
                                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                                   "AT+CGATT=0",
                                                   APP_NBIOT_REPORT_LOG_DETACH);
            if (status == APP_STATUS_OK)
            {
                g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_WAIT_CEREG0;
                detachWaitStatus = App_NbiotCarrierWaitForCereg0(APP_NBIOT_DETACH_WAIT_CEREG0_MS);
                if (detachWaitStatus != APP_STATUS_OK)
                {
                    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " incomplete -> force power off (status=%d)", (int)detachWaitStatus);
                }
            }
            else
            {
                detachWaitStatus = status;
                APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " command failed -> force power off (status=%d)", (int)status);
            }
        }
        else
        {
            detachWaitStatus = APP_STATUS_OK;
            APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " skipped (attach not ready, state=%s)",
                     App_NbiotCarrierAttachStateString(g_appNbiotCarrierContext.attachState));
        }

        cfunMinStatus = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CFUN_SET_MIN,
                                                      APP_BC95_AT_RX_TIMEOUT_MS,
                                                      "AT+CFUN=0",
                                                      APP_NBIOT_REPORT_LOG_POWEROFF);
        if (cfunMinStatus != APP_STATUS_OK)
        {
            APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                     " CFUN=0 failed before power cut (status=%d)", (int)cfunMinStatus);
        }
    }
    else
    {
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                 " AT context not initialized, direct power off");
    }

    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_FORCE_OFF;
    status = App_GpioLpSetNbiotPowered(APP_FALSE);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF " power pin off failed (status=%d)", (int)status);
        g_appNbiotCarrierContext.lastStatus = status;
        return status;
    }

    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_DONE;
    g_appNbiotCarrierContext.lastPowerOffDoneTick = HAL_GetTick();
    g_appNbiotCarrierContext.lastPowerOffCfun0Status = cfunMinStatus;
    g_appNbiotCarrierContext.lastStatus = APP_STATUS_OK;
    APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
             " done state=%s detachStatus=%d cfun0Status=%d",
             App_NbiotCarrierPowerOffStateString(g_appNbiotCarrierContext.powerOffState),
             (int)detachWaitStatus,
             (int)cfunMinStatus);
    return APP_STATUS_OK;
#endif
}

const AppNbiotCarrierContext_t *App_NBIoTCarrierGetContext(void)
{
    return &g_appNbiotCarrierContext;
}

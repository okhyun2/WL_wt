#include "app_debug.h"

#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_dualboot.h"
#include "app_fsm.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_system.h"
#include "app_selftest.h"
#include "app_meter_storage.h"
#include "app_meter_server_format.h"
#include "app_aux.h"
#include "app_nbiot.h"
#include "app_nfc_seoul_format.h"

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
static const char g_appDebugPrompt[] = APP_DEBUG_CONSOLE_PROMPT;
#define APP_DEBUG_CONSOLE_RX_FIFO_SIZE    (128u)
static volatile uint8_t g_appDebugRxItByte;
static volatile uint8_t g_appDebugRxFifo[APP_DEBUG_CONSOLE_RX_FIFO_SIZE];
static volatile uint16_t g_appDebugRxFifoHead;
static volatile uint16_t g_appDebugRxFifoTail;
static volatile uint32_t g_appDebugRxFifoOverflowCount;
static volatile uint8_t g_appDebugRxInterruptArmed;
#endif
static AppDebugConsoleContext_t g_appDebugConsoleContext;

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
static AppStatus_t App_DebugConsoleWriteLine(const char *p_text);
static AppStatus_t App_DebugConsoleStartRxInterrupt(void);
static uint8_t App_DebugConsolePopRxByte(uint8_t *p_rxByte);
static void App_DebugConsolePushRxByteFromIsr(uint8_t rxByte);
static AppStatus_t App_DebugConsolePrintSelfTestSummary(const char *p_prefix)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppSelfTestContext_t *p_context;
    int32_t formattedLength;

    p_context = App_SelfTestGetContext();
    APP_RETURN_IF_FALSE((p_context != NULL), APP_STATUS_NOT_INITIALIZED);

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "%s status=%lu pass=%lu fail=%lu last_tick=%lu running=%u",
                               (p_prefix != NULL) ? p_prefix : "selftest",
                               (unsigned long)p_context->lastSequenceStatus,
                               (unsigned long)p_context->passCount,
                               (unsigned long)p_context->failCount,
                               (unsigned long)p_context->lastRunTickMs,
                               (unsigned int)p_context->running);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    return App_DebugConsoleWriteLine(txBuffer);
}

static AppStatus_t App_DebugConsolePrintMeterStorageSummary(void)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    AppMeterStorageInfo_t info;
    AppStatus_t status;
    int32_t formattedLength;

    status = App_MeterStorageGetInfo(&info);
    if (status != APP_STATUS_OK)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "mstor info failed status=%lu",
                                   (unsigned long)status);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "mstor init=%u head=%u count=%u next_seq=%u max=%u",
                               (unsigned int)info.initialized,
                               (unsigned int)info.head,
                               (unsigned int)info.count,
                               (unsigned int)info.nextSeq,
                               (unsigned int)APP_METER_STORAGE_MAX_RECORDS);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    return App_DebugConsoleWriteLine(txBuffer);
}

static AppStatus_t App_DebugConsolePrintMeterStorageDump(void)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    AppMeterStorageInfo_t info;
    AppMeterStorageRecord_t record;
    AppStatus_t status;
    uint8_t index;
    int32_t formattedLength;

    status = App_MeterStorageGetInfo(&info);
    if (status != APP_STATUS_OK)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "mstor dump failed status=%lu",
                                   (unsigned long)status);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (info.count == 0u)
    {
        return App_DebugConsoleWriteLine("mstor dump empty");
    }

    for (index = 0u; index < info.count; index++)
    {
        status = App_MeterStorageReadAt(index, &record);
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer,
                                       sizeof(txBuffer),
                                       "[%02u] read failed status=%lu",
                                       (unsigned int)index,
                                       (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
            continue;
        }

        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "[%02u] seq=%u src=%u flg=0x%02X ts=%02u/%02u/%02u %02u:%02u:%02u",
                                   (unsigned int)index,
                                   (unsigned int)record.seq,
                                   (unsigned int)record.srcType,
                                   (unsigned int)record.flags,
                                   (unsigned int)record.ts[0],
                                   (unsigned int)record.ts[1],
                                   (unsigned int)record.ts[2],
                                   (unsigned int)record.ts[3],
                                   (unsigned int)record.ts[4],
                                   (unsigned int)record.ts[5]);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "     id=%lu read=%lu stat=0x%02X batt=0x%02X type=0x%02X caldec=0x%02X crc=0x%02X",
                                   (unsigned long)record.meterId,
                                   (unsigned long)record.readingScaled,
                                   (unsigned int)record.meterStatus,
                                   (unsigned int)record.meterBattery,
                                   (unsigned int)record.meterType,
                                   (unsigned int)record.caliberDecimal,
                                   (unsigned int)record.crc8);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_DebugConsolePrintHexBuffer(const uint8_t *p_data, uint16_t length)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    uint16_t offset;
    uint16_t index;
    uint16_t lineIndex;
    int32_t formattedLength;
    int32_t appendLength;

    APP_RETURN_IF_FALSE(p_data != NULL, APP_STATUS_INVALID_PARAM);

    for (offset = 0u; offset < length; offset = (uint16_t)(offset + 16u))
    {
        formattedLength = snprintf(txBuffer, sizeof(txBuffer), "%04u :", (unsigned int)offset);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);

        for (index = offset, lineIndex = 0u; (index < length) && (lineIndex < 16u); index++, lineIndex++)
        {
            appendLength = snprintf(&txBuffer[formattedLength],
                                    (size_t)(sizeof(txBuffer) - (size_t)formattedLength),
                                    " %02X",
                                    (unsigned int)p_data[index]);
            APP_RETURN_IF_FALSE((appendLength >= 0), APP_STATUS_INIT_FAILED);
            formattedLength += appendLength;
        }

        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

static const char *App_DebugConsoleNfcReadSourceString(uint8_t source)
{
    switch (source)
    {
        case 1u: return "SRAM";
        case 2u: return "EEPROM";
        default: return "NONE";
    }
}

static AppStatus_t App_DebugConsoleDumpNfcPayload(void)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppNfcSeoulDebugInfo_t *p_info;
    int32_t formattedLength;

    p_info = App_NfcSeoulGetDebugInfo();
    APP_RETURN_IF_FALSE((p_info != NULL), APP_STATUS_NOT_INITIALIZED);

    if (p_info->initialized != APP_TRUE)
    {
        return App_DebugConsoleWriteLine("nfcdump empty");
    }

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "nfcdump req=%lu rsp=%lu stor=%lu tick=%lu src=%s handled=%u comm=%u status=%u",
                               (unsigned long)p_info->requestCount,
                               (unsigned long)p_info->responseCount,
                               (unsigned long)p_info->storageRefreshCount,
                               (unsigned long)p_info->lastTickMs,
                               App_DebugConsoleNfcReadSourceString(p_info->lastReadSource),
                               (unsigned int)p_info->lastHandled,
                               (unsigned int)p_info->lastCommRequested,
                               (unsigned int)p_info->lastStatus);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "nfcdump req cmd=%02X%02X len=%u rsp cmd=%02X%02X len=%u",
                               (unsigned int)p_info->lastRequestCmd1,
                               (unsigned int)p_info->lastRequestCmd2,
                               (unsigned int)p_info->lastRequestLength,
                               (unsigned int)p_info->lastResponseCmd1,
                               (unsigned int)p_info->lastResponseCmd2,
                               (unsigned int)p_info->lastResponseLength);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    if (p_info->lastRequestLength != 0u)
    {
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine("nfcdump request hex") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsolePrintHexBuffer(p_info->lastRequest,
                                                           p_info->lastRequestLength) == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }
    else
    {
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine("nfcdump request hex empty") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    if (p_info->lastResponseLength != 0u)
    {
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine("nfcdump response hex") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsolePrintHexBuffer(p_info->lastResponse,
                                                           p_info->lastResponseLength) == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
    }
    else
    {
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine("nfcdump response hex empty") == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

/* ============================================================
 *  NB-IoT 주기보고 메시지 (0xA3 / 0x70) 덤프 함수
 *  - 메시지 헤더 : 0xA3 (V1.5 기준, 이전 V1.4는 0xA1)
 *  - 메시지 커맨드: 0x70 (주기보고)
 *  - 멀티바이트 정수 : Little-Endian
 *  - BCD 필드        : 상위 니블 먼저
 *  - 체크섬          : 메시지 길이 다음 ~ 체크섬 직전까지의 합
 * ============================================================ */

#define APP_NBIOT_PERIODIC_MSG_HEADER       (0xA3u)
#define APP_NBIOT_PERIODIC_MSG_HEADER_V14   (0xA1u)
#define APP_NBIOT_PERIODIC_MSG_CMD          (0x70u)
#define APP_NBIOT_PERIODIC_MIN_LENGTH       (62u)   /* 헤더~체크섬 포함 최소 길이 */
#define APP_NBIOT_PERIODIC_FIXED_LENGTH     (53u)   /* 검침데이터 직전까지 (off=0..52) */


/* ---------- 내부 유틸리티 ---------- */

static uint32_t App_NbiotDumpReadLeUint(const uint8_t *buf, uint8_t len)
{
    uint32_t value = 0u;
    uint8_t  i;

    for (i = 0u; i < len; i++)
    {
        value |= ((uint32_t)buf[i]) << (8u * i);
    }
    return value;
}

static void App_NbiotDumpBcdToStr(const uint8_t *buf, uint8_t len, char *out)
{
    uint8_t i;
    uint8_t hi;
    uint8_t lo;

    for (i = 0u; i < len; i++)
    {
        hi = (buf[i] >> 4) & 0x0Fu;
        lo = buf[i] & 0x0Fu;
        out[(i * 2u) + 0u] = (hi < 10u) ? (char)('0' + hi) : (char)('A' + hi - 10u);
        out[(i * 2u) + 1u] = (lo < 10u) ? (char)('0' + lo) : (char)('A' + lo - 10u);
    }
    out[len * 2u] = '\0';
}

static const char *App_NbiotDumpDiameterString(uint8_t code)
{
    switch (code)
    {
        case 0x1u: return "15mm";
        case 0x2u: return "20mm";
        case 0x3u: return "25mm";
        case 0x4u: return "32mm";
        case 0x5u: return "40mm";
        case 0x6u: return "50mm";
        case 0x7u: return "80mm";
        case 0x8u: return "100mm";
        case 0x9u: return "150mm";
        case 0xAu: return "200mm";
        case 0xBu: return "250mm";
        case 0xCu: return "300mm";
        default:   return "Unknown";
    }
}

static AppStatus_t App_DebugConsoleDumpNbiotPeriodicReport(const uint8_t *packet, uint32_t length)
{
    char     txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    char     bcdBuffer[64];
    int32_t  formattedLength;
    uint32_t offset;
    uint8_t  decimalPos;
    uint8_t  recordCount;
    uint8_t  basePos;
    uint8_t  meterPeriod;
    uint32_t baseValue;
    uint32_t curValue;
    uint8_t  calcChecksum;
    uint8_t  rxChecksum;
    uint8_t  i;
    uint32_t k;

    /* 1) 입력 검증 */
    if ((packet == NULL) || (length < APP_NBIOT_PERIODIC_MIN_LENGTH))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "nbdump invalid len=%lu",
                                   (unsigned long)length);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((packet[0] != APP_NBIOT_PERIODIC_MSG_HEADER) &&
        (packet[0] != APP_NBIOT_PERIODIC_MSG_HEADER_V14))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "nbdump bad header=0x%02X",
                                   (unsigned int)packet[0]);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (packet[2] != APP_NBIOT_PERIODIC_MSG_CMD)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "nbdump bad cmd=0x%02X",
                                   (unsigned int)packet[2]);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    /* 2) 헤더 라인 + 전체 hex 덤프 */
    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "nbdump start len=%lu hdr=0x%02X cmd=0x%02X",
                               (unsigned long)length,
                               (unsigned int)packet[0],
                               (unsigned int)packet[2]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsolePrintHexBuffer(packet, length) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    offset = 0u;

    /* 3) 메시지 헤더 / 길이 / 형식 */
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] header        = 0x%02X (%s)",
                               (unsigned long)offset, (unsigned int)packet[offset],
                               (packet[offset] == APP_NBIOT_PERIODIC_MSG_HEADER) ? "V1.5" : "V1.4");
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] msg length    = %u bytes",
                               (unsigned long)offset, (unsigned int)packet[offset]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] msg command   = 0x%02X (periodic report)",
                               (unsigned long)offset, (unsigned int)packet[offset]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    /* 4) 이동통신 ID : IMEI(8) + IMSI(8) */
    App_NbiotDumpBcdToStr(&packet[offset], 8u, bcdBuffer);
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] IMEI          = %s",
                               (unsigned long)offset, bcdBuffer);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 8u;

    App_NbiotDumpBcdToStr(&packet[offset], 8u, bcdBuffer);
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] IMSI          = %s",
                               (unsigned long)offset, bcdBuffer);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 8u;

    /* 5) 무선품질 정보 (10B) */
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] RSSI          = -%u dBm",
                               (unsigned long)offset, (unsigned int)packet[offset]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] BER           = %u",
                               (unsigned long)offset, (unsigned int)packet[offset]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] CID           = %lu",
                               (unsigned long)offset,
                               (unsigned long)App_NbiotDumpReadLeUint(&packet[offset], 2u));
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 2u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] RSRP          = -%lu dBm",
                               (unsigned long)offset,
                               (unsigned long)App_NbiotDumpReadLeUint(&packet[offset], 2u));
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 2u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] RSRQ          = -%lu dBm",
                               (unsigned long)offset,
                               (unsigned long)App_NbiotDumpReadLeUint(&packet[offset], 2u));
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 2u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] SNR           = %lu dBm",
                               (unsigned long)offset,
                               (unsigned long)App_NbiotDumpReadLeUint(&packet[offset], 2u));
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 2u;

    /* 6) 단말기 정보 (8B) */
    App_NbiotDumpBcdToStr(&packet[offset], 5u, bcdBuffer);
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] dev serial    = %s",
                               (unsigned long)offset, bcdBuffer);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 5u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] FW version    = V%u.%u",
                               (unsigned long)offset,
                               (unsigned int)packet[offset],
                               (unsigned int)packet[offset + 1u]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 2u;

    {
        uint8_t b = packet[offset];
        uint8_t alarm = (b >> 7) & 0x01u;
        uint8_t v10 = b & 0x3Fu;   /* 전압 * 10 */
        formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                   "[%03lu] battery       = %u.%uV alarm=%u",
                                   (unsigned long)offset,
                                   (unsigned int)(v10 / 10u),
                                   (unsigned int)(v10 % 10u),
                                   (unsigned int)alarm);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }
    offset += 1u;

    /* 7) 계량기 정보 (7B) */
    App_NbiotDumpBcdToStr(&packet[offset], 4u, bcdBuffer);
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] meter serial  = %s",
                               (unsigned long)offset, bcdBuffer);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 4u;

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] meter type    = 0x%02X",
                               (unsigned long)offset, (unsigned int)packet[offset]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    {
        uint8_t b = packet[offset];
        uint8_t dia = (b >> 4) & 0x0Fu;
        decimalPos = b & 0x0Fu;
        formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                   "[%03lu] dia/decimal   = %s, decimal=%u",
                                   (unsigned long)offset,
                                   App_NbiotDumpDiameterString(dia),
                                   (unsigned int)decimalPos);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }
    offset += 1u;

    {
        uint8_t s = packet[offset];
        formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                   "[%03lu] meter status  = 0x%02X (ov=%u rev=%u leak=%u lowV=%u)",
                                   (unsigned long)offset, (unsigned int)s,
                                   (unsigned int)((s >> 7) & 1u),
                                   (unsigned int)((s >> 6) & 1u),
                                   (unsigned int)((s >> 5) & 1u),
                                   (unsigned int)((s >> 2) & 1u));
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }
    offset += 1u;

    /* 8) 검침/보고 주기 (2B) */
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] meter/report  = %uh / %uh",
                               (unsigned long)offset,
                               (unsigned int)packet[offset],
                               (unsigned int)packet[offset + 1u]);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 2u;

    /* 9) 검침 시간 (6B) */
    {
        uint8_t y  = packet[offset];
        uint8_t mo = packet[offset + 1u];
        uint8_t d  = packet[offset + 2u];
        uint8_t h  = packet[offset + 3u];
        uint8_t mi = packet[offset + 4u];
        uint8_t se = packet[offset + 5u];

        if ((y == 0xFFu) && (mo == 0xFFu) && (d == 0xFFu))
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                       "[%03lu] meter time    = unsync (-%02uh%02um%02us)",
                                       (unsigned long)offset,
                                       (unsigned int)h, (unsigned int)mi, (unsigned int)se);
        }
        else
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                       "[%03lu] meter time    = 20%02u-%02u-%02u %02u:%02u:%02u",
                                       (unsigned long)offset,
                                       (unsigned int)y, (unsigned int)mo, (unsigned int)d,
                                       (unsigned int)h, (unsigned int)mi, (unsigned int)se);
        }
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }
    offset += 6u;

    /* 10) 검침 데이터 영역 */
    meterPeriod = packet[offset];
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] data period   = %uh",
                               (unsigned long)offset, (unsigned int)meterPeriod);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    recordCount = packet[offset];
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] record count  = %u",
                               (unsigned long)offset, (unsigned int)recordCount);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    basePos = packet[offset];
    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] base position = %u%s",
                               (unsigned long)offset, (unsigned int)basePos,
                               (basePos == 0xFFu) ? " (no valid reading)" : "");
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 1u;

    baseValue = App_NbiotDumpReadLeUint(&packet[offset], 4u);
    if (baseValue == 0xFFFFFFFFu)
    {
        formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                   "[%03lu] base value    = 0xFFFFFFFF (invalid)",
                                   (unsigned long)offset);
    }
    else
    {
        /* 정수 표시 + 소수점 자리수 정보 */
        formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                   "[%03lu] base value    = %lu (decimal=%u)",
                                   (unsigned long)offset,
                                   (unsigned long)baseValue,
                                   (unsigned int)decimalPos);
    }
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    offset += 4u;

    /* 차이값 n개 (각 2B) - 기준 검침값에서 차례로 빼서 이전 검침값 산출 */
    curValue = baseValue;
    for (i = 0u; i < recordCount; i++)
    {
        uint16_t diff;

        if ((offset + 2u) > (length - 1u))
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                       "nbdump truncated at diff#%u", (unsigned int)i);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }

        diff = (uint16_t)App_NbiotDumpReadLeUint(&packet[offset], 2u);

        if (diff == 0xFFFFu)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                       "[%03lu] diff[%02u]      = 0xFFFF (read fail)",
                                       (unsigned long)offset, (unsigned int)i);
        }
        else if ((i >= basePos) && (curValue != 0xFFFFFFFFu))
        {
            curValue -= diff;
            formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                       "[%03lu] diff[%02u]      = -%u  => value=%lu",
                                       (unsigned long)offset, (unsigned int)i,
                                       (unsigned int)diff, (unsigned long)curValue);
        }
        else
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                                       "[%03lu] diff[%02u]      = -%u",
                                       (unsigned long)offset, (unsigned int)i,
                                       (unsigned int)diff);
        }
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
        offset += 2u;
    }

    /* 11) 체크섬 검증 (메시지 길이 다음 ~ 체크섬 직전까지의 합) */
    calcChecksum = 0u;
    for (k = 2u; k < (length - 1u); k++)
    {
        calcChecksum = (uint8_t)(calcChecksum + packet[k]);
    }
    rxChecksum = packet[length - 1u];

    formattedLength = snprintf(txBuffer, sizeof(txBuffer),
                               "[%03lu] checksum      = 0x%02X (calc=0x%02X %s)",
                               (unsigned long)(length - 1u),
                               (unsigned int)rxChecksum,
                               (unsigned int)calcChecksum,
                               (rxChecksum == calcChecksum) ? "OK" : "MISMATCH");
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    /* 12) 요약 라인 */
    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "nbdump ok len=%lu rec=%u base=%lu checksum=%s",
                               (unsigned long)length,
                               (unsigned int)recordCount,
                               (unsigned long)baseValue,
                               (rxChecksum == calcChecksum) ? "OK" : "NG");
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    return App_DebugConsoleWriteLine(txBuffer);
}

static AppStatus_t App_DebugConsoleRunMeterConvert(uint8_t clearOnSuccess)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    AppMeterStorageInfo_t info;
    AppMeterServerFormatOptions_t options;
    AppMeterServerFormatResult_t result;
    uint8_t packet[APP_METER_SERVER_FORMAT_MAX_PACKET_SIZE];
    AppStatus_t status;
    int32_t formattedLength;

    status = App_MeterStorageGetInfo(&info);
    if (status != APP_STATUS_OK)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "mconv info failed status=%lu",
                                   (unsigned long)status);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (info.count == 0u)
    {
        return App_DebugConsoleWriteLine("mconv empty");
    }

    //load saved data
    if (App_MeterServerOptionsLoad(&options) == APP_STATUS_NOT_INITIALIZED)
    {
        App_MeterServerOptionsSave(&options);
    }
    App_MeterServerOptionsDump(&options);

    if (clearOnSuccess == APP_TRUE)
    {
        status = App_MeterServerFormatBuildFromStorageAndClear(&options, packet, sizeof(packet), &result);
    }
    else
    {
        status = App_MeterServerFormatBuildFromStorage(&options, packet, sizeof(packet), &result);
    }

    if (status != APP_STATUS_OK)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "mconv failed status=%lu",
                                   (unsigned long)status);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "mconv ok len=%u payload=%u rec=%u checksum=0x%02X cleared=%u",
                               (unsigned int)result.packetLength,
                               (unsigned int)result.payloadLength,
                               (unsigned int)result.recordCount,
                               (unsigned int)result.checksum,
                               (unsigned int)result.cleared);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsolePrintHexBuffer(packet, result.packetLength) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    /* 생성된 패킷을 NB-IoT 주기보고 메시지 포맷으로 파싱하여 덤프 */
    APP_RETURN_IF_FALSE(App_DebugConsoleDumpNbiotPeriodicReport(packet, (uint32_t)result.packetLength) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);


    return App_DebugConsolePrintMeterStorageSummary();
}

#ifdef DEBUG
// a33b70869692066911391f450061225187489f58004a005e000a000e003324700005000924ffffffff01130101061a04020b353a01010000000000000042
static AppStatus_t App_DebugConsoleRunNbiotDumpTest(void)
{
    static const uint8_t packet[] = {
        0xA3, 0x3B, 0x70, 0x86, 0x96, 0x92, 0x06, 0x69,
        0x11, 0x39, 0x1F, 0x45, 0x00, 0x61, 0x22, 0x51,
        0x87, 0x48, 0x9F, 0x58, 0x00, 0x4A, 0x00, 0x5E,
        0x00, 0x0A, 0x00, 0x0E, 0x00, 0x33, 0x24, 0x70,
        0x00, 0x05, 0x00, 0x09, 0x24, 0xFF, 0xFF, 0xFF,
        0xFF, 0x01, 0x13, 0x01, 0x01, 0x06, 0x1A, 0x04,
        0x02, 0x0B, 0x35, 0x3A, 0x01, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x42};

    return App_DebugConsoleDumpNbiotPeriodicReport(packet, (uint32_t)sizeof(packet));
}
#endif // DEBUG

#endif

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
static AppStatus_t App_DebugConsoleStartRxInterrupt(void)
{
    HAL_StatusTypeDef halStatus;

    if (g_appDebugConsoleContext.initialized != APP_TRUE)
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    if (g_appDebugRxInterruptArmed == APP_TRUE)
    {
        return APP_STATUS_OK;
    }

    __HAL_UART_CLEAR_FLAG(APP_UART_DEBUG_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_DEBUG_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(APP_UART_DEBUG_HANDLE, (uint8_t *)&g_appDebugRxItByte, 1u);
    if ((halStatus != HAL_OK) && (halStatus != HAL_BUSY))
    {
        return APP_STATUS_UART_RX_FAILED;
    }

    g_appDebugRxInterruptArmed = APP_TRUE;
    return APP_STATUS_OK;
}

static uint8_t App_DebugConsolePopRxByte(uint8_t *p_rxByte)
{
    uint16_t tail;
    uint16_t nextTail;

    if (p_rxByte == NULL)
    {
        return APP_FALSE;
    }

    tail = g_appDebugRxFifoTail;
    if (tail == g_appDebugRxFifoHead)
    {
        return APP_FALSE;
    }

    *p_rxByte = g_appDebugRxFifo[tail];
    nextTail = (uint16_t)(tail + 1u);
    if (nextTail >= APP_DEBUG_CONSOLE_RX_FIFO_SIZE)
    {
        nextTail = 0u;
    }
    g_appDebugRxFifoTail = nextTail;
    return APP_TRUE;
}

static void App_DebugConsolePushRxByteFromIsr(uint8_t rxByte)
{
    uint16_t head;
    uint16_t nextHead;

    head = g_appDebugRxFifoHead;
    nextHead = (uint16_t)(head + 1u);
    if (nextHead >= APP_DEBUG_CONSOLE_RX_FIFO_SIZE)
    {
        nextHead = 0u;
    }

    if (nextHead == g_appDebugRxFifoTail)
    {
        g_appDebugRxFifoOverflowCount++;
        return;
    }

    g_appDebugRxFifo[head] = rxByte;
    g_appDebugRxFifoHead = nextHead;
}

static AppStatus_t App_DebugConsoleWriteLine(const char *p_text)
{
    AppStatus_t status;

    status = App_DebugConsoleWriteString(p_text);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    return App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_EOL);
}

static AppStatus_t App_DebugConsolePrintComponentTable(void)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppFsmSummary_t *p_fsmSummary;
    uint32_t index;
    int32_t formattedLength;
    uint32_t nowTick;

    p_fsmSummary = App_FsmGetSummary();
    nowTick = HAL_GetTick();

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "fsm=%s decision=%s disp=%lu loop=%lu q=%u",
                               App_FsmGetCurrentStateString(),
                               App_FsmGetDecisionString(),
                               (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                               (unsigned long)p_fsmSummary->loopCount,
                               (unsigned int)App_MsgqGetCount());
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    for (index = 0u; index < (uint32_t)APP_FSM_COMPONENT_COUNT; index++)
    {
        const AppFsmComponentContext_t *p_component;
        uint32_t ageMs;

        p_component = App_FsmGetComponent((AppFsmComponentId_t)index);
        if (p_component == NULL)
        {
            continue;
        }

        ageMs = (p_component->lastRunTickMs != 0u) ? (nowTick - p_component->lastRunTickMs) : 0u;
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "[%02lu] %-12s run=%lu int=%lu state=%-24s busy=%u evt=%u age=%lu last=%lu",
                                   (unsigned long)index,
                                   (p_component->p_name != NULL) ? p_component->p_name : "-",
                                   (unsigned long)p_component->runCount,
                                   (unsigned long)p_component->intervalMs,
                                   App_FsmGetStateName(p_component->state),
                                   (unsigned int)p_component->busy,
                                   (unsigned int)p_component->eventPending,
                                   (unsigned long)ageMs,
                                   (unsigned long)p_component->lastStatus);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_DebugConsoleParseState(const char *p_token, uint8_t *p_state)
{
    APP_RETURN_IF_FALSE((p_token != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_state != NULL), APP_STATUS_INVALID_PARAM);

    if (strcmp(p_token, "boot") == 0)
    {
        *p_state = APP_FSM_STATE_BOOT;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "idle") == 0) || (strcmp(p_token, "stop") == 0))
    {
        *p_state = APP_FSM_STATE_IDLE;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "debug") == 0) || (strcmp(p_token, "dbg") == 0))
    {
        *p_state = APP_FSM_STATE_DEBUG_POLL;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "power") == 0) || (strcmp(p_token, "reset") == 0))
    {
        *p_state = APP_FSM_STATE_POWER_WAIT_REQUEST;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "hk") == 0) || (strcmp(p_token, "housekeeping") == 0))
    {
        *p_state = APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "meter") == 0)
    {
        *p_state = APP_FSM_STATE_METER_WAIT_TRIGGER;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "nfc") == 0)
    {
        *p_state = APP_FSM_STATE_NFC_WAIT_EVENT;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "nbiot") == 0)
    {
        *p_state = APP_FSM_STATE_NBIOT_DECIDE_WAKE;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "server") == 0)
    {
        *p_state = APP_FSM_STATE_SERVER_PREPARE_PACKET;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "rtc") == 0)
    {
        *p_state = APP_FSM_STATE_RTC_WAKE_SERVICE;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "fault") == 0) || (strcmp(p_token, "safe") == 0))
    {
        *p_state = APP_FSM_STATE_FAULT;
        return APP_STATUS_OK;
    }

    return APP_STATUS_INVALID_PARAM;
}

static AppStatus_t App_DebugConsoleExecuteCommand(const char *p_command)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppSystemContext_t *p_systemContext;
    const AppClockContext_t *p_clockContext;
    const AppErrorRecord_t *p_errorRecord;
    const AppFsmSummary_t *p_fsmSummary;
    const AppMsgqContext_t *p_msgqContext;
    int32_t formattedLength;
    AppStatus_t status;

    if ((p_command == NULL) || (p_command[0] == '\0'))
    {
        return APP_STATUS_OK;
    }

    p_systemContext = App_SystemGetContext();
    p_clockContext = App_ClockGetContext();
    p_errorRecord = App_ErrorGetLast();
    p_fsmSummary = App_FsmGetSummary();
    p_msgqContext = App_MsgqGetContext();

    if (strcmp(p_command, "help") == 0)
    {
        if (App_DebugConsoleWriteLine("help                     : show command list") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("ver                      : show firmware version") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("status                   : show system summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("clock                    : show boot clock summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("time                     : show date time summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("error                    : show last error record") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("fsm                      : show state-machine summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("components               : show component table") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("q                        : show queue status") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("lp                       : show low-power state and wake source") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("slot                     : show current running slot") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("aux                      : show aux(adc, temperature) value ") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc help                 : show NFC CLI commands") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc status|driver|auth   : show NFC state/statistics") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc cmd|lp|uid           : show NFC command/lp/uid info") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc init|wake|exchange   : control NFC FSM/module") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc logout               : invalidate NFC session") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc dump                 : dump last Seoul NFC req/rsp payload") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
         if (App_DebugConsoleWriteLine("selftest                 : run self-test sequence") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("selftest status          : show last self-test summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nbiot                    : run nbiot-test sequence") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("mstor                    : show meter ring info and dump") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("mstor info               : show meter ring structure") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("mstor dump               : show stored meter EEPROM records") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("mconv test               : build server packet from EEPROM and print hex") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("mconv run                : build server packet and clear EEPROM on success") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("resetboot                : enter STM32 ROM bootloader") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("update2                  : mark slot2 update and enter ROM bootloader") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm                       : show current FSM state") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm front <state>         : push state command to front") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm back <state>          : push state command to rear") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("echo on                  : enable console echo") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("echo off                 : disable console echo") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        return APP_STATUS_OK;
    }

    if (strcmp(p_command, "ver") == 0)
    {
        formattedLength = snprintf(txBuffer, sizeof(txBuffer), "%s v%s", APP_NAME_STRING, App_SystemGetVersionString());
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "status") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "boot=%lu loop=%lu idle=%lu disp=%lu fsm=%s q=%u lp=%s stop_req=%u stop=%lu wake=%s",
                                   (unsigned long)p_systemContext->bootStage,
                                   (unsigned long)p_systemContext->loopCounter,
                                   (unsigned long)p_systemContext->idleCounter,
                                   (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                                   App_FsmGetCurrentStateString(),
                                   (unsigned int)App_MsgqGetCount(),
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   App_SystemGetWakeSourceString());
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "clock") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "src=%lu sys=%lu hclk=%lu pclk1=%lu pclk2=%lu msi=%lu lse=%u flash=%lu",
                                   (unsigned long)p_clockContext->sysclkSource,
                                   (unsigned long)p_clockContext->sysclkHz,
                                   (unsigned long)p_clockContext->hclkHz,
                                   (unsigned long)p_clockContext->pclk1Hz,
                                   (unsigned long)p_clockContext->pclk2Hz,
                                   (unsigned long)p_clockContext->msiRange,
                                   (unsigned int)p_clockContext->lseReady,
                                   (unsigned long)p_clockContext->flashLatency);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "time") == 0)
    {
        RTC_PrintTime();
        return APP_STATUS_OK;
    }

    if (strcmp(p_command, "error") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "last_error=%lu file=%s line=%lu",
                                   (unsigned long)p_errorRecord->code,
                                   p_errorRecord->file,
                                   (unsigned long)p_errorRecord->line);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strcmp(p_command, "components") == 0) || (strcmp(p_command, "mods") == 0))
    {
        return App_DebugConsolePrintComponentTable();
    }

    if ((strcmp(p_command, "fsm") == 0) || (strcmp(p_command, "main") == 0))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "fsm=%s decision=%s trans=%lu msg=%lu idle_req=%lu cmd_tick=%lu state_tick=%lu",
                                   App_FsmGetCurrentStateString(),
                                   App_FsmGetDecisionString(),
                                   (unsigned long)p_fsmSummary->transitionCount,
                                   (unsigned long)p_fsmSummary->processedMessageCount,
                                   (unsigned long)p_fsmSummary->queueEmptyStopCount,
                                   (unsigned long)p_fsmSummary->lastCommandTickMs,
                                   (unsigned long)p_fsmSummary->lastStateTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strcmp(p_command, "q") == 0) || (strcmp(p_command, "queue") == 0))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "q count=%u push_f=%lu push_b=%lu pop_f=%lu pop_b=%lu ovf=%lu",
                                   (unsigned int)p_msgqContext->count,
                                   (unsigned long)p_msgqContext->pushFrontCount,
                                   (unsigned long)p_msgqContext->pushBackCount,
                                   (unsigned long)p_msgqContext->popFrontCount,
                                   (unsigned long)p_msgqContext->popBackCount,
                                   (unsigned long)p_msgqContext->overflowCount);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "lp") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "lp=%s disp=%lu stop_req=%u qual=%u cand=%lu stop=%lu rtc=%lu lptim=%lu sleep=%lu wake=%s req_tick=%lu wake_tick=%lu",
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned int)p_systemContext->stopQualificationCount,
                                   (unsigned long)p_systemContext->stopCandidateCount,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   (unsigned long)p_systemContext->rtcWakeEventCount,
                                   (unsigned long)p_systemContext->lptimWakeEventCount,
                                   (unsigned long)p_systemContext->sleepEntryCount,
                                   App_SystemGetWakeSourceString(),
                                   (unsigned long)p_systemContext->lastStopRequestTickMs,
                                   (unsigned long)p_systemContext->lastWakeTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "slot") == 0)
    {
        const AppDualBootInfo_t *p_dualbootInfo;

        p_dualbootInfo = App_DualBootGetInfo();
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "current=%s(%lu) state=%s active=%lu pending=%lu",
                                   App_DualBootGetCurrentSlotName(),
                                   (unsigned long)App_DualBootGetCurrentSlotId(),
                                   App_DualBootGetBootStateString(),
                                   (unsigned long)((p_dualbootInfo != NULL) ? p_dualbootInfo->activeSlot : 0u),
                                   (unsigned long)((p_dualbootInfo != NULL) ? p_dualbootInfo->pendingSlot : 0u));
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "aux") == 0)
    {
        uint32_t adc_vref = 0, adc_vbat = 0, vbat_mv = 0, vdda_mv = 0;
        if (Battery_ReadVoltage_Averaged_mV(&adc_vref, &adc_vbat, &vdda_mv, &vbat_mv) == HAL_OK)
        {
            APP_LOGI("AUX", "ADC(vref:%lu, vbat:%lu) Volt(vdda:%lumV, vbat:%lumV)",
                     (unsigned long)adc_vref,
                     (unsigned long)adc_vbat,
                     (unsigned long)vdda_mv,
                     (unsigned long)vbat_mv);
        }
        else
        {
            APP_LOGE("AUX", "Read ADC error");
            return APP_STATUS_FATAL;
        }

        SHTC3_Data_t th;
        if (SHTC3_ReadTempHumidity(APP_I2C_AUX_HANDLE, &th) == HAL_OK)
        {
            int t_int = (int)th.temperature;
            int t_dec = (int)((th.temperature - t_int) * 100);
            int h_int = (int)th.humidity;
            int h_dec = (int)((th.humidity - h_int) * 100);
            if (t_dec < 0)
                t_dec = -t_dec;

            APP_LOGI("AUX", "T = %d.%02d C, RH = %d.%02d %%", t_int, t_dec, h_int, h_dec);
        }
        else
        {
            APP_LOGE("AUX", "Read SHTC3 error");
            return APP_STATUS_FATAL;
        }
        return APP_STATUS_OK;
    }

    if (strcmp(p_command, "selftest") == 0)
    {
        status = App_SelfTestRunBootSequence();
        if (status != APP_STATUS_OK)
        {
            return App_DebugConsolePrintSelfTestSummary("selftest done");
        }
        return App_DebugConsolePrintSelfTestSummary("selftest done");
    }

    if (strcmp(p_command, "selftest status") == 0)
    {
        return App_DebugConsolePrintSelfTestSummary("selftest");
    }

    if (strcmp(p_command, "nbiot") == 0)
    {
        (void)App_SystemSetNbiotPowered(APP_TRUE);

        APP_WWDGFeed();
        APP_RETURN_IF_FALSE(App_NBIoTBringUp() == APP_STATUS_OK, APP_STATUS_FATAL);
        APP_WWDGFeed();
        APP_RETURN_IF_FALSE(App_NBIoTNetworkBringUp() == APP_STATUS_OK, APP_STATUS_FATAL);
        APP_WWDGFeed();
        App_NBIoTReadIdentity(APP_TRUE);
        App_NBIoTReadQuality(APP_TRUE);

        APP_WWDGFeed();
        App_NBIoTTransmitUdp();
        APP_WWDGFeed();

        (void)App_SystemSetNbiotPowered(APP_FALSE);

        return (APP_STATUS_OK);
    }

    if (strcmp(p_command, "mstor") == 0)
    {
        APP_RETURN_IF_FALSE(App_DebugConsolePrintMeterStorageSummary() == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
        return App_DebugConsolePrintMeterStorageDump();
    }

    if (strcmp(p_command, "mstor info") == 0)
    {
        return App_DebugConsolePrintMeterStorageSummary();
    }

    if (strcmp(p_command, "mstor dump") == 0)
    {
        return App_DebugConsolePrintMeterStorageDump();
    }

    if (strcmp(p_command, "mconv test") == 0)
    {
        #ifdef DEBUG
        //App_DebugConsoleRunNbiotDumpTest();
        #endif // DEBUG

        return App_DebugConsoleRunMeterConvert(APP_FALSE);
    }

    if (strcmp(p_command, "mconv run") == 0)
    {
        return App_DebugConsoleRunMeterConvert(APP_TRUE);
    }

    if (strcmp(p_command, "resetboot") == 0)
    {
        status = App_FsmRequestResetBoot();
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "resetboot failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }
        return App_DebugConsoleWriteLine("resetboot queued");
    }

    if (strcmp(p_command, "update2") == 0)
    {
        status = App_DualBootRequestUpdateToSlot2();
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "update2 prep failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }

        status = App_FsmRequestResetBoot();
        if (status != APP_STATUS_OK)
        {
            (void)App_DualBootCancelUpdateRequest();
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "update2 resetboot failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }

        formattedLength = snprintf(txBuffer, sizeof(txBuffer), "update2 queued target=%s addr=0x%08lX",
                                   App_DualBootGetTargetSlotName(),
                                   (unsigned long)App_DualBootGetTargetSlotAddress());
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strcmp(p_command, "nfc dump") == 0) ||
        (strcmp(p_command, "nfc payload") == 0))
    {
        return App_DebugConsoleDumpNfcPayload();
    }

    if ((strcmp(p_command, "nfc") == 0) || (strncmp(p_command, "nfc ", 4) == 0))
    {
        const char *p_subcommand;

        p_subcommand = (p_command[3] == '\0') ? "" : &p_command[4];
        status = App_FsmNfcCliExecute(p_subcommand, txBuffer, (uint16_t)sizeof(txBuffer));
        if ((status != APP_STATUS_OK) && (txBuffer[0] == '\0'))
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "nfc command failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        }
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "sm") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "sm=%s queued=%u last=%u front=%u",
                                   App_FsmGetCurrentStateString(),
                                   (unsigned int)App_MsgqGetCount(),
                                   (unsigned int)p_fsmSummary->lastQueuedState,
                                   (unsigned int)p_fsmSummary->lastDequeFromFront);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strncmp(p_command, "sm front ", 9) == 0) || (strncmp(p_command, "sm back ", 8) == 0))
    {
        char stateToken[24];
        uint8_t nextState;
        uint8_t pushFront;

        pushFront = (p_command[3] == 'f') ? APP_TRUE : APP_FALSE;
        if (sscanf(p_command, pushFront == APP_TRUE ? "sm front %23s" : "sm back %23s", stateToken) != 1)
        {
            return App_DebugConsoleWriteLine("usage: sm <front|back> <boot|idle|debug|power|hk|meter|nfc|nbiot|server|rtc|fault>");
        }

        status = App_DebugConsoleParseState(stateToken, &nextState);
        if (status != APP_STATUS_OK)
        {
            return App_DebugConsoleWriteLine("state: boot, idle, debug, power, hk, meter, nfc, nbiot, server, rtc, fault");
        }

        status = (pushFront == APP_TRUE)
                 ? App_FsmQueueStateFront(nextState, 0u, 0u)
                 : App_FsmQueueStateBack(nextState, 0u, 0u);
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "sm queue failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }

        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "sm queued mode=%s state=%s",
                                   (pushFront == APP_TRUE) ? "front" : "back",
                                   App_FsmGetStateName(nextState));
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "echo on") == 0)
    {
        g_appDebugConsoleContext.echoEnabled = APP_TRUE;
        return App_DebugConsoleWriteLine("echo enabled");
    }

    if (strcmp(p_command, "echo off") == 0)
    {
        g_appDebugConsoleContext.echoEnabled = APP_FALSE;
        return App_DebugConsoleWriteLine("echo disabled");
    }

    g_appDebugConsoleContext.unknownCommandCount++;
    return App_DebugConsoleWriteLine("unknown command; type 'help'");
}

static AppStatus_t App_DebugConsoleCommitLine(void)
{
    AppStatus_t status;

    g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = '\0';

    if (g_appDebugConsoleContext.echoEnabled == APP_TRUE)
    {
        status = App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_EOL);
        if (status != APP_STATUS_OK)
        {
            return status;
        }
    }

    status = App_DebugConsoleExecuteCommand(g_appDebugConsoleContext.rxLine);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appDebugConsoleContext.commandCount++;
    g_appDebugConsoleContext.rxLength = 0u;
    g_appDebugConsoleContext.rxLine[0] = '\0';

    return App_DebugConsolePrintPrompt();
}

static AppStatus_t App_DebugConsoleHandleByte(uint8_t rxByte)
{
    static const uint8_t backspaceSequence[] = {'\b', ' ', '\b'};

    if ((rxByte == '\n') && (g_appDebugConsoleContext.ignoreLineFeed == APP_TRUE))
    {
        g_appDebugConsoleContext.ignoreLineFeed = APP_FALSE;
        return APP_STATUS_OK;
    }

    if ((rxByte == '\r') || (rxByte == '\n'))
    {
        g_appDebugConsoleContext.ignoreLineFeed = (rxByte == '\r') ? APP_TRUE : APP_FALSE;
        return App_DebugConsoleCommitLine();
    }

    if ((rxByte == 0x08u) || (rxByte == 0x7Fu))
    {
        if (g_appDebugConsoleContext.rxLength > 0u)
        {
            g_appDebugConsoleContext.rxLength--;
            g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = '\0';
            if (g_appDebugConsoleContext.echoEnabled == APP_TRUE)
            {
                return App_DebugConsoleWrite(backspaceSequence, (uint16_t)sizeof(backspaceSequence));
            }
        }
        return APP_STATUS_OK;
    }

    if ((rxByte < 0x20u) || (rxByte > 0x7Eu))
    {
        return APP_STATUS_OK;
    }

    if (g_appDebugConsoleContext.rxLength >= (APP_DEBUG_CONSOLE_RX_LINE_SIZE - 1u))
    {
        g_appDebugConsoleContext.rxLength = 0u;
        g_appDebugConsoleContext.rxLine[0] = '\0';
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine("input overflow") == APP_STATUS_OK, APP_STATUS_BUFFER_OVERFLOW);
        return App_DebugConsolePrintPrompt();
    }

    g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = (char)rxByte;
    g_appDebugConsoleContext.rxLength++;
    g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = '\0';

    if (g_appDebugConsoleContext.echoEnabled == APP_TRUE)
    {
        return App_DebugConsoleWrite(&rxByte, 1u);
    }

    return APP_STATUS_OK;
}

#endif

AppStatus_t App_DebugConsoleInit(void)
{
    (void)memset(&g_appDebugConsoleContext, 0, sizeof(g_appDebugConsoleContext));
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    g_appDebugConsoleContext.echoEnabled = APP_TRUE;
    g_appDebugRxItByte = 0u;
    g_appDebugRxFifoHead = 0u;
    g_appDebugRxFifoTail = 0u;
    g_appDebugRxFifoOverflowCount = 0u;
    g_appDebugRxInterruptArmed = APP_FALSE;
#else
    g_appDebugConsoleContext.echoEnabled = APP_FALSE;
#endif
    g_appDebugConsoleContext.initialized = APP_TRUE;
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    return App_DebugConsoleStartRxInterrupt();
#else
    return APP_STATUS_OK;
#endif
}

AppStatus_t App_DebugConsoleProcess(void)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    AppStatus_t status;
    uint8_t rxByte;

    APP_RETURN_IF_FALSE(g_appDebugConsoleContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    status = App_DebugConsoleStartRxInterrupt();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    while (App_DebugConsolePopRxByte(&rxByte) == APP_TRUE)
    {
        AppStatus_t byteStatus;

        byteStatus = App_DebugConsoleHandleByte(rxByte);
        if (byteStatus != APP_STATUS_OK)
        {
            return byteStatus;
        }
    }
#endif

    return APP_STATUS_OK;
}

AppStatus_t App_DebugConsoleWrite(const uint8_t *p_data, uint16_t length)
{
    APP_RETURN_IF_FALSE(g_appDebugConsoleContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(((p_data != NULL) || (length == 0u)), APP_STATUS_INVALID_PARAM);

    if (length == 0u)
    {
        return APP_STATUS_OK;
    }

    APP_RETURN_IF_HAL_ERROR(HAL_UART_Transmit(APP_UART_DEBUG_HANDLE,
                                              (uint8_t *)p_data,
                                              length,
                                              APP_DEBUG_UART_TIMEOUT_MS),
                            APP_STATUS_UART_TX_FAILED);
    return APP_STATUS_OK;
}

AppStatus_t App_DebugConsoleWriteString(const char *p_text)
{
    APP_RETURN_IF_FALSE((p_text != NULL), APP_STATUS_INVALID_PARAM);
    return App_DebugConsoleWrite((const uint8_t *)p_text, (uint16_t)strlen(p_text));
}

AppStatus_t App_DebugConsolePrintPrompt(void)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    return App_DebugConsoleWrite((const uint8_t *)g_appDebugPrompt, (uint16_t)(sizeof(g_appDebugPrompt) - 1u));
#else
    return APP_STATUS_OK;
#endif
}


void App_DebugConsoleOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    HAL_StatusTypeDef halStatus;

    if ((p_huart == NULL) || (p_huart != APP_UART_DEBUG_HANDLE))
    {
        return;
    }

    App_DebugConsolePushRxByteFromIsr(g_appDebugRxItByte);
    g_appDebugRxInterruptArmed = APP_FALSE;
    halStatus = HAL_UART_Receive_IT(APP_UART_DEBUG_HANDLE, (uint8_t *)&g_appDebugRxItByte, 1u);
    if ((halStatus == HAL_OK) || (halStatus == HAL_BUSY))
    {
        g_appDebugRxInterruptArmed = APP_TRUE;
    }
#else
    (void)p_huart;
#endif
}

void App_DebugConsoleOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    HAL_StatusTypeDef halStatus;

    if ((p_huart == NULL) || (p_huart != APP_UART_DEBUG_HANDLE))
    {
        return;
    }

    __HAL_UART_CLEAR_FLAG(APP_UART_DEBUG_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_DEBUG_HANDLE, UART_RXDATA_FLUSH_REQUEST);
    g_appDebugRxInterruptArmed = APP_FALSE;
    halStatus = HAL_UART_Receive_IT(APP_UART_DEBUG_HANDLE, (uint8_t *)&g_appDebugRxItByte, 1u);
    if ((halStatus == HAL_OK) || (halStatus == HAL_BUSY))
    {
        g_appDebugRxInterruptArmed = APP_TRUE;
    }
#else
    (void)p_huart;
#endif
}

const AppDebugConsoleContext_t *App_DebugConsoleGetContext(void)
{
    return &g_appDebugConsoleContext;
}

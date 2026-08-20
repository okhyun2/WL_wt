#include "app_nfc_seoul_format.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_log.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"

#define APP_NFC_SEOUL_TLV_NDEF_MESSAGE            (0x03u)
#define APP_NFC_SEOUL_TLV_TERMINATOR              (0xFEu)
#define APP_NFC_SEOUL_NDEF_HEADER_SHORT_UNKNOWN   (0xD5u)
#define APP_NFC_SEOUL_NDEF_MAX_BYTES              (64u)
#define APP_NFC_SEOUL_NDEF_MAX_BLOCKS             ((APP_NFC_SEOUL_NDEF_MAX_BYTES + 3u) / 4u)
#define APP_NFC_SEOUL_FORMAT_VERSION              (0x10u)
#define APP_NFC_SEOUL_LAYER1_READ_ONLY            (0x01u)
#define APP_NFC_SEOUL_LAYER2_PATENT_SUPPORTED     (0x01u)
#define APP_NFC_SEOUL_METER_CODE_UNKNOWN          (0xFFu)
#define APP_NFC_SEOUL_CARRIER_UNKNOWN             (0xFFu)
#define APP_NFC_SEOUL_ACK_UNKNOWN                 (0xFFu)
#define APP_NFC_SEOUL_COMM_ON                     (0x01u)
#define APP_NFC_SEOUL_COMM_OFF                    (0x0Fu)

#define APP_NFC_SEOUL_CMD_REQ_GROUP               (0xD4u)
#define APP_NFC_SEOUL_CMD_RES_GROUP               (0xD5u)
#define APP_NFC_SEOUL_CMD_STOR_RES                (0x00u)
#define APP_NFC_SEOUL_CMD_MTR_REQ                 (0x01u)
#define APP_NFC_SEOUL_CMD_MTR_RES                 (0x02u)
#define APP_NFC_SEOUL_CMD_AMI_REQ                 (0x03u)
#define APP_NFC_SEOUL_CMD_AMI_RES                 (0x04u)
#define APP_NFC_SEOUL_CMD_RSET_REQ                (0x05u)
#define APP_NFC_SEOUL_CMD_RSET_RES                (0x06u)

#define APP_NFC_SEOUL_NDEF_EEPROM_BLOCK           (NFC_NDEF_START_BLOCK)
#define APP_NFC_SEOUL_NDEF_SRAM_BLOCK             (NFC_SRAM_BASE_ADDR + 1u)
#define APP_NFC_SEOUL_EEPROM_SETTLE_DELAY_MS      (5u)
#define APP_NFC_SEOUL_EEPROM_BLOCK_DELAY_MS       (2u)
#define APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET (6u)
#define APP_NFC_SEOUL_STOR_RES_READING_TIME_OFFSET (12u)
#define APP_NFC_SEOUL_STOR_RES_RECORD_COUNT_OFFSET (18u)

typedef struct
{
    uint8_t meterIdBcd[4];
    uint8_t reportTime[6];
    uint8_t readingTime[6];
    uint8_t recordCount;
    uint8_t reading[4];
    uint8_t caliberDecimal;
    uint8_t meterCode;
    uint8_t terminalId[4];
    uint8_t firmwareVersion[2];
    uint8_t formatVersion;
    uint8_t alarmStatus;
    uint8_t rsrp[2];
    uint8_t ackCount;
    uint8_t carrier;
    uint8_t modemStatus;
    uint8_t battery;
    uint8_t commState;
} AppNfcSeoulSnapshot_t;

static NFC_NTP53321_Handle_t *g_appNfcSeoulTag;
static uint8_t g_appNfcSeoulAttached;
static uint8_t g_appNfcSeoulPayloadDirty;
static uint8_t g_appNfcSeoulSramSyncPending;
static uint8_t g_appNfcSeoulLiveRecordValid;
static AppMeterStorageRecord_t g_appNfcSeoulLiveRecord;
static AppNfcSeoulDebugInfo_t g_appNfcSeoulDebugInfo;
static const AppNfcSeoulLayer1Info_t g_appNfcSeoulLayer1Info = {
    APP_NFC_SEOUL_FORMAT_VERSION,
    APP_NFC_SEOUL_LAYER1_READ_ONLY,
    APP_NFC_SEOUL_LAYER2_PATENT_SUPPORTED,
    0u
};
#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
static uint32_t g_appNfcSeoulTestModeLastRefreshTickMs;
static uint8_t g_appNfcSeoulTestModeCounter;
#endif

static void App_NfcSeoulDebugStorePayload(uint8_t *p_dst, uint8_t *p_lengthDst, const uint8_t *p_src, uint8_t srcLength)
{
    uint8_t copyLength;

    if ((p_dst == NULL) || (p_lengthDst == NULL))
    {
        return;
    }

    copyLength = srcLength;
    if (copyLength > 64u)
    {
        copyLength = 64u;
    }

    if ((p_src != NULL) && (copyLength != 0u))
    {
        (void)memcpy(p_dst, p_src, copyLength);
    }
    if (copyLength < 64u)
    {
        (void)memset(&p_dst[copyLength], 0, (uint32_t)(64u - copyLength));
    }
    *p_lengthDst = copyLength;
}

static void App_NfcSeoulDebugRecordRequest(const uint8_t *p_payload, uint8_t payloadLength, uint8_t readSource)
{
    g_appNfcSeoulDebugInfo.initialized = APP_TRUE;
    g_appNfcSeoulDebugInfo.lastReadSource = readSource;
    g_appNfcSeoulDebugInfo.lastTickMs = HAL_GetTick();
    g_appNfcSeoulDebugInfo.requestCount++;
    g_appNfcSeoulDebugInfo.lastHandled = APP_FALSE;
    g_appNfcSeoulDebugInfo.lastCommRequested = APP_FALSE;
    g_appNfcSeoulDebugInfo.lastResponseCmd1 = 0u;
    g_appNfcSeoulDebugInfo.lastResponseCmd2 = 0u;
    g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)APP_STATUS_OK;
    App_NfcSeoulDebugStorePayload(g_appNfcSeoulDebugInfo.lastRequest,
                                  &g_appNfcSeoulDebugInfo.lastRequestLength,
                                  p_payload,
                                  payloadLength);
    g_appNfcSeoulDebugInfo.lastRequestCmd1 = (payloadLength >= 1u) ? p_payload[0] : 0u;
    g_appNfcSeoulDebugInfo.lastRequestCmd2 = (payloadLength >= 2u) ? p_payload[1] : 0u;
}

static void App_NfcSeoulDebugRecordResponse(const uint8_t *p_payload, uint8_t payloadLength, uint8_t handled, uint8_t commRequested, AppStatus_t status)
{
    g_appNfcSeoulDebugInfo.initialized = APP_TRUE;
    g_appNfcSeoulDebugInfo.lastTickMs = HAL_GetTick();
    g_appNfcSeoulDebugInfo.responseCount++;
    g_appNfcSeoulDebugInfo.lastHandled = handled;
    g_appNfcSeoulDebugInfo.lastCommRequested = commRequested;
    g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
    App_NfcSeoulDebugStorePayload(g_appNfcSeoulDebugInfo.lastResponse,
                                  &g_appNfcSeoulDebugInfo.lastResponseLength,
                                  p_payload,
                                  payloadLength);
    g_appNfcSeoulDebugInfo.lastResponseCmd1 = (payloadLength >= 1u) ? p_payload[0] : 0u;
    g_appNfcSeoulDebugInfo.lastResponseCmd2 = (payloadLength >= 2u) ? p_payload[1] : 0u;
}

static void App_NfcSeoulFillUnknown(uint8_t *p_buf, uint8_t length)
{
    if (p_buf == NULL)
    {
        return;
    }

    (void)memset(p_buf, 0xFF, length);
}

static void App_NfcSeoulU32ToBe(uint32_t value, uint8_t *p_out)
{
    p_out[0] = (uint8_t)((value >> 24) & 0xFFu);
    p_out[1] = (uint8_t)((value >> 16) & 0xFFu);
    p_out[2] = (uint8_t)((value >> 8) & 0xFFu);
    p_out[3] = (uint8_t)(value & 0xFFu);
}

static void App_NfcSeoulU16ToBe(uint16_t value, uint8_t *p_out)
{
    p_out[0] = (uint8_t)((value >> 8) & 0xFFu);
    p_out[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t App_NfcSeoulReadLe16(const uint8_t *p_buf)
{
    return (uint16_t)((uint16_t)p_buf[0] | ((uint16_t)p_buf[1] << 8));
}

static void App_NfcSeoulEncodeBcdBe(uint32_t value, uint8_t *p_out, uint8_t digitBytes)
{
    uint8_t i;

    for (i = 0u; i < digitBytes; i++)
    {
        uint8_t low;
        uint8_t high;

        low = (uint8_t)(value % 10u);
        value /= 10u;
        high = (uint8_t)(value % 10u);
        value /= 10u;

        p_out[(digitBytes - 1u) - i] = (uint8_t)((high << 4) | low);
    }
}

static uint8_t App_NfcSeoulIsAllZero(const uint8_t *p_buf, uint8_t length)
{
    uint8_t i;

    for (i = 0u; i < length; i++)
    {
        if (p_buf[i] != 0u)
        {
            return APP_FALSE;
        }
    }

    return APP_TRUE;
}

static AppStatus_t App_NfcSeoulLoadOptions(AppMeterServerFormatOptions_t *p_options)
{
    AppStatus_t status;

    if (p_options == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    App_MeterServerOptionsSetDefaults(p_options);
    status = App_MeterServerOptionsLoad(p_options);
    if ((status == APP_STATUS_OK) || (status == APP_STATUS_NOT_INITIALIZED))
    {
        return APP_STATUS_OK;
    }

    return status;
}

static uint32_t App_NfcSeoulReadBe32(const uint8_t src[4])
{
    if (src == NULL)
    {
        return 0u;
    }

    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8)  |
           ((uint32_t)src[3]);
}

static uint16_t App_NfcSeoulReadBe16(const uint8_t src[2])
{
    if (src == NULL)
    {
        return 0u;
    }

    return (uint16_t)(((uint16_t)src[0] << 8) | (uint16_t)src[1]);
}

static void App_NfcSeoulLogLayer1Profile(const AppNfcSeoulSnapshot_t *p_snapshot,
                                         uint8_t responseCmd)
{
    if (p_snapshot == NULL)
    {
        return;
    }

    APP_LOGI("NFC",
             "[[NFC-L1]] cmd=0x%02X fmt=0x%02X readonly=%u patent_l2=%u recordCount=%u comm=%u battery=0x%02X",
             (unsigned int)responseCmd,
             (unsigned int)g_appNfcSeoulLayer1Info.snapshotFormatVersion,
             (unsigned int)g_appNfcSeoulLayer1Info.readOnlyLayer1,
             (unsigned int)g_appNfcSeoulLayer1Info.patentLayer2Supported,
             (unsigned int)p_snapshot->recordCount,
             (unsigned int)p_snapshot->commState,
             (unsigned int)p_snapshot->battery);
}

static void App_NfcSeoulPrintSnapshot(const AppNfcSeoulSnapshot_t *p_snapshot)
{
    uint32_t meterId;
    uint32_t reading;
    uint16_t rsrp;

    if (p_snapshot == NULL)
    {
        APP_LOGE("NFC", "snapshot print: null");
        return;
    }

    meterId = App_NfcSeoulReadBe32(p_snapshot->meterIdBcd);
    reading = App_NfcSeoulReadBe32(p_snapshot->reading);
    rsrp    = App_NfcSeoulReadBe16(p_snapshot->rsrp);

    APP_LOGI("NFC", "----- Seoul Snapshot -----");
    APP_LOGI("NFC",
             "meterIdBcd=%02X%02X%02X%02X reading=%lu recordCount=%u",
             (unsigned int)p_snapshot->meterIdBcd[0],
             (unsigned int)p_snapshot->meterIdBcd[1],
             (unsigned int)p_snapshot->meterIdBcd[2],
             (unsigned int)p_snapshot->meterIdBcd[3],
             (unsigned long)reading,
             (unsigned int)p_snapshot->recordCount);

    APP_LOGI("NFC",
             "reportTime=%02u/%02u/%02u %02u:%02u:%02u",
             (unsigned int)p_snapshot->reportTime[0],
             (unsigned int)p_snapshot->reportTime[1],
             (unsigned int)p_snapshot->reportTime[2],
             (unsigned int)p_snapshot->reportTime[3],
             (unsigned int)p_snapshot->reportTime[4],
             (unsigned int)p_snapshot->reportTime[5]);

    APP_LOGI("NFC",
             "readingTime=%02u/%02u/%02u %02u:%02u:%02u",
             (unsigned int)p_snapshot->readingTime[0],
             (unsigned int)p_snapshot->readingTime[1],
             (unsigned int)p_snapshot->readingTime[2],
             (unsigned int)p_snapshot->readingTime[3],
             (unsigned int)p_snapshot->readingTime[4],
             (unsigned int)p_snapshot->readingTime[5]);

    APP_LOGI("NFC",
             "readingRaw=%02X %02X %02X %02X meterIdRaw=%02X %02X %02X %02X",
             (unsigned int)p_snapshot->reading[0],
             (unsigned int)p_snapshot->reading[1],
             (unsigned int)p_snapshot->reading[2],
             (unsigned int)p_snapshot->reading[3],
             (unsigned int)p_snapshot->meterIdBcd[0],
             (unsigned int)p_snapshot->meterIdBcd[1],
             (unsigned int)p_snapshot->meterIdBcd[2],
             (unsigned int)p_snapshot->meterIdBcd[3]);

    APP_LOGI("NFC",
             "meterCode=0x%02X caliberDecimal=0x%02X alarmStatus=0x%02X",
             (unsigned int)p_snapshot->meterCode,
             (unsigned int)p_snapshot->caliberDecimal,
             (unsigned int)p_snapshot->alarmStatus);

    APP_LOGI("NFC",
             "terminalId=%02X%02X%02X%02X fw=%u.%u fmt=0x%02X",
             (unsigned int)p_snapshot->terminalId[0],
             (unsigned int)p_snapshot->terminalId[1],
             (unsigned int)p_snapshot->terminalId[2],
             (unsigned int)p_snapshot->terminalId[3],
             (unsigned int)p_snapshot->firmwareVersion[0],
             (unsigned int)p_snapshot->firmwareVersion[1],
             (unsigned int)p_snapshot->formatVersion);

    APP_LOGI("NFC",
             "rsrp=0x%04X ack=0x%02X carrier=0x%02X modem=0x%02X battery=0x%02X comm=0x%02X",
             (unsigned int)rsrp,
             (unsigned int)p_snapshot->ackCount,
             (unsigned int)p_snapshot->carrier,
             (unsigned int)p_snapshot->modemStatus,
             (unsigned int)p_snapshot->battery,
             (unsigned int)p_snapshot->commState);

    APP_LOGI("NFC",
             "alarm bits: overflow=%u reverse=%u leak=%u",
             (unsigned int)((p_snapshot->alarmStatus & APP_METER_STORAGE_STATUS_OVERFLOW) != 0u),
             (unsigned int)((p_snapshot->alarmStatus & APP_METER_STORAGE_STATUS_REVERSE_FLOW) != 0u),
             (unsigned int)((p_snapshot->alarmStatus & APP_METER_STORAGE_STATUS_LEAK) != 0u));

    APP_LOGI("NFC",
             "commState=%s meterIdBe=0x%08lX",
             (p_snapshot->commState == APP_NFC_SEOUL_COMM_ON) ? "ON" : "OFF",
             (unsigned long)meterId);
}

static void App_NfcSeoulApplyRecordToSnapshot(AppNfcSeoulSnapshot_t *p_snapshot,
                                              const AppMeterStorageRecord_t *p_record)
{
    if ((p_snapshot == NULL) || (p_record == NULL))
    {
        return;
    }

    App_NfcSeoulEncodeBcdBe(p_record->meterId,
                            p_snapshot->meterIdBcd,
                            (uint8_t)sizeof(p_snapshot->meterIdBcd));
    App_NfcSeoulU32ToBe(p_record->readingScaled, p_snapshot->reading);
    p_snapshot->caliberDecimal = p_record->caliberDecimal;
    p_snapshot->alarmStatus = (uint8_t)(p_record->meterStatus &
                                        (APP_METER_STORAGE_STATUS_OVERFLOW |
                                         APP_METER_STORAGE_STATUS_REVERSE_FLOW |
                                         APP_METER_STORAGE_STATUS_LEAK));

    if ((p_record->flags & APP_METER_STORAGE_FLAG_TIME_VALID) != 0u)
    {
        (void)memcpy(p_snapshot->reportTime, p_record->ts, sizeof(p_record->ts));
        (void)memcpy(p_snapshot->readingTime, p_record->ts, sizeof(p_record->ts));
    }

    p_snapshot->commState = APP_NFC_SEOUL_COMM_ON;
}

static void App_NfcSeoulBuildSnapshot(AppNfcSeoulSnapshot_t *p_snapshot)
{
    AppMeterServerFormatOptions_t options;
    AppMeterStorageInfo_t info;
    AppMeterStorageRecord_t record;
    AppStatus_t status;
    uint16_t rsrpValue;
    uint8_t hasRecord;

    if (p_snapshot == NULL)
    {
        return;
    }

    (void)memset(p_snapshot, 0, sizeof(*p_snapshot));
    App_NfcSeoulFillUnknown(p_snapshot->meterIdBcd, (uint8_t)sizeof(p_snapshot->meterIdBcd));
    App_NfcSeoulFillUnknown(p_snapshot->reportTime, (uint8_t)sizeof(p_snapshot->reportTime));
    App_NfcSeoulFillUnknown(p_snapshot->readingTime, (uint8_t)sizeof(p_snapshot->readingTime));
    App_NfcSeoulFillUnknown(p_snapshot->reading, (uint8_t)sizeof(p_snapshot->reading));
    App_NfcSeoulFillUnknown(p_snapshot->terminalId, (uint8_t)sizeof(p_snapshot->terminalId));
    App_NfcSeoulFillUnknown(p_snapshot->rsrp, (uint8_t)sizeof(p_snapshot->rsrp));

    p_snapshot->meterCode = APP_NFC_SEOUL_METER_CODE_UNKNOWN;
    p_snapshot->firmwareVersion[0] = (uint8_t)APP_FW_VERSION_MAJOR;
    p_snapshot->firmwareVersion[1] = (uint8_t)APP_FW_VERSION_MINOR;
    p_snapshot->formatVersion = APP_NFC_SEOUL_FORMAT_VERSION;
    p_snapshot->ackCount = APP_NFC_SEOUL_ACK_UNKNOWN;
    p_snapshot->carrier = APP_NFC_SEOUL_CARRIER_UNKNOWN;
    p_snapshot->modemStatus = 0x00u;
    p_snapshot->battery = 0xFFu;
    p_snapshot->commState = APP_NFC_SEOUL_COMM_ON;
    p_snapshot->caliberDecimal = 0xFFu;
    p_snapshot->recordCount = 0u;

    (void)memset(&options, 0, sizeof(options));
    if (App_NfcSeoulLoadOptions(&options) == APP_STATUS_OK)
    {
        p_snapshot->battery = options.terminalBattery.value;
        memcpy(p_snapshot->terminalId, &options.deviceSerialBcd[1], (uint8_t)sizeof(p_snapshot->terminalId)); //get last 4byte

        if (App_NfcSeoulIsAllZero(options.wirelessQuality, (uint8_t)sizeof(options.wirelessQuality)) != APP_TRUE)
        {
            rsrpValue = App_NfcSeoulReadLe16(&options.wirelessQuality[4]);
            App_NfcSeoulU16ToBe(rsrpValue, p_snapshot->rsrp);
        }
    }

    hasRecord = APP_FALSE;
    status = App_MeterStorageGetInfo(&info);
    if (status == APP_STATUS_OK)
    {
        p_snapshot->recordCount = info.count;

        if (info.count != 0u)
        {
            status = App_MeterStorageReadAt((uint8_t)(info.count - 1u), &record);
            if (status == APP_STATUS_OK)
            {
                App_NfcSeoulApplyRecordToSnapshot(p_snapshot, &record);
                hasRecord = APP_TRUE;
            }
        }
    }

    if (g_appNfcSeoulLiveRecordValid == APP_TRUE)
    {
        App_NfcSeoulApplyRecordToSnapshot(p_snapshot, &g_appNfcSeoulLiveRecord);
        hasRecord = APP_TRUE;
    }

    if (hasRecord != APP_TRUE)
    {
        p_snapshot->commState = APP_NFC_SEOUL_COMM_OFF;
    }
}

static void App_NfcSeoulReadTestTimestamp(uint8_t ts[6])
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if ((ts == NULL) ||
        (HAL_RTC_GetTime(APP_RTC_HANDLE, &sTime, RTC_FORMAT_BIN) != HAL_OK) ||
        (HAL_RTC_GetDate(APP_RTC_HANDLE, &sDate, RTC_FORMAT_BIN) != HAL_OK))
    {
        return;
    }

    ts[0] = sDate.Year;
    ts[1] = sDate.Month;
    ts[2] = sDate.Date;
    ts[3] = sTime.Hours;
    ts[4] = sTime.Minutes;
    ts[5] = sTime.Seconds;
}

static void App_NfcSeoulApplyTestModeLiveFields(uint8_t *p_payload, uint8_t payloadLength)
{
#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
    uint8_t ts[6] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};

    if ((p_payload == NULL) || (payloadLength <= APP_NFC_SEOUL_STOR_RES_RECORD_COUNT_OFFSET))
    {
        return;
    }

    App_NfcSeoulReadTestTimestamp(ts);
    (void)memcpy(&p_payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET], ts, sizeof(ts));
    (void)memcpy(&p_payload[APP_NFC_SEOUL_STOR_RES_READING_TIME_OFFSET], ts, sizeof(ts));
    p_payload[APP_NFC_SEOUL_STOR_RES_RECORD_COUNT_OFFSET] = g_appNfcSeoulTestModeCounter++;
#else
    (void)p_payload;
    (void)payloadLength;
#endif
}

static AppStatus_t App_NfcSeoulBuildResponsePayload(uint8_t cmd2, uint8_t *p_payload, uint8_t payloadCapacity, uint8_t *p_payloadLength)
{
    AppNfcSeoulSnapshot_t snapshot;
    uint8_t cursor;

    APP_LOGI("NFC", "Seoul SRAM mirror cmd:0x%02x", cmd2);
    if ((p_payload == NULL) || (p_payloadLength == NULL))
    {
        return APP_STATUS_INVALID_PARAM;
    }

    App_NfcSeoulBuildSnapshot(&snapshot);
    App_NfcSeoulLogLayer1Profile(&snapshot, cmd2);
    App_NfcSeoulPrintSnapshot(&snapshot);
    App_LogHexDump(APP_LOG_LEVEL_INFO, "NFC", (const uint8_t *)&snapshot, sizeof(AppNfcSeoulSnapshot_t));

    cursor = 0u;
    p_payload[cursor++] = APP_NFC_SEOUL_CMD_RES_GROUP;
    p_payload[cursor++] = cmd2;

#define APP_NFC_SEOUL_APPEND(buf_, len_)                                            \
    do                                                                               \
    {                                                                                \
        uint8_t localLen_ = (uint8_t)(len_);                                         \
        if ((uint16_t)cursor + (uint16_t)localLen_ > (uint16_t)payloadCapacity)      \
        {                                                                            \
            return APP_STATUS_BUFFER_OVERFLOW;                                       \
        }                                                                            \
        (void)memcpy(&p_payload[cursor], (buf_), localLen_);                         \
        cursor = (uint8_t)(cursor + localLen_);                                      \
    } while (0)

    switch (cmd2)
    {
        case APP_NFC_SEOUL_CMD_MTR_RES:
            APP_NFC_SEOUL_APPEND(snapshot.meterIdBcd, sizeof(snapshot.meterIdBcd));
            APP_NFC_SEOUL_APPEND(snapshot.reportTime, sizeof(snapshot.reportTime));
            APP_NFC_SEOUL_APPEND(snapshot.readingTime, sizeof(snapshot.readingTime));
            APP_NFC_SEOUL_APPEND(&snapshot.recordCount, 1u);
            APP_NFC_SEOUL_APPEND(snapshot.reading, sizeof(snapshot.reading));
            APP_NFC_SEOUL_APPEND(&snapshot.caliberDecimal, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.meterCode, 1u);
            APP_NFC_SEOUL_APPEND(snapshot.terminalId, sizeof(snapshot.terminalId));
            APP_NFC_SEOUL_APPEND(snapshot.firmwareVersion, sizeof(snapshot.firmwareVersion));
            APP_NFC_SEOUL_APPEND(&snapshot.formatVersion, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.battery, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.commState, 1u);
            break;

        case APP_NFC_SEOUL_CMD_AMI_RES:
        case APP_NFC_SEOUL_CMD_STOR_RES:
            APP_NFC_SEOUL_APPEND(snapshot.meterIdBcd, sizeof(snapshot.meterIdBcd));
            APP_NFC_SEOUL_APPEND(snapshot.reportTime, sizeof(snapshot.reportTime));
            APP_NFC_SEOUL_APPEND(snapshot.readingTime, sizeof(snapshot.readingTime));
            APP_NFC_SEOUL_APPEND(&snapshot.recordCount, 1u);
            APP_NFC_SEOUL_APPEND(snapshot.reading, sizeof(snapshot.reading));
            APP_NFC_SEOUL_APPEND(&snapshot.caliberDecimal, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.alarmStatus, 1u);
            APP_NFC_SEOUL_APPEND(snapshot.rsrp, sizeof(snapshot.rsrp));
            APP_NFC_SEOUL_APPEND(&snapshot.ackCount, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.carrier, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.modemStatus, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.battery, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.commState, 1u);
            break;

        case APP_NFC_SEOUL_CMD_RSET_RES:
            APP_NFC_SEOUL_APPEND(snapshot.meterIdBcd, sizeof(snapshot.meterIdBcd));
            APP_NFC_SEOUL_APPEND(snapshot.reportTime, sizeof(snapshot.reportTime));
            APP_NFC_SEOUL_APPEND(snapshot.readingTime, sizeof(snapshot.readingTime));
            APP_NFC_SEOUL_APPEND(&snapshot.recordCount, 1u);
            APP_NFC_SEOUL_APPEND(snapshot.reading, sizeof(snapshot.reading));
            APP_NFC_SEOUL_APPEND(&snapshot.caliberDecimal, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.battery, 1u);
            APP_NFC_SEOUL_APPEND(&snapshot.commState, 1u);
            break;

        default:
            return APP_STATUS_INVALID_PARAM;
    }

#undef APP_NFC_SEOUL_APPEND

    *p_payloadLength = cursor;
    return APP_STATUS_OK;
}

static AppStatus_t App_NfcSeoulBuildNdefMessage(const uint8_t *p_payload,
                                               uint8_t payloadLength,
                                               uint8_t *p_ndef,
                                               uint16_t ndefCapacity,
                                               uint16_t *p_numBlocks)
{
    uint16_t idx;

    if ((p_payload == NULL) || (p_ndef == NULL) || (p_numBlocks == NULL))
    {
        return APP_STATUS_INVALID_PARAM;
    }

    if ((uint16_t)payloadLength > ndefCapacity)
    {
        return APP_STATUS_BUFFER_OVERFLOW;
    }

    (void)memset(p_ndef, 0, ndefCapacity);
    idx = 0u;
    p_ndef[idx++] = APP_NFC_SEOUL_TLV_NDEF_MESSAGE;
    p_ndef[idx++] = (uint8_t)(3u + payloadLength);
    p_ndef[idx++] = APP_NFC_SEOUL_NDEF_HEADER_SHORT_UNKNOWN;
    p_ndef[idx++] = 0u;
    p_ndef[idx++] = payloadLength;
    p_ndef[idx++] = 0u; //align block 4byte
    p_ndef[idx++] = 0u; //align block 4byte
    p_ndef[idx++] = 0u; //align block 4byte
    (void)memcpy(&p_ndef[idx], p_payload, payloadLength);
    idx = (uint16_t)(idx + payloadLength);
    p_ndef[idx++] = APP_NFC_SEOUL_TLV_TERMINATOR;

    while ((idx % 4u) != 0u)
    {
        p_ndef[idx++] = 0u;
    }

    *p_numBlocks = (uint16_t)(idx / 4u);
    return APP_STATUS_OK;
}

#if 1
static uint8_t App_NfcSeoulWaitEepromAccessReady(const char *p_stage, uint16_t blockAddr)
{
    (void)p_stage;
    (void)blockAddr;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL))
    {
        return APP_FALSE;
    }

    HAL_Delay(APP_NFC_SEOUL_EEPROM_BLOCK_DELAY_MS);
    return APP_TRUE;
}
#else
static uint8_t App_NfcSeoulWaitEepromAccessReady(const char *p_stage, uint16_t blockAddr)
{
    uint32_t startTick;
    uint8_t status0;
    uint8_t status1;
    uint8_t eeBusy;
    uint8_t i2cLocked;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL))
    {
        return APP_FALSE;
    }

    startTick = HAL_GetTick();
    status0 = 0u;
    status1 = 0u;

    while ((HAL_GetTick() - startTick) < APP_NFC_SEOUL_EEPROM_READY_TIMEOUT_MS)
    {
        if ((NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                         NFC_SESSION_STATUS_ADDR,
                                         0u,
                                         &status0) == NFC_RESULT_OK) &&
            (NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                         NFC_SESSION_STATUS_ADDR,
                                         1u,
                                         &status1) == NFC_RESULT_OK))
        {
            eeBusy = ((status0 & NFC_STATUS0_EEPROM_WR_BUSY) != 0u) ? APP_TRUE : APP_FALSE;
            i2cLocked = ((status1 & NFC_STATUS1_I2C_IF_LOCKED) != 0u) ? APP_TRUE : APP_FALSE;
            if ((eeBusy == APP_FALSE) && (i2cLocked == APP_FALSE))
            {
                return APP_TRUE;
            }
        }
        HAL_Delay(1u);
    }

    APP_LOGW("NFC", "Seoul EEPROM wait timeout stage=%s blk=0x%04X status0=0x%02X status1=0x%02X",
             (p_stage != NULL) ? p_stage : "?",
             (unsigned int)blockAddr,
             (unsigned int)status0,
             (unsigned int)status1);
    return APP_FALSE;
}
#endif

static uint8_t App_NfcSeoulIsSramMirrorReady(void)
{
    uint8_t status0;
    uint8_t status1;
    uint8_t cfg1;
    uint8_t arbiter;
    uint8_t sramEnabled;
    uint8_t vccOk;
    uint8_t fieldOk;
    uint8_t vccBootOk;
    uint8_t nfcBootOk;
    uint8_t i2cUnlocked;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL))
    {
        return APP_FALSE;
    }

    if (NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                    NFC_SESSION_STATUS_ADDR,
                                    0u,
                                    &status0) != NFC_RESULT_OK)
    {
        APP_LOGW("NFC", "Seoul SRAM mirror check status0 read failed");
        return APP_FALSE;
    }

    if (NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                    NFC_SESSION_STATUS_ADDR,
                                    1u,
                                    &status1) != NFC_RESULT_OK)
    {
        APP_LOGW("NFC", "Seoul SRAM mirror check status1 read failed");
        return APP_FALSE;
    }

    if (NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                    NFC_SESSION_CONFIG_REG_ADDR,
                                    1u,
                                    &cfg1) != NFC_RESULT_OK)
    {
        APP_LOGW("NFC", "Seoul SRAM mirror check cfg1 read failed");
        return APP_FALSE;
    }

    arbiter = (uint8_t)(cfg1 & NFC_CONFIG1_ARBITER_MODE_MASK);
    sramEnabled = ((cfg1 & NFC_CONFIG1_SRAM_ENABLED) != 0u) ? APP_TRUE : APP_FALSE;
    vccOk = ((status0 & NFC_STATUS0_VCC_SUPPLY_OK) != 0u) ? APP_TRUE : APP_FALSE;
    fieldOk = ((status0 & NFC_STATUS0_NFC_FIELD_OK) != 0u) ? APP_TRUE : APP_FALSE;
    vccBootOk = ((status1 & NFC_STATUS1_VCC_BOOT_OK) != 0u) ? APP_TRUE : APP_FALSE;
    nfcBootOk = ((status1 & NFC_STATUS1_NFC_BOOT_OK) != 0u) ? APP_TRUE : APP_FALSE;
    i2cUnlocked = ((status1 & NFC_STATUS1_I2C_IF_LOCKED) == 0u) ? APP_TRUE : APP_FALSE;

    APP_LOGI("NFC", "Seoul SRAM factors status0=0x%02X status1=0x%02X cfg1=0x%02X arb=0x%02X vcc=%u field=%u vcc_boot=%u nfc_boot=%u i2c_unlocked=%u sram_en=%u",
             (unsigned int)status0,
             (unsigned int)status1,
             (unsigned int)cfg1,
             (unsigned int)arbiter,
             (unsigned int)vccOk,
             (unsigned int)fieldOk,
             (unsigned int)vccBootOk,
             (unsigned int)nfcBootOk,
             (unsigned int)i2cUnlocked,
             (unsigned int)sramEnabled);

    if ((fieldOk == APP_TRUE) &&
        ((arbiter != NFC_CONFIG1_ARBITER_SRAM_MIRROR) || (sramEnabled != APP_TRUE)))
    {
        APP_LOGW("NFC", "Seoul SRAM field detected but session config not ready yet (arb=0x%02X sram_en=%u)",
                 (unsigned int)arbiter,
                 (unsigned int)sramEnabled);
    }

    if (arbiter != NFC_CONFIG1_ARBITER_SRAM_MIRROR)
    {
        APP_LOGW("NFC", "Seoul SRAM write skipped, mirror arbiter not ready");
        return APP_FALSE;
    }

    if ((vccOk != APP_TRUE) || (vccBootOk != APP_TRUE) || (nfcBootOk != APP_TRUE))
    {
        APP_LOGW("NFC", "Seoul SRAM write skipped, power/boot condition not ready");
        return APP_FALSE;
    }

    if (i2cUnlocked != APP_TRUE)
    {
        APP_LOGW("NFC", "Seoul SRAM write skipped, I2C interface locked");
        return APP_FALSE;
    }

    if (sramEnabled != APP_TRUE)
    {
        APP_LOGW("NFC", "Seoul SRAM write skipped, SRAM not enabled in cfg1");
        return APP_FALSE;
    }

    if (fieldOk != APP_TRUE)
    {
        APP_LOGW("NFC", "Seoul SRAM write skipped, NFC field not detected yet");
        return APP_FALSE;
    }

    return APP_TRUE;
}

static AppStatus_t App_NfcSeoulWriteNdefToBlock(uint16_t startBlock,
                                                const uint8_t *p_ndef,
                                                uint16_t numBlocks,
                                                const char *p_name)
{
    uint8_t verify[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint16_t byteLength;
    uint16_t blockIndex;
    NFC_Result_t ret;
    uint8_t useSingleBlock;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL) || (p_ndef == NULL) || (numBlocks == 0u) || (p_name == NULL))
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    byteLength = (uint16_t)(numBlocks * 4u);
    if (byteLength > (uint16_t)sizeof(verify))
    {
        return APP_STATUS_BUFFER_OVERFLOW;
    }

    useSingleBlock = (startBlock == APP_NFC_SEOUL_NDEF_EEPROM_BLOCK) ? APP_TRUE : APP_FALSE;

    if (useSingleBlock == APP_TRUE)
    {
        for (blockIndex = 0u; blockIndex < numBlocks; ++blockIndex)
        {
            if (App_NfcSeoulWaitEepromAccessReady("write", (uint16_t)(startBlock + blockIndex)) != APP_TRUE)
            {
                return APP_STATUS_INIT_FAILED;
            }

            ret = NFC_NTP53321_WriteBlock(g_appNfcSeoulTag,
                                          (uint16_t)(startBlock + blockIndex),
                                          &p_ndef[blockIndex * 4u]);
            if (ret != NFC_RESULT_OK)
            {
                APP_LOGE("NFC", "Seoul %s single-block write fail blk=0x%04X ret=%d",
                         p_name,
                         (unsigned int)(startBlock + blockIndex),
                         (int)ret);
                return APP_STATUS_INIT_FAILED;
            }
        }

        for (blockIndex = 0u; blockIndex < numBlocks; ++blockIndex)
        {
            if (App_NfcSeoulWaitEepromAccessReady("read", (uint16_t)(startBlock + blockIndex)) != APP_TRUE)
            {
                return APP_STATUS_INIT_FAILED;
            }

            ret = NFC_NTP53321_ReadBlock(g_appNfcSeoulTag,
                                         (uint16_t)(startBlock + blockIndex),
                                         &verify[blockIndex * 4u]);
            if (ret != NFC_RESULT_OK)
            {
                APP_LOGE("NFC", "Seoul %s single-block verify-read fail blk=0x%04X ret=%d",
                         p_name,
                         (unsigned int)(startBlock + blockIndex),
                         (int)ret);
                return APP_STATUS_INIT_FAILED;
            }
        }
    }
    else
    {
        ret = NFC_NTP53321_WriteMultiBlock(g_appNfcSeoulTag,
                                           startBlock,
                                           p_ndef,
                                           numBlocks);
        if (ret != NFC_RESULT_OK)
        {
            APP_LOGE("NFC", "Seoul %s write fail blk=0x%04X ret=%d",
                     p_name,
                     (unsigned int)startBlock,
                     (int)ret);
            return APP_STATUS_INIT_FAILED;
        }

        ret = NFC_NTP53321_ReadMultiBlock(g_appNfcSeoulTag,
                                          startBlock,
                                          verify,
                                          numBlocks);
        if (ret != NFC_RESULT_OK)
        {
            APP_LOGE("NFC", "Seoul %s verify-read fail blk=0x%04X ret=%d",
                     p_name,
                     (unsigned int)startBlock,
                     (int)ret);
            return APP_STATUS_INIT_FAILED;
        }
    }

    if (memcmp(verify, p_ndef, byteLength) != 0)
    {
        APP_LOGE("NFC", "Seoul %s verify mismatch blk=0x%04X",
                 p_name,
                 (unsigned int)startBlock);
        return APP_STATUS_INIT_FAILED;
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_NfcSeoulWriteSramPayloadOnly(const uint8_t *p_payload, uint8_t payloadLength)
{
    uint8_t ndef[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint16_t numBlocks;
    AppStatus_t status;
    NFC_Result_t nfcRet;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL) || (p_payload == NULL))
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    status = App_NfcSeoulBuildNdefMessage(p_payload,
                                          payloadLength,
                                          ndef,
                                          (uint16_t)sizeof(ndef),
                                          &numBlocks);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    {
        uint8_t status0;

        status0 = 0u;
        if ((NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                         NFC_SESSION_STATUS_ADDR,
                                         0u,
                                         &status0) != NFC_RESULT_OK) ||
            ((status0 & NFC_STATUS0_NFC_FIELD_OK) == 0u))
        {
            g_appNfcSeoulSramSyncPending = APP_TRUE;
            return APP_STATUS_NOT_INITIALIZED;
        }
    }

    uint8_t attempt;

    /* mirror enable + ready 확인을 재시도 (I2C_IF_LOCKED / arbitration 충돌 흡수) */
    #define APP_NFC_SEOUL_MIRROR_RETRY_MAX   (10u)
    #define APP_NFC_SEOUL_MIRROR_RETRY_DELAY (5u)

    for (attempt = 0u; attempt < APP_NFC_SEOUL_MIRROR_RETRY_MAX; attempt++)
    {
        nfcRet = NFC_NTP53321_EnableSRAMMirror(g_appNfcSeoulTag, true);
        if (nfcRet == NFC_RESULT_OK)
        {
            HAL_Delay(APP_NFC_SEOUL_MIRROR_RETRY_DELAY);
            if (App_NfcSeoulIsSramMirrorReady() == APP_TRUE)
            {
                break;   /* 준비 완료 */
            }
        }
        HAL_Delay(APP_NFC_SEOUL_MIRROR_RETRY_DELAY);
    }
    if(attempt >= APP_NFC_SEOUL_MIRROR_RETRY_MAX)
    {
        /* I2C_IF_LOCKED(=BUSY) 등은 잠깐 후 재시도 */
        APP_LOGE("NFC", "Seoul mirror enable retry %u/%u ret=%d",
                 (unsigned int)(attempt + 1u),
                 (unsigned int)APP_NFC_SEOUL_MIRROR_RETRY_MAX,
                 (int)nfcRet);

        g_appNfcSeoulSramSyncPending = APP_TRUE;
        return APP_STATUS_NOT_INITIALIZED;
    }

    status = App_NfcSeoulWriteNdefToBlock(APP_NFC_SEOUL_NDEF_SRAM_BLOCK,
                                          ndef,
                                          numBlocks,
                                          "SRAM");
    if (status == APP_STATUS_OK)
    {
        g_appNfcSeoulSramSyncPending = APP_FALSE;
    }
    return status;
}

static AppStatus_t App_NfcSeoulWritePayloadEepromOnly(const uint8_t *p_payload, uint8_t payloadLength)
{
    uint8_t ndef[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint16_t numBlocks;
    AppStatus_t status;
    AppStatus_t eepromStatus;
    NFC_Result_t nfcRet;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL) || (p_payload == NULL))
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    status = App_NfcSeoulBuildNdefMessage(p_payload,
                                          payloadLength,
                                          ndef,
                                          (uint16_t)sizeof(ndef),
                                          &numBlocks);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    nfcRet = NFC_NTP53321_EnableSRAMMirror(g_appNfcSeoulTag, false);
    if (nfcRet != NFC_RESULT_OK)
    {
        APP_LOGW("NFC", "Seoul SRAM mirror disable returned %d before EEPROM write", (int)nfcRet);
    }
    HAL_Delay(APP_NFC_SEOUL_EEPROM_SETTLE_DELAY_MS);
    eepromStatus = App_NfcSeoulWriteNdefToBlock(APP_NFC_SEOUL_NDEF_EEPROM_BLOCK,
                                                ndef,
                                                numBlocks,
                                                "EEPROM");

    if (eepromStatus != APP_STATUS_OK)
    {
        APP_LOGE("NFC", "Seoul NDEF EEPROM write failed");
        return APP_STATUS_INIT_FAILED;
    }

    g_appNfcSeoulSramSyncPending = APP_TRUE;
    APP_LOGI("NFC", "Seoul NDEF SRAM sync deferred until NFC field detect");
    APP_LOGI("NFC", "Seoul NDEF updated cmd=%02X%02X len=%u",
             (unsigned int)p_payload[0],
             (unsigned int)p_payload[1],
             (unsigned int)payloadLength);
    return APP_STATUS_OK;
}

static AppStatus_t App_NfcSeoulWritePayload(const uint8_t *p_payload, uint8_t payloadLength)
{
    uint8_t ndef[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint16_t numBlocks;
    AppStatus_t status;
    AppStatus_t eepromStatus;
    AppStatus_t sramStatus;
    NFC_Result_t nfcRet;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL) || (p_payload == NULL))
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    status = App_NfcSeoulBuildNdefMessage(p_payload,
                                          payloadLength,
                                          ndef,
                                          (uint16_t)sizeof(ndef),
                                          &numBlocks);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    nfcRet = NFC_NTP53321_EnableSRAMMirror(g_appNfcSeoulTag, false);
    if (nfcRet != NFC_RESULT_OK)
    {
        APP_LOGW("NFC", "Seoul SRAM mirror disable returned %d before EEPROM write", (int)nfcRet);
    }
    HAL_Delay(APP_NFC_SEOUL_EEPROM_SETTLE_DELAY_MS);
    eepromStatus = App_NfcSeoulWriteNdefToBlock(APP_NFC_SEOUL_NDEF_EEPROM_BLOCK,
                                                ndef,
                                                numBlocks,
                                                "EEPROM");

    sramStatus = App_NfcSeoulWriteSramPayloadOnly(p_payload, payloadLength);

    if (eepromStatus != APP_STATUS_OK)
    {
        APP_LOGE("NFC", "Seoul NDEF EEPROM write failed, sram=%d", (int)sramStatus);
        return APP_STATUS_INIT_FAILED;
    }

    if (sramStatus != APP_STATUS_OK)
    {
        g_appNfcSeoulSramSyncPending = APP_TRUE;
        if (sramStatus == APP_STATUS_NOT_INITIALIZED)
        {
            APP_LOGI("NFC", "Seoul NDEF SRAM sync deferred until NFC field detect, EEPROM is valid (sram_status=%d)", (int)sramStatus);
        }
        else
        {
            APP_LOGW("NFC", "Seoul NDEF SRAM write failed, EEPROM is valid (sram_status=%d)", (int)sramStatus);
        }
    }
    else
    {
        g_appNfcSeoulSramSyncPending = APP_FALSE;
    }

    APP_LOGI("NFC", "Seoul NDEF updated cmd=%02X%02X len=%u",
             (unsigned int)p_payload[0],
             (unsigned int)p_payload[1],
             (unsigned int)payloadLength);
    return APP_STATUS_OK;
}

static uint8_t App_NfcSeoulTryExtractPayload(uint16_t startBlock, uint8_t *p_payload, uint8_t *p_payloadLength)
{
    uint8_t raw[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint16_t idx;
    uint16_t payloadLength;
    uint16_t typeLength;
    uint16_t idLength;
    uint8_t recordHeader;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL) || (p_payload == NULL) || (p_payloadLength == NULL))
    {
        return APP_FALSE;
    }

    if (NFC_NTP53321_ReadMultiBlock(g_appNfcSeoulTag,
                                    startBlock,
                                    raw,
                                    APP_NFC_SEOUL_NDEF_MAX_BLOCKS) != NFC_RESULT_OK)
    {
        return APP_FALSE;
    }

    //debug
    #if 1 //kiki TODO delete
    APP_LOGI("NFC", "read startBlock:%04x", startBlock);
    App_LogHexDump(APP_LOG_LEVEL_INFO, "NFC-Debug", (const uint8_t *)raw, APP_NFC_SEOUL_NDEF_MAX_BYTES);
    #endif

    if((raw[8] != APP_NFC_SEOUL_CMD_REQ_GROUP) && (raw[8] != APP_NFC_SEOUL_CMD_RES_GROUP) )
    {
        return APP_FALSE;
    }

    if(raw[8] == APP_NFC_SEOUL_CMD_REQ_GROUP)
    {
        payloadLength = 0;
    }
    else if(raw[8] == APP_NFC_SEOUL_CMD_RES_GROUP)
    {
        switch(raw[9])
        {
            case APP_NFC_SEOUL_CMD_MTR_RES:
                payloadLength = 2+32;
            break;
            case APP_NFC_SEOUL_CMD_AMI_RES:
                payloadLength = 2+30;
            break;
            case APP_NFC_SEOUL_CMD_RSET_RES:
                payloadLength = 2+24;
            break;
            case APP_NFC_SEOUL_CMD_STOR_RES:
                payloadLength = 2+30;
            break;
            default:
                payloadLength = 0;
            break;
        }
    }

    idx = 8;
    if ((payloadLength == 0u) || ((uint32_t)idx + (uint32_t)payloadLength > (uint32_t)sizeof(raw)))
    {
        return APP_FALSE;
    }

    (void)memcpy(p_payload, &raw[idx], payloadLength);
    *p_payloadLength = (uint8_t)payloadLength;
    return APP_TRUE;
}

AppStatus_t App_NfcSeoulInit(NFC_NTP53321_Handle_t *p_tag)
{
    if (p_tag == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    g_appNfcSeoulTag = p_tag;
    g_appNfcSeoulAttached = APP_TRUE;
    g_appNfcSeoulPayloadDirty = APP_TRUE;
    g_appNfcSeoulSramSyncPending = APP_TRUE;
    g_appNfcSeoulLiveRecordValid = APP_FALSE;
    (void)memset(&g_appNfcSeoulLiveRecord, 0, sizeof(g_appNfcSeoulLiveRecord));
#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
    g_appNfcSeoulTestModeLastRefreshTickMs = 0u;
    g_appNfcSeoulTestModeCounter = 0u;
#endif
    (void)memset(&g_appNfcSeoulDebugInfo, 0, sizeof(g_appNfcSeoulDebugInfo));

    APP_LOGI("NFC",
             "[[NFC-L1]] Seoul format init fmt=0x%02X readonly=%u patent_l2=%u",
             (unsigned int)g_appNfcSeoulLayer1Info.snapshotFormatVersion,
             (unsigned int)g_appNfcSeoulLayer1Info.readOnlyLayer1,
             (unsigned int)g_appNfcSeoulLayer1Info.patentLayer2Supported);

    {
        uint8_t cc[4] = {
            NFC_CC_MAGIC_BYTE, /* 0xE1 */
            NFC_CC_VERSION,    /* 0x40 (NDEF Mapping v1.0) */
            NFC_CC_SIZE,       /* 0x80 (0x40×8 = 512 bytes) */
            NFC_CC_ACCESS      /* 0x09 (Read/Write) */
        };

        if ((NFC_NTP53321_WriteBlock(g_appNfcSeoulTag, NFC_SRAM_BASE_ADDR, cc)))
        {
            APP_LOGE("NFC", "SRAM Tag write fail");
        }
    }

    return APP_STATUS_OK;
}

AppStatus_t App_NfcSeoulRetrySramMirrorOnField(void)
{
    uint8_t payload[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint8_t payloadLength;
    AppStatus_t status;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL))
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    if (g_appNfcSeoulSramSyncPending != APP_TRUE)
    {
        APP_LOGI("NFC", "Seoul SRAM synced. return");
        return APP_STATUS_OK;
    }

    status = App_NfcSeoulBuildResponsePayload(APP_NFC_SEOUL_CMD_STOR_RES,
                                              payload,
                                              (uint8_t)sizeof(payload),
                                              &payloadLength);
    if (status != APP_STATUS_OK)
    {
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
        return status;
    }

    /* storage 재조회 대신 EEPROM에 이미 검증된 payload를 그대로 재사용 */
    if (App_NfcSeoulTryExtractPayload(APP_NFC_SEOUL_NDEF_EEPROM_BLOCK,
                                      payload, &payloadLength) != APP_TRUE)
    {
        APP_LOGE("NFC", "Read eeprom fail");
        return APP_STATUS_NOT_INITIALIZED;
    }

#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
    App_NfcSeoulApplyTestModeLiveFields(payload, payloadLength);
#endif

    App_LogHexDump(APP_LOG_LEVEL_INFO, "NFC", (const uint8_t *)payload, payloadLength);
    status = App_NfcSeoulWriteSramPayloadOnly(payload, payloadLength);
    if (status == APP_STATUS_OK)
    {
        APP_LOGI("NFC", "Seoul SRAM retry synced on field cmd=%02X%02X len=%u",
                 (unsigned int)payload[0],
                 (unsigned int)payload[1],
                 (unsigned int)payloadLength);
    }
    else
    {
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
        APP_LOGW("NFC", "Seoul SRAM retry deferred status=%d", (int)status);
    }

    return status;
}

AppStatus_t App_NfcSeoulServiceTestMode(void)
{
#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
    uint8_t payload[40];
    uint8_t payloadLength;
    uint8_t status0;
    uint32_t nowTick;
    AppStatus_t status;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL))
    {
        return APP_STATUS_OK;
    }

    nowTick = HAL_GetTick();
    if ((nowTick - g_appNfcSeoulTestModeLastRefreshTickMs) < APP_NFC_TEST_MODE_REFRESH_MS)
    {
        return APP_STATUS_OK;
    }

    status0 = 0u;
    if ((NFC_NTP53321_ReadSessionReg(g_appNfcSeoulTag,
                                     NFC_SESSION_STATUS_ADDR,
                                     0u,
                                     &status0) != NFC_RESULT_OK) ||
        ((status0 & NFC_STATUS0_NFC_FIELD_OK) == 0u))
    {
        return APP_STATUS_OK;
    }

    status = App_NfcSeoulBuildResponsePayload(APP_NFC_SEOUL_CMD_STOR_RES,
                                              payload,
                                              (uint8_t)sizeof(payload),
                                              &payloadLength);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    App_NfcSeoulApplyTestModeLiveFields(payload, payloadLength);
    status = App_NfcSeoulWriteSramPayloadOnly(payload, payloadLength);
    if (status == APP_STATUS_OK)
    {
        APP_LOGI("NFC", "test refresh cnt=%u, ts=%02u%02u%02u%02u%02u%02u",
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_RECORD_COUNT_OFFSET],
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET + 0u],
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET + 1u],
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET + 2u],
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET + 3u],
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET + 4u],
                 (unsigned int)payload[APP_NFC_SEOUL_STOR_RES_REPORT_TIME_OFFSET + 5u]);
        g_appNfcSeoulTestModeLastRefreshTickMs = nowTick;
    }
    return status;
#else
    return APP_STATUS_OK;
#endif
}

AppStatus_t App_NfcSeoulNotifyStorageChanged(void)
{
    uint8_t payload[40];
    uint8_t payloadLength;
    AppStatus_t status;

    g_appNfcSeoulLiveRecordValid = APP_FALSE;
    (void)memset(&g_appNfcSeoulLiveRecord, 0, sizeof(g_appNfcSeoulLiveRecord));

    status = App_NfcSeoulBuildResponsePayload(APP_NFC_SEOUL_CMD_STOR_RES,
                                              payload,
                                              (uint8_t)sizeof(payload),
                                              &payloadLength);
    if (status != APP_STATUS_OK)
    {
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
        g_appNfcSeoulPayloadDirty = APP_TRUE;
        return status;
    }

    status = App_NfcSeoulWritePayloadEepromOnly(payload, payloadLength);
    if (status == APP_STATUS_OK)
    {
        g_appNfcSeoulPayloadDirty = APP_FALSE;
        g_appNfcSeoulDebugInfo.storageRefreshCount++;
        App_NfcSeoulDebugRecordResponse(payload, payloadLength, APP_FALSE, APP_FALSE, status);
    }
    else
    {
        g_appNfcSeoulPayloadDirty = APP_TRUE;
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
    }

    return status;
}

AppStatus_t App_NfcSeoulNotifyLiveMeterRecord(const AppMeterStorageRecord_t *p_record)
{
    uint8_t payload[40];
    uint8_t payloadLength;
    AppStatus_t status;

    if (p_record == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    (void)memcpy(&g_appNfcSeoulLiveRecord, p_record, sizeof(g_appNfcSeoulLiveRecord));
    g_appNfcSeoulLiveRecordValid = APP_TRUE;

    status = App_NfcSeoulBuildResponsePayload(APP_NFC_SEOUL_CMD_STOR_RES,
                                              payload,
                                              (uint8_t)sizeof(payload),
                                              &payloadLength);
    if (status != APP_STATUS_OK)
    {
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
        g_appNfcSeoulPayloadDirty = APP_TRUE;
        return status;
    }

    status = App_NfcSeoulWritePayload(payload, payloadLength);
    if (status == APP_STATUS_OK)
    {
        g_appNfcSeoulPayloadDirty = APP_FALSE;
        g_appNfcSeoulDebugInfo.storageRefreshCount++;
        App_NfcSeoulDebugRecordResponse(payload, payloadLength, APP_FALSE, APP_FALSE, status);
        APP_LOGI("NFC", "Live meter snapshot reflected immediately");
    }
    else
    {
        g_appNfcSeoulPayloadDirty = APP_TRUE;
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
    }

    return status;
}

AppStatus_t App_NfcSeoulProcessTag(AppNfcSeoulProcessResult_t *p_result)
{
    uint8_t payload[64];
    uint8_t response[APP_NFC_SEOUL_NDEF_MAX_BYTES];
    uint8_t payloadLength;
    uint8_t responseLength;
    AppStatus_t status;
    uint8_t cmd2;
    uint8_t readSource;

    if (p_result == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    (void)memset(p_result, 0, sizeof(*p_result));
    readSource = 0u;

    if (App_NfcSeoulTryExtractPayload(APP_NFC_SEOUL_NDEF_SRAM_BLOCK, payload, &payloadLength) == APP_TRUE)
    {
        readSource = 1u;
    }
    else if (App_NfcSeoulTryExtractPayload(APP_NFC_SEOUL_NDEF_EEPROM_BLOCK, payload, &payloadLength) == APP_TRUE)
    {
        readSource = 2u;
    }
    else
    {
        if (g_appNfcSeoulPayloadDirty == APP_TRUE)
        {
            (void)App_NfcSeoulNotifyStorageChanged();
        }
        return APP_STATUS_OK;
    }

    APP_LOGI("NFC", "Seoul request cmd=%02X%02X len=%u src=%s",
             (unsigned int)payload[0],
             (unsigned int)payload[1],
             (unsigned int)payloadLength,
             (readSource == 1u) ? "SRAM" : "EEPROM");

    if ((payloadLength < 2u) || (payload[0] != APP_NFC_SEOUL_CMD_REQ_GROUP))
    {
        return APP_STATUS_OK;
    }

    p_result->requestCmd1 = payload[0];
    p_result->requestCmd2 = payload[1];
    App_NfcSeoulDebugRecordRequest(payload, payloadLength, readSource);
    switch (payload[1])
    {
        case APP_NFC_SEOUL_CMD_MTR_REQ:
            cmd2 = APP_NFC_SEOUL_CMD_MTR_RES;
            break;

        case APP_NFC_SEOUL_CMD_AMI_REQ:
            cmd2 = APP_NFC_SEOUL_CMD_AMI_RES;
            break;

        case APP_NFC_SEOUL_CMD_RSET_REQ:
            cmd2 = APP_NFC_SEOUL_CMD_RSET_RES;
            p_result->commRequested = APP_TRUE;
            break;

        default:
            return APP_STATUS_OK;
    }

    status = App_NfcSeoulBuildResponsePayload(cmd2,
                                              response,
                                              (uint8_t)sizeof(response),
                                              &responseLength);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_NfcSeoulWritePayload(response, responseLength);
    if (status != APP_STATUS_OK)
    {
        g_appNfcSeoulDebugInfo.lastStatus = (uint8_t)status;
        return status;
    }

    p_result->handled = APP_TRUE;
    p_result->responseCmd1 = response[0];
    p_result->responseCmd2 = response[1];
    App_NfcSeoulDebugRecordResponse(response, responseLength, p_result->handled, p_result->commRequested, APP_STATUS_OK);
    APP_LOGI("NFC", "Seoul response cmd=%02X%02X len=%u comm=%u",
             (unsigned int)response[0],
             (unsigned int)response[1],
             (unsigned int)responseLength,
             (unsigned int)p_result->commRequested);
    return APP_STATUS_OK;
}

const AppNfcSeoulDebugInfo_t *App_NfcSeoulGetDebugInfo(void)
{
    return &g_appNfcSeoulDebugInfo;
}

const AppNfcSeoulLayer1Info_t *App_NfcSeoulGetLayer1Info(void)
{
    return &g_appNfcSeoulLayer1Info;
}

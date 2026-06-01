#include "app_nfc_seoul_format.h"

#include <string.h>

#include "app_build_config.h"
#include "app_log.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"

#define APP_NFC_SEOUL_TLV_NDEF_MESSAGE            (0x03u)
#define APP_NFC_SEOUL_TLV_TERMINATOR              (0xFEu)
#define APP_NFC_SEOUL_NDEF_HEADER_SHORT_UNKNOWN   (0xD5u)
#define APP_NFC_SEOUL_NDEF_MAX_BYTES              (96u)
#define APP_NFC_SEOUL_NDEF_MAX_BLOCKS             ((APP_NFC_SEOUL_NDEF_MAX_BYTES + 3u) / 4u)
#define APP_NFC_SEOUL_FORMAT_VERSION              (0x10u)
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
static AppNfcSeoulDebugInfo_t g_appNfcSeoulDebugInfo;

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

        if (App_NfcSeoulIsAllZero(options.wirelessQuality, (uint8_t)sizeof(options.wirelessQuality)) != APP_TRUE)
        {
            rsrpValue = App_NfcSeoulReadLe16(&options.wirelessQuality[4]);
            App_NfcSeoulU16ToBe(rsrpValue, p_snapshot->rsrp);
        }
    }

    hasRecord = APP_FALSE;
    status = App_MeterStorageGetInfo(&info);
    if ((status == APP_STATUS_OK) && (info.count != 0u))
    {
        status = App_MeterStorageReadAt((uint8_t)(info.count - 1u), &record);
        if (status == APP_STATUS_OK)
        {
            hasRecord = APP_TRUE;
            p_snapshot->recordCount = info.count;
            App_NfcSeoulEncodeBcdBe(record.meterId, p_snapshot->meterIdBcd, (uint8_t)sizeof(p_snapshot->meterIdBcd));
            App_NfcSeoulU32ToBe(record.readingScaled, p_snapshot->reading);
            p_snapshot->caliberDecimal = record.caliberDecimal;
            p_snapshot->alarmStatus = (uint8_t)(record.meterStatus & (APP_METER_STORAGE_STATUS_OVERFLOW |
                                                                      APP_METER_STORAGE_STATUS_REVERSE_FLOW |
                                                                      APP_METER_STORAGE_STATUS_LEAK));
            if ((record.flags & APP_METER_STORAGE_FLAG_TIME_VALID) != 0u)
            {
                (void)memcpy(p_snapshot->reportTime, record.ts, sizeof(record.ts));
                (void)memcpy(p_snapshot->readingTime, record.ts, sizeof(record.ts));
            }
        }
    }

    if (hasRecord != APP_TRUE)
    {
        p_snapshot->commState = APP_NFC_SEOUL_COMM_OFF;
    }
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

    if ((uint16_t)payloadLength + 6u > ndefCapacity)
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
    NFC_Result_t ret;

    if ((g_appNfcSeoulAttached != APP_TRUE) || (g_appNfcSeoulTag == NULL) || (p_ndef == NULL) || (numBlocks == 0u) || (p_name == NULL))
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    byteLength = (uint16_t)(numBlocks * 4u);
    if (byteLength > (uint16_t)sizeof(verify))
    {
        return APP_STATUS_BUFFER_OVERFLOW;
    }

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

    nfcRet = NFC_NTP53321_EnableSRAMMirror(g_appNfcSeoulTag, true);
    if (nfcRet != NFC_RESULT_OK)
    {
        APP_LOGW("NFC", "Seoul SRAM mirror enable returned %d before SRAM write", (int)nfcRet);
    }
    HAL_Delay(5U);
    if (App_NfcSeoulIsSramMirrorReady() != APP_TRUE)
    {
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
    HAL_Delay(2U);
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
        APP_LOGW("NFC", "Seoul NDEF SRAM write skipped/failed, EEPROM is valid (sram_status=%d)", (int)sramStatus);
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

    if (raw[0] != APP_NFC_SEOUL_TLV_NDEF_MESSAGE)
    {
        return APP_FALSE;
    }

    idx = 2u;
    recordHeader = raw[idx++];
    typeLength = raw[idx++];

    if ((recordHeader & 0x10u) != 0u)
    {
        payloadLength = raw[idx++];
    }
    else
    {
        payloadLength = (uint16_t)(((uint32_t)raw[idx] << 24) |
                                   ((uint32_t)raw[idx + 1u] << 16) |
                                   ((uint32_t)raw[idx + 2u] << 8) |
                                   ((uint32_t)raw[idx + 3u]));
        idx = (uint16_t)(idx + 4u);
    }

    idLength = 0u;
    if ((recordHeader & 0x08u) != 0u)
    {
        idLength = raw[idx++];
    }

    idx = (uint16_t)(idx + typeLength + idLength);
    if ((payloadLength == 0u) || ((uint32_t)idx + (uint32_t)payloadLength > (uint32_t)sizeof(raw)))
    {
        return APP_FALSE;
    }

    if (payloadLength > 64u)
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
    (void)memset(&g_appNfcSeoulDebugInfo, 0, sizeof(g_appNfcSeoulDebugInfo));

    return APP_STATUS_OK;
}

AppStatus_t App_NfcSeoulRetrySramMirrorOnField(void)
{
    uint8_t payload[40];
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

AppStatus_t App_NfcSeoulNotifyStorageChanged(void)
{
    uint8_t payload[40];
    uint8_t payloadLength;
    AppStatus_t status;

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
    uint8_t response[40];
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

    if ((payloadLength < 2u) || (payload[0] != APP_NFC_SEOUL_CMD_REQ_GROUP))
    {
        return APP_STATUS_OK;
    }

    p_result->requestCmd1 = payload[0];
    p_result->requestCmd2 = payload[1];
    App_NfcSeoulDebugRecordRequest(payload, payloadLength, readSource);
    APP_LOGI("NFC", "Seoul request cmd=%02X%02X len=%u src=%s",
             (unsigned int)payload[0],
             (unsigned int)payload[1],
             (unsigned int)payloadLength,
             (readSource == 1u) ? "SRAM" : "EEPROM");

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

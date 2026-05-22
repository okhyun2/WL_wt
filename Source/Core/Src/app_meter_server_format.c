#include "app_meter_server_format.h"

#include <string.h>
#include <stdio.h>

#include "app_build_config.h"
#include "app_meter_storage.h"
#include "app_log.h"

#define APP_METER_SERVER_FORMAT_FIXED_LORA_QUALITY_BYTES   (2u)
#define APP_METER_SERVER_FORMAT_FIXED_NBIOT_QUALITY_BYTES  (10u)
#define APP_METER_SERVER_FORMAT_FIXED_NBIOT_MOBILE_ID_BYTES (16u)

static void App_MeterServerFormatU32ToLe(uint32_t value, uint8_t *p_out)
{
    p_out[0] = (uint8_t)(value & 0xFFu);
    p_out[1] = (uint8_t)((value >> 8) & 0xFFu);
    p_out[2] = (uint8_t)((value >> 16) & 0xFFu);
    p_out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static void App_MeterServerFormatU16ToLe(uint16_t value, uint8_t *p_out)
{
    p_out[0] = (uint8_t)(value & 0xFFu);
    p_out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void App_MeterServerFormatEncodeBcdBe(uint32_t value, uint8_t *p_out, uint8_t digitBytes)
{
    uint8_t digits[10];
    int32_t index;
    uint8_t digitIndex;

    for (index = 0; index < 10; index++)
    {
        digits[index] = 0u;
    }

    for (index = 9; index >= 0; index--)
    {
        digits[index] = (uint8_t)(value % 10u);
        value /= 10u;
    }

    digitIndex = (uint8_t)(10u - (digitBytes * 2u));
    for (index = 0; index < digitBytes; index++)
    {
        p_out[index] = (uint8_t)((digits[digitIndex] << 4) | digits[digitIndex + 1u]);
        digitIndex = (uint8_t)(digitIndex + 2u);
    }
}

static uint8_t App_MeterServerFormatCalcChecksum(const uint8_t *p_packet, uint16_t startIndex, uint16_t endIndex)
{
    uint32_t sum;
    uint16_t index;

    sum = 0u;
    for (index = startIndex; index < endIndex; index++)
    {
        sum += p_packet[index];
    }

    return (uint8_t)(sum & 0xFFu);
}

static AppStatus_t App_MeterServerFormatAppendBytes(uint8_t *p_packet,
                                                    uint16_t packetCapacity,
                                                    uint16_t *p_cursor,
                                                    const uint8_t *p_data,
                                                    uint16_t dataLength)
{
    APP_RETURN_IF_FALSE((p_packet != NULL) && (p_cursor != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(((uint32_t)(*p_cursor) + dataLength) <= packetCapacity, APP_STATUS_BUFFER_OVERFLOW);

    if ((p_data != NULL) && (dataLength > 0u))
    {
        (void)memcpy(&p_packet[*p_cursor], p_data, dataLength);
        *p_cursor = (uint16_t)(*p_cursor + dataLength);
    }

    return APP_STATUS_OK;
}

void App_MeterServerOptionsSetDefaults(AppMeterServerFormatOptions_t *p_options)
{
    if (p_options == NULL)
    {
        return;
    }

    (void)memset(p_options, 0, sizeof(*p_options));
    p_options->linkHeader = APP_METER_SERVER_FORMAT_HEADER_NBIOT;
    p_options->command = APP_METER_SERVER_FORMAT_COMMAND_PERIOD_REPORT;

    p_options->wirelessQuality[0] = 0;
    p_options->mobileIdBcd[0] = 0;
    p_options->deviceSerialBcd[0] = 0;

    p_options->firmwareVersion[0] = (uint8_t)APP_FW_VERSION_MAJOR;
    p_options->firmwareVersion[1] = (uint8_t)APP_FW_VERSION_MINOR;

    p_options->terminalBattery.b.alarm = 0;
    p_options->terminalBattery.b.volt = 36; //volt * 10

    p_options->meteringPeriodHours = 1u;
    p_options->reportingPeriodHours = 1u;
}

static AppStatus_t App_MeterServerFormatBuildInternal(const AppMeterServerFormatOptions_t *p_options,
                                                      uint8_t *p_packet,
                                                      uint16_t packetCapacity,
                                                      AppMeterServerFormatResult_t *p_result,
                                                      uint8_t clearOnSuccess)
{
    AppMeterStorageInfo_t info;
    AppMeterStorageRecord_t latestRecord;
    AppMeterStorageRecord_t record;
    AppMeterStorageRecord_t prevRecord;
    uint16_t cursor;
    uint16_t lengthIndex;
    uint8_t qualityLength;
    uint8_t mobileIdLength;
    uint8_t maxRecordCount;
    uint8_t usedRecordCount;
    uint8_t logicalIndex;
    uint8_t basePos;
    uint8_t packetMeterIdBcd[4];
    uint8_t valueBytes[4];
    uint8_t deltaBytes[2];
    uint32_t baseValue;
    uint32_t prevValue;
    uint32_t curValue;
    uint32_t deltaValue;
    AppStatus_t status;

    APP_RETURN_IF_FALSE((p_options != NULL) && (p_packet != NULL) && (p_result != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_LORA) ||
                        (p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_NBIOT),
                        APP_STATUS_INVALID_PARAM);

    status = App_MeterStorageGetInfo(&info);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    APP_LOGI("MSTOR", "Saved meter data count:%d(%s)", info.count, (info.count > 0)? "build send data":"skip build send data");
    APP_RETURN_IF_FALSE(info.count > 0u, APP_STATUS_INVALID_PARAM);

    qualityLength = (p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_LORA)
                    ? APP_METER_SERVER_FORMAT_FIXED_LORA_QUALITY_BYTES
                    : APP_METER_SERVER_FORMAT_FIXED_NBIOT_QUALITY_BYTES;
    mobileIdLength = (p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_NBIOT)
                     ? APP_METER_SERVER_FORMAT_FIXED_NBIOT_MOBILE_ID_BYTES
                     : 0u;
    maxRecordCount = (p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_LORA)
                     ? APP_METER_SERVER_FORMAT_MAX_RECORDS_LORA
                     : APP_METER_SERVER_FORMAT_MAX_RECORDS_NBIOT;
    usedRecordCount = (info.count < maxRecordCount) ? (uint8_t)info.count : maxRecordCount;

    /* 가장 최신 검침 레코드(기준 검침값 후보) */
    status = App_MeterStorageReadAt((uint8_t)(info.count - 1u), &latestRecord);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    (void)memset(p_result, 0, sizeof(*p_result));
    (void)memset(p_packet, 0, packetCapacity);

    cursor = 0u;

    /* ---------- 헤더부 (문서 6.4절 순서) ---------- */

    /* [0] 메시지 헤더 */
    p_packet[cursor++] = p_options->linkHeader;

    /* [1] 메시지 길이 (나중에 채움) */
    lengthIndex = cursor;
    p_packet[cursor++] = 0u;

    /* [2] 메시지 형식(커맨드) */
    p_packet[cursor++] = p_options->command;

    /* [3..] 이동통신 ID (NB-IoT만, 16B = IMEI 8 + IMSI 8) */
    status = App_MeterServerFormatAppendBytes(p_packet,
                                              packetCapacity,
                                              &cursor,
                                              p_options->mobileIdBcd,
                                              mobileIdLength);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* 무선품질 정보 (LoRa 2B / NB-IoT 10B) */
    status = App_MeterServerFormatAppendBytes(p_packet,
                                              packetCapacity,
                                              &cursor,
                                              p_options->wirelessQuality,
                                              qualityLength);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* 단말기 정보 (8B): 일련번호(5) + F/W(2) + 배터리(1) */
    status = App_MeterServerFormatAppendBytes(p_packet,
                                              packetCapacity,
                                              &cursor,
                                              p_options->deviceSerialBcd,
                                              sizeof(p_options->deviceSerialBcd));
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    status = App_MeterServerFormatAppendBytes(p_packet,
                                              packetCapacity,
                                              &cursor,
                                              p_options->firmwareVersion,
                                              sizeof(p_options->firmwareVersion));
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    p_packet[cursor++] = p_options->terminalBattery.value;

    /* 계량기 정보 (7B): 기물번호(4 BCD) + 형식(1) + 구경/소수점(1) + 상태(1) */
    App_MeterServerFormatEncodeBcdBe(latestRecord.meterId, packetMeterIdBcd, (uint8_t)sizeof(packetMeterIdBcd));
    status = App_MeterServerFormatAppendBytes(p_packet, packetCapacity, &cursor,
                                              packetMeterIdBcd, sizeof(packetMeterIdBcd));
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    p_packet[cursor++] = latestRecord.meterType;
    p_packet[cursor++] = latestRecord.caliberDecimal;
    p_packet[cursor++] = latestRecord.meterStatus;

    /* 검침 및 보고 주기 (2B) */
    p_packet[cursor++] = p_options->meteringPeriodHours;
    p_packet[cursor++] = p_options->reportingPeriodHours;

    /* 검침 시간 (6B): YY MM DD HH MM SS */
    status = App_MeterServerFormatAppendBytes(p_packet, packetCapacity, &cursor,
                                              latestRecord.ts, sizeof(latestRecord.ts));
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* ---------- 검침 데이터부 (문서 5.8절) ---------- */

    /* 검침 주기(1B) + 데이터 개수(1B) + 기준 검침값 위치(1B) */
    p_packet[cursor++] = p_options->meteringPeriodHours;
    p_packet[cursor++] = usedRecordCount;

    /* 기준 검침값 위치: 가장 최신부터 첫 valid 레코드 찾기 */
    basePos = 0xFFu;
    baseValue = 0xFFFFFFFFu;
    for (logicalIndex = 0u; logicalIndex < usedRecordCount; logicalIndex++)
    {
        status = App_MeterStorageReadAt((uint8_t)((info.count - 1u) - logicalIndex), &record);
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

        if ((record.flags & APP_METER_STORAGE_FLAG_READING_VALID) != 0u)
        {
            basePos = logicalIndex;
            baseValue = record.readingScaled;
            break;
        }
    }
    p_packet[cursor++] = basePos;

    /* 기준 검침값(4B, LE) */
    App_MeterServerFormatU32ToLe(baseValue, valueBytes);
    status = App_MeterServerFormatAppendBytes(p_packet, packetCapacity, &cursor,
                                              valueBytes, sizeof(valueBytes));
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* 차이값 n개: 이전 데이터(=직전 logicalIndex)와의 차이
     *  - logicalIndex < basePos : 검침 실패(0xFFFF)
     *  - logicalIndex == basePos: 기준점이므로 0
     *  - logicalIndex >  basePos: prevValue - curValue
     *  - 현재 record가 invalid면 0xFFFF, prevValue는 그대로 유지(다음 valid까지)
     */
    prevValue = baseValue;
    for (logicalIndex = 0u; logicalIndex < usedRecordCount; logicalIndex++)
    {
        status = App_MeterStorageReadAt((uint8_t)((info.count - 1u) - logicalIndex), &record);
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

        if (basePos == 0xFFu)
        {
            /* 기준 검침값이 없으면 모든 차이값 0xFFFF */
            deltaValue = 0xFFFFu;
        }
        else if (logicalIndex < basePos)
        {
            deltaValue = 0xFFFFu;
        }
        else if (logicalIndex == basePos)
        {
            deltaValue = 0u;
            prevValue = baseValue;
        }
        else if ((record.flags & APP_METER_STORAGE_FLAG_READING_VALID) == 0u)
        {
            deltaValue = 0xFFFFu;
            /* prevValue는 유지: 다음 valid 레코드와 비교 시점에서도 가장 가까운 valid 기준 */
        }
        else
        {
            curValue = record.readingScaled;
            if (prevValue < curValue)
            {
                /* 단조감소 위배 -> 검침 실패로 표시 */
                deltaValue = 0xFFFFu;
            }
            else
            {
                deltaValue = prevValue - curValue;
                if (deltaValue > 0xFFFEu)
                {
                    deltaValue = 0xFFFFu;
                }
                else
                {
                    prevValue = curValue;  /* 다음 비교 기준 갱신 */
                }
            }
        }

        App_MeterServerFormatU16ToLe((uint16_t)deltaValue, deltaBytes);
        status = App_MeterServerFormatAppendBytes(p_packet, packetCapacity, &cursor,
                                                  deltaBytes, sizeof(deltaBytes));
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    }

    /* ---------- 메시지 길이 / 체크섬 ---------- */

    /* 메시지 길이: 길이 필드 다음부터 체크섬 직전까지의 바이트 수 */
    p_packet[lengthIndex] = (uint8_t)(cursor - (lengthIndex + 1u));

    /* 체크섬: 메시지 길이 필드 '다음'부터 체크섬 직전까지의 합 */
    p_result->checksum = App_MeterServerFormatCalcChecksum(p_packet,
                                                           (uint16_t)(lengthIndex + 1u),
                                                           cursor);

    APP_RETURN_IF_FALSE((uint32_t)cursor < packetCapacity, APP_STATUS_BUFFER_OVERFLOW);
    p_packet[cursor++] = p_result->checksum;

    p_result->payloadLength = p_packet[lengthIndex];
    p_result->packetLength  = cursor;
    p_result->recordCount   = usedRecordCount;
    p_result->cleared       = APP_FALSE;

    (void)prevRecord;  /* reserved for future use */

    if (clearOnSuccess == APP_TRUE)
    {
        status = App_MeterStorageClearAll();
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        p_result->cleared = APP_TRUE;
    }

    return APP_STATUS_OK;
}

AppStatus_t App_MeterServerFormatBuildFromStorage(const AppMeterServerFormatOptions_t *p_options,
                                                  uint8_t *p_packet,
                                                  uint16_t packetCapacity,
                                                  AppMeterServerFormatResult_t *p_result)
{
    return App_MeterServerFormatBuildInternal(p_options, p_packet, packetCapacity, p_result, APP_FALSE);
}

AppStatus_t App_MeterServerFormatBuildFromStorageAndClear(const AppMeterServerFormatOptions_t *p_options,
                                                          uint8_t *p_packet,
                                                          uint16_t packetCapacity,
                                                          AppMeterServerFormatResult_t *p_result)
{
    return App_MeterServerFormatBuildInternal(p_options, p_packet, packetCapacity, p_result, APP_TRUE);
}

/* ================================================================
 *  Options 영구 저장 (Bank1, app_meter_storage의 슬롯 헬퍼 사용)
 * ================================================================ */
static AppConfigSlotRegion_t g_appMeterServerOptionsRegion =
{
    .regionOffset    = APP_STORAGE_METER_OPTION_EEPROM_OFFSET_BYTES,  /* 1024 */
    .regionSize      = APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES,    /* 1024 */
    .slotCount       = APP_STORAGE_OPTION_SLOT_COUNT,
    .payloadSize     = (uint8_t)sizeof(AppMeterServerFormatOptions_t),
    .latestSlotIndex = 0u,
    .latestSeq       = 0u,
    .initialized     = APP_FALSE,
};

AppStatus_t App_MeterServerOptionsInit(void)
{
    return App_ConfigSlotInit(&g_appMeterServerOptionsRegion);
}

AppStatus_t App_MeterServerOptionsLoad(AppMeterServerFormatOptions_t *p_options)
{
    uint8_t found;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_options != NULL, APP_STATUS_INVALID_PARAM);

    status = App_ConfigSlotLoad(&g_appMeterServerOptionsRegion, p_options, &found);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    if (found == APP_FALSE)
    {
        App_MeterServerOptionsSetDefaults(p_options);
        return APP_STATUS_NOT_INITIALIZED;
    }
    return APP_STATUS_OK;
}

AppStatus_t App_MeterServerOptionsSave(const AppMeterServerFormatOptions_t *p_options)
{
    APP_RETURN_IF_FALSE(p_options != NULL, APP_STATUS_INVALID_PARAM);
    return App_ConfigSlotSave(&g_appMeterServerOptionsRegion, p_options);
}

AppStatus_t App_MeterServerOptionsClear(void)
{
    return App_ConfigSlotClear(&g_appMeterServerOptionsRegion);
}

void App_MeterServerOptionsInfo(void)
{
    APP_LOGI("OPT", "Bank1 options: addr=0x%08lx, size=%lu, slot(cnt=%u, payload=%lu)",
             (uint32_t)(DATA_EEPROM_BASE + APP_STORAGE_METER_OPTION_EEPROM_OFFSET_BYTES),
             (uint32_t)APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES,
             (unsigned)APP_STORAGE_OPTION_SLOT_COUNT,
             (uint32_t)sizeof(AppMeterServerFormatOptions_t));
}

static void App_MeterServerFormatDumpHex(const char *p_tag,
                                         const char *p_name,
                                         const uint8_t *p_data,
                                         uint32_t length)
{
    char     line[3 * 16 + 1];      /* "XX " * 16 + null */
    uint32_t i;
    uint32_t pos;

    pos = 0u;
    line[0] = '\0';
    for (i = 0u; i < length; i++)
    {
        pos += (uint32_t)snprintf(&line[pos], sizeof(line) - pos, "%02X ", p_data[i]);
        if (((i + 1u) % 16u == 0u) || ((i + 1u) == length))
        {
            APP_LOGI(p_tag, "  %s[%02lu..]: %s",
                     p_name, (uint32_t)(i - (i % 16u)), line);
            pos = 0u;
            line[0] = '\0';
        }
    }
}

void App_MeterServerOptionsDump(const AppMeterServerFormatOptions_t *p_options)
{
    if (p_options == NULL)
    {
        APP_LOGW("OPT", "dump: null");
        return;
    }

    APP_LOGI("OPT", "===== Meter Server Options =====");
    APP_LOGI("OPT", "  linkHeader     : 0x%02X (%s)",
             p_options->linkHeader,
             (p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_LORA)  ? "LoRa"  :
             (p_options->linkHeader == APP_METER_SERVER_FORMAT_HEADER_NBIOT) ? "NB-IoT": "unknown");
    APP_LOGI("OPT", "  command        : 0x%02X", p_options->command);

    App_MeterServerFormatDumpHex("OPT", "wirelessQuality",
                                 p_options->wirelessQuality,
                                 sizeof(p_options->wirelessQuality));

    App_MeterServerFormatDumpHex("OPT", "mobileIdBcd",
                                 p_options->mobileIdBcd,
                                 sizeof(p_options->mobileIdBcd));

    App_MeterServerFormatDumpHex("OPT", "deviceSerialBcd",
                                 p_options->deviceSerialBcd,
                                 sizeof(p_options->deviceSerialBcd));

    APP_LOGI("OPT", "  firmwareVer    : %u.%u",
             (unsigned)p_options->firmwareVersion[0],
             (unsigned)p_options->firmwareVersion[1]);

    APP_LOGI("OPT", "  battery        : raw=0x%02X, volt=%u(x0.1V), alarm=%u",
             p_options->terminalBattery.value,
             (unsigned)p_options->terminalBattery.b.volt,
             (unsigned)p_options->terminalBattery.b.alarm);

    APP_LOGI("OPT", "  meteringPeriod : %u hour(s)", (unsigned)p_options->meteringPeriodHours);
    APP_LOGI("OPT", "  reportingPeriod: %u hour(s)", (unsigned)p_options->reportingPeriodHours);

    APP_LOGI("OPT", "  slot(idx=%u, seq=%u)",
             (unsigned)g_appMeterServerOptionsRegion.latestSlotIndex,
             (unsigned)g_appMeterServerOptionsRegion.latestSeq);
}

/* ================================================================
 *  Options 필드 빌더 구현
 * ================================================================ */
void App_MeterServerOptionsSetLink(AppMeterServerFormatOptions_t *p_options,
                                   uint8_t linkHeader,
                                   uint8_t command)
{
    if (p_options == NULL) { return; }
    p_options->linkHeader = linkHeader;
    p_options->command    = command;
}

void App_MeterServerOptionsSetWirelessQuality(AppMeterServerFormatOptions_t *p_options,
                                              const uint8_t *p_quality,
                                              uint8_t length)
{
    uint8_t copyLen;

    if ((p_options == NULL) || (p_quality == NULL) || (length == 0u))
    {
        return;
    }

    copyLen = (length > sizeof(p_options->wirelessQuality))
              ? (uint8_t)sizeof(p_options->wirelessQuality)
              : length;

    (void)memset(p_options->wirelessQuality, 0, sizeof(p_options->wirelessQuality));
    (void)memcpy(p_options->wirelessQuality, p_quality, copyLen);
}

void App_MeterServerOptionsSetMobileId(AppMeterServerFormatOptions_t *p_options,
                                       const uint8_t *p_imeiBcd8,
                                       const uint8_t *p_imsiBcd8)
{
    if (p_options == NULL) { return; }

    (void)memset(p_options->mobileIdBcd, 0, sizeof(p_options->mobileIdBcd));

    if (p_imeiBcd8 != NULL)
    {
        (void)memcpy(&p_options->mobileIdBcd[0], p_imeiBcd8, 8u);
    }
    if (p_imsiBcd8 != NULL)
    {
        (void)memcpy(&p_options->mobileIdBcd[8], p_imsiBcd8, 8u);
    }
}

void App_MeterServerOptionsSetDeviceInfo(AppMeterServerFormatOptions_t *p_options,
                                         const uint8_t *p_serialBcd5,
                                         uint8_t fwMajor,
                                         uint8_t fwMinor)
{
    if (p_options == NULL) { return; }

    if (p_serialBcd5 != NULL)
    {
        (void)memcpy(p_options->deviceSerialBcd, p_serialBcd5,
                     sizeof(p_options->deviceSerialBcd));
    }
    p_options->firmwareVersion[0] = fwMajor;
    p_options->firmwareVersion[1] = fwMinor;
}

void App_MeterServerOptionsSetBattery(AppMeterServerFormatOptions_t *p_options,
                                      uint8_t voltX10,
                                      uint8_t alarm)
{
    if (p_options == NULL) { return; }

    p_options->terminalBattery.value   = 0u;
    p_options->terminalBattery.b.volt  = (uint8_t)(voltX10 & 0x3Fu);   /* 6bit */
    p_options->terminalBattery.b.alarm = (uint8_t)(alarm   & 0x01u);   /* 1bit */
}

void App_MeterServerOptionsSetPeriod(AppMeterServerFormatOptions_t *p_options,
                                     uint8_t meteringHours,
                                     uint8_t reportingHours)
{
    if (p_options == NULL) { return; }
    p_options->meteringPeriodHours  = meteringHours;
    p_options->reportingPeriodHours = reportingHours;
}

/* ----------------------------------------------------------------
 *  Update: 현재 EEPROM 값과 비교해서 다를 때만 저장
 *  - 같은 값을 매번 쓰는 것을 방지 (셀 수명 보호)
 * ---------------------------------------------------------------- */
AppStatus_t App_MeterServerOptionsUpdate(const AppMeterServerFormatOptions_t *p_options)
{
    AppMeterServerFormatOptions_t current;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_options != NULL, APP_STATUS_INVALID_PARAM);

    status = App_MeterServerOptionsLoad(&current);
    if ((status == APP_STATUS_OK) &&
        (memcmp(&current, p_options, sizeof(current)) == 0))
    {
        APP_LOGI("OPT", "update: no change, skip write");
        return APP_STATUS_OK;
    }

    APP_LOGI("OPT", "update: write new options");
    return App_MeterServerOptionsSave(p_options);
}

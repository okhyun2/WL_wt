#include "app_meter.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "app_build_config.h"
#include "app_log.h"
#include "app_hw.h"
#include "app_clock.h"
#include "app_meter_storage.h"
#include "app_nfc_seoul_format.h"

static uint8_t g_appMeterStorageEnabled = APP_TRUE;

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
/* EPC 시험 전용: 계량기 프레임 처리 시도 횟수(성공/실패 무관, 부팅 후 누적) */
static uint32_t g_epcMeterAttemptSeq = 0u;
#endif

/* 4바이트 배열 → uint32_t (Little Endian) */
static uint32_t App_MeterBytesToUint32LE(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]       |
           ((uint32_t)bytes[1] << 8)  |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/* uint32_t → 4바이트 배열 (Little Endian) */
#if 0 //optimize for del
static void App_MeterUint32ToBytes(uint32_t value, uint8_t *bytes)
{
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)((value >> 8) & 0xFF);
    bytes[2] = (uint8_t)((value >> 16) & 0xFF);
    bytes[3] = (uint8_t)((value >> 24) & 0xFF);
}
#endif

/* ================================================
 * BCD 유효성 검사: 각 니블이 0-9 범위인지 확인
 * ================================================ */
static bool BCD_IsValid(uint32_t bcd_value)
{
    for (int i = 0; i < 8; i++) {
        uint8_t nibble = (bcd_value >> (i * 4)) & 0xF;
        if (nibble > 9) {
            return false;  // A-F는 BCD에서 유효하지 않음
        }
    }
    return true;
}

/* ================================================
 * BCD → 10진수 변환 (비트 연산 방식 - 가장 효율적)
 * 0x12345678 → 12345678
 * ================================================ */
static uint32_t BCD_To_Decimal(uint32_t bcd_value)
{
    if (!BCD_IsValid(bcd_value)) {
        return 0;  // 에러 처리
    }
    
    uint32_t decimal = 0;
    uint32_t multiplier = 1;
    
    /* 하위 니블부터 처리 */
    for (int i = 0; i < 8; i++) {
        uint8_t digit = (bcd_value >> (i * 4)) & 0xF;
        decimal += digit * multiplier;
        multiplier *= 10;
    }
    
    return decimal;
}

/* ================================================
 * 10진수 → BCD 변환 (역변환)
 * 12345678 → 0x12345678
 * ================================================ */
#if 0 //optimize for del
static uint32_t Decimal_To_BCD(uint32_t decimal_value)
{
    uint32_t bcd = 0;
    
    for (int i = 0; i < 8; i++) {
        uint8_t digit = decimal_value % 10;
        bcd |= ((uint32_t)digit << (i * 4));
        decimal_value /= 10;
        
        if (decimal_value == 0) break;  // 더 이상 처리할 자릿수 없음
    }
    
    return bcd;
}
#endif

static void App_MeterGetTimestamp(uint8_t ts[6], uint8_t *p_timeValid)
{
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};

    if (p_timeValid == NULL)
    {
        APP_LOGE("METER", "Invalie param");
        return;
    }

    if (!IsUpdatedRTC())
    {
        APP_LOGE("METER", "Not yet ready rtc");
        *p_timeValid = APP_FALSE;
        return;
    }

    if ((HAL_RTC_GetTime(APP_RTC_HANDLE, &sTime, RTC_FORMAT_BIN) == HAL_OK) &&
        (HAL_RTC_GetDate(APP_RTC_HANDLE, &sDate, RTC_FORMAT_BIN) == HAL_OK))
    {
        ts[0] = sDate.Year;
        ts[1] = sDate.Month;
        ts[2] = sDate.Date;
        ts[3] = sTime.Hours;
        ts[4] = sTime.Minutes;
        ts[5] = sTime.Seconds;
        *p_timeValid = APP_TRUE;
    }
    else
    {
        ts[0] = 0xFFu;
        ts[1] = 0xFFu;
        ts[2] = 0xFFu;
        ts[3] = 0u;
        ts[4] = 0u;
        ts[5] = 0u;
        *p_timeValid = APP_FALSE;
    }
}

static uint8_t App_MeterDecodeDecimalPos(uint8_t vif)
{
    return (uint8_t)(vif & 0x0Fu);
}

static uint8_t App_MeterDecodeDigitalCaliber(uint8_t dif)
{
    return (uint8_t)((dif & 0xF0u)>>4);
}

static uint32_t App_MeterScaleToMilli(uint32_t value, uint8_t decimal)
{
    while (decimal > 0u)
    {
        value /= 10u;
        decimal--;
    }

    return value;
}

static uint8_t App_MeterNormalizeDigitalStatus(uint8_t rawStatus)
{
    uint8_t status = 0u;

    if ((rawStatus & (1u << 7)) != 0u)
    {
        status |= APP_METER_STORAGE_STATUS_OVERFLOW;
    }
    if ((rawStatus & (1u << 6)) != 0u)
    {
        status |= APP_METER_STORAGE_STATUS_REVERSE_FLOW;
    }
    if ((rawStatus & (1u << 5)) != 0u)
    {
        status |= APP_METER_STORAGE_STATUS_LEAK;
    }

    return status;
}

static uint8_t App_MeterNormalizeScStatus(uint8_t rawStatus, uint8_t battery)
{
    uint8_t status = 0u;

    if ((rawStatus & (1u << 1)) != 0u)
    {
        status |= APP_METER_STORAGE_STATUS_OVERFLOW;
    }
    if ((rawStatus & (1u << 4)) != 0u)
    {
        status |= APP_METER_STORAGE_STATUS_REVERSE_FLOW;
    }
    if ((rawStatus & (1u << 3)) != 0u)
    {
        status |= APP_METER_STORAGE_STATUS_LEAK;
    }
    if (battery != 0x02u)
    {
        status |= APP_METER_STORAGE_STATUS_LOW_BATTERY;
    }

    return status;
}

static AppStatus_t App_MeterBuildDigitalRecord(const App_MeterUnion_t *pRxFrame,
                                                 AppMeterStorageRecord_t *p_record)
{
    AppMeterStorageRecord_t record;
    uint32_t identRaw;
    uint32_t dataRaw;
    uint32_t identDecimal;
    uint32_t dataDecimal;
    uint8_t timeValid;
    uint8_t decimalPos;
    uint8_t caliber;

    APP_RETURN_IF_FALSE(pRxFrame != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);

    (void)memset(&record, 0, sizeof(record));
    App_MeterGetTimestamp(record.ts, &timeValid);

    identRaw = App_MeterGetIdentificationNumber((App_MeterUnion_t *)pRxFrame);
    dataRaw = App_MeterGetMeasurementData((App_MeterUnion_t *)pRxFrame);
    identDecimal = BCD_To_Decimal(identRaw);
    dataDecimal = BCD_To_Decimal(dataRaw);
    decimalPos = App_MeterDecodeDecimalPos(pRxFrame->frame.UserData.VIF);
    caliber = App_MeterDecodeDigitalCaliber(pRxFrame->frame.UserData.DIF);

    record.srcType = APP_METER_STORAGE_SRC_DIGITAL_UART;
    record.flags = APP_METER_STORAGE_FLAG_READING_VALID;
    if (timeValid == APP_TRUE)
    {
        record.flags |= APP_METER_STORAGE_FLAG_TIME_VALID;
    }
    record.meterId = identDecimal;
    record.readingScaled = App_MeterScaleToMilli(dataDecimal, 0); //no scale.
    record.caliberDecimal = (uint8_t)(((caliber & 0x0Fu) << 4) | (decimalPos & 0x0Fu));
    record.meterStatus = App_MeterNormalizeDigitalStatus(pRxFrame->frame.UserData.Status);
    record.meterBattery = (uint8_t)(pRxFrame->frame.UserData.Status & 0x1Fu);
    record.meterType = APP_METER_STORAGE_METER_TYPE_DIGITAL_UART;

    App_MeterStoragePrintRecord(&record);
    *p_record = record;

    /* [[PlaceholderRecord]] RTC 시각이 아직 유효하지 않아도 계량 데이터 자체는 정상
     * 수신되었으므로 레코드를 폐기하지 않고 그대로 저장/전송 대상으로 삼는다.
     * 이 경우 TIME_VALID 플래그만 세팅되지 않은 채로 남는다. */
    if (timeValid != APP_TRUE)
    {
        APP_LOGW("METER", "RTC time invalid. Store record without TIME_VALID flag.");
    }

    return APP_STATUS_OK;
}

void App_MeterSetStorageEnabled(uint8_t enabled)
{
    g_appMeterStorageEnabled = (enabled != APP_FALSE) ? APP_TRUE : APP_FALSE;
}

uint8_t App_MeterIsStorageEnabled(void)
{
    return g_appMeterStorageEnabled;
}

static AppStatus_t App_MeterBuildSc1xxxRecord(const App_MeterSC1xxxUnion_t *pRxFrame,
                                                AppMeterStorageRecord_t *p_record)
{
    AppMeterStorageRecord_t record;
    uint32_t identRaw;
    uint32_t dataRaw;
    uint8_t timeValid;

    APP_RETURN_IF_FALSE(pRxFrame != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);

    (void)memset(&record, 0, sizeof(record));
    App_MeterGetTimestamp(record.ts, &timeValid);

    identRaw = App_MeterSC1xxxGetIdentificationNumber((App_MeterSC1xxxUnion_t *)pRxFrame);
    dataRaw = App_MeterSC1xxxGetMeasurementData((App_MeterSC1xxxUnion_t *)pRxFrame);

    record.srcType = APP_METER_STORAGE_SRC_SC1XXX;
    record.flags = APP_METER_STORAGE_FLAG_READING_VALID;
    if (timeValid == APP_TRUE)
    {
        record.flags |= APP_METER_STORAGE_FLAG_TIME_VALID;
    }
    record.meterId = BCD_To_Decimal(identRaw);
    record.readingScaled = BCD_To_Decimal(dataRaw);
    record.meterStatus = App_MeterNormalizeScStatus(pRxFrame->frame.UserData.Status,
                                                    pRxFrame->frame.UserData.Battery);
    record.meterBattery = pRxFrame->frame.UserData.Battery;
    record.meterType = APP_METER_STORAGE_METER_TYPE_SC1XXX;
    record.caliberDecimal = (uint8_t)((APP_METER_STORAGE_CALIBER_UNKNOWN << 4) | 0x03u);
    *p_record = record;

    /* [[PlaceholderRecord]] RTC 시각이 아직 유효하지 않아도 계량 데이터 자체는 정상
     * 수신되었으므로 레코드를 폐기하지 않고 그대로 저장/전송 대상으로 삼는다. */
    if (timeValid != APP_TRUE)
    {
        APP_LOGW("METER", "RTC time invalid. Store record without TIME_VALID flag.");
    }

    return APP_STATUS_OK;
}

#if 0 //optimize for del
static AppStatus_t App_MeterSaveSc1xxxRecord(const App_MeterSC1xxxUnion_t *pRxFrame)
{
    AppMeterStorageRecord_t record;
    AppStatus_t status;

    status = App_MeterBuildSc1xxxRecord(pRxFrame, &record);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    return(App_MeterStoragePush(&record));
}
#endif

/* ================================================
 * [[PlaceholderRecord]] 검침 실패(무응답/파싱오류) 시 치환 레코드 생성
 * - meterId/readingScaled/meterBattery 는 규격상 정의된 sentinel 값으로 채운다.
 * - flags 에 READ_FAILED 를 세팅하여 서버/관제에서 실패 이력을 구분 가능하게 한다.
 * - RTC 가 유효하면 timestamp 는 실제 시각을 채우고 TIME_VALID 플래그도 세팅한다.
 * ================================================ */
AppStatus_t App_MeterBuildPlaceholderRecord(AppMeterStorageRecord_t *p_record,
                                             uint8_t srcType,
                                             uint8_t meterType)
{
    AppMeterStorageRecord_t record;
    uint8_t timeValid;

    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);

    (void)memset(&record, 0, sizeof(record));
    App_MeterGetTimestamp(record.ts, &timeValid);

    record.srcType = srcType;
    record.flags = APP_METER_STORAGE_FLAG_READ_FAILED;
    if (timeValid == APP_TRUE)
    {
        record.flags |= APP_METER_STORAGE_FLAG_TIME_VALID;
    }
    record.meterId = APP_METER_STORAGE_METERID_INVALID;
    record.readingScaled = APP_METER_STORAGE_READING_INVALID;
    record.meterStatus = 0u;
    record.meterBattery = APP_METER_STORAGE_BATTERY_INVALID;
    record.meterType = meterType;
    record.caliberDecimal = (uint8_t)((APP_METER_STORAGE_CALIBER_UNKNOWN << 4) | 0x0Fu);

    APP_LOGW("METER", "Build placeholder record (read failed) srcType=%u meterType=%u timeValid=%u",
             (unsigned int)srcType, (unsigned int)meterType, (unsigned int)timeValid);
    App_MeterStoragePrintRecord(&record);

    *p_record = record;
    return APP_STATUS_OK;
}

/* ================================================
 * [[PlaceholderRecord]] 검침 실패 시 치환 레코드를 저장소에 push 하고
 * NFC 갱신까지 정상 검침 성공 경로와 동일하게 수행한다.
 * ================================================ */
AppStatus_t App_MeterStorePlaceholderRecord(uint8_t srcType, uint8_t meterType)
{
    AppMeterStorageRecord_t record = {0};
    AppStatus_t status;
    uint8_t storageEnabled;

    status = App_MeterBuildPlaceholderRecord(&record, srcType, meterType);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    storageEnabled = App_MeterIsStorageEnabled();
    if (storageEnabled == APP_TRUE)
    {
        APP_RETURN_IF_FALSE(App_MeterStoragePush(&record) == APP_STATUS_OK, APP_STATUS_FATAL);
    }

    APP_LOGI("NFC", "Update nfc meter info (placeholder/read-failed).");
    status = (storageEnabled == APP_TRUE)
             ? App_NfcSeoulNotifyStorageChanged()
             : App_NfcSeoulNotifyLiveMeterRecord(&record);
    (void)status;

    return APP_STATUS_OK;
}

/////////////////////////////////////////////////////////////////////////////

uint8_t App_MeterCalculateCheckSum(const App_MeterUnion_t *frame)
{
    uint8_t checksum = 0;
    
    /* M-Bus 표준: C Field부터 User Data 끝까지 합산 */
    checksum += frame->frame.C_Field;
    checksum += frame->frame.A_Field; 
    checksum += frame->frame.CI_Field;
    
    /* User Data 전체 바이트 합산 */
    const uint8_t *user_data_ptr = (const uint8_t*)&frame->frame.UserData;
    for (int i = 0; i < sizeof(App_MeterUserData_t); i++) {
        checksum += user_data_ptr[i];
    }
    
    return checksum;  // uint8_t 자동 오버플로우로 0xFF 마스킹
}

/* 편의 함수: Identification Number 접근 */
uint32_t App_MeterGetIdentificationNumber(const App_MeterUnion_t *meterPacket)
{
    return App_MeterBytesToUint32LE(meterPacket->frame.UserData.IdentNr);
}

/* 편의 함수: Measurement Data 접근 */
uint32_t App_MeterGetMeasurementData(const App_MeterUnion_t *meterPacket)
{
    return App_MeterBytesToUint32LE(meterPacket->frame.UserData.Data);
}

App_MeterResult_t App_MeterParseFrame(App_MeterUnion_t *meterPacket, const uint8_t *binary_data, uint16_t length)
{
    /* 크기 검증 */
    if (length < sizeof(App_MeterFrame_t)) {
        return APP_METER_ERR_SIZE;
    }
    
    /* 핵심: Binary → Union 직접 복사 (Cascading) */
    memcpy(meterPacket->raw, binary_data, sizeof(App_MeterFrame_t));
    
    /* 프레임 헤더 검증 */
    if (meterPacket->frame.Start1 != 0x68 || meterPacket->frame.Start2 != 0x68) {
        return APP_METER_ERR_START;
    }
    
    /* L Field 일치 검증 */
    if (meterPacket->frame.L_Field1 != meterPacket->frame.L_Field2) {
        return APP_METER_ERR_LENGTH;
    }
    
    /* Stop Byte 검증 */
    if (meterPacket->frame.Stop != 0x16) {
        return APP_METER_ERR_STOP;
    }
    
    /* 체크섬 검증 */
    uint8_t calculated_checksum = App_MeterCalculateCheckSum(meterPacket);
    if (calculated_checksum != meterPacket->frame.CheckSum) {
        return APP_METER_ERR_CHECKSUM;
    }
    
    return APP_METER_OK;
}

AppStatus_t App_MeterProcessReceivedData(const uint8_t *pRxBuf, const uint8_t length)
{
    #if 0
    /* UART로 수신된 바이너리 데이터 시뮬레이션 */
    uint8_t uart_rx_buffer[] = {
        0x68, 0x0F, 0x0F, 0x68,             // Header
        0x08, 0x01, 0x78,                   // Control Fields
        0x0F,                               // MDH
        0x56, 0x34, 0x12, 0x09,            // Ident.Nr (Little Endian)
        0x00,                               // Status
        0x1C, 0x13,                         // DIF, VIF
        0x78, 0x56, 0x34, 0x12,            // Data (Little Endian)
        0x78, 0x16                          // CheckSum, Stop
    };
    #endif
    
    App_MeterUnion_t rx_frame = {0};
    AppMeterStorageRecord_t liveRecord = {0};
    AppStatus_t status;
    uint8_t storageEnabled;
    
    /* 핵심: Binary → Union Cascading */
    //App_MeterResult_t result = App_MeterParseFrame(&rx_frame, uart_rx_buffer, sizeof(uart_rx_buffer));
    App_MeterResult_t result = App_MeterParseFrame(&rx_frame, pRxBuf, length);

    #if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
    g_epcMeterAttemptSeq++;
    #endif

    if (result == APP_METER_OK)
    {
        APP_LOGD("METER", "Success Meter parsing.");

        App_MeterPrintUnionDetailed(&rx_frame);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
        /* Test1 파싱용 단일 라인: seq,res,id,val */
        EPC_LOGI("test=%s,seq=%lu,res=OK,id=%08lu,val=%08lu",
                 APP_EPC_TEST_ID_STRING,
                 (unsigned long)g_epcMeterAttemptSeq,
                 (unsigned long)BCD_To_Decimal(App_MeterGetIdentificationNumber(&rx_frame)),
                 (unsigned long)BCD_To_Decimal(App_MeterGetMeasurementData(&rx_frame)));
#endif

        APP_RETURN_IF_FALSE(App_MeterBuildDigitalRecord(&rx_frame, &liveRecord) == APP_STATUS_OK, APP_STATUS_FATAL);

        storageEnabled = App_MeterIsStorageEnabled();
        if (storageEnabled == APP_TRUE)
        {
            APP_RETURN_IF_FALSE(App_MeterStoragePush(&liveRecord) == APP_STATUS_OK, APP_STATUS_FATAL);
        }

        //nfc update
        APP_LOGI("NFC", "Update nfc meter info.");
        status = (storageEnabled == APP_TRUE)
                 ? App_NfcSeoulNotifyStorageChanged()
                 : App_NfcSeoulNotifyLiveMeterRecord(&liveRecord);
        (void)status;

        return(APP_STATUS_OK);
    }
    else
    {
        APP_LOGE("METER", "Fail Meter parsing(%d)", result);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
        /* 실패 시에도 seq를 남겨 시뮬레이터 로그와 매칭 시 "몇 번째 시도가 빠졌는지" 추적 가능 */
        EPC_LOGI("test=%s,seq=%lu,res=ERR%d,id=--------,val=--------",
                 APP_EPC_TEST_ID_STRING,
                 (unsigned long)g_epcMeterAttemptSeq,
                 (int)result);
#endif

        /* [[PlaceholderRecord]] 파싱 실패 시에도 데이터를 폐기하지 않고
         * 치환(placeholder) 레코드로 저장/전송 대상에 반드시 포함시킨다. */
        APP_LOGW("METER", "Store placeholder record due to parse failure.");
        return App_MeterStorePlaceholderRecord(APP_METER_STORAGE_SRC_DIGITAL_UART,
                                                APP_METER_STORAGE_METER_TYPE_DIGITAL_UART);
    }
}

AppStatus_t App_MeterBuildLiveRecordFromReceivedData(const uint8_t *pRxBuf,
                                                     const uint8_t length,
                                                     AppMeterStorageRecord_t *p_record)
{
    App_MeterUnion_t rx_frame = {0};
    AppStatus_t status;
    App_MeterResult_t result;

    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);

    result = App_MeterParseFrame(&rx_frame, pRxBuf, length);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
    /* [[BootLiveTx]] 등 저장 없이 즉시 전송하는 경로도 App_MeterProcessReceivedData()와
     * 동일한 전역 카운터(g_epcMeterAttemptSeq)를 공유하여 증가시킨다.
     * 이렇게 해야 시뮬레이터가 세는 "물리적 UART 요청 순번"과 펌웨어가 EPC 로그에 찍는
     * seq 번호가 1:1로 어긋나지 않는다(기존에는 이 경로가 카운터를 건드리지 않아,
     * BootLiveTx 1회만큼 두 seq 체계 사이에 오프셋이 발생했다). */
    g_epcMeterAttemptSeq++;
#endif

    if (result != APP_METER_OK)
    {
        APP_LOGE("METER", "Fail Meter parsing(%d)", result);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
        /* 실패 시에도 seq를 남겨 시뮬레이터 로그와 매칭 시 "몇 번째 시도가 빠졌는지" 추적 가능 */
        EPC_LOGI("test=%s,seq=%lu,res=ERR%d,id=--------,val=--------",
                 APP_EPC_TEST_ID_STRING,
                 (unsigned long)g_epcMeterAttemptSeq,
                 (int)result);
#endif

        /* [[PlaceholderRecord]] 최초 부팅 등 저장 없이 즉시 전송하는 경로에서도
         * 파싱 실패 시 치환 레코드를 만들어 반드시 전송 대상이 되게 한다. */
        APP_LOGW("METER", "Build placeholder live record (no storage) due to parse failure.");
        return App_MeterBuildPlaceholderRecord(p_record,
                                               APP_METER_STORAGE_SRC_DIGITAL_UART,
                                               APP_METER_STORAGE_METER_TYPE_DIGITAL_UART);
    }

    APP_LOGD("METER", "Success Meter parsing (live/no-store).");
    App_MeterPrintUnionDetailed(&rx_frame);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
    /* Test1 파싱용 단일 라인: seq,res,id,val (App_MeterProcessReceivedData()와 동일 포맷) */
    EPC_LOGI("test=%s,seq=%lu,res=OK,id=%08lu,val=%08lu",
             APP_EPC_TEST_ID_STRING,
             (unsigned long)g_epcMeterAttemptSeq,
             (unsigned long)BCD_To_Decimal(App_MeterGetIdentificationNumber(&rx_frame)),
             (unsigned long)BCD_To_Decimal(App_MeterGetMeasurementData(&rx_frame)));
#endif

    APP_RETURN_IF_FALSE(App_MeterBuildDigitalRecord(&rx_frame, p_record) == APP_STATUS_OK, APP_STATUS_FATAL);

    APP_LOGI("NFC", "Update nfc live meter info (no storage).");
    status = App_NfcSeoulNotifyLiveMeterRecord(p_record);
    (void)status;
    return APP_STATUS_OK;
}

//SC1xxx
/////////////////////////////////////////////////////////////////////////////

uint8_t App_MeterSC1xxxCalculateCheckSum(const App_MeterSC1xxxUnion_t *frame)
{
    uint8_t checksum = 0;
    
    checksum = frame->frame.STX;
    checksum ^= frame->frame.LENGTH; 
    checksum ^= frame->frame.ETX;
    
    /* User Data 전체 바이트 합산 */
    const uint8_t *user_data_ptr = (const uint8_t*)&frame->frame.UserData;
    for (int i = 0; i < sizeof(App_MeterSC1xxxUserData_t); i++) {
        checksum ^= user_data_ptr[i];
    }
    
    return checksum;  // uint8_t 자동 오버플로우로 0xFF 마스킹
}

/* 편의 함수: Identification Number 접근 */
uint32_t App_MeterSC1xxxGetIdentificationNumber(const App_MeterSC1xxxUnion_t *meterPacket)
{
    return App_MeterBytesToUint32LE(meterPacket->frame.UserData.IdentNr);
}

/* 편의 함수: Measurement Data 접근 */
uint32_t App_MeterSC1xxxGetMeasurementData(const App_MeterSC1xxxUnion_t *meterPacket)
{
    return App_MeterBytesToUint32LE(meterPacket->frame.UserData.Data);
}

App_MeterResult_t App_MeterSC1xxxParseFrame(App_MeterSC1xxxUnion_t *meterPacket, const uint8_t *binary_data, uint16_t length)
{
    /* 크기 검증 */
    if (length < sizeof(App_MeterSC1xxxFrame_t)) {
        return APP_METER_ERR_SIZE;
    }
    
    /* 핵심: Binary → Union 직접 복사 (Cascading) */
    memcpy(meterPacket->raw, binary_data, sizeof(App_MeterSC1xxxFrame_t));
    
    /* 프레임 헤더 검증 */
    if (meterPacket->frame.ACK != 0x06 || meterPacket->frame.STX != 0x02) {
        return APP_METER_ERR_START;
    }
    
    /* LENGTH 일치 검증 */
    if (meterPacket->frame.LENGTH != 0x0B) {
        return APP_METER_ERR_LENGTH;
    }
    
    /* Stop Byte 검증 */
    if (meterPacket->frame.ETX != 0x03) {
        return APP_METER_ERR_STOP;
    }
    
    /* 체크섬 검증 */
    uint8_t calculated_checksum = App_MeterSC1xxxCalculateCheckSum(meterPacket);
    APP_LOGD("METER", "Checksum: 0x%02X-0x%02X", calculated_checksum, meterPacket->frame.CheckSum);
    if (calculated_checksum != meterPacket->frame.CheckSum) {
        return APP_METER_ERR_CHECKSUM;
    }
    
    return APP_METER_OK;
}

void App_MeterSC1xxxPrintData(App_MeterSC1xxxUnion_t *pRxFrame)
{
    APP_LOGD("METER", "STX: 0x%02X", pRxFrame->frame.STX);
    APP_LOGD("METER", "LENGTH: 0x%02X", pRxFrame->frame.LENGTH);

    /* 엔디안 안전 접근 */
    uint32_t ident = App_MeterSC1xxxGetIdentificationNumber(pRxFrame);
    uint32_t data = App_MeterSC1xxxGetMeasurementData(pRxFrame);
    uint8_t battery = pRxFrame->frame.UserData.Battery;
    uint8_t status = pRxFrame->frame.UserData.Status;

    APP_LOGI("METER", "Identification: 0x%08lX(%08d)", ident, BCD_To_Decimal(ident));
    APP_LOGI("METER", "Measurement   : 0x%08lX(%08d)", data, BCD_To_Decimal(data));
    APP_LOGI("METER", "Battery       : 0x%02X", battery);
    APP_LOGI("METER", "Status        : 0x%02X", status);

    /* Raw 바이트 확인 */
    App_LogHexDump(APP_LOG_LEVEL_DEBUG, "METER", (const uint8_t *)pRxFrame->raw, sizeof(App_MeterSC1xxxFrame_t));
}

AppStatus_t App_MeterSC1xxxProcessReceivedData(const uint8_t *pRxBuf, const uint8_t length)
{
#if 0
    /* UART로 수신된 바이너리 데이터 시뮬레이션 */
    uint8_t uart_rx_buffer[] = {
        0x06, 0x02, 0x0B, 0x56,
        0x34, 0x12, 0x26, 0x01,
        0x00, 0x00, 0x00, 0x02,
        0x00, 0x03, 0x5F};
#endif

    App_MeterSC1xxxUnion_t rx_frame = {0};
    AppMeterStorageRecord_t liveRecord = {0};
    AppStatus_t status;
    uint8_t storageEnabled;

    /* 핵심: Binary → Union Cascading */
    App_MeterResult_t result = App_MeterSC1xxxParseFrame(&rx_frame, pRxBuf, length);
    
    if (result == APP_METER_OK) {
        APP_LOGI("METER", "Success MeterSC1xxx parsing.");
        
        App_MeterSC1xxxPrintData(&rx_frame);
        APP_RETURN_IF_FALSE(App_MeterBuildSc1xxxRecord(&rx_frame, &liveRecord) == APP_STATUS_OK, APP_STATUS_FATAL);
        storageEnabled = App_MeterIsStorageEnabled();
        if (storageEnabled == APP_TRUE)
        {
            APP_RETURN_IF_FALSE(App_MeterStoragePush(&liveRecord) == APP_STATUS_OK, APP_STATUS_FATAL);
        }
        APP_LOGI("NFC", "Update nfc meter info.");
        status = (storageEnabled == APP_TRUE)
                 ? App_NfcSeoulNotifyStorageChanged()
                 : App_NfcSeoulNotifyLiveMeterRecord(&liveRecord);
        (void)status;
        return(APP_STATUS_OK);
        
    } else {
        APP_LOGE("METER", "Fail MeterSC1xxx parsing(%d) -> substitute placeholder record", result);

        /* [[PlaceholderRecord]] 파싱 실패 시에도 데이터를 폐기하지 않고
         * 치환(placeholder) 레코드로 저장/전송 대상에 반드시 포함시킨다. */
        APP_LOGW("METER", "Store placeholder record due to parse failure.");
        return App_MeterStorePlaceholderRecord(APP_METER_STORAGE_SRC_SC1XXX,
                                                APP_METER_STORAGE_METER_TYPE_SC1XXX);
    }
}

AppStatus_t App_MeterSC1xxxBuildLiveRecordFromReceivedData(const uint8_t *pRxBuf,
                                                           const uint8_t length,
                                                           AppMeterStorageRecord_t *p_record)
{
    App_MeterSC1xxxUnion_t rx_frame = {0};
    AppStatus_t status;
    App_MeterResult_t result;

    APP_RETURN_IF_FALSE(p_record != NULL, APP_STATUS_INVALID_PARAM);

    result = App_MeterSC1xxxParseFrame(&rx_frame, pRxBuf, length);
    if (result != APP_METER_OK)
    {
        APP_LOGE("METER", "Fail MeterSC1xxx parsing(%d)", result);

        /* [[PlaceholderRecord]] 최초 부팅 등 저장 없이 즉시 전송하는 경로에서도
         * 파싱 실패 시 치환 레코드를 만들어 반드시 전송 대상이 되게 한다. */
        APP_LOGW("METER", "Build placeholder live record (no storage) due to parse failure.");
        return App_MeterBuildPlaceholderRecord(p_record,
                                                APP_METER_STORAGE_SRC_SC1XXX,
                                                APP_METER_STORAGE_METER_TYPE_SC1XXX);
    }

    APP_LOGI("METER", "Success MeterSC1xxx parsing (live/no-store).");
    App_MeterSC1xxxPrintData(&rx_frame);
    APP_RETURN_IF_FALSE(App_MeterBuildSc1xxxRecord(&rx_frame, p_record) == APP_STATUS_OK, APP_STATUS_FATAL);

    APP_LOGI("NFC", "Update nfc live meter info (no storage).");
    status = App_NfcSeoulNotifyLiveMeterRecord(p_record);
    (void)status;
    return APP_STATUS_OK;
}

/////////////////////////////////////////////////////////////////////////////

void App_MeterPrintUnionDetailed(const App_MeterUnion_t *pRxFrame)
{
    uint32_t ident;
    uint32_t data;

    if (pRxFrame == NULL)
    {
        APP_LOGE("METER", "PrintUnionDetailed: null frame");
        return;
    }

    ident = App_MeterGetIdentificationNumber((App_MeterUnion_t *)pRxFrame);
    data  = App_MeterGetMeasurementData((App_MeterUnion_t *)pRxFrame);

    APP_LOGI("METER", "----- Meter Union Dump -----");
    APP_LOGI("METER", "Start1      : 0x%02X", pRxFrame->frame.Start1);
    APP_LOGI("METER", "L_Field1    : 0x%02X", pRxFrame->frame.L_Field1);
    APP_LOGI("METER", "L_Field2    : 0x%02X", pRxFrame->frame.L_Field2);
    APP_LOGI("METER", "Start2      : 0x%02X", pRxFrame->frame.Start2);
    APP_LOGI("METER", "C_Field     : 0x%02X", pRxFrame->frame.C_Field);
    APP_LOGI("METER", "A_Field     : 0x%02X", pRxFrame->frame.A_Field);
    APP_LOGI("METER", "CI_Field    : 0x%02X", pRxFrame->frame.CI_Field);

    APP_LOGI("METER", "MDH         : 0x%02X", pRxFrame->frame.UserData.MDH);
    APP_LOGI("METER", "IdentNr raw : %02X %02X %02X %02X",
             pRxFrame->frame.UserData.IdentNr[0],
             pRxFrame->frame.UserData.IdentNr[1],
             pRxFrame->frame.UserData.IdentNr[2],
             pRxFrame->frame.UserData.IdentNr[3]);
    APP_LOGI("METER", "Status      : 0x%02X", pRxFrame->frame.UserData.Status);
    APP_LOGI("METER", "DIF         : 0x%02X", pRxFrame->frame.UserData.DIF);
    APP_LOGI("METER", "VIF         : 0x%02X", pRxFrame->frame.UserData.VIF);
    APP_LOGI("METER", "Data raw    : %02X %02X %02X %02X",
             pRxFrame->frame.UserData.Data[0],
             pRxFrame->frame.UserData.Data[1],
             pRxFrame->frame.UserData.Data[2],
             pRxFrame->frame.UserData.Data[3]);

    APP_LOGI("METER", "CheckSum    : 0x%02X", pRxFrame->frame.CheckSum);
    APP_LOGI("METER", "Stop        : 0x%02X", pRxFrame->frame.Stop);
    APP_LOGI("METER", "----------------------------");

    APP_LOGI("METER", "IdentNr     : 0x%08lX (%08d)",
             (unsigned long)ident,
             BCD_To_Decimal(ident));
    APP_LOGI("METER", "Measurement : 0x%08lX (%08d)",
             (unsigned long)data,
             BCD_To_Decimal(data));

    #if 0  //raw byte dump
    App_LogHexDump(APP_LOG_LEVEL_INFO,
                   "METER",
                   (const uint8_t *)pRxFrame->raw,
                   sizeof(App_MeterFrame_t));
    #endif
}


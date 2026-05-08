#include "app_meter.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "app_build_config.h"
#include "app_log.h"

/* 4바이트 배열 → uint32_t (Little Endian) */
static uint32_t App_MeterBytesToUint32LE(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]       |
           ((uint32_t)bytes[1] << 8)  |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

/* uint32_t → 4바이트 배열 (Little Endian) */
static void App_MeterUint32ToBytes(uint32_t value, uint8_t *bytes)
{
    bytes[0] = (uint8_t)(value & 0xFF);
    bytes[1] = (uint8_t)((value >> 8) & 0xFF);
    bytes[2] = (uint8_t)((value >> 16) & 0xFF);
    bytes[3] = (uint8_t)((value >> 24) & 0xFF);
}

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

void App_MeterPrintData(App_MeterUnion_t *pRxFrame)
{
    APP_LOGI("METER", "Control Field: 0x%02X", pRxFrame->frame.C_Field);
    APP_LOGI("METER", "Address Field: 0x%02X", pRxFrame->frame.A_Field);

    /* 엔디안 안전 접근 */
    uint32_t ident = App_MeterGetIdentificationNumber(pRxFrame);
    uint32_t data = App_MeterGetMeasurementData(pRxFrame);

    APP_LOGI("METER", "Identification: 0x%08lX(%08d)", ident, BCD_To_Decimal(ident));
    APP_LOGI("METER", "Measurement   : 0x%08lX(%08d)", data, BCD_To_Decimal(data));

    /* Raw 바이트 확인 */
    //App_LogHexDump(APP_LOG_LEVEL_DEBUG, "METER", (const uint8_t *)pRxFrame->raw, sizeof(App_MeterFrame_t));
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
    
    /* 핵심: Binary → Union Cascading */
    //App_MeterResult_t result = App_MeterParseFrame(&rx_frame, uart_rx_buffer, sizeof(uart_rx_buffer));
    App_MeterResult_t result = App_MeterParseFrame(&rx_frame, pRxBuf, length);
    
    if (result == APP_METER_OK) {
        APP_LOGI("METER", "Success Meter parsing.");
        
        App_MeterPrintData(&rx_frame);
        return(APP_STATUS_OK);
        
    } else {
        APP_LOGE("METER", "Fail Meter parsing(%d)", result);
        return(APP_STATUS_FATAL);
    }
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

    /* 핵심: Binary → Union Cascading */
    //App_MeterResult_t result = App_MeterSC1xxxParseFrame(&rx_frame, uart_rx_buffer, sizeof(uart_rx_buffer));
    App_MeterResult_t result = App_MeterSC1xxxParseFrame(&rx_frame, pRxBuf, length);
    
    if (result == APP_METER_OK) {
        APP_LOGI("METER", "Success MeterSC1xxx parsing.");
        
        App_MeterSC1xxxPrintData(&rx_frame);
        return(APP_STATUS_OK);
        
    } else {
        APP_LOGE("METER", "Fail MeterSC1xxx parsing(%d)", result);
        return(APP_STATUS_FATAL);
    }
}

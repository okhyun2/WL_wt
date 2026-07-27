#ifndef APP_METER_SERVER_FORMAT_H
#define APP_METER_SERVER_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_error.h"

#define APP_METER_SERVER_FORMAT_HEADER_LORA              (0xA1u)
#define APP_METER_SERVER_FORMAT_HEADER_NBIOT             (0xA3u)
#define APP_METER_SERVER_FORMAT_COMMAND_PERIOD_REPORT    (0x70u)
#define APP_METER_SERVER_FORMAT_MAX_PACKET_SIZE          (128u)
#define APP_METER_SERVER_FORMAT_MAX_RECORDS_NBIOT        (24u)
#define APP_METER_SERVER_FORMAT_MAX_RECORDS_LORA         (12u)

typedef union
{
    uint8_t value;
    struct {
        uint8_t volt:6;
        uint8_t reserved:1;
        uint8_t alarm:1;
    }b;
}_unBattery;

//If APP_STORAGE_OPTION_SLOT_COUNT:4 -> Max 1KByte/4 = 256Byte
typedef struct
{
    uint8_t linkHeader;
    uint8_t command;
    uint8_t wirelessQuality[10];/* 무선품질 정보 (LoRa 2B / NB-IoT 10B) */
    uint8_t mobileIdBcd[16];    /* 이동통신 ID (NB-IoT만, 16B = IMEI 8(BCD) + IMSI 8(BCD)) */
    uint8_t deviceSerialBcd[5]; /* 단말기 정보 (8B): 일련번호(5(BCD)) */
    uint8_t firmwareVersion[2]; /*                  + F/W(2(float)) Major(1) + Minor(1)*/
    _unBattery terminalBattery; /*                  + 배터리(1(bit)) */
    uint8_t meteringPeriodHours;
    uint8_t reportingPeriodHours;
} AppMeterServerFormatOptions_t;

typedef struct
{
    uint16_t packetLength;
    uint8_t payloadLength;
    uint8_t recordCount;
    uint8_t checksum;
    uint8_t cleared;
} AppMeterServerFormatResult_t;

void App_MeterServerOptionsSetDefaults(AppMeterServerFormatOptions_t *p_options);
uint8_t App_MeterServerOptionsIsPeriodSupported(uint8_t hours);
uint8_t App_MeterServerOptionsNormalizePeriod(uint8_t hours);
AppStatus_t App_MeterServerOptionsValidate(AppMeterServerFormatOptions_t *p_options);
AppStatus_t App_MeterServerFormatBuildFromStorage(const AppMeterServerFormatOptions_t *p_options,
                                                  uint8_t *p_packet,
                                                  uint16_t packetCapacity,
                                                  AppMeterServerFormatResult_t *p_result);
AppStatus_t App_MeterServerFormatBuildFromStorageAndClear(const AppMeterServerFormatOptions_t *p_options,
                                                          uint8_t *p_packet,
                                                          uint16_t packetCapacity,
                                                          AppMeterServerFormatResult_t *p_result);

/* ================================================================
 *  Options 영구 저장 (Bank1)
 * ================================================================ */
void        App_MeterServerOptionsInfo (void);
AppStatus_t App_MeterServerOptionsInit (void);
AppStatus_t App_MeterServerOptionsLoad (AppMeterServerFormatOptions_t *p_options);
AppStatus_t App_MeterServerOptionsSave (const AppMeterServerFormatOptions_t *p_options);
AppStatus_t App_MeterServerOptionsClear(void);
void App_MeterServerOptionsDump(const AppMeterServerFormatOptions_t *p_options);

/* ================================================================
 *  Options 필드 빌더 (RAM 상의 options 구조체를 항목별로 채움)
 * ================================================================ */
void App_MeterServerOptionsSetLink     (AppMeterServerFormatOptions_t *p_options,
                                        uint8_t linkHeader,
                                        uint8_t command);

void App_MeterServerOptionsSetWirelessQuality(AppMeterServerFormatOptions_t *p_options,
                                              const uint8_t *p_quality,
                                              uint8_t length);

void App_MeterServerOptionsSetMobileId (AppMeterServerFormatOptions_t *p_options,
                                        const uint8_t *p_imeiBcd8,
                                        const uint8_t *p_imsiBcd8);

void App_MeterServerOptionsSetDeviceInfo(AppMeterServerFormatOptions_t *p_options,
                                         const uint8_t *p_serialBcd5,
                                         uint8_t fwMajor,
                                         uint8_t fwMinor);

void App_MeterServerOptionsSetBattery  (AppMeterServerFormatOptions_t *p_options,
                                        uint8_t voltX10,
                                        uint8_t alarm);

void App_MeterServerOptionsSetPeriod   (AppMeterServerFormatOptions_t *p_options,
                                        uint8_t meteringHours,
                                        uint8_t reportingHours);

/* 채워진 options를 EEPROM에 저장 (변경된 경우에만 실제 write) */
AppStatus_t App_MeterServerOptionsUpdate(const AppMeterServerFormatOptions_t *p_options);

void App_ReservedEepromInfo(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_METER_SERVER_FORMAT_H */

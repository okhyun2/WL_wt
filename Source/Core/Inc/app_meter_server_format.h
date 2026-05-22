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

void App_MeterServerFormatSetTestDefaults(AppMeterServerFormatOptions_t *p_options);
AppStatus_t App_MeterServerFormatBuildFromStorage(const AppMeterServerFormatOptions_t *p_options,
                                                  uint8_t *p_packet,
                                                  uint16_t packetCapacity,
                                                  AppMeterServerFormatResult_t *p_result);
AppStatus_t App_MeterServerFormatBuildFromStorageAndClear(const AppMeterServerFormatOptions_t *p_options,
                                                          uint8_t *p_packet,
                                                          uint16_t packetCapacity,
                                                          AppMeterServerFormatResult_t *p_result);

#ifdef __cplusplus
}
#endif

#endif /* APP_METER_SERVER_FORMAT_H */

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

typedef struct
{
    uint8_t linkHeader;
    uint8_t command;
    uint8_t wirelessQuality[10];
    uint8_t mobileIdBcd[16];
    uint8_t deviceSerialBcd[5];
    uint8_t firmwareVersion[2];
    uint8_t terminalBattery;
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

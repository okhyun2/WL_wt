#ifndef APP_METER_H
#define APP_METER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define APP_METER_BAUDRATE_NORMAL (1200)
#define APP_METER_BAUDRATE_SC1xxx (600)
#define APP_METER_BAUDRATE APP_METER_BAUDRATE_NORMAL

typedef enum {
    APP_METER_OK = 0,
    APP_METER_ERR_SIZE = -1,
    APP_METER_ERR_START = -2,
    APP_METER_ERR_LENGTH = -3,
    APP_METER_ERR_STOP = -4,
    APP_METER_ERR_CHECKSUM = -5
} App_MeterResult_t;

#define PACKED __attribute__((packed))

///////////////////////////////////////////////////////////////////////////////////////
/* User Data 구조체 (12 bytes) */
typedef struct PACKED {
    uint8_t  MDH;
    uint8_t  IdentNr[4];
    uint8_t  Status;
    uint8_t  DIF;
    uint8_t  VIF;
    uint8_t  Data[4];
} App_MeterUserData_t;

/* M-Bus 전체 프레임 구조체 (21 bytes) */
typedef struct PACKED {
    uint8_t         Start1;
    uint8_t         L_Field1;
    uint8_t         L_Field2;
    uint8_t         Start2;
    uint8_t         C_Field;
    uint8_t         A_Field;
    uint8_t         CI_Field;
    App_MeterUserData_t UserData;
    uint8_t         CheckSum;
    uint8_t         Stop;
} App_MeterFrame_t;

typedef union {
    App_MeterFrame_t frame;
    uint8_t      raw[sizeof(App_MeterFrame_t)];
} App_MeterUnion_t;

uint8_t App_MeterCalculateCheckSum(const App_MeterUnion_t *frame);
uint32_t App_MeterGetIdentificationNumber(const App_MeterUnion_t *meterPacket);
uint32_t App_MeterGetMeasurementData(const App_MeterUnion_t *meterPacket);
App_MeterResult_t App_MeterParseFrame(App_MeterUnion_t *meterPacket, const uint8_t *binary_data, uint16_t length);
void App_MeterPrintData(App_MeterUnion_t *pRxFrame);
void App_MeterSetStorageEnabled(uint8_t enabled);
uint8_t App_MeterIsStorageEnabled(void);
AppStatus_t App_MeterProcessReceivedData(const uint8_t *pRxBuf, const uint8_t length);

///////////////////////////////////////////////////////////////////////////////////////
/* SC1xxx */
/* User Data 구조체 (10 bytes) */
typedef struct PACKED {
    uint8_t  IdentNr[4];
    uint8_t  Data[4];
    uint8_t  Battery;
    uint8_t  Status;
} App_MeterSC1xxxUserData_t;

/* M-Bus 전체 프레임 구조체 (15 bytes) */
typedef struct PACKED {
    uint8_t         ACK;
    uint8_t         STX;
    uint8_t         LENGTH;
    App_MeterSC1xxxUserData_t UserData;
    uint8_t         ETX;
    uint8_t         CheckSum;
} App_MeterSC1xxxFrame_t;

typedef union {
    App_MeterSC1xxxFrame_t frame;
    uint8_t      raw[sizeof(App_MeterSC1xxxFrame_t)];
} App_MeterSC1xxxUnion_t;

uint8_t App_MeterSC1xxxCalculateCheckSum(const App_MeterSC1xxxUnion_t *frame);
uint32_t App_MeterSC1xxxGetIdentificationNumber(const App_MeterSC1xxxUnion_t *meterPacket);
uint32_t App_MeterSC1xxxGetMeasurementData(const App_MeterSC1xxxUnion_t *meterPacket);
App_MeterResult_t App_MeterSC1xxxParseFrame(App_MeterSC1xxxUnion_t *meterPacket, const uint8_t *binary_data, uint16_t length);
void App_MeterSC1xxxPrintData(App_MeterSC1xxxUnion_t *pRxFrame);
AppStatus_t App_MeterSC1xxxProcessReceivedData(const uint8_t *pRxBuf, const uint8_t length);

///////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* APP_METER_H */

#ifndef APP_METER_STORAGE_H
#define APP_METER_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "app_error.h"

#define APP_METER_STORAGE_MAGIC                 (0x4D535447u)
#define APP_METER_STORAGE_VERSION               (0x01u)
#define APP_METER_STORAGE_MAX_RECORDS           (72u)
#define APP_METER_STORAGE_META_SLOT_COUNT       (80u)

#define APP_METER_STORAGE_FLAG_VALID            (1u << 0)
#define APP_METER_STORAGE_FLAG_SENT             (1u << 1)
#define APP_METER_STORAGE_FLAG_TIME_VALID       (1u << 2)
#define APP_METER_STORAGE_FLAG_READING_VALID    (1u << 3)

#define APP_METER_STORAGE_STATUS_OVERFLOW       (1u << 7)
#define APP_METER_STORAGE_STATUS_REVERSE_FLOW   (1u << 6)
#define APP_METER_STORAGE_STATUS_LEAK           (1u << 5)
#define APP_METER_STORAGE_STATUS_LOW_BATTERY    (1u << 4)

#define APP_METER_STORAGE_METER_TYPE_DIGITAL_UART   (0x01u)
#define APP_METER_STORAGE_METER_TYPE_SC1XXX         (0x02u)
#define APP_METER_STORAGE_CALIBER_UNKNOWN           (0x0Fu)

#if defined(__GNUC__)
#define APP_METER_STORAGE_PACKED __attribute__((packed))
#else
#define APP_METER_STORAGE_PACKED
#endif

typedef enum
{
    APP_METER_STORAGE_SRC_DIGITAL_UART = 0u,
    APP_METER_STORAGE_SRC_SC1XXX = 1u,
} AppMeterStorageSource_t;

typedef struct APP_METER_STORAGE_PACKED
{
    uint16_t seq;               // 저장 순번
    uint8_t srcType;            // 0:digital_uart, 1:SC1000/1200
    uint8_t flags;              // bit0:valid, bit1:sent, bit2:time_valid, bit3:reading_valid
    uint8_t ts[6];              // YY/MM/DD/hh/mm/ss
    uint32_t meterId;           // 정규화된 기물번호
    uint32_t readingScaled;     // 공통 스케일 정수값(예: 0.001 단위)
    uint8_t meterStatus;        // 정규화된 공통 상태비트
    uint8_t meterBattery;       // 계량기 배터리 상태/정규화값
    uint8_t meterType;          // 서버 5.4용
    uint8_t caliberDecimal;     // 서버 5.4용 (구경/소수점)
    uint8_t crc8;
    uint8_t reserved;
} AppMeterStorageRecord_t;

typedef struct
{
    uint8_t initialized;
    uint8_t head;
    uint8_t count;
    uint16_t nextSeq;
} AppMeterStorageInfo_t;

AppStatus_t App_StorageDataEepromRead(uint32_t offset, void *p_data, uint32_t sizeBytes);
AppStatus_t App_StorageDataEepromWrite(uint32_t offset, const void *p_data, uint32_t sizeBytes);
void App_MeterStorageInfo(void);
AppStatus_t App_MeterStorageInit(void);
uint8_t App_MeterStorageIsInitialized(void);
AppStatus_t App_MeterStoragePush(const AppMeterStorageRecord_t *p_record);
AppStatus_t App_MeterStoragePeekOldest(AppMeterStorageRecord_t *p_record);
AppStatus_t App_MeterStorageReadAt(uint8_t logicalIndex, AppMeterStorageRecord_t *p_record);
AppStatus_t App_MeterStorageDeleteOldest(uint8_t deleteCount);
AppStatus_t App_MeterStorageMarkOldestSent(uint8_t sentCount);
uint8_t App_MeterStorageCount(void);
AppStatus_t App_MeterStorageClearAll(void);
AppStatus_t App_MeterStorageGetInfo(AppMeterStorageInfo_t *p_info);
void App_MeterStoragePrintRecord(const AppMeterStorageRecord_t *p_record);


/* ================================================================
 *  Device Config (Bank1)
 * ================================================================ */
#define APP_DEVICE_CONFIG_MAGIC         (0x44434647u)   /* 'DCFG' */
#define APP_DEVICE_CONFIG_VERSION       (0x01u)

//If APP_STORAGE_CONFIG_SLOT_COUNT:4 -> Max 1KByte/4 = 256Byte
typedef struct APP_METER_STORAGE_PACKED
{
    uint8_t  bootCountValid;
    uint32_t bootCount;
    uint8_t  logLevel;
    uint8_t  linkType;          /* 0:LoRa, 1:NB-IoT */
    uint8_t  reserved[32];      /* 향후 확장용 */
} AppDeviceConfig_t;

/* Bank1 low-level EEPROM I/O */
AppStatus_t App_StorageConfigEepromRead (uint32_t offset, void *p_data, uint32_t sizeBytes);
AppStatus_t App_StorageConfigEepromWrite(uint32_t offset, const void *p_data, uint32_t sizeBytes);

/* 공용 슬롯 헬퍼 (Options 모듈에서도 사용) */
typedef struct
{
    uint32_t regionOffset;
    uint32_t regionSize;
    uint8_t  slotCount;
    uint8_t  payloadSize;
    uint8_t  latestSlotIndex;
    uint16_t latestSeq;
    uint8_t  initialized;
} AppConfigSlotRegion_t;

AppStatus_t App_ConfigSlotInit (AppConfigSlotRegion_t *p_region);
AppStatus_t App_ConfigSlotLoad (AppConfigSlotRegion_t *p_region, void *p_payload, uint8_t *p_found);
AppStatus_t App_ConfigSlotSave (AppConfigSlotRegion_t *p_region, const void *p_payload);
AppStatus_t App_ConfigSlotClear(AppConfigSlotRegion_t *p_region);

/* Device Config API */
void        App_DeviceConfigSetDefaults(AppDeviceConfig_t *p_config);
AppStatus_t App_DeviceConfigInit (void);
AppStatus_t App_DeviceConfigLoad (AppDeviceConfig_t *p_config);
AppStatus_t App_DeviceConfigSave (const AppDeviceConfig_t *p_config);
AppStatus_t App_DeviceConfigClear(void);
void        App_DeviceConfigInfo (void);
void App_DeviceConfigDump(const AppDeviceConfig_t *p_config);

#ifdef __cplusplus
}
#endif

#endif /* APP_METER_STORAGE_H */

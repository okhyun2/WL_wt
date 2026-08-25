#ifndef APP_NFC_SEOUL_FORMAT_H
#define APP_NFC_SEOUL_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_error.h"
#include "app_meter_storage.h"
#include "nfc_ntag5_ntp53321.h"

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
#define APP_NFC_SEOUL_CMD_STOR_REQ                (0x00u)
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

typedef struct
{
    uint8_t handled;
    uint8_t commRequested;
    uint8_t requestCmd1;
    uint8_t requestCmd2;
    uint8_t responseCmd1;
    uint8_t responseCmd2;
} AppNfcSeoulProcessResult_t;

typedef struct
{
    uint8_t initialized;
    uint8_t lastReadSource;
    uint8_t lastHandled;
    uint8_t lastCommRequested;
    uint8_t lastRequestCmd1;
    uint8_t lastRequestCmd2;
    uint8_t lastResponseCmd1;
    uint8_t lastResponseCmd2;
    uint8_t lastStatus;
    uint8_t lastRequestLength;
    uint8_t lastResponseLength;
    uint32_t lastTickMs;
    uint32_t requestCount;
    uint32_t responseCount;
    uint32_t storageRefreshCount;
    uint8_t lastRequest[64];
    uint8_t lastResponse[64];
} AppNfcSeoulDebugInfo_t;

typedef struct
{
    uint8_t snapshotFormatVersion;
    uint8_t readOnlyLayer1;
    uint8_t patentLayer2Supported;
    uint8_t reserved;
} AppNfcSeoulLayer1Info_t;

AppStatus_t App_NfcSeoulInit(NFC_NTP53321_Handle_t *p_tag);
AppStatus_t App_NfcSeoulProcessTag(AppNfcSeoulProcessResult_t *p_result);
AppStatus_t App_NfcSeoulProcessCommandFrame(const uint8_t *p_frame, uint8_t frame_length, AppNfcSeoulProcessResult_t *p_result);
AppStatus_t App_NfcSeoulNotifyStorageChanged(void);
AppStatus_t App_NfcSeoulNotifyLiveMeterRecord(const AppMeterStorageRecord_t *p_record);
AppStatus_t App_NfcSeoulRetrySramMirrorOnField(void);
AppStatus_t App_NfcSeoulServiceTestMode(void);
const AppNfcSeoulDebugInfo_t *App_NfcSeoulGetDebugInfo(void);
const AppNfcSeoulLayer1Info_t *App_NfcSeoulGetLayer1Info(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_NFC_SEOUL_FORMAT_H */

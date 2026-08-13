#ifndef APP_NFC_SEOUL_FORMAT_H
#define APP_NFC_SEOUL_FORMAT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_error.h"
#include "app_meter_storage.h"
#include "nfc_ntag5_ntp53321.h"

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

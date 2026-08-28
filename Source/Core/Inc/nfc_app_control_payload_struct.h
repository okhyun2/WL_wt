#ifndef NFC_APP_CONTROL_PAYLOAD_STRUCT_H
#define NFC_APP_CONTROL_PAYLOAD_STRUCT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#pragma pack(push, 1)

/* UCMD Req/Rsp payload struct */
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define NFC_APP_CTRL_PAYLOAD_SIZE      (32U)
#define NFC_APP_CTRL_REQ_MAX_LEN       NFC_APP_CTRL_PAYLOAD_SIZE
#define NFC_APP_CTRL_RSP_MAX_LEN       NFC_APP_CTRL_PAYLOAD_SIZE

/* NB group */
typedef struct { uint8_t resetMode; uint8_t delay100ms; uint8_t reserved[30]; } NfcReqNbResetExecute_t;
typedef struct { uint8_t accepted; uint8_t appliedMode; uint8_t reserved[30]; } NfcRspNbResetExecute_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t reserved[30]; } NfcReqNbPeriodSet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t verifyResult; uint8_t reserved[29]; } NfcRspNbPeriodSet_t;
typedef struct { uint8_t reserved[32]; } NfcReqNbPeriodGet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t reserved[30]; } NfcRspNbPeriodGet_t;
typedef struct { uint8_t reportingSpreadHours; uint8_t reserved[31]; } NfcReqNbSpreadSet_t;
typedef struct { uint8_t reportingSpreadHours; uint8_t verifyResult; uint8_t reserved[30]; } NfcRspNbSpreadSet_t;
typedef struct { uint8_t reserved[32]; } NfcReqNbSpreadGet_t;
typedef struct { uint8_t reportingSpreadHours; uint8_t reportingPeriodHours; uint8_t reserved[30]; } NfcRspNbSpreadGet_t;
typedef struct { uint8_t reserved[32]; } NfcReqNbAckStatusGet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t lastAckState; uint8_t pendingTx; uint8_t reserved0; uint8_t reserved[28]; } NfcRspNbAckStatusGet_t;

/* SELFTEST group */
typedef struct { uint8_t option; uint8_t reserved[31]; } NfcReqSelftestRunQuick_t;
typedef struct { uint8_t diagSeq; uint8_t diagState; uint8_t failCount; uint8_t reserved0; uint8_t reserved[28]; } NfcRspSelftestRunQuick_t;
typedef struct { uint8_t option; uint8_t reserved[31]; } NfcReqSelftestRunFull_t;
typedef NfcRspSelftestRunQuick_t NfcRspSelftestRunFull_t;
typedef struct { uint8_t reserved[32]; } NfcReqSelftestSummaryGet_t;
typedef struct { uint8_t diagSeq; uint8_t executedMaskLe[2]; uint8_t passedMaskLe[2]; uint8_t failCount; uint8_t lastStatusLe[2]; uint8_t reserved[24]; } NfcRspSelftestSummaryGet_t;
typedef struct { uint8_t itemId; uint8_t reserved[31]; } NfcReqSelftestDetailGet_t;
typedef struct { uint8_t itemId; uint8_t executed; uint8_t passed; uint8_t statusLe[2]; uint8_t tickMsLe[4]; uint8_t reserved[23]; } NfcRspSelftestDetailGet_t;
typedef struct { uint8_t itemId; uint8_t reserved[31]; } NfcReqSelftestRetryItem_t;
typedef struct { uint8_t itemId; uint8_t executed; uint8_t passed; uint8_t statusLe[2]; uint8_t reserved[27]; } NfcRspSelftestRetryItem_t;
typedef struct { uint8_t clearCode; uint8_t reserved[31]; } NfcReqSelftestClear_t;
typedef struct { uint8_t cleared; uint8_t reserved[31]; } NfcRspSelftestClear_t;

/* PARAMETER group */
typedef struct { uint8_t reserved[32]; } NfcReqParamScheduleGet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t flags; uint8_t reserved[27]; } NfcRspParamScheduleGet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t applyMask; uint8_t reserved[27]; } NfcReqParamScheduleSet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t verifyResult; uint8_t reserved[27]; } NfcRspParamScheduleSet_t;
typedef struct { uint8_t reserved[32]; } NfcReqParamPolicyGet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t ackTimeout100ms; uint8_t ackPoll100ms; uint8_t deleteAfterSend; uint8_t flags; uint8_t reserved[27]; } NfcRspParamPolicyGet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t ackTimeout100ms; uint8_t ackPoll100ms; uint8_t deleteAfterSend; uint8_t applyMask; uint8_t reserved[27]; } NfcReqParamPolicySet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t ackTimeout100ms; uint8_t ackPoll100ms; uint8_t deleteAfterSend; uint8_t verifyResult; uint8_t reserved[27]; } NfcRspParamPolicySet_t;
typedef struct { uint8_t reserved[32]; } NfcReqParamDeviceGet_t;
typedef struct { uint8_t logLevel; uint8_t linkType; uint8_t diagProfile; uint8_t resetEnable; uint8_t flags; uint8_t reserved[27]; } NfcRspParamDeviceGet_t;
typedef struct { uint8_t logLevel; uint8_t linkType; uint8_t diagProfile; uint8_t resetEnable; uint8_t applyMask; uint8_t reserved[27]; } NfcReqParamDeviceSet_t;
typedef struct { uint8_t logLevel; uint8_t linkType; uint8_t diagProfile; uint8_t resetEnable; uint8_t verifyResult; uint8_t reserved[27]; } NfcRspParamDeviceSet_t;
typedef struct { uint8_t targetGroup; uint8_t restoreCode; uint8_t reserved[30]; } NfcReqParamRestoreDefault_t;
typedef struct { uint8_t targetGroup; uint8_t verifyResult; uint8_t reserved[30]; } NfcRspParamRestoreDefault_t;
typedef struct { uint8_t targetGroup; uint8_t reserved[31]; } NfcReqParamReadbackGet_t;
typedef struct { uint8_t mismatchCount; uint8_t lastError; uint8_t reserved0; uint8_t reserved1; uint8_t reserved[28]; } NfcRspParamReadbackGet_t;
#pragma pack(pop)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef union
{
    uint8_t raw[NFC_APP_CTRL_PAYLOAD_SIZE];
    NfcReqNbResetExecute_t      nbResetExecute;
    NfcReqNbPeriodSet_t         nbPeriodSet;
    NfcReqNbPeriodGet_t         nbPeriodGet;
    NfcReqNbSpreadSet_t         nbSpreadSet;
    NfcReqNbSpreadGet_t         nbSpreadGet;
    NfcReqNbAckStatusGet_t      nbAckStatusGet;
    NfcReqSelftestRunQuick_t    selftestRunQuick;
    NfcReqSelftestRunFull_t     selftestRunFull;
    NfcReqSelftestSummaryGet_t  selftestSummaryGet;
    NfcReqSelftestDetailGet_t   selftestDetailGet;
    NfcReqSelftestRetryItem_t   selftestRetryItem;
    NfcReqSelftestClear_t       selftestClear;
    NfcReqParamScheduleGet_t    paramScheduleGet;
    NfcReqParamScheduleSet_t    paramScheduleSet;
    NfcReqParamPolicyGet_t      paramPolicyGet;
    NfcReqParamPolicySet_t      paramPolicySet;
    NfcReqParamDeviceGet_t      paramDeviceGet;
    NfcReqParamDeviceSet_t      paramDeviceSet;
    NfcReqParamRestoreDefault_t paramRestoreDefault;
    NfcReqParamReadbackGet_t    paramReadbackGet;
} NfcAppCtrlReqPayload_u;

typedef union
{
    uint8_t raw[NFC_APP_CTRL_PAYLOAD_SIZE];
    NfcRspNbResetExecute_t      nbResetExecute;
    NfcRspNbPeriodSet_t         nbPeriodSet;
    NfcRspNbPeriodGet_t         nbPeriodGet;
    NfcRspNbSpreadSet_t         nbSpreadSet;
    NfcRspNbSpreadGet_t         nbSpreadGet;
    NfcRspNbAckStatusGet_t      nbAckStatusGet;
    NfcRspSelftestRunQuick_t    selftestRunQuick;
    NfcRspSelftestRunFull_t     selftestRunFull;
    NfcRspSelftestSummaryGet_t  selftestSummaryGet;
    NfcRspSelftestDetailGet_t   selftestDetailGet;
    NfcRspSelftestRetryItem_t   selftestRetryItem;
    NfcRspSelftestClear_t       selftestClear;
    NfcRspParamScheduleGet_t    paramScheduleGet;
    NfcRspParamScheduleSet_t    paramScheduleSet;
    NfcRspParamPolicyGet_t      paramPolicyGet;
    NfcRspParamPolicySet_t      paramPolicySet;
    NfcRspParamDeviceGet_t      paramDeviceGet;
    NfcRspParamDeviceSet_t      paramDeviceSet;
    NfcRspParamRestoreDefault_t paramRestoreDefault;
    NfcRspParamReadbackGet_t    paramReadbackGet;
} NfcAppCtrlRspPayload_u;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* NFC_APP_CONTROL_PAYLOAD_STRUCT_H */

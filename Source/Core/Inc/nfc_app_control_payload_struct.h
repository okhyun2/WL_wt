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
#define NFC_APP_CTRL_PAYLOAD_SIZE      (64U)
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

/* DIAGNOSIS group */
//typedef struct { uint8_t option; uint8_t reserved[31]; } NfcReqDiagnosisRunQuick_t;
//typedef struct { uint8_t diagSeq; uint8_t diagState; uint8_t failCount; uint8_t reserved0; uint8_t reserved[28]; } NfcRspDiagnosisRunQuick_t;
//typedef struct { uint8_t option; uint8_t reserved[31]; } NfcReqDiagnosisRunFull_t;
//typedef NfcRspDiagnosisRunQuick_t NfcRspDiagnosisRunFull_t;
typedef struct { uint8_t reserved[32]; } NfcReqDiagnosisRunSummaryGet_t;
typedef struct { uint8_t diagSeq; uint8_t executedMaskLe[2]; uint8_t passedMaskLe[2]; uint8_t failCount; uint8_t lastStatusLe[2]; uint8_t reserved[24]; } NfcRspDiagnosisRunSummaryGet_t;
typedef struct { uint8_t itemId; uint8_t reserved[31]; } NfcReqDiagnosisDetailGet_t;
typedef struct { uint8_t itemId; uint8_t executed; uint8_t passed; uint8_t statusLe[2]; uint8_t tickMsLe[4]; uint8_t reserved[23]; } NfcRspDiagnosisDetailGet_t;
//typedef struct { uint8_t itemId; uint8_t reserved[31]; } NfcReqDiagnosisRetryItem_t;
//typedef struct { uint8_t itemId; uint8_t executed; uint8_t passed; uint8_t statusLe[2]; uint8_t reserved[27]; } NfcRspDiagnosisRetryItem_t;
//typedef struct { uint8_t clearCode; uint8_t reserved[31]; } NfcReqDiagnosisClear_t;
//typedef struct { uint8_t cleared; uint8_t reserved[31]; } NfcRspDiagnosisClear_t;

/* PARAMETER group */
typedef struct { uint8_t reserved[32]; } NfcReqParamScheduleGet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t flags; uint8_t reserved[27]; } NfcRspParamScheduleGet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t applyMask; uint8_t reserved[27]; } NfcReqParamScheduleSet_t;
typedef struct { uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t verifyResult; uint8_t reserved[27]; } NfcRspParamScheduleSet_t;
typedef struct { uint8_t reserved[32]; } NfcReqParamPolicyGet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t ackTimeout100ms; uint8_t ackPoll100ms; uint8_t deleteAfterSend; uint8_t flags; uint8_t strongReportingHours; uint8_t weakReportingHours; uint8_t strongTxPeriodMin; uint8_t weakTxPeriodMin; uint8_t strongMeterPeriodMin; uint8_t weakMeterPeriodMin; uint8_t adaptiveFlags; uint8_t reserved[20]; } NfcRspParamPolicyGet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t ackTimeout100ms; uint8_t ackPoll100ms; uint8_t deleteAfterSend; uint8_t applyMask; uint8_t strongReportingHours; uint8_t weakReportingHours; uint8_t strongTxPeriodMin; uint8_t weakTxPeriodMin; uint8_t strongMeterPeriodMin; uint8_t weakMeterPeriodMin; uint8_t adaptiveFlags; uint8_t reserved[20]; } NfcReqParamPolicySet_t;
typedef struct { uint8_t ackWaitEnabled; uint8_t ackTimeout100ms; uint8_t ackPoll100ms; uint8_t deleteAfterSend; uint8_t verifyResult; uint8_t strongReportingHours; uint8_t weakReportingHours; uint8_t strongTxPeriodMin; uint8_t weakTxPeriodMin; uint8_t strongMeterPeriodMin; uint8_t weakMeterPeriodMin; uint8_t adaptiveFlags; uint8_t reserved[20]; } NfcRspParamPolicySet_t;
typedef struct { uint8_t reserved[32]; } NfcReqParamDeviceGet_t;
typedef struct { uint8_t logLevel; uint8_t linkType; uint8_t diagProfile; uint8_t resetEnable; uint8_t flags; uint8_t reserved[27]; } NfcRspParamDeviceGet_t;
typedef struct { uint8_t logLevel; uint8_t linkType; uint8_t diagProfile; uint8_t resetEnable; uint8_t applyMask; uint8_t reserved[27]; } NfcReqParamDeviceSet_t;
typedef struct { uint8_t logLevel; uint8_t linkType; uint8_t diagProfile; uint8_t resetEnable; uint8_t verifyResult; uint8_t reserved[27]; } NfcRspParamDeviceSet_t;
typedef struct { uint8_t targetGroup; uint8_t restoreCode; uint8_t reserved[30]; } NfcReqParamRestoreDefault_t;
typedef struct { uint8_t targetGroup; uint8_t verifyResult; uint8_t reserved[30]; } NfcRspParamRestoreDefault_t;
typedef struct { uint8_t targetGroup; uint8_t reserved[31]; } NfcReqParamReadbackGet_t;
typedef struct { uint8_t mismatchCount; uint8_t lastError; uint8_t reserved0; uint8_t reserved1; uint8_t reserved[28]; } NfcRspParamReadbackGet_t;
typedef struct { uint8_t reserved[32]; } NfcReqParamRuntimeInfoGet_t;
typedef struct { uint8_t imeiBcd[8]; uint8_t rssiDbmLe[2]; uint8_t rsrpDbmLe[2]; uint8_t deviceSerialBcd[5]; uint8_t meteringPeriodHours; uint8_t reportingPeriodHours; uint8_t managementReportingPeriodHours; uint8_t reportingSpreadHours; uint8_t flags; uint8_t reserved[10]; } NfcRspParamRuntimeInfoGet_t;
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
    //NfcReqDiagnosisRunQuick_t   diagnosisRunQuick;
    //NfcReqDiagnosisRunFull_t    diagnosisRunFull;
    NfcReqDiagnosisRunSummaryGet_t diagnosisRunSummaryGet;
    NfcReqDiagnosisDetailGet_t  diagnosisDetailGet;
    //NfcReqDiagnosisRetryItem_t  diagnosisRetryItem;
    //NfcReqDiagnosisClear_t      diagnosisClear;
    NfcReqParamScheduleGet_t    paramScheduleGet;
    NfcReqParamScheduleSet_t    paramScheduleSet;
    NfcReqParamPolicyGet_t      paramPolicyGet;
    NfcReqParamPolicySet_t      paramPolicySet;
    NfcReqParamDeviceGet_t      paramDeviceGet;
    NfcReqParamDeviceSet_t      paramDeviceSet;
    NfcReqParamRestoreDefault_t paramRestoreDefault;
    NfcReqParamReadbackGet_t    paramReadbackGet;
    NfcReqParamRuntimeInfoGet_t paramRuntimeInfoGet;
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
    //NfcRspDiagnosisRunQuick_t   diagnosisRunQuick;
    //NfcRspDiagnosisRunFull_t    diagnosisRunFull;
    NfcRspDiagnosisRunSummaryGet_t diagnosisRunSummaryGet;
    NfcRspDiagnosisDetailGet_t  diagnosisDetailGet;
    //NfcRspDiagnosisRetryItem_t  diagnosisRetryItem;
    //NfcRspDiagnosisClear_t      diagnosisClear;
    NfcRspParamScheduleGet_t    paramScheduleGet;
    NfcRspParamScheduleSet_t    paramScheduleSet;
    NfcRspParamPolicyGet_t      paramPolicyGet;
    NfcRspParamPolicySet_t      paramPolicySet;
    NfcRspParamDeviceGet_t      paramDeviceGet;
    NfcRspParamDeviceSet_t      paramDeviceSet;
    NfcRspParamRestoreDefault_t paramRestoreDefault;
    NfcRspParamReadbackGet_t    paramReadbackGet;
    NfcRspParamRuntimeInfoGet_t paramRuntimeInfoGet;
} NfcAppCtrlRspPayload_u;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* NFC_APP_CONTROL_PAYLOAD_STRUCT_H */

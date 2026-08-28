#ifndef NFC_APP_CONTROL_COMMAND_H
#define NFC_APP_CONTROL_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define NFC_APP_CTRL_CMD_CLASS         (0x20U)
#define NFC_APP_CTRL_CMD_SIZE          (4U)
#define NFC_APP_CTRL_PAYLOAD_SIZE      (32U)
#define NFC_APP_CTRL_REQ_MAX_LEN       NFC_APP_CTRL_PAYLOAD_SIZE
#define NFC_APP_CTRL_RSP_MAX_LEN       NFC_APP_CTRL_PAYLOAD_SIZE

typedef enum
{
    NFC_APP_CTRL_GROUP_NB_CONTROL = 0x01U,
    NFC_APP_CTRL_GROUP_SELFTEST   = 0x02U,
    NFC_APP_CTRL_GROUP_PARAMETER  = 0x03U
} NfcAppCtrlGroup_t;

typedef enum
{
    NFC_APP_CTRL_NB_RESET_EXECUTE   = 0x02U,
    NFC_APP_CTRL_NB_PERIOD_SET      = 0x10U,
    NFC_APP_CTRL_NB_PERIOD_GET      = 0x11U,
    NFC_APP_CTRL_NB_SPREAD_SET      = 0x12U,
    NFC_APP_CTRL_NB_SPREAD_GET      = 0x13U,
    NFC_APP_CTRL_NB_ACK_STATUS_GET  = 0x14U,
    NFC_APP_CTRL_NB_RESERVED        = 0x1FU
} NfcAppCtrlNbItem_t;

typedef enum
{
    NFC_APP_CTRL_SELFTEST_RUN_QUICK   = 0x01U,
    NFC_APP_CTRL_SELFTEST_RUN_FULL    = 0x02U,
    NFC_APP_CTRL_SELFTEST_SUMMARY_GET = 0x03U,
    NFC_APP_CTRL_SELFTEST_DETAIL_GET  = 0x04U,
    NFC_APP_CTRL_SELFTEST_RETRY_ITEM  = 0x05U,
    NFC_APP_CTRL_SELFTEST_CLEAR       = 0x06U,
    NFC_APP_CTRL_SELFTEST_RESERVED    = 0x1FU
} NfcAppCtrlSelfTestItemCmd_t;

typedef enum
{
    NFC_APP_CTRL_PARAM_SCHEDULE_GET    = 0x01U,
    NFC_APP_CTRL_PARAM_SCHEDULE_SET    = 0x02U,
    NFC_APP_CTRL_PARAM_POLICY_GET      = 0x03U,
    NFC_APP_CTRL_PARAM_POLICY_SET      = 0x04U,
    NFC_APP_CTRL_PARAM_DEVICE_GET      = 0x05U,
    NFC_APP_CTRL_PARAM_DEVICE_SET      = 0x06U,
    NFC_APP_CTRL_PARAM_RESTORE_DEFAULT = 0x07U,
    NFC_APP_CTRL_PARAM_READBACK_GET    = 0x08U,
    NFC_APP_CTRL_PARAM_RESERVED        = 0x1FU
} NfcAppCtrlParamItem_t;

typedef enum
{
    NFC_APP_CTRL_OP_OK            = 0x00U,
    NFC_APP_CTRL_OP_FAIL          = 0x01U,
    NFC_APP_CTRL_OP_BUSY          = 0x02U,
    NFC_APP_CTRL_OP_NOT_SUPPORTED = 0x03U,
    NFC_APP_CTRL_OP_RANGE_ERROR   = 0x04U,
    NFC_APP_CTRL_OP_VERIFY_FAIL   = 0x05U,
    NFC_APP_CTRL_OP_STORAGE_FAIL  = 0x06U,
    NFC_APP_CTRL_OP_IN_PROGRESS   = 0x07U
} NfcAppCtrlOpStatus_t;

typedef enum
{
    NFC_APP_CTRL_DIAG_STATE_IDLE        = 0x00U,
    NFC_APP_CTRL_DIAG_STATE_RUNNING     = 0x01U,
    NFC_APP_CTRL_DIAG_STATE_DONE        = 0x02U,
    NFC_APP_CTRL_DIAG_STATE_FAIL        = 0x03U,
    NFC_APP_CTRL_DIAG_STATE_UNAVAILABLE = 0x04U
} NfcAppCtrlDiagState_t;

typedef enum
{
    NFC_APP_CTRL_PERIOD_DISABLED = 0x00U,
    NFC_APP_CTRL_PERIOD_1H       = 0x01U,
    NFC_APP_CTRL_PERIOD_2H       = 0x02U,
    NFC_APP_CTRL_PERIOD_3H       = 0x03U,
    NFC_APP_CTRL_PERIOD_4H       = 0x04U,
    NFC_APP_CTRL_PERIOD_6H       = 0x06U,
    NFC_APP_CTRL_PERIOD_12H      = 0x0CU
} NfcAppCtrlPeriod_t;

typedef enum
{
    NFC_APP_CTRL_RESET_MODE_IMMEDIATE = 0x00U,
    NFC_APP_CTRL_RESET_MODE_GRACEFUL  = 0x01U
} NfcAppCtrlResetMode_t;

#pragma pack(push, 1)
typedef struct
{
    uint8_t xx;
    uint8_t yy;
    uint8_t zz;
    uint8_t reserved;
} NfcAppCtrlCmd_t;

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

typedef char _nfc_app_ctrl_req_union_size_chk[(sizeof(NfcAppCtrlReqPayload_u) == NFC_APP_CTRL_PAYLOAD_SIZE) ? 1 : -1];
typedef char _nfc_app_ctrl_rsp_union_size_chk[(sizeof(NfcAppCtrlRspPayload_u) == NFC_APP_CTRL_PAYLOAD_SIZE) ? 1 : -1];

uint8_t NfcAppCtrl_Execute(const NfcAppCtrlCmd_t *p_cmd,
                           const uint8_t *p_req_raw,
                           uint8_t *p_rsp_raw,
                           uint8_t *p_rsp_len,
                           uint8_t *p_op_status);
bool NfcAppCtrl_ConsumePendingReset(void);
uint32_t NfcAppCtrl_GetPendingResetDelayMs(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_APP_CONTROL_COMMAND_H */

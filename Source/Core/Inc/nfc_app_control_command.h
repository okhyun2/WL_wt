#ifndef NFC_APP_CONTROL_COMMAND_H
#define NFC_APP_CONTROL_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "nfc_app_control_payload_struct.h"

#define NFC_APP_CTRL_CMD_CLASS         (0x20U)
#define NFC_APP_CTRL_CMD_SIZE          (4U)

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
#pragma pack(pop)

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

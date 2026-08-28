#ifndef NFC_APP_CONTROL_COMMAND_H
#define NFC_APP_CONTROL_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define NFC_APP_CTRL_CMD_CLASS         (0x20U)
#define NFC_APP_CTRL_CMD_SIZE          (4U)
#define NFC_APP_CTRL_REQ_MAX_LEN       (32U)
#define NFC_APP_CTRL_RSP_MAX_LEN       (32U)
#define NFC_APP_CTRL_REQ_BODY_MAX      (NFC_APP_CTRL_REQ_MAX_LEN - NFC_APP_CTRL_CMD_SIZE)
#define NFC_APP_CTRL_RSP_OVERHEAD      (5U)  /* op_status(1) + CMD echo(4) */
#define NFC_APP_CTRL_RSP_BODY_MAX      (NFC_APP_CTRL_RSP_MAX_LEN - NFC_APP_CTRL_RSP_OVERHEAD)

typedef enum
{
    NFC_APP_CTRL_GROUP_NB_CONTROL = 0x01,
    NFC_APP_CTRL_GROUP_SELFTEST   = 0x02,
    NFC_APP_CTRL_GROUP_PARAMETER  = 0x03,
    NFC_APP_CTRL_GROUP_VENDOR     = 0x7FU
} NfcAppCtrlGroup_t;

typedef enum
{
    NFC_APP_CTRL_NB_RESET_EXECUTE    = 0x01,
    NFC_APP_CTRL_NB_PERIOD_SET       = 0x02,
    NFC_APP_CTRL_NB_PERIOD_GET       = 0x03,
    NFC_APP_CTRL_NB_SPREAD_SET       = 0x04,
    NFC_APP_CTRL_NB_SPREAD_GET       = 0x05,
    NFC_APP_CTRL_NB_ACK_STATUS_GET   = 0x06,
    NFC_APP_CTRL_NB_RESET_SUPPORT_GET = 0x07
} NfcAppCtrlNbItem_t;

typedef enum
{
    NFC_APP_CTRL_SELFTEST_RUN_QUICK  = 0x01,
    NFC_APP_CTRL_SELFTEST_RUN_FULL   = 0x02,
    NFC_APP_CTRL_SELFTEST_SUMMARY_GET = 0x03,
    NFC_APP_CTRL_SELFTEST_DETAIL_GET = 0x04
} NfcAppCtrlSelfTestItemCmd_t;

typedef enum
{
    NFC_APP_CTRL_PARAM_SCHEDULE_GET    = 0x01,
    NFC_APP_CTRL_PARAM_SCHEDULE_SET    = 0x02,
    NFC_APP_CTRL_PARAM_POLICY_GET      = 0x03,
    NFC_APP_CTRL_PARAM_POLICY_SET      = 0x04,
    NFC_APP_CTRL_PARAM_DEVICE_GET      = 0x05,
    NFC_APP_CTRL_PARAM_DEVICE_SET      = 0x06,
    NFC_APP_CTRL_PARAM_RESTORE_DEFAULT = 0x07
} NfcAppCtrlParamItem_t;

typedef enum
{
    NFC_APP_CTRL_OP_OK            = 0x00,
    NFC_APP_CTRL_OP_FAIL          = 0x01,
    NFC_APP_CTRL_OP_BUSY          = 0x02,
    NFC_APP_CTRL_OP_NOT_SUPPORTED = 0x03,
    NFC_APP_CTRL_OP_RANGE_ERROR   = 0x04,
    NFC_APP_CTRL_OP_VERIFY_FAIL   = 0x05,
    NFC_APP_CTRL_OP_STORAGE_FAIL  = 0x06,
    NFC_APP_CTRL_OP_IN_PROGRESS   = 0x07
} NfcAppCtrlOpStatus_t;

typedef enum
{
    NFC_APP_CTRL_PERIOD_DISABLED = 0x00,
    NFC_APP_CTRL_PERIOD_1H = 0x01,
    NFC_APP_CTRL_PERIOD_2H = 0x02,
    NFC_APP_CTRL_PERIOD_3H = 0x03,
    NFC_APP_CTRL_PERIOD_4H = 0x04,
    NFC_APP_CTRL_PERIOD_6H = 0x06,
    NFC_APP_CTRL_PERIOD_12H = 0x0CU
} NfcAppCtrlPeriod_t;

typedef struct
{
    uint8_t xx;
    uint8_t yy;
    uint8_t zz;
    uint8_t reserved;
} NfcAppCtrlCmd_t;

typedef struct
{
    uint8_t opStatus;
    NfcAppCtrlCmd_t cmdEcho;
    uint8_t body[NFC_APP_CTRL_RSP_BODY_MAX];
} NfcAppCtrlResponseRaw_t;

typedef char _nfc_app_ctrl_req_body_chk[(NFC_APP_CTRL_REQ_BODY_MAX == 12U) ? 1 : -1];
typedef char _nfc_app_ctrl_rsp_body_chk[(NFC_APP_CTRL_RSP_BODY_MAX == 9U) ? 1 : -1];

uint8_t NfcAppCtrl_Execute(const uint8_t *p_req_raw,
                           uint8_t req_len,
                           uint8_t *p_rsp_raw,
                           uint8_t *p_rsp_len);
bool NfcAppCtrl_ConsumePendingReset(void);

#ifdef __cplusplus
}
#endif

#endif /* NFC_APP_CONTROL_COMMAND_H */

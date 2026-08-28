#include "nfc_app_control_command.h"

#include <string.h>

#include "app_log.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"
#include "app_nbiot.h"
#include "app_selftest.h"
#include "nfc_user_command.h"

#define NFC_APP_CTRL_DEVICE_RESERVED_DIAG_PROFILE_IDX       0U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_ENABLE_IDX       1U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_VALID_IDX  2U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_BOOT_IDX   3U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_REASON_IDX 4U

#define NFC_APP_CTRL_RESET_TRACK_VALID_MARKER               0xA5U
#define NFC_APP_CTRL_CLEAR_CODE                             0xA5U
#define NFC_APP_CTRL_RESTORE_CODE                           0xA5U

#define NFC_APP_CTRL_APPLY_METERING_BIT                     (1U << 0)
#define NFC_APP_CTRL_APPLY_REPORTING_BIT                    (1U << 1)
#define NFC_APP_CTRL_APPLY_MGMT_BIT                         (1U << 2)
#define NFC_APP_CTRL_APPLY_SPREAD_BIT                       (1U << 3)
#define NFC_APP_CTRL_APPLY_ACK_WAIT_BIT                     (1U << 0)
#define NFC_APP_CTRL_APPLY_ACK_TIMEOUT_BIT                  (1U << 1)
#define NFC_APP_CTRL_APPLY_ACK_POLL_BIT                     (1U << 2)
#define NFC_APP_CTRL_APPLY_DELETE_AFTER_SEND_BIT            (1U << 3)
#define NFC_APP_CTRL_APPLY_LOG_LEVEL_BIT                    (1U << 0)
#define NFC_APP_CTRL_APPLY_LINK_TYPE_BIT                    (1U << 1)
#define NFC_APP_CTRL_APPLY_DIAG_PROFILE_BIT                 (1U << 2)
#define NFC_APP_CTRL_APPLY_RESET_ENABLE_BIT                 (1U << 3)

#define NFC_APP_CTRL_READBACK_GROUP_ALL                     0x00U
#define NFC_APP_CTRL_READBACK_GROUP_SCHEDULE                0x01U
#define NFC_APP_CTRL_READBACK_GROUP_POLICY                  0x02U
#define NFC_APP_CTRL_READBACK_GROUP_DEVICE                  0x03U

#define NFC_APP_CTRL_MAX_LOG_LEVEL                          5U
#define NFC_APP_CTRL_MAX_DIAG_PROFILE                       3U
#define NFC_APP_CTRL_LINK_TYPE_LORA                         0U
#define NFC_APP_CTRL_LINK_TYPE_NBIOT                        1U

static bool s_appCtrlPendingReset = false;
static uint32_t s_appCtrlPendingResetDelayMs = 0U;
static uint8_t s_appCtrlDiagSeq = 0U;
static AppMeterServerFormatOptions_t s_lastSavedOptions;
static AppDeviceConfig_t s_lastSavedDevice;
static uint8_t s_lastSavedOptionsValid = 0U;
static uint8_t s_lastSavedDeviceValid = 0U;

static void nfc_app_ctrl_put_u16le(uint8_t out[2], uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void nfc_app_ctrl_put_u32le(uint8_t out[4], uint32_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
    out[2] = (uint8_t)((value >> 16) & 0xFFU);
    out[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static AppStatus_t nfc_app_ctrl_load_options(AppMeterServerFormatOptions_t *p_options)
{
    AppStatus_t status;

    if (p_options == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    status = App_MeterServerOptionsLoad(p_options);
    if (status == APP_STATUS_NOT_INITIALIZED)
    {
        App_MeterServerOptionsSetDefaults(p_options);
        return APP_STATUS_OK;
    }
    return status;
}

static AppStatus_t nfc_app_ctrl_load_device(AppDeviceConfig_t *p_config)
{
    AppStatus_t status;

    if (p_config == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    status = App_DeviceConfigLoad(p_config);
    if (status == APP_STATUS_NOT_INITIALIZED)
    {
        App_DeviceConfigSetDefaults(p_config);
        return APP_STATUS_OK;
    }
    return status;
}

static uint8_t nfc_app_ctrl_map_status(AppStatus_t status)
{
    switch (status)
    {
        case APP_STATUS_OK:
            return (uint8_t)NFC_APP_CTRL_OP_OK;
        case APP_STATUS_INVALID_PARAM:
            return (uint8_t)NFC_APP_CTRL_OP_RANGE_ERROR;
        case APP_STATUS_SELFTEST_FAILED:
            return (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
        default:
            return (uint8_t)NFC_APP_CTRL_OP_STORAGE_FAIL;
    }
}

static uint8_t nfc_app_ctrl_get_device_diag_profile(const AppDeviceConfig_t *p_device)
{
    return (p_device == NULL) ? 0U : p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_DIAG_PROFILE_IDX];
}

static uint8_t nfc_app_ctrl_get_device_reset_enable(const AppDeviceConfig_t *p_device)
{
    if (p_device == NULL)
    {
        return 1U;
    }
    return (uint8_t)(p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_ENABLE_IDX] != 0U ? 1U : 0U);
}

static void nfc_app_ctrl_set_device_diag_profile(AppDeviceConfig_t *p_device, uint8_t value)
{
    if (p_device != NULL)
    {
        p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_DIAG_PROFILE_IDX] = value;
    }
}

static void nfc_app_ctrl_set_device_reset_enable(AppDeviceConfig_t *p_device, uint8_t value)
{
    if (p_device != NULL)
    {
        p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_ENABLE_IDX] = (uint8_t)(value != 0U ? 1U : 0U);
    }
}

static void nfc_app_ctrl_set_reset_tracking(AppDeviceConfig_t *p_device, uint8_t bootCount, uint8_t reason)
{
    if (p_device == NULL)
    {
        return;
    }

    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_VALID_IDX] = NFC_APP_CTRL_RESET_TRACK_VALID_MARKER;
    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_BOOT_IDX] = bootCount;
    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_REASON_IDX] = reason;
}

static uint8_t nfc_app_ctrl_get_last_reset_reason(void)
{
    const AppNbiotCarrierContext_t *p_ctx = App_NBIoTCarrierGetContext();

    if (p_ctx == NULL)
    {
        return 0U;
    }

    switch (p_ctx->lastResetType)
    {
        case APP_NBIOT_CARRIER_RESET_SW:
            return 1U;
        case APP_NBIOT_CARRIER_RESET_HW:
            return 2U;
        default:
            return 0U;
    }
}

static void nfc_app_ctrl_store_shadow_options(const AppMeterServerFormatOptions_t *p_options)
{
    if (p_options != NULL)
    {
        s_lastSavedOptions = *p_options;
        s_lastSavedOptionsValid = 1U;
    }
}

static void nfc_app_ctrl_store_shadow_device(const AppDeviceConfig_t *p_device)
{
    if (p_device != NULL)
    {
        s_lastSavedDevice = *p_device;
        s_lastSavedDeviceValid = 1U;
    }
}

static uint8_t nfc_app_ctrl_is_binary_flag(uint8_t value)
{
    return (uint8_t)((value == 0U) || (value == 1U));
}

static uint8_t nfc_app_ctrl_is_supported_period(uint8_t period)
{
    return (uint8_t)((period == NFC_APP_CTRL_PERIOD_DISABLED) ||
                     (App_MeterServerOptionsIsPeriodSupported(period) != 0U));
}

static uint8_t nfc_app_ctrl_validate_cmd(const NfcAppCtrlCmd_t *p_cmd)
{
    if (p_cmd == NULL)
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }
    if ((p_cmd->xx != NFC_APP_CTRL_CMD_CLASS) || (p_cmd->reserved != 0U))
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
    switch (p_cmd->yy)
    {
        case NFC_APP_CTRL_GROUP_NB_CONTROL:
        case NFC_APP_CTRL_GROUP_SELFTEST:
        case NFC_APP_CTRL_GROUP_PARAMETER:
            return (uint8_t)NFC_CMD_RESULT_OK;
        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_validate_schedule_values(uint8_t metering, uint8_t reporting, uint8_t management, uint8_t spread)
{
    if ((nfc_app_ctrl_is_supported_period(metering) == 0U) ||
        (nfc_app_ctrl_is_supported_period(reporting) == 0U) ||
        (nfc_app_ctrl_is_supported_period(management) == 0U) ||
        (nfc_app_ctrl_is_supported_period(spread) == 0U))
    {
        return 0U;
    }
    if ((metering == 0U) && (reporting == 0U))
    {
        return 0U;
    }
    if ((reporting != 0U) && (metering != 0U) && (reporting < metering))
    {
        return 0U;
    }
    if ((spread != 0U) && (reporting != 0U) && (spread > reporting))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t nfc_app_ctrl_validate_policy_values(uint8_t ackWaitEnabled,
                                                   uint8_t ackTimeout100ms,
                                                   uint8_t ackPoll100ms,
                                                   uint8_t deleteAfterSend)
{
    if ((nfc_app_ctrl_is_binary_flag(ackWaitEnabled) == 0U) ||
        (nfc_app_ctrl_is_binary_flag(deleteAfterSend) == 0U))
    {
        return 0U;
    }
    if ((ackTimeout100ms == 0U) || (ackPoll100ms == 0U) || (ackPoll100ms > ackTimeout100ms))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t nfc_app_ctrl_validate_device_values(uint8_t logLevel,
                                                   uint8_t linkType,
                                                   uint8_t diagProfile,
                                                   uint8_t resetEnable)
{
    if (logLevel > NFC_APP_CTRL_MAX_LOG_LEVEL)
    {
        return 0U;
    }
    if ((linkType != NFC_APP_CTRL_LINK_TYPE_LORA) && (linkType != NFC_APP_CTRL_LINK_TYPE_NBIOT))
    {
        return 0U;
    }
    if (diagProfile > NFC_APP_CTRL_MAX_DIAG_PROFILE)
    {
        return 0U;
    }
    if (nfc_app_ctrl_is_binary_flag(resetEnable) == 0U)
    {
        return 0U;
    }
    return 1U;
}

static uint8_t nfc_app_ctrl_ack_timeout_sec_to_100ms(uint8_t ackTimeoutSec)
{
    uint16_t value = (uint16_t)ackTimeoutSec * 10U;
    if (value > 255U)
    {
        value = 255U;
    }
    return (uint8_t)value;
}

static uint8_t nfc_app_ctrl_ack_timeout_100ms_to_sec(uint8_t ackTimeout100ms)
{
    uint16_t rounded;

    if (ackTimeout100ms == 0U)
    {
        return 0U;
    }
    rounded = (uint16_t)ackTimeout100ms + 9U;
    rounded /= 10U;
    if (rounded == 0U)
    {
        rounded = 1U;
    }
    if (rounded > 255U)
    {
        rounded = 255U;
    }
    return (uint8_t)rounded;
}

static uint8_t nfc_app_ctrl_compare_schedule(const AppMeterServerFormatOptions_t *lhs,
                                             const AppMeterServerFormatOptions_t *rhs)
{
    uint8_t mismatch = 0U;

    mismatch += (uint8_t)(lhs->meteringPeriodHours != rhs->meteringPeriodHours);
    mismatch += (uint8_t)(lhs->reportingPeriodHours != rhs->reportingPeriodHours);
    mismatch += (uint8_t)(lhs->managementReportingPeriodHours != rhs->managementReportingPeriodHours);
    mismatch += (uint8_t)(lhs->reportingSpreadHours != rhs->reportingSpreadHours);
    return mismatch;
}

static uint8_t nfc_app_ctrl_compare_policy(const AppMeterServerFormatOptions_t *lhs,
                                           const AppMeterServerFormatOptions_t *rhs)
{
    uint8_t mismatch = 0U;

    mismatch += (uint8_t)(lhs->ackWaitEnabled != rhs->ackWaitEnabled);
    mismatch += (uint8_t)(lhs->ackTimeoutSec != rhs->ackTimeoutSec);
    mismatch += (uint8_t)(lhs->ackPoll100Ms != rhs->ackPoll100Ms);
    mismatch += (uint8_t)(lhs->deleteAfterSend != rhs->deleteAfterSend);
    return mismatch;
}

static uint8_t nfc_app_ctrl_compare_device(const AppDeviceConfig_t *lhs,
                                           const AppDeviceConfig_t *rhs)
{
    uint8_t mismatch = 0U;

    mismatch += (uint8_t)(lhs->logLevel != rhs->logLevel);
    mismatch += (uint8_t)(lhs->linkType != rhs->linkType);
    mismatch += (uint8_t)(nfc_app_ctrl_get_device_diag_profile(lhs) != nfc_app_ctrl_get_device_diag_profile(rhs));
    mismatch += (uint8_t)(nfc_app_ctrl_get_device_reset_enable(lhs) != nfc_app_ctrl_get_device_reset_enable(rhs));
    return mismatch;
}

static uint16_t nfc_app_ctrl_mask_from_context(const AppSelfTestContext_t *p_ctx, uint8_t passedOnly)
{
    uint16_t mask = 0U;
    uint8_t i;

    if (p_ctx == NULL)
    {
        return 0U;
    }

    for (i = 0U; i < (uint8_t)APP_SELFTEST_ITEM_COUNT; ++i)
    {
        if (p_ctx->items[i].executed != 0U)
        {
            if ((passedOnly == 0U) || (p_ctx->items[i].passed != 0U))
            {
                mask |= (uint16_t)(1U << i);
            }
        }
    }
    return mask;
}

static void nfc_app_ctrl_clear_selftest_context(void)
{
    AppSelfTestContext_t *p_ctx = (AppSelfTestContext_t *)App_SelfTestGetContext();

    if (p_ctx == NULL)
    {
        return;
    }

    (void)memset(p_ctx->items, 0, sizeof(p_ctx->items));
    p_ctx->running = 0U;
    p_ctx->passCount = 0U;
    p_ctx->failCount = 0U;
    p_ctx->lastRunTickMs = HAL_GetTick();
    p_ctx->lastSequenceStatus = APP_STATUS_OK;
}

static uint8_t nfc_app_ctrl_handle_nb(const NfcAppCtrlCmd_t *p_cmd,
                                      const NfcAppCtrlReqPayload_u *p_req,
                                      NfcAppCtrlRspPayload_u *p_rsp,
                                      uint8_t *p_rsp_len,
                                      uint8_t *p_op_status)
{
    AppMeterServerFormatOptions_t options;
    AppDeviceConfig_t device;
    AppStatus_t status;
    uint8_t verifyResult;

    switch (p_cmd->zz)
    {
        case NFC_APP_CTRL_NB_RESET_EXECUTE:
            if ((p_req->nbResetExecute.resetMode != NFC_APP_CTRL_RESET_MODE_IMMEDIATE) &&
                (p_req->nbResetExecute.resetMode != NFC_APP_CTRL_RESET_MODE_GRACEFUL))
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = nfc_app_ctrl_load_device(&device);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                *p_rsp_len = 0U;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if (nfc_app_ctrl_get_device_reset_enable(&device) == 0U)
            {
                p_rsp->nbResetExecute.accepted = 0U;
                p_rsp->nbResetExecute.appliedMode = 0U;
                *p_rsp_len = 2U;
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            nfc_app_ctrl_set_reset_tracking(&device,
                                            (uint8_t)(device.bootCount & 0xFFU),
                                            nfc_app_ctrl_get_last_reset_reason());
            status = App_DeviceConfigSave(&device);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status != APP_STATUS_OK)
            {
                *p_rsp_len = 0U;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            nfc_app_ctrl_store_shadow_device(&device);
            s_appCtrlPendingReset = true;
            s_appCtrlPendingResetDelayMs = (uint32_t)p_req->nbResetExecute.delay100ms * 100U;
            p_rsp->nbResetExecute.accepted = 1U;
            p_rsp->nbResetExecute.appliedMode = p_req->nbResetExecute.resetMode;
            *p_rsp_len = 2U;
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_PERIOD_SET:
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                *p_rsp_len = 0U;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if (nfc_app_ctrl_validate_schedule_values(p_req->nbPeriodSet.meteringPeriodHours,
                                                      p_req->nbPeriodSet.reportingPeriodHours,
                                                      options.managementReportingPeriodHours,
                                                      options.reportingSpreadHours) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            options.meteringPeriodHours = p_req->nbPeriodSet.meteringPeriodHours;
            options.reportingPeriodHours = p_req->nbPeriodSet.reportingPeriodHours;
            status = App_MeterServerOptionsValidate(&options);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsSave(&options);
            }
            verifyResult = (uint8_t)(status == APP_STATUS_OK ? 0U : 1U);
            if (status == APP_STATUS_OK)
            {
                nfc_app_ctrl_store_shadow_options(&options);
            }
            p_rsp->nbPeriodSet.meteringPeriodHours = options.meteringPeriodHours;
            p_rsp->nbPeriodSet.reportingPeriodHours = options.reportingPeriodHours;
            p_rsp->nbPeriodSet.verifyResult = verifyResult;
            *p_rsp_len = 3U;
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_PERIOD_GET:
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK)
            {
                p_rsp->nbPeriodGet.meteringPeriodHours = options.meteringPeriodHours;
                p_rsp->nbPeriodGet.reportingPeriodHours = options.reportingPeriodHours;
                *p_rsp_len = 2U;
            }
            else
            {
                *p_rsp_len = 0U;
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_SPREAD_SET:
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                *p_rsp_len = 0U;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if ((nfc_app_ctrl_is_supported_period(p_req->nbSpreadSet.reportingSpreadHours) == 0U) ||
                ((p_req->nbSpreadSet.reportingSpreadHours != 0U) &&
                 (p_req->nbSpreadSet.reportingSpreadHours > options.reportingPeriodHours)))
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            options.reportingSpreadHours = p_req->nbSpreadSet.reportingSpreadHours;
            status = App_MeterServerOptionsValidate(&options);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsSave(&options);
            }
            verifyResult = (uint8_t)(status == APP_STATUS_OK ? 0U : 1U);
            if (status == APP_STATUS_OK)
            {
                nfc_app_ctrl_store_shadow_options(&options);
            }
            p_rsp->nbSpreadSet.reportingSpreadHours = options.reportingSpreadHours;
            p_rsp->nbSpreadSet.verifyResult = verifyResult;
            *p_rsp_len = 2U;
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_SPREAD_GET:
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK)
            {
                p_rsp->nbSpreadGet.reportingSpreadHours = options.reportingSpreadHours;
                p_rsp->nbSpreadGet.reportingPeriodHours = options.reportingPeriodHours;
                *p_rsp_len = 2U;
            }
            else
            {
                *p_rsp_len = 0U;
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_ACK_STATUS_GET:
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK)
            {
                p_rsp->nbAckStatusGet.ackWaitEnabled = options.ackWaitEnabled;
                p_rsp->nbAckStatusGet.lastAckState = 0U;
                p_rsp->nbAckStatusGet.pendingTx = 0U;
                p_rsp->nbAckStatusGet.reserved0 = 0U;
                *p_rsp_len = 4U;
            }
            else
            {
                *p_rsp_len = 0U;
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_handle_selftest(const NfcAppCtrlCmd_t *p_cmd,
                                            const NfcAppCtrlReqPayload_u *p_req,
                                            NfcAppCtrlRspPayload_u *p_rsp,
                                            uint8_t *p_rsp_len,
                                            uint8_t *p_op_status)
{
    const AppSelfTestContext_t *p_ctx;
    AppStatus_t status;
    uint8_t itemId;
    uint16_t executedMask;
    uint16_t passedMask;

    (void)p_req;

    switch (p_cmd->zz)
    {
        case NFC_APP_CTRL_SELFTEST_RUN_QUICK:
        case NFC_APP_CTRL_SELFTEST_RUN_FULL:
            s_appCtrlDiagSeq++;
            if (s_appCtrlDiagSeq == 0U)
            {
                s_appCtrlDiagSeq = 1U;
            }
            status = App_SelfTestRunDataCollectionSequence();
            p_rsp->selftestRunQuick.diagSeq = s_appCtrlDiagSeq;
            p_rsp->selftestRunQuick.diagState = (status == APP_STATUS_OK) ?
                                                (uint8_t)NFC_APP_CTRL_DIAG_STATE_DONE :
                                                (uint8_t)NFC_APP_CTRL_DIAG_STATE_FAIL;
            p_rsp->selftestRunQuick.failCount = 0U;
            p_ctx = App_SelfTestGetContext();
            if (p_ctx != NULL)
            {
                p_rsp->selftestRunQuick.failCount = (uint8_t)(p_ctx->failCount & 0xFFU);
            }
            p_rsp->selftestRunQuick.reserved0 = 0U;
            *p_rsp_len = 4U;
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_SUMMARY_GET:
            p_ctx = App_SelfTestGetContext();
            if (p_ctx == NULL)
            {
                *p_rsp_len = 0U;
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            executedMask = nfc_app_ctrl_mask_from_context(p_ctx, 0U);
            passedMask = nfc_app_ctrl_mask_from_context(p_ctx, 1U);
            p_rsp->selftestSummaryGet.diagSeq = s_appCtrlDiagSeq;
            nfc_app_ctrl_put_u16le(p_rsp->selftestSummaryGet.executedMaskLe, executedMask);
            nfc_app_ctrl_put_u16le(p_rsp->selftestSummaryGet.passedMaskLe, passedMask);
            p_rsp->selftestSummaryGet.failCount = (uint8_t)(p_ctx->failCount & 0xFFU);
            nfc_app_ctrl_put_u16le(p_rsp->selftestSummaryGet.lastStatusLe, (uint16_t)p_ctx->lastSequenceStatus);
            *p_rsp_len = 8U;
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_DETAIL_GET:
            itemId = p_req->selftestDetailGet.itemId;
            if (itemId >= (uint8_t)APP_SELFTEST_ITEM_COUNT)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            p_ctx = App_SelfTestGetContext();
            if (p_ctx == NULL)
            {
                *p_rsp_len = 0U;
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            p_rsp->selftestDetailGet.itemId = itemId;
            p_rsp->selftestDetailGet.executed = p_ctx->items[itemId].executed;
            p_rsp->selftestDetailGet.passed = p_ctx->items[itemId].passed;
            nfc_app_ctrl_put_u16le(p_rsp->selftestDetailGet.statusLe, (uint16_t)p_ctx->items[itemId].status);
            nfc_app_ctrl_put_u32le(p_rsp->selftestDetailGet.tickMsLe, p_ctx->items[itemId].tickMs);
            *p_rsp_len = 9U;
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_RETRY_ITEM:
            itemId = p_req->selftestRetryItem.itemId;
            if (itemId >= (uint8_t)APP_SELFTEST_ITEM_COUNT)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            s_appCtrlDiagSeq++;
            if (s_appCtrlDiagSeq == 0U)
            {
                s_appCtrlDiagSeq = 1U;
            }
            status = App_SelfTestRunDataCollectionSequence();
            p_ctx = App_SelfTestGetContext();
            if (p_ctx == NULL)
            {
                *p_rsp_len = 0U;
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            p_rsp->selftestRetryItem.itemId = itemId;
            p_rsp->selftestRetryItem.executed = p_ctx->items[itemId].executed;
            p_rsp->selftestRetryItem.passed = p_ctx->items[itemId].passed;
            nfc_app_ctrl_put_u16le(p_rsp->selftestRetryItem.statusLe, (uint16_t)p_ctx->items[itemId].status);
            *p_rsp_len = 5U;
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_CLEAR:
            if (p_req->selftestClear.clearCode != NFC_APP_CTRL_CLEAR_CODE)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            nfc_app_ctrl_clear_selftest_context();
            p_rsp->selftestClear.cleared = 1U;
            *p_rsp_len = 1U;
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_handle_param_readback(const NfcReqParamReadbackGet_t *p_req,
                                                  NfcAppCtrlRspPayload_u *p_rsp,
                                                  uint8_t *p_rsp_len,
                                                  uint8_t *p_op_status)
{
    AppMeterServerFormatOptions_t options;
    AppDeviceConfig_t device;
    AppStatus_t status;
    uint8_t targetGroup = p_req->targetGroup;
    uint8_t mismatchCount = 0U;
    uint8_t lastError = 0U;

    if ((targetGroup != NFC_APP_CTRL_READBACK_GROUP_ALL) &&
        (targetGroup != NFC_APP_CTRL_READBACK_GROUP_SCHEDULE) &&
        (targetGroup != NFC_APP_CTRL_READBACK_GROUP_POLICY) &&
        (targetGroup != NFC_APP_CTRL_READBACK_GROUP_DEVICE))
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }

    *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;

    if ((targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
        (targetGroup == NFC_APP_CTRL_READBACK_GROUP_SCHEDULE) ||
        (targetGroup == NFC_APP_CTRL_READBACK_GROUP_POLICY))
    {
        status = nfc_app_ctrl_load_options(&options);
        if (status != APP_STATUS_OK)
        {
            *p_op_status = nfc_app_ctrl_map_status(status);
            lastError = 1U;
        }
        else if (s_lastSavedOptionsValid == 0U)
        {
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
            lastError = 2U;
        }
        else
        {
            if ((targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                (targetGroup == NFC_APP_CTRL_READBACK_GROUP_SCHEDULE))
            {
                mismatchCount = (uint8_t)(mismatchCount + nfc_app_ctrl_compare_schedule(&options, &s_lastSavedOptions));
            }
            if ((targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                (targetGroup == NFC_APP_CTRL_READBACK_GROUP_POLICY))
            {
                mismatchCount = (uint8_t)(mismatchCount + nfc_app_ctrl_compare_policy(&options, &s_lastSavedOptions));
            }
        }
    }

    if ((targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
        (targetGroup == NFC_APP_CTRL_READBACK_GROUP_DEVICE))
    {
        status = nfc_app_ctrl_load_device(&device);
        if (status != APP_STATUS_OK)
        {
            *p_op_status = nfc_app_ctrl_map_status(status);
            lastError = 3U;
        }
        else if (s_lastSavedDeviceValid == 0U)
        {
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
            lastError = 4U;
        }
        else
        {
            mismatchCount = (uint8_t)(mismatchCount + nfc_app_ctrl_compare_device(&device, &s_lastSavedDevice));
        }
    }

    p_rsp->paramReadbackGet.mismatchCount = mismatchCount;
    p_rsp->paramReadbackGet.lastError = lastError;
    p_rsp->paramReadbackGet.reserved0 = 0U;
    p_rsp->paramReadbackGet.reserved1 = 0U;
    *p_rsp_len = 4U;
    return (uint8_t)NFC_CMD_RESULT_OK;
}

static uint8_t nfc_app_ctrl_handle_param(const NfcAppCtrlCmd_t *p_cmd,
                                         const NfcAppCtrlReqPayload_u *p_req,
                                         NfcAppCtrlRspPayload_u *p_rsp,
                                         uint8_t *p_rsp_len,
                                         uint8_t *p_op_status)
{
    AppMeterServerFormatOptions_t options;
    AppMeterServerFormatOptions_t verifyOptions;
    AppMeterServerFormatOptions_t defaults;
    AppDeviceConfig_t device;
    AppDeviceConfig_t verifyDevice;
    AppDeviceConfig_t defaultDevice;
    AppStatus_t status;
    uint8_t verifyResult;
    uint8_t applyMask;
    uint8_t timeout100ms;

    switch (p_cmd->zz)
    {
        case NFC_APP_CTRL_PARAM_SCHEDULE_GET:
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK)
            {
                p_rsp->paramScheduleGet.meteringPeriodHours = options.meteringPeriodHours;
                p_rsp->paramScheduleGet.reportingPeriodHours = options.reportingPeriodHours;
                p_rsp->paramScheduleGet.managementReportingPeriodHours = options.managementReportingPeriodHours;
                p_rsp->paramScheduleGet.reportingSpreadHours = options.reportingSpreadHours;
                p_rsp->paramScheduleGet.flags = 0U;
                *p_rsp_len = 5U;
            }
            else
            {
                *p_rsp_len = 0U;
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_SCHEDULE_SET:
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_rsp_len = 0U;
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            applyMask = p_req->paramScheduleSet.applyMask;
            if ((applyMask & NFC_APP_CTRL_APPLY_METERING_BIT) != 0U) { options.meteringPeriodHours = p_req->paramScheduleSet.meteringPeriodHours; }
            if ((applyMask & NFC_APP_CTRL_APPLY_REPORTING_BIT) != 0U) { options.reportingPeriodHours = p_req->paramScheduleSet.reportingPeriodHours; }
            if ((applyMask & NFC_APP_CTRL_APPLY_MGMT_BIT) != 0U) { options.managementReportingPeriodHours = p_req->paramScheduleSet.managementReportingPeriodHours; }
            if ((applyMask & NFC_APP_CTRL_APPLY_SPREAD_BIT) != 0U) { options.reportingSpreadHours = p_req->paramScheduleSet.reportingSpreadHours; }
            if (nfc_app_ctrl_validate_schedule_values(options.meteringPeriodHours,
                                                      options.reportingPeriodHours,
                                                      options.managementReportingPeriodHours,
                                                      options.reportingSpreadHours) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = App_MeterServerOptionsValidate(&options);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsSave(&options);
            }
            verifyResult = 1U;
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsLoad(&verifyOptions);
                if (status == APP_STATUS_OK)
                {
                    verifyResult = (uint8_t)(nfc_app_ctrl_compare_schedule(&verifyOptions, &options) == 0U ? 0U : 1U);
                    nfc_app_ctrl_store_shadow_options(&options);
                }
            }
            p_rsp->paramScheduleSet.meteringPeriodHours = options.meteringPeriodHours;
            p_rsp->paramScheduleSet.reportingPeriodHours = options.reportingPeriodHours;
            p_rsp->paramScheduleSet.managementReportingPeriodHours = options.managementReportingPeriodHours;
            p_rsp->paramScheduleSet.reportingSpreadHours = options.reportingSpreadHours;
            p_rsp->paramScheduleSet.verifyResult = verifyResult;
            *p_rsp_len = 5U;
            *p_op_status = (verifyResult == 0U) ? (uint8_t)NFC_APP_CTRL_OP_OK : nfc_app_ctrl_map_status(status);
            if ((verifyResult != 0U) && (status == APP_STATUS_OK))
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_POLICY_GET:
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK)
            {
                timeout100ms = nfc_app_ctrl_ack_timeout_sec_to_100ms(options.ackTimeoutSec);
                p_rsp->paramPolicyGet.ackWaitEnabled = options.ackWaitEnabled;
                p_rsp->paramPolicyGet.ackTimeout100ms = timeout100ms;
                p_rsp->paramPolicyGet.ackPoll100ms = options.ackPoll100Ms;
                p_rsp->paramPolicyGet.deleteAfterSend = options.deleteAfterSend;
                p_rsp->paramPolicyGet.flags = 0U;
                *p_rsp_len = 5U;
            }
            else
            {
                *p_rsp_len = 0U;
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_POLICY_SET:
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_rsp_len = 0U;
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            applyMask = p_req->paramPolicySet.applyMask;
            if ((applyMask & NFC_APP_CTRL_APPLY_ACK_WAIT_BIT) != 0U) { options.ackWaitEnabled = p_req->paramPolicySet.ackWaitEnabled; }
            if ((applyMask & NFC_APP_CTRL_APPLY_ACK_TIMEOUT_BIT) != 0U) { options.ackTimeoutSec = nfc_app_ctrl_ack_timeout_100ms_to_sec(p_req->paramPolicySet.ackTimeout100ms); }
            if ((applyMask & NFC_APP_CTRL_APPLY_ACK_POLL_BIT) != 0U) { options.ackPoll100Ms = p_req->paramPolicySet.ackPoll100ms; }
            if ((applyMask & NFC_APP_CTRL_APPLY_DELETE_AFTER_SEND_BIT) != 0U) { options.deleteAfterSend = p_req->paramPolicySet.deleteAfterSend; }
            if (nfc_app_ctrl_validate_policy_values(options.ackWaitEnabled,
                                                    nfc_app_ctrl_ack_timeout_sec_to_100ms(options.ackTimeoutSec),
                                                    options.ackPoll100Ms,
                                                    options.deleteAfterSend) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = App_MeterServerOptionsSave(&options);
            verifyResult = 1U;
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsLoad(&verifyOptions);
                if (status == APP_STATUS_OK)
                {
                    verifyResult = (uint8_t)(nfc_app_ctrl_compare_policy(&verifyOptions, &options) == 0U ? 0U : 1U);
                    nfc_app_ctrl_store_shadow_options(&options);
                }
            }
            p_rsp->paramPolicySet.ackWaitEnabled = options.ackWaitEnabled;
            p_rsp->paramPolicySet.ackTimeout100ms = nfc_app_ctrl_ack_timeout_sec_to_100ms(options.ackTimeoutSec);
            p_rsp->paramPolicySet.ackPoll100ms = options.ackPoll100Ms;
            p_rsp->paramPolicySet.deleteAfterSend = options.deleteAfterSend;
            p_rsp->paramPolicySet.verifyResult = verifyResult;
            *p_rsp_len = 5U;
            *p_op_status = (verifyResult == 0U) ? (uint8_t)NFC_APP_CTRL_OP_OK : nfc_app_ctrl_map_status(status);
            if ((verifyResult != 0U) && (status == APP_STATUS_OK))
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_DEVICE_GET:
            status = nfc_app_ctrl_load_device(&device);
            if (status == APP_STATUS_OK)
            {
                p_rsp->paramDeviceGet.logLevel = device.logLevel;
                p_rsp->paramDeviceGet.linkType = device.linkType;
                p_rsp->paramDeviceGet.diagProfile = nfc_app_ctrl_get_device_diag_profile(&device);
                p_rsp->paramDeviceGet.resetEnable = nfc_app_ctrl_get_device_reset_enable(&device);
                p_rsp->paramDeviceGet.flags = 0U;
                *p_rsp_len = 5U;
            }
            else
            {
                *p_rsp_len = 0U;
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_DEVICE_SET:
            status = nfc_app_ctrl_load_device(&device);
            if (status != APP_STATUS_OK)
            {
                *p_rsp_len = 0U;
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            applyMask = p_req->paramDeviceSet.applyMask;
            if ((applyMask & NFC_APP_CTRL_APPLY_LOG_LEVEL_BIT) != 0U) { device.logLevel = p_req->paramDeviceSet.logLevel; }
            if ((applyMask & NFC_APP_CTRL_APPLY_LINK_TYPE_BIT) != 0U) { device.linkType = p_req->paramDeviceSet.linkType; }
            if ((applyMask & NFC_APP_CTRL_APPLY_DIAG_PROFILE_BIT) != 0U) { nfc_app_ctrl_set_device_diag_profile(&device, p_req->paramDeviceSet.diagProfile); }
            if ((applyMask & NFC_APP_CTRL_APPLY_RESET_ENABLE_BIT) != 0U) { nfc_app_ctrl_set_device_reset_enable(&device, p_req->paramDeviceSet.resetEnable); }
            if (nfc_app_ctrl_validate_device_values(device.logLevel,
                                                    device.linkType,
                                                    nfc_app_ctrl_get_device_diag_profile(&device),
                                                    nfc_app_ctrl_get_device_reset_enable(&device)) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = App_DeviceConfigSave(&device);
            verifyResult = 1U;
            if (status == APP_STATUS_OK)
            {
                status = App_DeviceConfigLoad(&verifyDevice);
                if (status == APP_STATUS_OK)
                {
                    verifyResult = (uint8_t)(nfc_app_ctrl_compare_device(&verifyDevice, &device) == 0U ? 0U : 1U);
                    nfc_app_ctrl_store_shadow_device(&device);
                }
            }
            p_rsp->paramDeviceSet.logLevel = device.logLevel;
            p_rsp->paramDeviceSet.linkType = device.linkType;
            p_rsp->paramDeviceSet.diagProfile = nfc_app_ctrl_get_device_diag_profile(&device);
            p_rsp->paramDeviceSet.resetEnable = nfc_app_ctrl_get_device_reset_enable(&device);
            p_rsp->paramDeviceSet.verifyResult = verifyResult;
            *p_rsp_len = 5U;
            *p_op_status = (verifyResult == 0U) ? (uint8_t)NFC_APP_CTRL_OP_OK : nfc_app_ctrl_map_status(status);
            if ((verifyResult != 0U) && (status == APP_STATUS_OK))
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_RESTORE_DEFAULT:
            if (p_req->paramRestoreDefault.restoreCode != NFC_APP_CTRL_RESTORE_CODE)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = APP_STATUS_OK;
            if ((p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                (p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_SCHEDULE) ||
                (p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_POLICY))
            {
                status = nfc_app_ctrl_load_options(&options);
                if (status == APP_STATUS_OK)
                {
                    App_MeterServerOptionsSetDefaults(&defaults);
                    if ((p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                        (p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_SCHEDULE))
                    {
                        options.meteringPeriodHours = defaults.meteringPeriodHours;
                        options.reportingPeriodHours = defaults.reportingPeriodHours;
                        options.managementReportingPeriodHours = defaults.managementReportingPeriodHours;
                        options.reportingSpreadHours = defaults.reportingSpreadHours;
                    }
                    if ((p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                        (p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_POLICY))
                    {
                        options.ackWaitEnabled = defaults.ackWaitEnabled;
                        options.ackTimeoutSec = defaults.ackTimeoutSec;
                        options.ackPoll100Ms = defaults.ackPoll100Ms;
                        options.deleteAfterSend = defaults.deleteAfterSend;
                    }
                    status = App_MeterServerOptionsSave(&options);
                    if (status == APP_STATUS_OK)
                    {
                        nfc_app_ctrl_store_shadow_options(&options);
                    }
                }
            }
            if ((status == APP_STATUS_OK) &&
                ((p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                 (p_req->paramRestoreDefault.targetGroup == NFC_APP_CTRL_READBACK_GROUP_DEVICE)))
            {
                App_DeviceConfigSetDefaults(&defaultDevice);
                nfc_app_ctrl_set_device_diag_profile(&defaultDevice, 0U);
                nfc_app_ctrl_set_device_reset_enable(&defaultDevice, 1U);
                status = App_DeviceConfigSave(&defaultDevice);
                if (status == APP_STATUS_OK)
                {
                    nfc_app_ctrl_store_shadow_device(&defaultDevice);
                }
            }
            p_rsp->paramRestoreDefault.targetGroup = p_req->paramRestoreDefault.targetGroup;
            p_rsp->paramRestoreDefault.verifyResult = (uint8_t)(status == APP_STATUS_OK ? 0U : 1U);
            *p_rsp_len = 2U;
            *p_op_status = nfc_app_ctrl_map_status(status);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_READBACK_GET:
            return nfc_app_ctrl_handle_param_readback(&p_req->paramReadbackGet,
                                                      p_rsp,
                                                      p_rsp_len,
                                                      p_op_status);

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

uint8_t NfcAppCtrl_Execute(const NfcAppCtrlCmd_t *p_cmd,
                           const uint8_t *p_req_raw,
                           uint8_t *p_rsp_raw,
                           uint8_t *p_rsp_len,
                           uint8_t *p_op_status)
{
    const NfcAppCtrlReqPayload_u *p_req;
    NfcAppCtrlRspPayload_u *p_rsp;
    uint8_t ret;

    if ((p_cmd == NULL) || (p_req_raw == NULL) || (p_rsp_raw == NULL) ||
        (p_rsp_len == NULL) || (p_op_status == NULL))
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }

    ret = nfc_app_ctrl_validate_cmd(p_cmd);
    if (ret != (uint8_t)NFC_CMD_RESULT_OK)
    {
        return ret;
    }

    p_req = (const NfcAppCtrlReqPayload_u *)(const void *)p_req_raw;
    p_rsp = (NfcAppCtrlRspPayload_u *)(void *)p_rsp_raw;

    (void)memset(p_rsp_raw, 0, NFC_APP_CTRL_RSP_MAX_LEN);
    *p_rsp_len = 0U;
    *p_op_status = (uint8_t)NFC_APP_CTRL_OP_FAIL;

    APP_LOGI("NFC", "AppCtrl cmd=%02X %02X %02X %02X",
             (unsigned int)p_cmd->xx,
             (unsigned int)p_cmd->yy,
             (unsigned int)p_cmd->zz,
             (unsigned int)p_cmd->reserved);

    switch (p_cmd->yy)
    {
        case NFC_APP_CTRL_GROUP_NB_CONTROL:
            return nfc_app_ctrl_handle_nb(p_cmd, p_req, p_rsp, p_rsp_len, p_op_status);
        case NFC_APP_CTRL_GROUP_SELFTEST:
            return nfc_app_ctrl_handle_selftest(p_cmd, p_req, p_rsp, p_rsp_len, p_op_status);
        case NFC_APP_CTRL_GROUP_PARAMETER:
            return nfc_app_ctrl_handle_param(p_cmd, p_req, p_rsp, p_rsp_len, p_op_status);
        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

bool NfcAppCtrl_ConsumePendingReset(void)
{
    bool pending = s_appCtrlPendingReset;
    s_appCtrlPendingReset = false;
    return pending;
}

uint32_t NfcAppCtrl_GetPendingResetDelayMs(void)
{
    return s_appCtrlPendingResetDelayMs;
}

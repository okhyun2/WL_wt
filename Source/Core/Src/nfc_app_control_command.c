#include "nfc_app_control_command.h"

#include <string.h>

#include "app_log.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"
#include "app_nbiot.h"
#include "app_selftest.h"
#include "nfc_user_command.h"

#define NFC_APP_CTRL_DEVICE_RESERVED_DIAG_PROFILE_IDX        0U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_ENABLE_IDX        1U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_VALID_IDX   2U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_BOOT_IDX    3U
#define NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_REASON_IDX  4U

#define NFC_APP_CTRL_RESET_TRACK_VALID_MARKER                0xA5U
#define NFC_APP_CTRL_CLEAR_CODE                              0xA5U
#define NFC_APP_CTRL_RESTORE_CODE                            0xA5U

#define NFC_APP_CTRL_APPLY_METERING_BIT                      (1U << 0)
#define NFC_APP_CTRL_APPLY_REPORTING_BIT                     (1U << 1)
#define NFC_APP_CTRL_APPLY_MGMT_BIT                          (1U << 2)
#define NFC_APP_CTRL_APPLY_SPREAD_BIT                        (1U << 3)
#define NFC_APP_CTRL_APPLY_ACK_WAIT_BIT                      (1U << 0)
#define NFC_APP_CTRL_APPLY_ACK_TIMEOUT_BIT                   (1U << 1)
#define NFC_APP_CTRL_APPLY_ACK_POLL_BIT                      (1U << 2)
#define NFC_APP_CTRL_APPLY_DELETE_AFTER_SEND_BIT             (1U << 3)
#define NFC_APP_CTRL_APPLY_LOG_LEVEL_BIT                     (1U << 0)
#define NFC_APP_CTRL_APPLY_LINK_TYPE_BIT                     (1U << 1)
#define NFC_APP_CTRL_APPLY_DIAG_PROFILE_BIT                  (1U << 2)
#define NFC_APP_CTRL_APPLY_RESET_ENABLE_BIT                  (1U << 3)

#define NFC_APP_CTRL_READBACK_GROUP_ALL                      0x00U
#define NFC_APP_CTRL_READBACK_GROUP_SCHEDULE                 0x01U
#define NFC_APP_CTRL_READBACK_GROUP_POLICY                   0x02U
#define NFC_APP_CTRL_READBACK_GROUP_DEVICE                   0x03U

#define NFC_APP_CTRL_MAX_LOG_LEVEL                           5U
#define NFC_APP_CTRL_MAX_DIAG_PROFILE                        3U
#define NFC_APP_CTRL_LINK_TYPE_LORA                          0U
#define NFC_APP_CTRL_LINK_TYPE_NBIOT                         1U

static bool s_appCtrlPendingReset = false;
static uint32_t s_appCtrlPendingResetDelayMs = 0U;
static uint8_t s_appCtrlDiagSeq = 0U;
static AppMeterServerFormatOptions_t s_lastSavedOptions;
static AppDeviceConfig_t s_lastSavedDevice;
static uint8_t s_lastSavedOptionsValid = 0U;
static uint8_t s_lastSavedDeviceValid = 0U;

static void nfc_app_ctrl_response_reset(uint8_t *p_rsp_payload,
                                        uint8_t *p_rsp_len)
{
    if ((p_rsp_payload == NULL) || (p_rsp_len == NULL))
    {
        return;
    }

    (void)memset(p_rsp_payload, 0, NFC_APP_CTRL_RSP_MAX_LEN);
    *p_rsp_len = 0U;
}

static uint8_t nfc_app_ctrl_append_byte(uint8_t *p_rsp_payload,
                                        uint8_t *p_rsp_len,
                                        uint8_t value)
{
    if ((p_rsp_payload == NULL) || (p_rsp_len == NULL))
    {
        return 0U;
    }
    if (*p_rsp_len >= NFC_APP_CTRL_RSP_MAX_LEN)
    {
        return 0U;
    }

    p_rsp_payload[*p_rsp_len] = value;
    (*p_rsp_len)++;
    return 1U;
}

static uint8_t nfc_app_ctrl_append_u16le(uint8_t *p_rsp_payload,
                                         uint8_t *p_rsp_len,
                                         uint16_t value)
{
    return (uint8_t)(nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)(value & 0xFFU)) &&
                     nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)((value >> 8) & 0xFFU)));
}

static uint8_t nfc_app_ctrl_append_u32le(uint8_t *p_rsp_payload,
                                         uint8_t *p_rsp_len,
                                         uint32_t value)
{
    return (uint8_t)(nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)(value & 0xFFU)) &&
                     nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)((value >> 8) & 0xFFU)) &&
                     nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)((value >> 16) & 0xFFU)) &&
                     nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)((value >> 24) & 0xFFU)));
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
    if (p_cmd->xx != NFC_APP_CTRL_CMD_CLASS)
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
    if (p_cmd->reserved != 0x00U)
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
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
        case APP_STATUS_NOT_INITIALIZED:
            return (uint8_t)NFC_APP_CTRL_OP_FAIL;
        case APP_STATUS_SELFTEST_FAILED:
            return (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
        default:
            return (uint8_t)NFC_APP_CTRL_OP_STORAGE_FAIL;
    }
}

static uint8_t nfc_app_ctrl_get_device_diag_profile(const AppDeviceConfig_t *p_device)
{
    if (p_device == NULL)
    {
        return 0U;
    }
    return p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_DIAG_PROFILE_IDX];
}

static uint8_t nfc_app_ctrl_get_device_reset_enable(const AppDeviceConfig_t *p_device)
{
    if (p_device == NULL)
    {
        return 1U;
    }
    return (uint8_t)(p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_ENABLE_IDX] != 0U ? 1U : 0U);
}

static void nfc_app_ctrl_set_device_diag_profile(AppDeviceConfig_t *p_device,
                                                 uint8_t diagProfile)
{
    if (p_device == NULL)
    {
        return;
    }
    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_DIAG_PROFILE_IDX] = diagProfile;
}

static void nfc_app_ctrl_set_device_reset_enable(AppDeviceConfig_t *p_device,
                                                 uint8_t resetEnable)
{
    if (p_device == NULL)
    {
        return;
    }
    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_ENABLE_IDX] = (uint8_t)(resetEnable != 0U ? 1U : 0U);
}

static void nfc_app_ctrl_set_reset_tracking(AppDeviceConfig_t *p_device,
                                            uint8_t lastBootCount,
                                            uint8_t lastResetReason)
{
    if (p_device == NULL)
    {
        return;
    }

    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_VALID_IDX] = NFC_APP_CTRL_RESET_TRACK_VALID_MARKER;
    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_BOOT_IDX] = lastBootCount;
    p_device->reserved[NFC_APP_CTRL_DEVICE_RESERVED_RESET_TRACK_REASON_IDX] = lastResetReason;
}

static uint8_t nfc_app_ctrl_get_last_reset_reason_from_context(void)
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

static uint8_t nfc_app_ctrl_get_attach_state_byte(void)
{
    const AppNbiotCarrierContext_t *p_ctx = App_NBIoTCarrierGetContext();

    if (p_ctx == NULL)
    {
        return 0U;
    }

    return (uint8_t)p_ctx->attachState;
}

static uint8_t nfc_app_ctrl_get_last_ack_state_byte(void)
{
    return 0U;
}

static uint8_t nfc_app_ctrl_get_pending_tx_byte(void)
{
    return (uint8_t)((App_MeterStorageCount() != 0U) ? 1U : 0U);
}

static uint16_t nfc_app_ctrl_mask_from_context(const AppSelfTestContext_t *p_ctx,
                                               uint8_t passedOnly)
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

static uint8_t nfc_app_ctrl_response_verify(uint8_t actual,
                                            uint8_t expected)
{
    return (uint8_t)((actual == expected) ? 0U : 1U);
}

static uint8_t nfc_app_ctrl_ack_timeout_sec_to_100ms(uint8_t ackTimeoutSec)
{
    uint16_t value100ms = (uint16_t)ackTimeoutSec * 10U;

    if (value100ms > 255U)
    {
        value100ms = 255U;
    }
    return (uint8_t)value100ms;
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

static void nfc_app_ctrl_shadow_store_options(const AppMeterServerFormatOptions_t *p_options)
{
    if (p_options == NULL)
    {
        return;
    }

    s_lastSavedOptions = *p_options;
    s_lastSavedOptionsValid = 1U;
}

static void nfc_app_ctrl_shadow_store_device(const AppDeviceConfig_t *p_device)
{
    if (p_device == NULL)
    {
        return;
    }

    s_lastSavedDevice = *p_device;
    s_lastSavedDeviceValid = 1U;
}

static uint8_t nfc_app_ctrl_compare_schedule(const AppMeterServerFormatOptions_t *p_lhs,
                                             const AppMeterServerFormatOptions_t *p_rhs)
{
    uint8_t mismatch = 0U;

    if ((p_lhs == NULL) || (p_rhs == NULL))
    {
        return 1U;
    }

    mismatch += (uint8_t)(p_lhs->meteringPeriodHours != p_rhs->meteringPeriodHours);
    mismatch += (uint8_t)(p_lhs->reportingPeriodHours != p_rhs->reportingPeriodHours);
    mismatch += (uint8_t)(p_lhs->managementReportingPeriodHours != p_rhs->managementReportingPeriodHours);
    mismatch += (uint8_t)(p_lhs->reportingSpreadHours != p_rhs->reportingSpreadHours);
    return mismatch;
}

static uint8_t nfc_app_ctrl_compare_policy(const AppMeterServerFormatOptions_t *p_lhs,
                                           const AppMeterServerFormatOptions_t *p_rhs)
{
    uint8_t mismatch = 0U;

    if ((p_lhs == NULL) || (p_rhs == NULL))
    {
        return 1U;
    }

    mismatch += (uint8_t)(p_lhs->ackWaitEnabled != p_rhs->ackWaitEnabled);
    mismatch += (uint8_t)(p_lhs->ackTimeoutSec != p_rhs->ackTimeoutSec);
    mismatch += (uint8_t)(p_lhs->ackPoll100Ms != p_rhs->ackPoll100Ms);
    mismatch += (uint8_t)(p_lhs->deleteAfterSend != p_rhs->deleteAfterSend);
    return mismatch;
}

static uint8_t nfc_app_ctrl_compare_device(const AppDeviceConfig_t *p_lhs,
                                           const AppDeviceConfig_t *p_rhs)
{
    uint8_t mismatch = 0U;

    if ((p_lhs == NULL) || (p_rhs == NULL))
    {
        return 1U;
    }

    mismatch += (uint8_t)(p_lhs->logLevel != p_rhs->logLevel);
    mismatch += (uint8_t)(p_lhs->linkType != p_rhs->linkType);
    mismatch += (uint8_t)(nfc_app_ctrl_get_device_diag_profile(p_lhs) != nfc_app_ctrl_get_device_diag_profile(p_rhs));
    mismatch += (uint8_t)(nfc_app_ctrl_get_device_reset_enable(p_lhs) != nfc_app_ctrl_get_device_reset_enable(p_rhs));
    return mismatch;
}

static uint8_t nfc_app_ctrl_validate_schedule_values(uint8_t metering,
                                                     uint8_t reporting,
                                                     uint8_t management,
                                                     uint8_t spread)
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
    if (reporting < metering)
    {
        return 0U;
    }
    if ((spread != 0U) && (spread > reporting))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t nfc_app_ctrl_validate_policy_values(uint8_t ackWaitEn,
                                                   uint8_t ackTimeout100ms,
                                                   uint8_t ackPoll100ms,
                                                   uint8_t deleteAfterSend)
{
    if ((nfc_app_ctrl_is_binary_flag(ackWaitEn) == 0U) ||
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

static uint8_t nfc_app_ctrl_handle_nb(const NfcAppCtrlCmd_t *p_cmd,
                                      const uint8_t *p_body,
                                      uint8_t body_len,
                                      uint8_t *p_rsp_payload,
                                      uint8_t *p_rsp_len,
                                      uint8_t *p_op_status)
{
    AppMeterServerFormatOptions_t options;
    AppDeviceConfig_t device;
    AppStatus_t status;
    uint8_t currentBootCount = 0U;
    uint8_t resetMode;
    uint8_t grace100ms;
    uint8_t verifyResult;

    (void)p_cmd;

    switch (p_cmd->zz)
    {
        case NFC_APP_CTRL_NB_RESET_EXECUTE:
            if (body_len < 2U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            resetMode = p_body[0];
            grace100ms = p_body[1];
            if ((resetMode != NFC_APP_CTRL_RESET_MODE_IMMEDIATE) &&
                (resetMode != NFC_APP_CTRL_RESET_MODE_GRACEFUL))
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = nfc_app_ctrl_load_device(&device);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if (nfc_app_ctrl_get_device_reset_enable(&device) == 0U)
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x00U);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x00U);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            currentBootCount = (uint8_t)(device.bootCount & 0xFFU);
            nfc_app_ctrl_set_reset_tracking(&device,
                                            currentBootCount,
                                            nfc_app_ctrl_get_last_reset_reason_from_context());
            status = App_DeviceConfigSave(&device);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            nfc_app_ctrl_shadow_store_device(&device);
            s_appCtrlPendingReset = true;
            s_appCtrlPendingResetDelayMs = (uint32_t)grace100ms * 100U;
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x01U);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len,
                                           (uint8_t)(resetMode == NFC_APP_CTRL_RESET_MODE_GRACEFUL ? 1U : 0U));
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_PERIOD_SET:
            if (body_len < 2U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if (nfc_app_ctrl_validate_schedule_values(p_body[0], p_body[1],
                                                      options.managementReportingPeriodHours,
                                                      options.reportingSpreadHours) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            options.meteringPeriodHours = p_body[0];
            options.reportingPeriodHours = p_body[1];
            status = App_MeterServerOptionsValidate(&options);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsSave(&options);
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            verifyResult = (status == APP_STATUS_OK) ? 0U : 1U;
            if (status == APP_STATUS_OK)
            {
                nfc_app_ctrl_shadow_store_options(&options);
            }
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.meteringPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, verifyResult);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_PERIOD_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.meteringPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingPeriodHours);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_SPREAD_SET:
            if (body_len < 1U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if ((nfc_app_ctrl_is_supported_period(p_body[0]) == 0U) ||
                ((p_body[0] != 0U) && (p_body[0] > options.reportingPeriodHours)))
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            options.reportingSpreadHours = p_body[0];
            status = App_MeterServerOptionsValidate(&options);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsSave(&options);
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            verifyResult = (status == APP_STATUS_OK) ? 0U : 1U;
            if (status == APP_STATUS_OK)
            {
                nfc_app_ctrl_shadow_store_options(&options);
            }
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingSpreadHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, verifyResult);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_SPREAD_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingSpreadHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingPeriodHours);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_ACK_STATUS_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.ackWaitEnabled);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, nfc_app_ctrl_get_last_ack_state_byte());
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, nfc_app_ctrl_get_pending_tx_byte());
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x00U);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_append_diag_run_result(uint8_t *p_rsp_payload,
                                                    uint8_t *p_rsp_len,
                                                    AppStatus_t runStatus)
{
    const AppSelfTestContext_t *p_ctx = App_SelfTestGetContext();
    uint8_t failCount = 0U;
    uint8_t state = (runStatus == APP_STATUS_OK) ? (uint8_t)NFC_APP_CTRL_DIAG_STATE_DONE
                                                  : (uint8_t)NFC_APP_CTRL_DIAG_STATE_FAIL;

    if (p_ctx != NULL)
    {
        failCount = (uint8_t)(p_ctx->failCount & 0xFFU);
    }

    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, s_appCtrlDiagSeq);
    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, state);
    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, failCount);
    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x00U);
    return (uint8_t)NFC_CMD_RESULT_OK;
}

static uint8_t nfc_app_ctrl_handle_selftest(const NfcAppCtrlCmd_t *p_cmd,
                                            const uint8_t *p_body,
                                            uint8_t body_len,
                                            uint8_t *p_rsp_payload,
                                            uint8_t *p_rsp_len,
                                            uint8_t *p_op_status)
{
    const AppSelfTestContext_t *p_ctx;
    AppStatus_t status;
    uint8_t itemId;
    uint16_t executedMask;
    uint16_t passedMask;

    switch (p_cmd->zz)
    {
        case NFC_APP_CTRL_SELFTEST_RUN_QUICK:
        case NFC_APP_CTRL_SELFTEST_RUN_FULL:
            if ((body_len != 0U) && (body_len < 2U))
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            s_appCtrlDiagSeq++;
            if (s_appCtrlDiagSeq == 0U)
            {
                s_appCtrlDiagSeq = 1U;
            }
            status = App_SelfTestRunDataCollectionSequence();
            *p_op_status = nfc_app_ctrl_map_status(status);
            return nfc_app_ctrl_append_diag_run_result(p_rsp_payload, p_rsp_len, status);

        case NFC_APP_CTRL_SELFTEST_SUMMARY_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            p_ctx = App_SelfTestGetContext();
            if (p_ctx == NULL)
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            executedMask = nfc_app_ctrl_mask_from_context(p_ctx, 0U);
            passedMask = nfc_app_ctrl_mask_from_context(p_ctx, 1U);
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, s_appCtrlDiagSeq);
            (void)nfc_app_ctrl_append_u16le(p_rsp_payload, p_rsp_len, executedMask);
            (void)nfc_app_ctrl_append_u16le(p_rsp_payload, p_rsp_len, passedMask);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, (uint8_t)(p_ctx->failCount & 0xFFU));
            (void)nfc_app_ctrl_append_u16le(p_rsp_payload, p_rsp_len, (uint16_t)p_ctx->lastSequenceStatus);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_DETAIL_GET:
            if (body_len < 1U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            itemId = p_body[0];
            if (itemId >= (uint8_t)APP_SELFTEST_ITEM_COUNT)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            p_ctx = App_SelfTestGetContext();
            if (p_ctx == NULL)
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, itemId);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, p_ctx->items[itemId].executed);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, p_ctx->items[itemId].passed);
            (void)nfc_app_ctrl_append_u16le(p_rsp_payload, p_rsp_len, (uint16_t)p_ctx->items[itemId].status);
            (void)nfc_app_ctrl_append_u32le(p_rsp_payload, p_rsp_len, p_ctx->items[itemId].tickMs);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_RETRY_ITEM:
            if (body_len < 1U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            itemId = p_body[0];
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
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (p_ctx != NULL)
            {
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, itemId);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, p_ctx->items[itemId].executed);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, p_ctx->items[itemId].passed);
                (void)nfc_app_ctrl_append_u16le(p_rsp_payload, p_rsp_len, (uint16_t)p_ctx->items[itemId].status);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_SELFTEST_CLEAR:
            if (body_len < 1U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            if (p_body[0] != NFC_APP_CTRL_CLEAR_CODE)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            nfc_app_ctrl_clear_selftest_context();
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x01U);
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_handle_param_readback(uint8_t targetGroup,
                                                  uint8_t *p_rsp_payload,
                                                  uint8_t *p_rsp_len,
                                                  uint8_t *p_op_status)
{
    AppMeterServerFormatOptions_t options;
    AppDeviceConfig_t device;
    AppStatus_t status;
    uint8_t mismatchCount = 0U;
    uint8_t lastError = 0U;

    switch (targetGroup)
    {
        case NFC_APP_CTRL_READBACK_GROUP_ALL:
        case NFC_APP_CTRL_READBACK_GROUP_SCHEDULE:
        case NFC_APP_CTRL_READBACK_GROUP_POLICY:
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                lastError = 1U;
                break;
            }
            if (s_lastSavedOptionsValid == 0U)
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED;
                lastError = 2U;
                break;
            }
            *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
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
            break;

        default:
            break;
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
            if (*p_op_status != (uint8_t)NFC_APP_CTRL_OP_NOT_SUPPORTED)
            {
                *p_op_status = (uint8_t)NFC_APP_CTRL_OP_OK;
            }
            mismatchCount = (uint8_t)(mismatchCount + nfc_app_ctrl_compare_device(&device, &s_lastSavedDevice));
        }
    }

    if ((targetGroup != NFC_APP_CTRL_READBACK_GROUP_ALL) &&
        (targetGroup != NFC_APP_CTRL_READBACK_GROUP_SCHEDULE) &&
        (targetGroup != NFC_APP_CTRL_READBACK_GROUP_POLICY) &&
        (targetGroup != NFC_APP_CTRL_READBACK_GROUP_DEVICE))
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }

    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, mismatchCount);
    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, lastError);
    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x00U);
    (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, 0x00U);
    return (uint8_t)NFC_CMD_RESULT_OK;
}

static uint8_t nfc_app_ctrl_handle_param(const NfcAppCtrlCmd_t *p_cmd,
                                         const uint8_t *p_body,
                                         uint8_t body_len,
                                         uint8_t *p_rsp_payload,
                                         uint8_t *p_rsp_len,
                                         uint8_t *p_op_status)
{
    AppMeterServerFormatOptions_t options;
    AppMeterServerFormatOptions_t verifyOptions;
    AppMeterServerFormatOptions_t defaultOptions;
    AppDeviceConfig_t device;
    AppDeviceConfig_t verifyDevice;
    AppDeviceConfig_t defaultDevice;
    AppStatus_t status;
    uint8_t applyMask;
    uint8_t verifyResult = 0U;
    uint8_t flags = 0U;
    uint8_t timeout100ms;

    switch (p_cmd->zz)
    {
        case NFC_APP_CTRL_PARAM_SCHEDULE_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.meteringPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.managementReportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingSpreadHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, flags);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_SCHEDULE_SET:
            if (body_len < 5U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            applyMask = p_body[4];
            if ((applyMask & NFC_APP_CTRL_APPLY_METERING_BIT) != 0U) { options.meteringPeriodHours = p_body[0]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_REPORTING_BIT) != 0U) { options.reportingPeriodHours = p_body[1]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_MGMT_BIT) != 0U) { options.managementReportingPeriodHours = p_body[2]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_SPREAD_BIT) != 0U) { options.reportingSpreadHours = p_body[3]; }
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
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsLoad(&verifyOptions);
                if (status == APP_STATUS_OK)
                {
                    verifyResult = nfc_app_ctrl_compare_schedule(&verifyOptions, &options);
                    nfc_app_ctrl_shadow_store_options(&options);
                    *p_op_status = (verifyResult == 0U) ? (uint8_t)NFC_APP_CTRL_OP_OK
                                                        : (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
                }
                else
                {
                    verifyResult = 1U;
                    *p_op_status = nfc_app_ctrl_map_status(status);
                }
            }
            else
            {
                verifyResult = 1U;
            }
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.meteringPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.managementReportingPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.reportingSpreadHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, verifyResult);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_POLICY_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                timeout100ms = nfc_app_ctrl_ack_timeout_sec_to_100ms(options.ackTimeoutSec);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.ackWaitEnabled);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, timeout100ms);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.ackPoll100Ms);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.deleteAfterSend);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, flags);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_POLICY_SET:
            if (body_len < 5U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            applyMask = p_body[4];
            timeout100ms = nfc_app_ctrl_ack_timeout_100ms_to_sec(p_body[1]);
            if ((applyMask & NFC_APP_CTRL_APPLY_ACK_WAIT_BIT) != 0U) { options.ackWaitEnabled = p_body[0]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_ACK_TIMEOUT_BIT) != 0U) { options.ackTimeoutSec = timeout100ms; }
            if ((applyMask & NFC_APP_CTRL_APPLY_ACK_POLL_BIT) != 0U) { options.ackPoll100Ms = p_body[2]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_DELETE_AFTER_SEND_BIT) != 0U) { options.deleteAfterSend = p_body[3]; }
            if (nfc_app_ctrl_validate_policy_values(options.ackWaitEnabled,
                                                    nfc_app_ctrl_ack_timeout_sec_to_100ms(options.ackTimeoutSec),
                                                    options.ackPoll100Ms,
                                                    options.deleteAfterSend) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = App_MeterServerOptionsSave(&options);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                status = App_MeterServerOptionsLoad(&verifyOptions);
                if (status == APP_STATUS_OK)
                {
                    verifyResult = nfc_app_ctrl_compare_policy(&verifyOptions, &options);
                    nfc_app_ctrl_shadow_store_options(&options);
                    *p_op_status = (verifyResult == 0U) ? (uint8_t)NFC_APP_CTRL_OP_OK
                                                        : (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
                }
                else
                {
                    verifyResult = 1U;
                    *p_op_status = nfc_app_ctrl_map_status(status);
                }
            }
            else
            {
                verifyResult = 1U;
            }
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.ackWaitEnabled);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len,
                                           nfc_app_ctrl_ack_timeout_sec_to_100ms(options.ackTimeoutSec));
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.ackPoll100Ms);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, options.deleteAfterSend);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, verifyResult);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_DEVICE_GET:
            if (body_len != 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_device(&device);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, device.logLevel);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, device.linkType);
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, nfc_app_ctrl_get_device_diag_profile(&device));
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, nfc_app_ctrl_get_device_reset_enable(&device));
                (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, flags);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_DEVICE_SET:
            if (body_len < 5U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_device(&device);
            if (status != APP_STATUS_OK)
            {
                *p_op_status = nfc_app_ctrl_map_status(status);
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            applyMask = p_body[4];
            if ((applyMask & NFC_APP_CTRL_APPLY_LOG_LEVEL_BIT) != 0U) { device.logLevel = p_body[0]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_LINK_TYPE_BIT) != 0U) { device.linkType = p_body[1]; }
            if ((applyMask & NFC_APP_CTRL_APPLY_DIAG_PROFILE_BIT) != 0U) { nfc_app_ctrl_set_device_diag_profile(&device, p_body[2]); }
            if ((applyMask & NFC_APP_CTRL_APPLY_RESET_ENABLE_BIT) != 0U) { nfc_app_ctrl_set_device_reset_enable(&device, p_body[3]); }
            if (nfc_app_ctrl_validate_device_values(device.logLevel,
                                                    device.linkType,
                                                    nfc_app_ctrl_get_device_diag_profile(&device),
                                                    nfc_app_ctrl_get_device_reset_enable(&device)) == 0U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = App_DeviceConfigSave(&device);
            *p_op_status = nfc_app_ctrl_map_status(status);
            if (status == APP_STATUS_OK)
            {
                status = App_DeviceConfigLoad(&verifyDevice);
                if (status == APP_STATUS_OK)
                {
                    verifyResult = nfc_app_ctrl_compare_device(&verifyDevice, &device);
                    nfc_app_ctrl_shadow_store_device(&device);
                    *p_op_status = (verifyResult == 0U) ? (uint8_t)NFC_APP_CTRL_OP_OK
                                                        : (uint8_t)NFC_APP_CTRL_OP_VERIFY_FAIL;
                }
                else
                {
                    verifyResult = 1U;
                    *p_op_status = nfc_app_ctrl_map_status(status);
                }
            }
            else
            {
                verifyResult = 1U;
            }
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, device.logLevel);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, device.linkType);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, nfc_app_ctrl_get_device_diag_profile(&device));
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, nfc_app_ctrl_get_device_reset_enable(&device));
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, verifyResult);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_RESTORE_DEFAULT:
            if (body_len < 2U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            if (p_body[1] != NFC_APP_CTRL_RESTORE_CODE)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = APP_STATUS_OK;
            if ((p_body[0] == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                (p_body[0] == NFC_APP_CTRL_READBACK_GROUP_SCHEDULE) ||
                (p_body[0] == NFC_APP_CTRL_READBACK_GROUP_POLICY))
            {
                status = nfc_app_ctrl_load_options(&options);
                if (status == APP_STATUS_OK)
                {
                    App_MeterServerOptionsSetDefaults(&defaultOptions);
                    if ((p_body[0] == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                        (p_body[0] == NFC_APP_CTRL_READBACK_GROUP_SCHEDULE))
                    {
                        options.meteringPeriodHours = defaultOptions.meteringPeriodHours;
                        options.reportingPeriodHours = defaultOptions.reportingPeriodHours;
                        options.managementReportingPeriodHours = defaultOptions.managementReportingPeriodHours;
                        options.reportingSpreadHours = defaultOptions.reportingSpreadHours;
                    }
                    if ((p_body[0] == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                        (p_body[0] == NFC_APP_CTRL_READBACK_GROUP_POLICY))
                    {
                        options.ackWaitEnabled = defaultOptions.ackWaitEnabled;
                        options.ackTimeoutSec = defaultOptions.ackTimeoutSec;
                        options.ackPoll100Ms = defaultOptions.ackPoll100Ms;
                        options.deleteAfterSend = defaultOptions.deleteAfterSend;
                    }
                    status = App_MeterServerOptionsSave(&options);
                    if (status == APP_STATUS_OK)
                    {
                        nfc_app_ctrl_shadow_store_options(&options);
                    }
                }
            }
            if ((status == APP_STATUS_OK) &&
                ((p_body[0] == NFC_APP_CTRL_READBACK_GROUP_ALL) ||
                 (p_body[0] == NFC_APP_CTRL_READBACK_GROUP_DEVICE)))
            {
                App_DeviceConfigSetDefaults(&defaultDevice);
                nfc_app_ctrl_set_device_diag_profile(&defaultDevice, 0U);
                nfc_app_ctrl_set_device_reset_enable(&defaultDevice, 1U);
                status = App_DeviceConfigSave(&defaultDevice);
                if (status == APP_STATUS_OK)
                {
                    nfc_app_ctrl_shadow_store_device(&defaultDevice);
                }
            }
            *p_op_status = nfc_app_ctrl_map_status(status);
            verifyResult = (status == APP_STATUS_OK) ? 0U : 1U;
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, p_body[0]);
            (void)nfc_app_ctrl_append_byte(p_rsp_payload, p_rsp_len, verifyResult);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_READBACK_GET:
            if (body_len < 1U)
            {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            return nfc_app_ctrl_handle_param_readback(p_body[0], p_rsp_payload, p_rsp_len, p_op_status);

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

uint8_t NfcAppCtrl_Execute(const NfcAppCtrlCmd_t *p_cmd,
                           const uint8_t *p_req_payload,
                           uint8_t req_payload_len,
                           uint8_t *p_rsp_payload,
                           uint8_t *p_rsp_payload_len,
                           uint8_t *p_op_status)
{
    uint8_t ret;

    if ((p_cmd == NULL) || (p_rsp_payload == NULL) || (p_rsp_payload_len == NULL) || (p_op_status == NULL))
    {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }

    nfc_app_ctrl_response_reset(p_rsp_payload, p_rsp_payload_len);
    *p_op_status = (uint8_t)NFC_APP_CTRL_OP_FAIL;

    ret = nfc_app_ctrl_validate_cmd(p_cmd);
    if (ret != (uint8_t)NFC_CMD_RESULT_OK)
    {
        return ret;
    }

    APP_LOGI("NFC", "AppCtrl cmd=%02X %02X %02X %02X payloadLen=%u",
             (unsigned int)p_cmd->xx,
             (unsigned int)p_cmd->yy,
             (unsigned int)p_cmd->zz,
             (unsigned int)p_cmd->reserved,
             (unsigned int)req_payload_len);

    switch (p_cmd->yy)
    {
        case NFC_APP_CTRL_GROUP_NB_CONTROL:
            return nfc_app_ctrl_handle_nb(p_cmd,
                                          p_req_payload,
                                          req_payload_len,
                                          p_rsp_payload,
                                          p_rsp_payload_len,
                                          p_op_status);
        case NFC_APP_CTRL_GROUP_SELFTEST:
            return nfc_app_ctrl_handle_selftest(p_cmd,
                                                p_req_payload,
                                                req_payload_len,
                                                p_rsp_payload,
                                                p_rsp_payload_len,
                                                p_op_status);
        case NFC_APP_CTRL_GROUP_PARAMETER:
            return nfc_app_ctrl_handle_param(p_cmd,
                                             p_req_payload,
                                             req_payload_len,
                                             p_rsp_payload,
                                             p_rsp_payload_len,
                                             p_op_status);
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

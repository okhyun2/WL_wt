#include "nfc_app_control_command.h"

#include <string.h>

#include "app_log.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"
#include "app_selftest.h"
#include "nfc_user_command.h"

static bool s_appCtrlPendingReset = false;

static void nfc_app_ctrl_response_init(uint8_t *p_rsp_raw,
                                       uint8_t *p_rsp_len,
                                       const NfcAppCtrlCmd_t *p_cmd,
                                       uint8_t opStatus)
{
    if ((p_rsp_raw == NULL) || (p_rsp_len == NULL) || (p_cmd == NULL)) {
        return;
    }

    p_rsp_raw[0] = opStatus;
    p_rsp_raw[1] = p_cmd->xx;
    p_rsp_raw[2] = p_cmd->yy;
    p_rsp_raw[3] = p_cmd->zz;
    p_rsp_raw[4] = p_cmd->reserved;
    *p_rsp_len = 5U;
}

static uint8_t nfc_app_ctrl_append_byte(uint8_t *p_rsp_raw,
                                        uint8_t *p_rsp_len,
                                        uint8_t value)
{
    if ((p_rsp_raw == NULL) || (p_rsp_len == NULL)) {
        return 0U;
    }
    if (*p_rsp_len >= NFC_APP_CTRL_RSP_MAX_LEN) {
        return 0U;
    }
    p_rsp_raw[*p_rsp_len] = value;
    (*p_rsp_len)++;
    return 1U;
}

static uint8_t nfc_app_ctrl_append_u16le(uint8_t *p_rsp_raw,
                                         uint8_t *p_rsp_len,
                                         uint16_t value)
{
    return (uint8_t)(nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, (uint8_t)(value & 0xFFU)) &&
                     nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, (uint8_t)((value >> 8) & 0xFFU)));
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

static AppStatus_t nfc_app_ctrl_load_options(AppMeterServerFormatOptions_t *p_options)
{
    AppStatus_t status;

    if (p_options == NULL) {
        return APP_STATUS_INVALID_PARAM;
    }

    status = App_MeterServerOptionsLoad(p_options);
    if (status == APP_STATUS_NOT_INITIALIZED) {
        App_MeterServerOptionsSetDefaults(p_options);
        return APP_STATUS_OK;
    }
    return status;
}

static AppStatus_t nfc_app_ctrl_load_device(AppDeviceConfig_t *p_config)
{
    AppStatus_t status;

    if (p_config == NULL) {
        return APP_STATUS_INVALID_PARAM;
    }

    status = App_DeviceConfigLoad(p_config);
    if (status == APP_STATUS_NOT_INITIALIZED) {
        App_DeviceConfigSetDefaults(p_config);
        return APP_STATUS_OK;
    }
    return status;
}

static uint8_t nfc_app_ctrl_map_status(AppStatus_t status)
{
    switch (status) {
        case APP_STATUS_OK:
            return (uint8_t)NFC_APP_CTRL_OP_OK;
        case APP_STATUS_INVALID_PARAM:
            return (uint8_t)NFC_APP_CTRL_OP_RANGE_ERROR;
        case APP_STATUS_NOT_INITIALIZED:
            return (uint8_t)NFC_APP_CTRL_OP_FAIL;
        case APP_STATUS_SELFTEST_FAILED:
            return (uint8_t)NFC_APP_CTRL_OP_FAIL;
        default:
            return (uint8_t)NFC_APP_CTRL_OP_STORAGE_FAIL;
    }
}

static uint16_t nfc_app_ctrl_mask_from_context(const AppSelfTestContext_t *p_ctx, uint8_t passedOnly)
{
    uint16_t mask = 0U;
    uint8_t i;

    if (p_ctx == NULL) {
        return 0U;
    }

    for (i = 0U; i < (uint8_t)APP_SELFTEST_ITEM_COUNT; ++i) {
        if (p_ctx->items[i].executed != 0U) {
            if ((passedOnly == 0U) || (p_ctx->items[i].passed != 0U)) {
                mask |= (uint16_t)(1U << i);
            }
        }
    }
    return mask;
}

static uint8_t nfc_app_ctrl_validate_cmd(const NfcAppCtrlCmd_t *p_cmd)
{
    if (p_cmd == NULL) {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }
    if (p_cmd->xx != NFC_APP_CTRL_CMD_CLASS) {
        return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
    if (p_cmd->reserved != 0x00U) {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }
    switch (p_cmd->yy) {
        case NFC_APP_CTRL_GROUP_NB_CONTROL:
        case NFC_APP_CTRL_GROUP_SELFTEST:
        case NFC_APP_CTRL_GROUP_PARAMETER:
            return (uint8_t)NFC_CMD_RESULT_OK;
        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_handle_nb(const NfcAppCtrlCmd_t *p_cmd,
                                      const uint8_t *p_body,
                                      uint8_t body_len,
                                      uint8_t *p_rsp_raw,
                                      uint8_t *p_rsp_len)
{
    AppMeterServerFormatOptions_t options;
    AppStatus_t status;

    switch (p_cmd->zz) {
        case NFC_APP_CTRL_NB_RESET_EXECUTE:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            s_appCtrlPendingReset = true;
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, (uint8_t)NFC_APP_CTRL_OP_OK);
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, 0x01U); /* accepted */
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_PERIOD_SET:
            if (body_len != 2U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            if ((nfc_app_ctrl_is_supported_period(p_body[0]) == 0U) ||
                (nfc_app_ctrl_is_supported_period(p_body[1]) == 0U) ||
                (p_body[1] < p_body[0]) ||
                ((p_body[3] != 0U) && ((nfc_app_ctrl_is_supported_period(p_body[3]) == 0U) || (p_body[3] > p_body[1])))) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK) {
                nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            options.meteringPeriodHours = p_body[0];
            options.reportingPeriodHours = p_body[1];
            status = App_MeterServerOptionsValidate(&options);
            if (status == APP_STATUS_OK) {
                status = App_MeterServerOptionsSave(&options);
            }
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.meteringPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingPeriodHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.managementReportingPeriodHours);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_PERIOD_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.meteringPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.managementReportingPeriodHours);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_SPREAD_SET:
            if (body_len != 1U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status != APP_STATUS_OK) {
                nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
                return (uint8_t)NFC_CMD_RESULT_OK;
            }
            if ((p_body[0] != 0U) &&
                ((nfc_app_ctrl_is_supported_period(p_body[0]) == 0U) ||
                 (p_body[0] > options.reportingPeriodHours))) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            options.reportingSpreadHours = p_body[0];
            status = App_MeterServerOptionsSave(&options);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingSpreadHours);
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingPeriodHours);
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_SPREAD_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingSpreadHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingPeriodHours);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_ACK_STATUS_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackWaitEnabled);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackTimeoutSec);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackPoll100Ms);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.deleteAfterSend);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_NB_RESET_SUPPORT_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, (uint8_t)NFC_APP_CTRL_OP_OK);
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, 0x01U);
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_append_selftest_summary(const NfcAppCtrlCmd_t *p_cmd,
                                                    uint8_t *p_rsp_raw,
                                                    uint8_t *p_rsp_len,
                                                    AppStatus_t seqStatus)
{
    const AppSelfTestContext_t *p_ctx = App_SelfTestGetContext();
    uint16_t executedMask;
    uint16_t passedMask;

    nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(seqStatus));
    if (p_ctx == NULL) {
        p_rsp_raw[0] = (uint8_t)NFC_APP_CTRL_OP_FAIL;
        return (uint8_t)NFC_CMD_RESULT_OK;
    }

    executedMask = nfc_app_ctrl_mask_from_context(p_ctx, 0U);
    passedMask = nfc_app_ctrl_mask_from_context(p_ctx, 1U);
    (void)nfc_app_ctrl_append_u16le(p_rsp_raw, p_rsp_len, executedMask);
    (void)nfc_app_ctrl_append_u16le(p_rsp_raw, p_rsp_len, passedMask);
    (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, (uint8_t)(p_ctx->failCount & 0xFFU));
    return (uint8_t)NFC_CMD_RESULT_OK;
}

static uint8_t nfc_app_ctrl_handle_selftest(const NfcAppCtrlCmd_t *p_cmd,
                                            const uint8_t *p_body,
                                            uint8_t body_len,
                                            uint8_t *p_rsp_raw,
                                            uint8_t *p_rsp_len)
{
    const AppSelfTestContext_t *p_ctx;
    AppStatus_t status;
    uint8_t itemId;

    switch (p_cmd->zz) {
        case NFC_APP_CTRL_SELFTEST_RUN_QUICK:
        case NFC_APP_CTRL_SELFTEST_RUN_FULL:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = App_SelfTestRunDataCollectionSequence();
            return nfc_app_ctrl_append_selftest_summary(p_cmd, p_rsp_raw, p_rsp_len, status);

        case NFC_APP_CTRL_SELFTEST_SUMMARY_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            return nfc_app_ctrl_append_selftest_summary(p_cmd,
                                                        p_rsp_raw,
                                                        p_rsp_len,
                                                        APP_STATUS_OK);

        case NFC_APP_CTRL_SELFTEST_DETAIL_GET:
            if (body_len != 1U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            itemId = p_body[0];
            if (itemId >= (uint8_t)APP_SELFTEST_ITEM_COUNT) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            p_ctx = App_SelfTestGetContext();
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd,
                                       (p_ctx != NULL) ? (uint8_t)NFC_APP_CTRL_OP_OK
                                                       : (uint8_t)NFC_APP_CTRL_OP_FAIL);
            if (p_ctx != NULL) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, itemId);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, p_ctx->items[itemId].executed);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, p_ctx->items[itemId].passed);
                (void)nfc_app_ctrl_append_u16le(p_rsp_raw, p_rsp_len,
                                                (uint16_t)p_ctx->items[itemId].status);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

static uint8_t nfc_app_ctrl_handle_param(const NfcAppCtrlCmd_t *p_cmd,
                                         const uint8_t *p_body,
                                         uint8_t body_len,
                                         uint8_t *p_rsp_raw,
                                         uint8_t *p_rsp_len)
{
    AppMeterServerFormatOptions_t options;
    AppDeviceConfig_t device;
    AppStatus_t status;

    switch (p_cmd->zz) {
        case NFC_APP_CTRL_PARAM_SCHEDULE_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.meteringPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.managementReportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingSpreadHours);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_SCHEDULE_SET:
            if (body_len != 4U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            if ((nfc_app_ctrl_is_supported_period(p_body[0]) == 0U) ||
                (nfc_app_ctrl_is_supported_period(p_body[1]) == 0U) ||
                (nfc_app_ctrl_is_supported_period(p_body[2]) == 0U) ||
                (p_body[1] < p_body[0]) ||
                ((p_body[3] != 0U) && ((nfc_app_ctrl_is_supported_period(p_body[3]) == 0U) || (p_body[3] > p_body[1])))) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK) {
                options.meteringPeriodHours = p_body[0];
                options.reportingPeriodHours = p_body[1];
                options.managementReportingPeriodHours = p_body[2];
                options.reportingSpreadHours = p_body[3];
                status = App_MeterServerOptionsValidate(&options);
                if (status == APP_STATUS_OK) {
                    status = App_MeterServerOptionsSave(&options);
                }
            }
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.meteringPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.managementReportingPeriodHours);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.reportingSpreadHours);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_POLICY_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_options(&options);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackWaitEnabled);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackTimeoutSec);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackPoll100Ms);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.deleteAfterSend);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_POLICY_SET:
            if (body_len != 4U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            if ((nfc_app_ctrl_is_binary_flag(p_body[0]) == 0U) ||
                (p_body[1] == 0U) ||
                (p_body[2] == 0U) ||
                (((uint32_t)p_body[2] * 100U) > ((uint32_t)p_body[1] * 1000U)) ||
                (nfc_app_ctrl_is_binary_flag(p_body[3]) == 0U)) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
            }
            status = nfc_app_ctrl_load_options(&options);
            if (status == APP_STATUS_OK) {
                options.ackWaitEnabled = p_body[0];
                options.ackTimeoutSec = p_body[1];
                options.ackPoll100Ms = p_body[2];
                options.deleteAfterSend = p_body[3];
                status = App_MeterServerOptionsSave(&options);
            }
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackWaitEnabled);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackTimeoutSec);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.ackPoll100Ms);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, options.deleteAfterSend);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_DEVICE_GET:
            if (body_len != 0U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_device(&device);
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, device.logLevel);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, device.linkType);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, device.bootCountValid);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_DEVICE_SET:
            if (body_len != 2U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            status = nfc_app_ctrl_load_device(&device);
            if (status == APP_STATUS_OK) {
                device.logLevel = p_body[0];
                device.linkType = p_body[1];
                status = App_DeviceConfigSave(&device);
            }
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, nfc_app_ctrl_map_status(status));
            if (status == APP_STATUS_OK) {
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, device.logLevel);
                (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, device.linkType);
            }
            return (uint8_t)NFC_CMD_RESULT_OK;

        case NFC_APP_CTRL_PARAM_RESTORE_DEFAULT:
            if (body_len != 1U) {
                return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
            }
            nfc_app_ctrl_response_init(p_rsp_raw, p_rsp_len, p_cmd, (uint8_t)NFC_APP_CTRL_OP_OK);
            if ((p_body[0] == 0x00U) || (p_body[0] == 0x01U) || (p_body[0] == 0x02U)) {
                App_MeterServerOptionsSetDefaults(&options);
                status = App_MeterServerOptionsSave(&options);
                p_rsp_raw[0] = nfc_app_ctrl_map_status(status);
            }
            if ((p_body[0] == 0x00U) || (p_body[0] == 0x03U)) {
                App_DeviceConfigSetDefaults(&device);
                status = App_DeviceConfigSave(&device);
                p_rsp_raw[0] = nfc_app_ctrl_map_status(status);
            }
            (void)nfc_app_ctrl_append_byte(p_rsp_raw, p_rsp_len, p_body[0]);
            return (uint8_t)NFC_CMD_RESULT_OK;

        default:
            return (uint8_t)NFC_CMD_RESULT_INVALID_CMD;
    }
}

uint8_t NfcAppCtrl_Execute(const uint8_t *p_req_raw,
                           uint8_t req_len,
                           uint8_t *p_rsp_raw,
                           uint8_t *p_rsp_len)
{
    const NfcAppCtrlCmd_t *p_cmd;
    const uint8_t *p_body;
    uint8_t body_len;
    uint8_t ret;

    if ((p_req_raw == NULL) || (p_rsp_raw == NULL) || (p_rsp_len == NULL)) {
        return (uint8_t)NFC_CMD_RESULT_INVALID_PARAM;
    }
    if ((req_len < NFC_APP_CTRL_CMD_SIZE) || (req_len > NFC_APP_CTRL_REQ_MAX_LEN)) {
        return (uint8_t)NFC_CMD_RESULT_INVALID_LEN;
    }

    memset(p_rsp_raw, 0, NFC_APP_CTRL_RSP_MAX_LEN);
    *p_rsp_len = 0U;

    p_cmd = (const NfcAppCtrlCmd_t *)p_req_raw;
    p_body = &p_req_raw[NFC_APP_CTRL_CMD_SIZE];
    body_len = (uint8_t)(req_len - NFC_APP_CTRL_CMD_SIZE);

    ret = nfc_app_ctrl_validate_cmd(p_cmd);
    if (ret != (uint8_t)NFC_CMD_RESULT_OK) {
        return ret;
    }

    APP_LOGI("NFC", "AppCtrl req: %02X %02X %02X %02X len=%u",
             (unsigned int)p_cmd->xx,
             (unsigned int)p_cmd->yy,
             (unsigned int)p_cmd->zz,
             (unsigned int)p_cmd->reserved,
             (unsigned int)body_len);

    switch (p_cmd->yy) {
        case NFC_APP_CTRL_GROUP_NB_CONTROL:
            return nfc_app_ctrl_handle_nb(p_cmd, p_body, body_len, p_rsp_raw, p_rsp_len);
        case NFC_APP_CTRL_GROUP_SELFTEST:
            return nfc_app_ctrl_handle_selftest(p_cmd, p_body, body_len, p_rsp_raw, p_rsp_len);
        case NFC_APP_CTRL_GROUP_PARAMETER:
            return nfc_app_ctrl_handle_param(p_cmd, p_body, body_len, p_rsp_raw, p_rsp_len);
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

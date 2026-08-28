/**
 * @file    nfc_user_command.c
 * @brief   NFC Layer2 UCMD transport for direct APP_CONTROL commands.
 */

#include "nfc_user_command.h"
#include "nfc_app_control_command.h"

#include <string.h>

#include "app_log.h"

#define NFC_CMD_SYNC_WAIT_TIMEOUT_MS    300U
#define NFC_CMD_SYNC_WAIT_POLL_MS       10U
#define NFC_CMD_MIN_RESPONSE_BLOCKS     1U

static NFC_CMD_Result_t nfc_cmd_read_indicate(NFC_CMD_Handle_t *hcmd,
                                              NFC_CMD_Indicate_t *ind);
static NFC_CMD_Result_t nfc_cmd_write_indicate(NFC_CMD_Handle_t *hcmd,
                                               uint8_t prefix,
                                               uint8_t addr,
                                               uint8_t block_len,
                                               uint8_t suffix);
static NFC_CMD_Result_t nfc_cmd_write_status(NFC_CMD_Handle_t *hcmd,
                                             uint8_t status,
                                             uint8_t opStatus,
                                             uint8_t payloadLen);
static NFC_CMD_Result_t nfc_cmd_write_response_payload(NFC_CMD_Handle_t *hcmd,
                                                       const uint8_t *p_payload,
                                                       uint8_t payloadLen,
                                                       uint8_t *p_writtenBlocks);
static NFC_CMD_Result_t nfc_cmd_read_packet(NFC_CMD_Handle_t *hcmd,
                                            const NFC_CMD_Indicate_t *ind,
                                            NFC_CMD_Packet_t *pkt);
static NFC_CMD_Result_t nfc_cmd_publish_response_indicate(NFC_CMD_Handle_t *hcmd,
                                                          uint8_t payloadBlocks);
static bool nfc_cmd_is_valid_request_indicate(const NFC_CMD_Indicate_t *ind);
static uint8_t nfc_cmd_calc_payload_blocks(uint8_t payloadLen);
static void nfc_cmd_wait_sync_read(NFC_CMD_Handle_t *hcmd);
static NFC_CMD_Result_t nfc_cmd_wait_sync_write(NFC_CMD_Handle_t *hcmd);

static uint8_t nfc_cmd_calc_payload_blocks(uint8_t payloadLen)
{
    uint8_t blocks;

    if (payloadLen == 0U)
    {
        return NFC_CMD_MIN_RESPONSE_BLOCKS;
    }

    blocks = (uint8_t)((payloadLen + 3U) / 4U);
    if (blocks > NFC_CMD_IND_RSP_BLOCK_LEN_MAX)
    {
        blocks = NFC_CMD_IND_RSP_BLOCK_LEN_MAX;
    }
    return blocks;
}

static NFC_CMD_Result_t nfc_cmd_wait_sync_write(NFC_CMD_Handle_t *hcmd)
{
    uint32_t startTick = HAL_GetTick();
    uint8_t status0 = 0U;

    do
    {
        if (NFC_NTP53321_ReadSessionReg(hcmd->hntag,
                                        NFC_SESSION_STATUS_ADDR,
                                        0U,
                                        &status0) == NFC_RESULT_OK)
        {
            if ((status0 & NFC_STATUS0_SYNCH_BLOCK_WRITE) != 0U)
            {
                APP_LOGI("NFC", "UCMD sync-write gate ok status0=0x%02X", (unsigned int)status0);
                return NFC_CMD_RESULT_OK;
            }
        }
        HAL_Delay(NFC_CMD_SYNC_WAIT_POLL_MS);
    } while ((HAL_GetTick() - startTick) < NFC_CMD_SYNC_WAIT_TIMEOUT_MS);

    APP_LOGW("NFC", "UCMD sync-write gate timeout status0=0x%02X", (unsigned int)status0);
    return NFC_CMD_RESULT_I2C_ERROR;
}

static void nfc_cmd_wait_sync_read(NFC_CMD_Handle_t *hcmd)
{
    uint32_t startTick = HAL_GetTick();
    uint8_t status0 = 0U;

    do
    {
        if (NFC_NTP53321_ReadSessionReg(hcmd->hntag,
                                        NFC_SESSION_STATUS_ADDR,
                                        0U,
                                        &status0) == NFC_RESULT_OK)
        {
            if ((status0 & NFC_STATUS0_SYNCH_BLOCK_READ) != 0U)
            {
                APP_LOGI("NFC", "UCMD sync-read ack status0=0x%02X", (unsigned int)status0);
                return;
            }
        }
        HAL_Delay(NFC_CMD_SYNC_WAIT_POLL_MS);
    } while ((HAL_GetTick() - startTick) < NFC_CMD_SYNC_WAIT_TIMEOUT_MS);

    APP_LOGW("NFC", "UCMD sync-read ack timeout status0=0x%02X", (unsigned int)status0);
}

static NFC_CMD_Result_t nfc_cmd_read_indicate(NFC_CMD_Handle_t *hcmd,
                                              NFC_CMD_Indicate_t *ind)
{
    NFC_Result_t ret;

    if ((hcmd == NULL) || (ind == NULL))
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    ret = NFC_NTP53321_ReadBlock(hcmd->hntag,
                                 NFC_SRAM_UCMD_IND_BLOCK,
                                 (uint8_t *)ind);
    if (ret != NFC_RESULT_OK)
    {
        APP_LOGE("NFC", "UCMD read indicate failed blk=0x%04X ret=%d",
                 (unsigned int)NFC_SRAM_UCMD_IND_BLOCK,
                 (int)ret);
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t nfc_cmd_write_indicate(NFC_CMD_Handle_t *hcmd,
                                               uint8_t prefix,
                                               uint8_t addr,
                                               uint8_t block_len,
                                               uint8_t suffix)
{
    NFC_CMD_Indicate_t ind;
    NFC_Result_t ret;

    if (hcmd == NULL)
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    ind.prefix = prefix;
    ind.addr = addr;
    ind.block_len = block_len;
    ind.suffix = suffix;

    ret = NFC_NTP53321_WriteBlock(hcmd->hntag,
                                  NFC_SRAM_UCMD_IND_BLOCK,
                                  (const uint8_t *)&ind);
    return (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK : NFC_CMD_RESULT_I2C_ERROR;
}

static bool nfc_cmd_is_valid_request_indicate(const NFC_CMD_Indicate_t *ind)
{
    if (ind == NULL)
    {
        return false;
    }

    return (ind->prefix == NFC_CMD_IND_NFC_TO_I2C_PREFIX) &&
           (ind->suffix == NFC_CMD_IND_NFC_TO_I2C_SUFFIX) &&
           (ind->addr == NFC_CMD_IND_REQ_ADDR);
}

static NFC_CMD_Result_t nfc_cmd_read_packet(NFC_CMD_Handle_t *hcmd,
                                            const NFC_CMD_Indicate_t *ind,
                                            NFC_CMD_Packet_t *pkt)
{
    NFC_Result_t ret;
    (void)ind;

    if ((hcmd == NULL) || (pkt == NULL))
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    (void)memset(pkt, 0, sizeof(*pkt));

    ret = NFC_NTP53321_ReadBlock(hcmd->hntag,
                                 NFC_SRAM_UCMD_CMD_BLOCK,
                                 pkt->cmd);
    if (ret != NFC_RESULT_OK)
    {
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    ret = NFC_NTP53321_ReadMultiBlock(hcmd->hntag,
                                      NFC_SRAM_UCMD_PAYLOAD_BLOCK_START,
                                      pkt->payload,
                                      NFC_CMD_IND_RSP_BLOCK_LEN_MAX);
    if (ret != NFC_RESULT_OK)
    {
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    pkt->payload_len = NFC_CMD_MAX_PAYLOAD;
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t nfc_cmd_write_response_payload(NFC_CMD_Handle_t *hcmd,
                                                       const uint8_t *p_payload,
                                                       uint8_t payloadLen,
                                                       uint8_t *p_writtenBlocks)
{
    uint8_t blockCount;
    uint8_t raw[NFC_CMD_MAX_RESULT];
    NFC_Result_t ret;

    if ((hcmd == NULL) || (p_writtenBlocks == NULL))
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    if (payloadLen > NFC_CMD_MAX_RESULT)
    {
        return NFC_CMD_RESULT_INVALID_LEN;
    }

    (void)memset(raw, 0, sizeof(raw));
    if ((payloadLen > 0U) && (p_payload != NULL))
    {
        (void)memcpy(raw, p_payload, payloadLen);
    }

    blockCount = nfc_cmd_calc_payload_blocks(payloadLen);
    ret = NFC_NTP53321_WriteMultiBlock(hcmd->hntag,
                                       NFC_SRAM_UCMD_PAYLOAD_BLOCK_START,
                                       raw,
                                       blockCount);
    if (ret != NFC_RESULT_OK)
    {
        APP_LOGE("NFC", "UCMD write payload failed blk=0x%04X ret=%d blocks=%u",
                 (unsigned int)NFC_SRAM_UCMD_PAYLOAD_BLOCK_START,
                 (int)ret,
                 (unsigned int)blockCount);
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    *p_writtenBlocks = blockCount;
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t nfc_cmd_write_status(NFC_CMD_Handle_t *hcmd,
                                             uint8_t status,
                                             uint8_t opStatus,
                                             uint8_t payloadLen)
{
    NFC_CMD_ResultPacket_t packet;
    NFC_Result_t ret;

    if (hcmd == NULL)
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    packet.status = status;
    packet.op_status = opStatus;
    packet.payload_len = payloadLen;
    packet.reserved = 0x00U;

    ret = NFC_NTP53321_WriteBlock(hcmd->hntag,
                                  NFC_SRAM_UCMD_STATUS_BLOCK,
                                  (const uint8_t *)&packet);
    return (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK : NFC_CMD_RESULT_I2C_ERROR;
}

static NFC_CMD_Result_t nfc_cmd_publish_response_indicate(NFC_CMD_Handle_t *hcmd,
                                                          uint8_t payloadBlocks)
{
    if (payloadBlocks == 0U)
    {
        payloadBlocks = NFC_CMD_MIN_RESPONSE_BLOCKS;
    }

    return nfc_cmd_write_indicate(hcmd,
                                  NFC_CMD_IND_I2C_TO_NFC_PREFIX,
                                  NFC_CMD_IND_RSP_ADDR,
                                  payloadBlocks,
                                  NFC_CMD_IND_I2C_TO_NFC_SUFFIX);
}

NFC_CMD_Result_t NFC_CMD_Init(NFC_CMD_Handle_t *hcmd,
                              NFC_NTP53321_Handle_t *hntag,
                              NFC_AUTH_Handle_t *hauth)
{
    if ((hcmd == NULL) || (hntag == NULL) || (hauth == NULL))
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    (void)memset(hcmd, 0, sizeof(*hcmd));
    hcmd->hntag = hntag;
    hcmd->hauth = hauth;
    hcmd->initialized = true;

    (void)nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_IDLE, 0U, 0U);
    return NFC_CMD_RESULT_OK;
}

NFC_CMD_Result_t NFC_CMD_Process(NFC_CMD_Handle_t *hcmd)
{
    NFC_CMD_Indicate_t ind;
    NFC_CMD_Packet_t pkt;
    NfcAppCtrlCmd_t cmd;
    uint8_t rspPayload[NFC_CMD_MAX_RESULT];
    uint8_t rspPayloadLen = 0U;
    uint8_t opStatus = (uint8_t)NFC_APP_CTRL_OP_FAIL;
    uint8_t writtenBlocks = NFC_CMD_MIN_RESPONSE_BLOCKS;
    NFC_CMD_Result_t status;
    uint32_t resetDelayMs = 0U;

    if ((hcmd == NULL) || (hcmd->initialized != true) || (hcmd->hntag == NULL) || (hcmd->hauth == NULL))
    {
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    status = nfc_cmd_wait_sync_write(hcmd);
    if (status != NFC_CMD_RESULT_OK)
    {
        return status;
    }

    status = nfc_cmd_read_indicate(hcmd, &ind);
    if (status != NFC_CMD_RESULT_OK)
    {
        return status;
    }

    if (nfc_cmd_is_valid_request_indicate(&ind) != true)
    {
        APP_LOGW("NFC", "UCMD invalid indicate %02X %02X %02X %02X",
                 (unsigned int)ind.prefix,
                 (unsigned int)ind.addr,
                 (unsigned int)ind.block_len,
                 (unsigned int)ind.suffix);
        (void)nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_DONE_FAIL, (uint8_t)NFC_APP_CTRL_OP_RANGE_ERROR, 0U);
        return NFC_CMD_RESULT_INVALID_PARAM;
    }

    status = nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_PROCESSING, (uint8_t)NFC_APP_CTRL_OP_BUSY, 0U);
    if (status != NFC_CMD_RESULT_OK)
    {
        return status;
    }

    if ((hcmd->hauth->state != NFC_AUTH_STATE_AUTHENTICATED) ||
        (hcmd->hauth->session.active != true))
    {
        hcmd->cmd_no_auth_count++;
        (void)nfc_cmd_write_response_payload(hcmd, NULL, 0U, &writtenBlocks);
        (void)nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_DONE_FAIL, (uint8_t)NFC_APP_CTRL_OP_FAIL, 0U);
        (void)nfc_cmd_publish_response_indicate(hcmd, writtenBlocks);
        nfc_cmd_wait_sync_read(hcmd);
        return NFC_CMD_RESULT_NOT_AUTH;
    }

    status = nfc_cmd_read_packet(hcmd, &ind, &pkt);
    if (status != NFC_CMD_RESULT_OK)
    {
        (void)nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_DONE_FAIL, (uint8_t)NFC_APP_CTRL_OP_STORAGE_FAIL, 0U);
        return status;
    }

    (void)memcpy(&cmd, pkt.cmd, sizeof(cmd));
    status = (NFC_CMD_Result_t)NfcAppCtrl_Execute(&cmd,
                                                  pkt.payload,
                                                  pkt.payload_len,
                                                  rspPayload,
                                                  &rspPayloadLen,
                                                  &opStatus);

    if (status == NFC_CMD_RESULT_OK)
    {
        hcmd->cmd_success_count++;
    }
    else
    {
        hcmd->cmd_fail_count++;
    }

    if (nfc_cmd_write_response_payload(hcmd,
                                       rspPayload,
                                       rspPayloadLen,
                                       &writtenBlocks) != NFC_CMD_RESULT_OK)
    {
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    if (nfc_cmd_write_status(hcmd,
                             (status == NFC_CMD_RESULT_OK) ? NFC_CMD_STATUS_DONE_OK : NFC_CMD_STATUS_DONE_FAIL,
                             opStatus,
                             rspPayloadLen) != NFC_CMD_RESULT_OK)
    {
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    if (nfc_cmd_publish_response_indicate(hcmd, writtenBlocks) != NFC_CMD_RESULT_OK)
    {
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    nfc_cmd_wait_sync_read(hcmd);

    if ((status == NFC_CMD_RESULT_OK) && (NfcAppCtrl_ConsumePendingReset() == true))
    {
        resetDelayMs = NfcAppCtrl_GetPendingResetDelayMs();
        if (resetDelayMs != 0U)
        {
            HAL_Delay(resetDelayMs);
        }
        NVIC_SystemReset();
    }

    return status;
}

void NFC_CMD_PrintStats(NFC_CMD_Handle_t *hcmd)
{
    if (hcmd == NULL)
    {
        return;
    }

    APP_LOGI("NFC", "UCMD stats ok=%lu fail=%lu noauth=%lu init=%u",
             (unsigned long)hcmd->cmd_success_count,
             (unsigned long)hcmd->cmd_fail_count,
             (unsigned long)hcmd->cmd_no_auth_count,
             (unsigned int)(hcmd->initialized ? 1u : 0u));
}

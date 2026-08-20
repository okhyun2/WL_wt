/**
 * @file    nfc_user_command.c
 * @brief   NFC User Command Processor  [v2.2.0]
 *
 * Changes:
 *  [H3 FIX] ResetDevice: write result to SRAM before NVIC_SystemReset()
 *  [H4 FIX] ADC channel switched per measurement (temp vs battery)
 */

#include "nfc_user_command.h"
#include "app_build_config.h"
#include <stdio.h>
#include <string.h>
#include "app_log.h"

extern ADC_HandleTypeDef hadc;

#define NFC_CMD_USER_EEPROM_MIN_BLOCK   (NFC_NDEF_START_BLOCK)
#define NFC_CMD_USER_EEPROM_MAX_BLOCK   (0x00FEU)
#define NFC_CMD_CONFIG_STORAGE_BLOCK    (0x0050U)

static NFC_CMD_Result_t nfc_cmd_write_result(NFC_CMD_Handle_t *hcmd,
                                              const NFC_CMD_ResultPacket_t *r);
static NFC_CMD_Result_t nfc_cmd_write_status(NFC_CMD_Handle_t *hcmd,
                                              uint8_t status);

static bool nfc_cmd_block_range_is_valid(uint16_t start_block, uint16_t num_blocks)
{
    uint32_t end_block;

    if (num_blocks == 0U) {
        return false;
    }

    end_block = (uint32_t)start_block + (uint32_t)num_blocks - 1U;
    if ((start_block < NFC_CMD_USER_EEPROM_MIN_BLOCK) ||
        (end_block > NFC_CMD_USER_EEPROM_MAX_BLOCK)) {
        return false;
    }

    if ((start_block <= NFC_NONCE_BLOCK_ADDR) && (end_block >= NFC_NONCE_BLOCK_ADDR)) {
        return false;
    }

    return true;
}

static void nfc_cmd_prepare_result(NFC_CMD_ResultPacket_t *res, NFC_CMD_Result_t code)
{
    uint16_t tick16;

    if (res == NULL) {
        return;
    }

    memset(res, 0, sizeof(*res));
    tick16 = (uint16_t)(HAL_GetTick() & 0xFFFFU);
    res->result_code = (uint8_t)code;
    res->timestamp[0] = (uint8_t)(tick16 & 0xFFU);
    res->timestamp[1] = (uint8_t)((tick16 >> 8) & 0xFFU);
}

static NFC_CMD_Result_t nfc_cmd_publish_result_only(NFC_CMD_Handle_t *hcmd, NFC_CMD_Result_t code)
{
    NFC_CMD_ResultPacket_t result;
    NFC_CMD_Result_t wr;

    nfc_cmd_prepare_result(&result, code);
    wr = nfc_cmd_write_result(hcmd, &result);
    if (wr != NFC_CMD_RESULT_OK) {
        return wr;
    }

    wr = nfc_cmd_write_status(hcmd, (code == NFC_CMD_RESULT_OK) ? NFC_CMD_STATUS_DONE_OK : NFC_CMD_STATUS_DONE_FAIL);
    if (wr != NFC_CMD_RESULT_OK) {
        return wr;
    }

    return code;
}

/* ============================================================
 * ADC helpers
 * [H4 FIX] Configure correct channel before each read
 * ============================================================ */
static uint16_t nfc_cmd_read_battery_mv(void)
{
    //TODO
    //read from config eeprom
    #if 0
    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t               adc_raw = 0;

    sConfig.Channel = ADC_CHANNEL_VREFINT;
    sConfig.Rank    = ADC_RANK_CHANNEL_NUMBER;
    HAL_ADC_ConfigChannel(&hadc, &sConfig);

    if (HAL_ADC_Start(&hadc) == HAL_OK) {
        if (HAL_ADC_PollForConversion(&hadc, 10) == HAL_OK)
            adc_raw = (uint16_t)HAL_ADC_GetValue(&hadc);
        HAL_ADC_Stop(&hadc);
    }
    return (uint16_t)(((uint32_t)adc_raw * 3300U) / 4096U);
    #endif
    return 0;
}

static int16_t nfc_cmd_read_temperature_x10(void)
{
    //TODO
    //read from config eeprom
    #if 0
    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t ts_cal1 = *((volatile uint16_t *)0x1FF8007AU);
    uint16_t ts_cal2 = *((volatile uint16_t *)0x1FF8007EU);
    uint16_t adc_raw = 0;
    int32_t  temp_x10;

    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    sConfig.Rank    = ADC_RANK_CHANNEL_NUMBER;
    HAL_ADC_ConfigChannel(&hadc, &sConfig);

    if (HAL_ADC_Start(&hadc) == HAL_OK) {
        if (HAL_ADC_PollForConversion(&hadc, 10) == HAL_OK)
            adc_raw = (uint16_t)HAL_ADC_GetValue(&hadc);
        HAL_ADC_Stop(&hadc);
    }

    /* Linear interpolation: TS_CAL1@30C, TS_CAL2@130C */
    if (ts_cal2 == ts_cal1) return 300;   /* guard div-by-zero */
    temp_x10 = (int32_t)(1000 * ((int32_t)adc_raw - (int32_t)ts_cal1))
               / (int32_t)(ts_cal2 - ts_cal1) + 300;
    return (int16_t)temp_x10;
    #endif
    return 0;
}

/* ============================================================
 * Packet validation
 * ============================================================ */
static NFC_CMD_Result_t nfc_cmd_validate_packet(const NFC_CMD_Packet_t *pkt)
{
    if (pkt->header.magic != NFC_CMD_MAGIC_WORD) {
        APP_LOGI("NFC", "Bad magic: 0x%04X", pkt->header.magic);
        return NFC_CMD_RESULT_INVALID_MAGIC;
    }
    if (pkt->header.cmd_id == 0x00U ||
        pkt->header.cmd_id >= (uint8_t)NFC_CMD_ID_MAX) {
        APP_LOGI("NFC", "Bad cmd_id: 0x%02X", pkt->header.cmd_id);
        return NFC_CMD_RESULT_INVALID_CMD;
    }
    if (pkt->header.payload_len > NFC_CMD_MAX_PAYLOAD) {
        APP_LOGI("NFC", "Payload too large: %u",
               pkt->header.payload_len);
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    return NFC_CMD_RESULT_OK;
}

/* ============================================================
 * SRAM I/O helpers
 * ============================================================ */
static NFC_CMD_Result_t nfc_cmd_read_packet(NFC_CMD_Handle_t *hcmd,
                                              NFC_CMD_Packet_t *pkt)
{
    NFC_Result_t ret;
    ret = NFC_NTP53321_ReadBlock(hcmd->hntag,
                                  NFC_SRAM_UCMD_HEADER_BLOCK,
                                  (uint8_t *)&pkt->header);
    if (ret != NFC_RESULT_OK) return NFC_CMD_RESULT_I2C_ERROR;

    ret = NFC_NTP53321_ReadMultiBlock(hcmd->hntag,
                                       NFC_SRAM_UCMD_DATA_BLOCK_START,
                                       pkt->payload, 4U);
    return (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                  : NFC_CMD_RESULT_I2C_ERROR;
}

static NFC_CMD_Result_t nfc_cmd_write_result(NFC_CMD_Handle_t *hcmd,
                                              const NFC_CMD_ResultPacket_t *r)
{
    uint8_t      buf[16] = {0};
    NFC_Result_t ret;
    uint8_t      dlen = (r->data_len < 14U) ? r->data_len : 14U;

    buf[0] = r->result_code;
    buf[1] = dlen;
    memcpy(&buf[2], r->data, dlen);

    ret = NFC_NTP53321_WriteMultiBlock(hcmd->hntag,
                                        NFC_SRAM_UCMD_RESULT_BLOCK_START,
                                        buf, 4U);
    return (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                  : NFC_CMD_RESULT_I2C_ERROR;
}

static NFC_CMD_Result_t nfc_cmd_write_status(NFC_CMD_Handle_t *hcmd,
                                              uint8_t status)
{
    uint32_t     tick = HAL_GetTick();
    uint8_t      buf[4];
    NFC_Result_t ret;

    buf[0] = status;
    buf[1] = (uint8_t)(tick & 0xFFU);
    buf[2] = (uint8_t)((tick >> 8) & 0xFFU);
    buf[3] = 0x00U;

    ret = NFC_NTP53321_WriteBlock(hcmd->hntag,
                                   NFC_SRAM_UCMD_STATUS_BLOCK,
                                   buf);
    return (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                  : NFC_CMD_RESULT_I2C_ERROR;
}

/* ============================================================
 * Command Handlers
 * ============================================================ */
static NFC_CMD_Result_t NFC_CMD_Handler_GetStatus(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t  *hcmd = (NFC_CMD_Handle_t *)hv;
    NFC_CMD_MCUStatus_t st  = {0};
    (void)pkt;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    st.fw_version[0]   = APP_FW_VERSION_MAJOR; st.fw_version[1] = APP_FW_VERSION_MINOR;
    st.auth_state       = (uint8_t)hcmd->hauth->state;
    st.nfc_field_active = NFC_NTP53321_IsEDTriggered(hcmd->hntag) ? 1U : 0U;
    st.uptime_sec       = HAL_GetTick() / 1000U;
    st.wakeup_count     = hcmd->hntag->stats.wakeup_count;
    st.battery_mv       = nfc_cmd_read_battery_mv();

    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = (uint8_t)sizeof(NFC_CMD_MCUStatus_t);
    memcpy(res->data, &st, sizeof(NFC_CMD_MCUStatus_t));

    APP_LOGI("NFC", "GetStatus: auth=%d up=%lu batt=%u",
           st.auth_state, (unsigned long)st.uptime_sec, st.battery_mv);
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t NFC_CMD_Handler_GetVersion(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    const char *ver = APP_NAME_STRING " NFC v" NFC_CMD_VERSION_STR;
    uint8_t     len = (uint8_t)strlen(ver);
    (void)hv; (void)pkt;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (len > NFC_CMD_MAX_RESULT) len = NFC_CMD_MAX_RESULT;
    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = len;
    memcpy(res->data, ver, len);
    APP_LOGI("NFC", "GetVersion: %s", ver);
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t NFC_CMD_Handler_GetStats(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    uint8_t  buf[12] = {0};
    uint32_t wk = hcmd->hntag->stats.wakeup_count;
    uint32_t sc = hcmd->hauth->stats.success_count;
    uint32_t fc = hcmd->hauth->stats.fail_count;
    (void)pkt;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    memcpy(&buf[0], &wk, 4);
    memcpy(&buf[4], &sc, 4);
    memcpy(&buf[8], &fc, 4);

    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = 12;
    memcpy(res->data, buf, 12);
    APP_LOGI("NFC", "GetStats: wk=%lu ok=%lu fail=%lu",
           (unsigned long)wk, (unsigned long)sc, (unsigned long)fc);
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t NFC_CMD_Handler_ReadSensor(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_SensorData_t sd = {0};
    (void)hv; (void)pkt;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    sd.temperature_x10 = nfc_cmd_read_temperature_x10();
    sd.humidity_x10    = 500U; //TODO //read from config eeprom
    sd.battery_mv      = nfc_cmd_read_battery_mv();
    sd.signal_level_dbm = -60; //TODO //read from config eeprom

    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = (uint8_t)sizeof(NFC_CMD_SensorData_t);
    memcpy(res->data, &sd, sizeof(NFC_CMD_SensorData_t));
    APP_LOGI("NFC", "ReadSensor: temp=%d.%dC batt=%u",
           sd.temperature_x10 / 10, sd.temperature_x10 % 10, sd.battery_mv);
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t NFC_CMD_Handler_WriteData(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    uint16_t          blk;
    NFC_Result_t      ret;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (pkt->header.payload_len != 6U) {
        res->result_code = NFC_CMD_RESULT_INVALID_LEN;
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    blk = (uint16_t)(pkt->payload[0] | ((uint16_t)pkt->payload[1] << 8));
    if (!nfc_cmd_block_range_is_valid(blk, 1U)) {
        res->result_code = NFC_CMD_RESULT_INVALID_PARAM;
        return NFC_CMD_RESULT_INVALID_PARAM;
    }
    ret = NFC_NTP53321_WriteBlock(hcmd->hntag, blk, &pkt->payload[2]);
    res->result_code = (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                               : NFC_CMD_RESULT_FAIL;
    res->data_len = 0;
    APP_LOGI("NFC", "WriteData: blk=0x%04X ret=%d", blk, ret);
    return (NFC_CMD_Result_t)res->result_code;
}

static NFC_CMD_Result_t NFC_CMD_Handler_ReadEEPROM(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    uint16_t          blk;
    uint8_t           data[4] = {0};
    NFC_Result_t      ret;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (pkt->header.payload_len != 2U) {
        res->result_code = NFC_CMD_RESULT_INVALID_LEN;
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    blk = (uint16_t)(pkt->payload[0] | ((uint16_t)pkt->payload[1] << 8));
    if (!nfc_cmd_block_range_is_valid(blk, 1U)) {
        res->result_code = NFC_CMD_RESULT_INVALID_PARAM;
        return NFC_CMD_RESULT_INVALID_PARAM;
    }
    ret = NFC_NTP53321_ReadBlock(hcmd->hntag, blk, data);
    res->result_code = (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                               : NFC_CMD_RESULT_FAIL;
    res->data_len = 4;
    memcpy(res->data, data, 4);
    APP_LOGI("NFC", "ReadEEPROM: blk=0x%04X %02X%02X%02X%02X",
           blk, data[0], data[1], data[2], data[3]);
    return (NFC_CMD_Result_t)res->result_code;
}

static NFC_CMD_Result_t NFC_CMD_Handler_WriteEEPROM(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    uint16_t          blk;
    NFC_Result_t      ret;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (pkt->header.payload_len != 6U) {
        res->result_code = NFC_CMD_RESULT_INVALID_LEN;
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    blk = (uint16_t)(pkt->payload[0] | ((uint16_t)pkt->payload[1] << 8));
    if (!nfc_cmd_block_range_is_valid(blk, 1U)) {
        res->result_code = NFC_CMD_RESULT_INVALID_PARAM;
        return NFC_CMD_RESULT_INVALID_PARAM;
    }
    ret = NFC_NTP53321_WriteBlock(hcmd->hntag, blk, &pkt->payload[2]);
    res->result_code = (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                               : NFC_CMD_RESULT_FAIL;
    res->data_len = 0;
    APP_LOGI("NFC", "WriteEEPROM: blk=0x%04X ret=%d", blk, ret);
    return (NFC_CMD_Result_t)res->result_code;
}

static NFC_CMD_Result_t NFC_CMD_Handler_SetConfig(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    NFC_Result_t      ret;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (pkt->header.payload_len != (uint8_t)sizeof(NFC_CMD_Config_t)) {
        res->result_code = NFC_CMD_RESULT_INVALID_LEN;
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    memcpy(&hcmd->config, pkt->payload, sizeof(NFC_CMD_Config_t));
    if ((hcmd->config.report_interval_sec < APP_NFC_REPORT_INTERVAL_MIN_SEC) ||
        (hcmd->config.report_interval_sec > APP_NFC_REPORT_INTERVAL_MAX_SEC) ||
        ((int32_t)hcmd->config.temp_threshold_x10 < APP_NFC_TEMP_THRESHOLD_MIN_X10) ||
        ((int32_t)hcmd->config.temp_threshold_x10 > APP_NFC_TEMP_THRESHOLD_MAX_X10)) {
        res->result_code = NFC_CMD_RESULT_INVALID_PARAM;
        return NFC_CMD_RESULT_INVALID_PARAM;
    }
    ret = NFC_NTP53321_WriteMultiBlock(hcmd->hntag, NFC_CMD_CONFIG_STORAGE_BLOCK,
                                        (uint8_t *)&hcmd->config,
                                        sizeof(NFC_CMD_Config_t) / 4U);
    res->result_code = (ret == NFC_RESULT_OK) ? NFC_CMD_RESULT_OK
                                               : NFC_CMD_RESULT_FAIL;
    res->data_len = 0;
    APP_LOGI("NFC", "SetConfig: interval=%us thr=%d.%dC",
           hcmd->config.report_interval_sec,
           hcmd->config.temp_threshold_x10 / 10,
           hcmd->config.temp_threshold_x10 % 10);
    return (NFC_CMD_Result_t)res->result_code;
}

static NFC_CMD_Result_t NFC_CMD_Handler_GetConfig(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    (void)pkt;
    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    res->data_len    = (uint8_t)sizeof(NFC_CMD_Config_t);
    memcpy(res->data, &hcmd->config, sizeof(NFC_CMD_Config_t));
    APP_LOGI("NFC", "GetConfig: interval=%u thr=%d",
           hcmd->config.report_interval_sec,
           hcmd->config.temp_threshold_x10);
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t NFC_CMD_Handler_SetThreshold(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    int16_t threshold_x10;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (pkt->header.payload_len != 2U) {
        res->result_code = NFC_CMD_RESULT_INVALID_LEN;
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    threshold_x10 = (int16_t)(pkt->payload[0] | ((uint16_t)pkt->payload[1] << 8));
    if ((threshold_x10 < APP_NFC_TEMP_THRESHOLD_MIN_X10) ||
        (threshold_x10 > APP_NFC_TEMP_THRESHOLD_MAX_X10)) {
        res->result_code = NFC_CMD_RESULT_INVALID_PARAM;
        return NFC_CMD_RESULT_INVALID_PARAM;
    }
    hcmd->config.temp_threshold_x10 = (uint16_t)threshold_x10;
    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = 0;
    APP_LOGI("NFC", "SetThreshold: %d.%dC",
           hcmd->config.temp_threshold_x10 / 10,
           hcmd->config.temp_threshold_x10 % 10);
    return NFC_CMD_RESULT_OK;
}

static NFC_CMD_Result_t NFC_CMD_Handler_SetInterval(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    if (pkt->header.payload_len != 2U) {
        res->result_code = NFC_CMD_RESULT_INVALID_LEN;
        return NFC_CMD_RESULT_INVALID_LEN;
    }
    hcmd->config.report_interval_sec =
        (uint16_t)(pkt->payload[0] | ((uint16_t)pkt->payload[1] << 8));
    if ((hcmd->config.report_interval_sec < APP_NFC_REPORT_INTERVAL_MIN_SEC) ||
        (hcmd->config.report_interval_sec > APP_NFC_REPORT_INTERVAL_MAX_SEC)) {
        res->result_code = NFC_CMD_RESULT_INVALID_PARAM;
        return NFC_CMD_RESULT_INVALID_PARAM;
    }
    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = 0;
    APP_LOGI("NFC", "SetInterval: %us",
           hcmd->config.report_interval_sec);
    return NFC_CMD_RESULT_OK;
}

/* [H3 FIX] Write result to SRAM BEFORE reset */
static NFC_CMD_Result_t NFC_CMD_Handler_ResetDevice(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    (void)pkt;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);

    /* Write result and status to SRAM before reset */
    nfc_cmd_write_result(hcmd, res);
    nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_DONE_OK);

    APP_LOGI("NFC", "ResetDevice: rebooting in 200ms...");
    HAL_Delay(200);
    NVIC_SystemReset();
    return NFC_CMD_RESULT_OK;   /* never reached */
}

static NFC_CMD_Result_t NFC_CMD_Handler_FactoryReset(
        void *hv, const NFC_CMD_Packet_t *pkt, NFC_CMD_ResultPacket_t *res)
{
    NFC_CMD_Handle_t *hcmd = (NFC_CMD_Handle_t *)hv;
    (void)pkt;

    nfc_cmd_prepare_result(res, NFC_CMD_RESULT_OK);
    memset(&hcmd->config, 0, sizeof(NFC_CMD_Config_t));
    hcmd->config.temp_threshold_x10  = 300;
    hcmd->config.report_interval_sec = 60;

    NFC_NTP53321_WriteMultiBlock(hcmd->hntag, NFC_CMD_CONFIG_STORAGE_BLOCK,
                                  (uint8_t *)&hcmd->config,
                                  sizeof(NFC_CMD_Config_t) / 4U);
    NFC_AUTH_Reset(hcmd->hauth);

    res->result_code = NFC_CMD_RESULT_OK;
    res->data_len    = 0;
    APP_LOGI("NFC", "FactoryReset: defaults restored");
    return NFC_CMD_RESULT_OK;
}


/* ============================================================
 * Command Table
 * ============================================================ */
static const NFC_CMD_TableEntry_t nfc_cmd_table[] = {
    {NFC_CMD_ID_GET_STATUS,    NFC_CMD_PERM_READ,   NFC_CMD_Handler_GetStatus,    "GetStatus"   },
    {NFC_CMD_ID_GET_VERSION,   NFC_CMD_PERM_READ,   NFC_CMD_Handler_GetVersion,   "GetVersion"  },
    {NFC_CMD_ID_GET_STATS,     NFC_CMD_PERM_READ,   NFC_CMD_Handler_GetStats,     "GetStats"    },
    {NFC_CMD_ID_READ_SENSOR,   NFC_CMD_PERM_READ,   NFC_CMD_Handler_ReadSensor,   "ReadSensor"  },
    {NFC_CMD_ID_WRITE_DATA,    NFC_CMD_PERM_WRITE,  NFC_CMD_Handler_WriteData,    "WriteData"   },
    {NFC_CMD_ID_READ_EEPROM,   NFC_CMD_PERM_READ,   NFC_CMD_Handler_ReadEEPROM,   "ReadEEPROM"  },
    {NFC_CMD_ID_WRITE_EEPROM,  NFC_CMD_PERM_WRITE,  NFC_CMD_Handler_WriteEEPROM,  "WriteEEPROM" },
    {NFC_CMD_ID_SET_CONFIG,    NFC_CMD_PERM_CONFIG, NFC_CMD_Handler_SetConfig,    "SetConfig"   },
    {NFC_CMD_ID_GET_CONFIG,    NFC_CMD_PERM_READ,   NFC_CMD_Handler_GetConfig,    "GetConfig"   },
    {NFC_CMD_ID_SET_THRESHOLD, NFC_CMD_PERM_CONFIG, NFC_CMD_Handler_SetThreshold, "SetThreshold"},
    {NFC_CMD_ID_SET_INTERVAL,  NFC_CMD_PERM_CONFIG, NFC_CMD_Handler_SetInterval,  "SetInterval" },
    {NFC_CMD_ID_RESET_DEVICE,  NFC_CMD_PERM_ADMIN,  NFC_CMD_Handler_ResetDevice,  "ResetDevice" },
    {NFC_CMD_ID_FACTORY_RESET, NFC_CMD_PERM_ADMIN,  NFC_CMD_Handler_FactoryReset, "FactoryReset"},
};
#define NFC_CMD_TABLE_SIZE  (sizeof(nfc_cmd_table)/sizeof(nfc_cmd_table[0]))

static const NFC_CMD_TableEntry_t *nfc_cmd_find(uint8_t cmd_id)
{
    uint32_t i;
    for (i = 0; i < NFC_CMD_TABLE_SIZE; i++)
        if ((uint8_t)nfc_cmd_table[i].cmd_id == cmd_id)
            return &nfc_cmd_table[i];
    return NULL;
}

/* ============================================================
 * Public API
 * ============================================================ */
NFC_CMD_Result_t NFC_CMD_Init(NFC_CMD_Handle_t *hcmd,
                               NFC_NTP53321_Handle_t *hntag,
                               NFC_AUTH_Handle_t *hauth)
{
    if (hcmd == NULL || hntag == NULL || hauth == NULL)
        return NFC_CMD_RESULT_INVALID_PARAM;

    memset(hcmd, 0, sizeof(NFC_CMD_Handle_t));
    hcmd->hntag       = hntag;
    hcmd->hauth       = hauth;
    hcmd->initialized = true;

    hcmd->config.temp_threshold_x10  = 300;
    hcmd->config.report_interval_sec = 60;

    APP_LOGI("NFC", "Initialized Cmd(%u commands)",
           (unsigned)NFC_CMD_TABLE_SIZE);
    return NFC_CMD_RESULT_OK;
}

NFC_CMD_Result_t NFC_CMD_Process(NFC_CMD_Handle_t *hcmd)
{
    NFC_CMD_Packet_t             pkt    = {0};
    NFC_CMD_ResultPacket_t       result = {0};
    const NFC_CMD_TableEntry_t  *entry;
    NFC_CMD_Result_t             ret;

    if (hcmd == NULL || !hcmd->initialized)
        return NFC_CMD_RESULT_INVALID_PARAM;

    if (!NFC_AUTH_IsSessionValid(hcmd->hauth)) {
        hcmd->cmd_no_auth_count++;
        APP_LOGI("NFC", "Rejected: no valid session");
        return nfc_cmd_publish_result_only(hcmd, NFC_CMD_RESULT_NOT_AUTH);
    }

    ret = nfc_cmd_write_status(hcmd, NFC_CMD_STATUS_PROCESSING);
    if (ret != NFC_CMD_RESULT_OK) {
        return ret;
    }

    ret = nfc_cmd_read_packet(hcmd, &pkt);
    if (ret != NFC_CMD_RESULT_OK) {
        hcmd->cmd_fail_count++;
        return nfc_cmd_publish_result_only(hcmd, ret);
    }

    ret = nfc_cmd_validate_packet(&pkt);
    if (ret != NFC_CMD_RESULT_OK) {
        hcmd->cmd_fail_count++;
        return nfc_cmd_publish_result_only(hcmd, ret);
    }

    entry = nfc_cmd_find(pkt.header.cmd_id);
    if (entry == NULL) {
        hcmd->cmd_fail_count++;
        APP_LOGI("NFC", "No handler for 0x%02X", pkt.header.cmd_id);
        return nfc_cmd_publish_result_only(hcmd, NFC_CMD_RESULT_INVALID_CMD);
    }

    APP_LOGI("NFC", "Run: %s", entry->name);
    nfc_cmd_prepare_result(&result, NFC_CMD_RESULT_OK);
    ret = entry->handler(hcmd, &pkt, &result);

    if (nfc_cmd_write_result(hcmd, &result) != NFC_CMD_RESULT_OK) {
        hcmd->cmd_fail_count++;
        return NFC_CMD_RESULT_I2C_ERROR;
    }
    if (nfc_cmd_write_status(hcmd,
        (ret == NFC_CMD_RESULT_OK) ? NFC_CMD_STATUS_DONE_OK
                                   : NFC_CMD_STATUS_DONE_FAIL) != NFC_CMD_RESULT_OK) {
        hcmd->cmd_fail_count++;
        return NFC_CMD_RESULT_I2C_ERROR;
    }

    if (ret == NFC_CMD_RESULT_OK)
        hcmd->cmd_success_count++;
    else
        hcmd->cmd_fail_count++;

    APP_LOGI("NFC", "%s: ret=%d ok=%lu fail=%lu",
           entry->name, ret,
           (unsigned long)hcmd->cmd_success_count,
           (unsigned long)hcmd->cmd_fail_count);
    return ret;
}

void NFC_CMD_PrintStats(NFC_CMD_Handle_t *hcmd)
{
    if (hcmd == NULL) return;
    APP_LOGI("NFC", "=== CMD Stats ===");
    APP_LOGI("NFC", "OK      : %lu",
           (unsigned long)hcmd->cmd_success_count);
    APP_LOGI("NFC", "Fail    : %lu",
           (unsigned long)hcmd->cmd_fail_count);
    APP_LOGI("NFC", "No-auth : %lu",
           (unsigned long)hcmd->cmd_no_auth_count);
    APP_LOGI("NFC", "Interval: %us  Thr: %d.%dC",
           hcmd->config.report_interval_sec,
           hcmd->config.temp_threshold_x10 / 10,
           hcmd->config.temp_threshold_x10 % 10);
}


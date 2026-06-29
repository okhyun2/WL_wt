/**
 * @file    nfc_ntag5_ntp53321.c
 * @brief   NTP53321 Driver  [v3.0.0]
 *
 * Changes from v2.2:
 *  - [FIX] Session register 주소 전면 수정
 *    * 0x10A0 = STATUS_REG  (이전 코드에서 CONFIG로 잘못 사용됨)
 *    * 0x10A1 = CONFIG_REG  (CONFIG_0/1/2_REG 포함)
 *    * ED_CONF / WDT / I2C_SLAVE_CONFIG → 구성 메모리 0x103D/0x103C/0x103E
 *  - [NEW] NFC_NTP53321_WriteSessionReg / ReadSessionReg 구현
 *    * I2C WRITE REGISTER 패킷: [BL_AD1][BL_AD0][REGA][MASK][REGDAT]
 *    * HAL_I2C_Master_Transmit 직접 사용 (Mem_Write 사용 불가)
 *  - [FIX] SetEDMode: STATUS_REG → 구성 메모리(0x103D) + CONFIG_REG(0x10A1)
 *  - [FIX] EnableSRAMMirror: 구성 메모리(0x1037) CONFIG_REG(0x10A1) 분리
 *  - [FIX] NFC_CC_VERSION 0x40 → 0x10 (NDEF Mapping v1.0)
 */

#include "nfc_ntag5_ntp53321.h"
#include <stdio.h>
#include "app_log.h"

/* ============================================================
 * Private prototypes
 * ============================================================ */
static NFC_Result_t nfc_i2c_mem_write(NFC_NTP53321_Handle_t *h,
                                       uint16_t block_addr,
                                       const uint8_t *data, uint16_t len);
static NFC_Result_t nfc_i2c_mem_read(NFC_NTP53321_Handle_t *h,
                                      uint16_t block_addr,
                                      uint8_t *data, uint16_t len);
static NFC_Result_t nfc_i2c_reg_write(NFC_NTP53321_Handle_t *h,
                                       uint16_t block_addr,
                                       uint8_t  reg_offset,
                                       uint8_t  mask,
                                       uint8_t  value);
static NFC_Result_t nfc_i2c_reg_read(NFC_NTP53321_Handle_t *h,
                                      uint16_t block_addr,
                                      uint8_t  reg_offset,
                                      uint8_t  *out_value);

/* ============================================================
 * Init / DeInit / Reset
 * ============================================================ */
NFC_Result_t NFC_NTP53321_Init(NFC_NTP53321_Handle_t *hntag, I2C_HandleTypeDef *hi2c)
{
    NFC_Result_t ret;
    uint8_t      reg_block[4] = {0};

    if (hntag == NULL || hi2c == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;

    memset(hntag, 0, sizeof(NFC_NTP53321_Handle_t));
    hntag->hi2c  = hi2c;
    hntag->state = NFC_STATE_UNINITIALIZED;

    /* CC Block 읽기로 I2C 링크 확인 */
    ret = nfc_i2c_mem_read(hntag,
                           NFC_BLOCK_TO_I2C_ADDR(NFC_CC_BLOCK_ADDR),
                           reg_block, 4U);
    if (ret != NFC_RESULT_OK) {
        APP_LOGE("NFC", "Init: I2C read FAILED (ret=%d)", ret);
        hntag->state = NFC_STATE_ERROR;
        return ret;
    }

    APP_LOGI("NFC", "Get CC Block: %02X %02X %02X %02X",
             reg_block[0], reg_block[1], reg_block[2], reg_block[3]);

    if (reg_block[0] != NFC_CC_MAGIC_BYTE) {
        APP_LOGE("NFC", "CC Magic FAIL: 0x%02X (expected 0x%02X)",
                 reg_block[0], NFC_CC_MAGIC_BYTE);
        hntag->state = NFC_STATE_ERROR;
        return NFC_RESULT_ERROR;
    }

    /* STATUS 레지스터 읽기 (0x10A0) — VCC/NFC 부트 확인 */
    {
        uint8_t status0 = 0U;
        uint8_t status1 = 0U;

        ret = nfc_i2c_reg_read(hntag, NFC_SESSION_STATUS_ADDR, 0U, &status0);
        if (ret == NFC_RESULT_OK) {
            nfc_i2c_reg_read(hntag, NFC_SESSION_STATUS_ADDR, 1U, &status1);
            APP_LOGI("NFC", "STATUS0=0x%02X STATUS1=0x%02X", status0, status1);
            APP_LOGI("NFC", "  VCC_OK=%d NFC_FIELD=%d VCC_BOOT=%d NFC_BOOT=%d",
                     (status0 & NFC_STATUS0_VCC_SUPPLY_OK) ? 1 : 0,
                     (status0 & NFC_STATUS0_NFC_FIELD_OK)  ? 1 : 0,
                     (status1 & NFC_STATUS1_VCC_BOOT_OK)   ? 1 : 0,
                     (status1 & NFC_STATUS1_NFC_BOOT_OK)   ? 1 : 0);
        }
    }

    /* Enable SRAM */
    ret = nfc_i2c_mem_read(hntag,
                           NFC_BLOCK_TO_I2C_ADDR(NFC_CFG_CONFIG_ADDR),
                           reg_block, 4U);
    if (ret != NFC_RESULT_OK)
    {
        APP_LOGE("NFC", "Init: I2C read FAILED (ret=%d)", ret);
        hntag->state = NFC_STATE_ERROR;
        return ret;
    }
    else
    {
        APP_LOGI("NFC", "Get Config Block: %02X %02X %02X %02X",
                 reg_block[0], reg_block[1], reg_block[2], reg_block[3]);

        reg_block[1] |= 0x02;
        ret = nfc_i2c_mem_write(hntag,
                                NFC_BLOCK_TO_I2C_ADDR(NFC_CFG_CONFIG_ADDR),
                                reg_block, 4U);
        if (ret == NFC_RESULT_OK)
        {
            APP_LOGI("NFC", "Enable SRAM");
        }
    }

    /* ED 모드 설정 (구성 메모리 0x103D의 ED_CONF 바이트 사용) */
    ret = NFC_NTP53321_SetEDMode(hntag, NFC_ED_MODE_FIELD_DETECT);
    if (ret != NFC_RESULT_OK)
    {
        APP_LOGE("NFC", "SetEDMode FAILED (ret=%d)", ret);
        hntag->state = NFC_STATE_ERROR;
        return ret;
    }

    hntag->state = NFC_STATE_IDLE;
    APP_LOGI("NFC", "NTP53321 initialized");
    return NFC_RESULT_OK;
}

NFC_Result_t NFC_NTP53321_DeInit(NFC_NTP53321_Handle_t *hntag)
{
    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;
    HAL_NVIC_DisableIRQ(EXTI4_15_IRQn);
    HAL_GPIO_DeInit(NFC_ED_GPIO_Port, NFC_ED_Pin);
    hntag->state = NFC_STATE_UNINITIALIZED;
    APP_LOGI("NFC", "De-initialized");
    return NFC_RESULT_OK;
}

NFC_Result_t NFC_NTP53321_Reset(NFC_NTP53321_Handle_t *hntag)
{
    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;
    NFC_NTP53321_DeInit(hntag);
    HAL_Delay(10U);
    return NFC_NTP53321_Init(hntag, hntag->hi2c);
}

/* ============================================================
 * Configuration
 * ============================================================ */

/**
 * @brief ED 모드 설정
 *
 * ED_CONF는 구성 메모리 0x103D(NFC_CFG_EH_ED_CONFIG_ADDR)의 Byte2에 위치합니다.
 * → WRITE MEMORY (nfc_i2c_mem_write) 사용: 4바이트 블록 전체를 읽은 후 Byte2만 수정.
 *
 * CONFIG_REG(0x10A1)는 세션 레지스터이며 POR 시 CONFIG(0x1037)에서 복사됩니다.
 * ED 기능 활성화는 ED_CONF 필드(0x103D Byte2)로 제어합니다.
 */
NFC_Result_t NFC_NTP53321_SetEDMode(NFC_NTP53321_Handle_t *hntag, NFC_EDMode_t mode)
{
    uint8_t      blk[4] = {0};
    NFC_Result_t ret;

    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;

    /* 0x103D 블록 읽기 (Byte0=EH_CONF, Byte1=RFU, Byte2=ED_CONF, Byte3=RFU) */
    ret = nfc_i2c_mem_read(hntag,
                           NFC_BLOCK_TO_I2C_ADDR(NFC_CFG_EH_ED_CONFIG_ADDR),
                           blk, 4U);
    if (ret != NFC_RESULT_OK) return ret;

    /* Byte2 = ED_CONF 필드 수정 (하위 4비트만 사용, 상위 4비트는 RFU=0) */
    blk[2] = (uint8_t)((blk[2] & 0xF0U) | ((uint8_t)mode & 0x0FU));

    ret = nfc_i2c_mem_write(hntag,
                            NFC_BLOCK_TO_I2C_ADDR(NFC_CFG_EH_ED_CONFIG_ADDR),
                            blk, 4U);
    if (ret == NFC_RESULT_OK) {
        hntag->ed_mode = mode;
        APP_LOGI("NFC", "ED mode = 0x%02X (%s)", (uint8_t)mode,
            (mode == NFC_ED_MODE_DISABLED)     ? "Disabled"      :
            (mode == NFC_ED_MODE_FIELD_DETECT) ? "Field Detect"  :
            (mode == NFC_ED_MODE_DATA_READY)   ? "NFC->I2C PT"   :
                                                 "SW Interrupt");
    }
    return ret;
}

/**
 * @brief CC Block 초기화 (User EEPROM block 0)
 *
 * CC Block은 User EEPROM(0x0000)에 위치하며 WRITE MEMORY로 접근합니다.
 */
NFC_Result_t NFC_NTP53321_ConfigureCC(NFC_NTP53321_Handle_t *hntag)
{
    uint8_t cc[4] = {
        NFC_CC_MAGIC_BYTE,   /* 0xE1 */
        NFC_CC_VERSION,      /* 0x10 (NDEF Mapping v1.0) */
        NFC_CC_SIZE,         /* 0x40 (0x40×8 = 512 bytes) */
        NFC_CC_ACCESS        /* 0x00 (Read/Write) */
    };
    NFC_Result_t ret;

    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;

    ret = nfc_i2c_mem_write(hntag,
                            NFC_BLOCK_TO_I2C_ADDR(NFC_CC_BLOCK_ADDR),
                            cc, 4U);
    if (ret == NFC_RESULT_OK)
        APP_LOGI("NFC", "Set CC block = %02X %02X %02X %02X",
                 cc[0], cc[1], cc[2], cc[3]);
    return ret;
}

/**
 * @brief SRAM Mirror 활성화/비활성화
 *
 * ARBITER_MODE는 CONFIG_REG(세션, 0x10A1 Byte1)의 bit[3:2]로 즉시 제어합니다.
 * → WRITE REGISTER 사용 (nfc_i2c_reg_write)
 *   block=0x10A1, reg_offset=1(Byte1=CONFIG_1_REG),
 *   mask=0x0C(bit3:2), value=0x04(mirror) 또는 0x00(normal)
 */
NFC_Result_t NFC_NTP53321_EnableSRAMMirror(NFC_NTP53321_Handle_t *hntag, bool enable)
{
    NFC_Result_t ret;
    uint8_t      value;
    uint8_t      status0 = 0U;
    uint8_t      status1 = 0U;
    uint8_t      cfg1 = 0U;
    uint8_t      arbiter = 0U;

    if ((hntag == NULL) || (hntag->hi2c == NULL))
    {
        return NFC_RESULT_ERROR_INVALID_PARAM;
    }

    value = enable ? NFC_CONFIG1_ARBITER_SRAM_MIRROR : NFC_CONFIG1_ARBITER_NORMAL;

    ret = NFC_NTP53321_ReadSessionReg(hntag, NFC_SESSION_STATUS_ADDR, 0U, &status0);
    if (ret != NFC_RESULT_OK)
    {
        return ret;
    }

    ret = NFC_NTP53321_ReadSessionReg(hntag, NFC_SESSION_STATUS_ADDR, 1U, &status1);
    if (ret != NFC_RESULT_OK)
    {
        return ret;
    }

    if ((status1 & NFC_STATUS1_I2C_IF_LOCKED) != 0u)
    {
    APP_LOGE("NFC", "I2C_IF_LOCKED");
        return NFC_RESULT_ERROR_BUSY;
    }

    if (enable)
    {
        if (((status0 & NFC_STATUS0_VCC_SUPPLY_OK) == 0u) ||
            ((status0 & NFC_STATUS0_NFC_FIELD_OK) == 0u) ||
            ((status1 & NFC_STATUS1_VCC_BOOT_OK) == 0u) ||
            ((status1 & NFC_STATUS1_NFC_BOOT_OK) == 0u))
        {
            return NFC_RESULT_ERROR_BUSY;
        }
    }

    ret = nfc_i2c_reg_write(hntag,
                            NFC_SESSION_CONFIG_REG_ADDR,
                            1U,
                            NFC_CONFIG1_ARBITER_MODE_MASK,
                            value);
    if (ret != NFC_RESULT_OK)
    {
        return ret;
    }

    ret = NFC_NTP53321_ReadSessionReg(hntag,
                                      NFC_SESSION_CONFIG_REG_ADDR,
                                      1U,
                                      &cfg1);
    if (ret != NFC_RESULT_OK)
    {
        return ret;
    }

    arbiter = (uint8_t)(cfg1 & NFC_CONFIG1_ARBITER_MODE_MASK);
    if ((arbiter != value) || (enable && ((cfg1 & NFC_CONFIG1_SRAM_ENABLED) == 0u)))
    {
        APP_LOGE("NFC", "SRAM_EN");
        return NFC_RESULT_ERROR_BUSY;
    }

    APP_LOGI("NFC", "SRAM mirror %s cfg1=0x%02X", enable ? "ON" : "OFF", (unsigned int)cfg1);
    return NFC_RESULT_OK;
}

/* ============================================================
 * Memory Access  (WRITE MEMORY / READ MEMORY)
 * ============================================================ */
NFC_Result_t NFC_NTP53321_ReadBlock(NFC_NTP53321_Handle_t *hntag,
                                     uint16_t block_addr, uint8_t *data)
{
    if (hntag == NULL || data == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;
    return nfc_i2c_mem_read(hntag,
                            NFC_BLOCK_TO_I2C_ADDR(block_addr),
                            data, NFC_EEPROM_BLOCK_SIZE);
}

NFC_Result_t NFC_NTP53321_WriteBlock(NFC_NTP53321_Handle_t *hntag,
                                      uint16_t block_addr, const uint8_t *data)
{
    if (hntag == NULL || data == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;
    return nfc_i2c_mem_write(hntag,
                             NFC_BLOCK_TO_I2C_ADDR(block_addr),
                             data, NFC_EEPROM_BLOCK_SIZE);
}

NFC_Result_t NFC_NTP53321_ReadMultiBlock(NFC_NTP53321_Handle_t *hntag,
                                          uint16_t block_addr,
                                          uint8_t *data, uint16_t num_blocks)
{
    if (hntag == NULL || data == NULL || num_blocks == 0U)
        return NFC_RESULT_ERROR_INVALID_PARAM;
    return nfc_i2c_mem_read(hntag,
                            NFC_BLOCK_TO_I2C_ADDR(block_addr),
                            data,
                            num_blocks * NFC_EEPROM_BLOCK_SIZE);
}

NFC_Result_t NFC_NTP53321_WriteMultiBlock(NFC_NTP53321_Handle_t *hntag,
                                           uint16_t block_addr,
                                           const uint8_t *data, uint16_t num_blocks)
{
    NFC_Result_t ret;
    uint16_t     last_block;
    uint16_t     i;
    bool         is_eeprom;
    bool         is_sram;

    if (hntag == NULL || data == NULL || num_blocks == 0U)
        return NFC_RESULT_ERROR_INVALID_PARAM;

    last_block = (uint16_t)(block_addr + num_blocks - 1U);
    is_eeprom = ((block_addr >= NFC_EEPROM_BASE_ADDR) && (last_block <= NFC_EEPROM_END_ADDR));
    is_sram = ((block_addr >= NFC_SRAM_BASE_ADDR) && (last_block <= NFC_SRAM_END_ADDR));
    if ((!is_eeprom) && (!is_sram))
    {
        APP_LOGE("NFC", "WriteMultiBlock range invalid start=0x%04X blocks=%u",
                 (unsigned int)block_addr,
                 (unsigned int)num_blocks);
        return NFC_RESULT_ERROR_INVALID_PARAM;
    }

    if (num_blocks == 1U)
        return NFC_NTP53321_WriteBlock(hntag, block_addr, data);

    ret = nfc_i2c_mem_write(hntag,
                            NFC_BLOCK_TO_I2C_ADDR(block_addr),
                            data,
                            num_blocks * NFC_EEPROM_BLOCK_SIZE);
    if (ret == NFC_RESULT_OK)
        return NFC_RESULT_OK;

    APP_LOGW("NFC", "WriteMultiBlock burst failed start=0x%04X blocks=%u, fallback single-block",
             (unsigned int)block_addr,
             (unsigned int)num_blocks);

    for (i = 0U; i < num_blocks; i++)
    {
        ret = NFC_NTP53321_WriteBlock(hntag,
                                      (uint16_t)(block_addr + i),
                                      &data[i * NFC_EEPROM_BLOCK_SIZE]);
        if (ret != NFC_RESULT_OK)
        {
            APP_LOGE("NFC", "WriteMultiBlock fallback fail blk=0x%04X idx=%u ret=%d",
                     (unsigned int)(block_addr + i),
                     (unsigned int)i,
                     (int)ret);
            return ret;
        }

        if (is_eeprom)
            HAL_Delay(2U);
    }

    return NFC_RESULT_OK;
}

/* ============================================================
 * Session Register Access  (WRITE REGISTER / READ REGISTER)
 *
 * NXP 공식 패킷 구조 (데이터시트 Figure 11):
 *   WRITE: [START][SL_AD+W][BL_AD1][BL_AD0][REGA][MASK][REGDAT][STOP]
 *   READ:  [START][SL_AD+W][BL_AD1][BL_AD0][REGA][rSTART][SL_AD+R][REGDAT][STOP]
 *
 * HAL_I2C_Mem_Write 는 내부적으로 [SL_AD+W][MemAddr MSB][MemAddr LSB][Data...]
 * 형태이므로, WRITE REGISTER의 [REGA][MASK][REGDAT] 3바이트를 Data 부분에
 * 넣으면 패킷이 올바르게 생성됩니다.
 * ============================================================ */
NFC_Result_t NFC_NTP53321_WriteSessionReg(NFC_NTP53321_Handle_t *hntag,
                                           uint16_t block_addr,
                                           uint8_t  reg_offset,
                                           uint8_t  mask,
                                           uint8_t  value)
{
    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;
    return nfc_i2c_reg_write(hntag, block_addr, reg_offset, mask, value);
}

NFC_Result_t NFC_NTP53321_ReadSessionReg(NFC_NTP53321_Handle_t *hntag,
                                          uint16_t block_addr,
                                          uint8_t  reg_offset,
                                          uint8_t  *out_value)
{
    if (hntag == NULL || out_value == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;
    return nfc_i2c_reg_read(hntag, block_addr, reg_offset, out_value);
}

/* ============================================================
 * NDEF Write / Read
 * ============================================================ */
NFC_Result_t NFC_NTP53321_WriteNDEFText(NFC_NTP53321_Handle_t *hntag, const char *text)
{
    uint8_t      buf[64] = {0};
    uint16_t     idx     = 0U;
    uint16_t     text_len;
    uint16_t     payload_len;
    uint16_t     num_blocks;
    NFC_Result_t ret;

    if (hntag == NULL || text == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;

    text_len    = (uint16_t)strlen(text);
    payload_len = 3U + text_len;  /* status(1) + 'e'(1) + 'n'(1) + text */

    /* NDEF TLV */
    buf[idx++] = 0x03U;                              /* T: NDEF Message */
    buf[idx++] = (uint8_t)(1U + 1U + 1U + payload_len); /* L: Record len */

    /* NDEF Record Header: MB=1 ME=1 SR=1 TNF=001 (Well-Known) */
    buf[idx++] = 0xD1U;
    buf[idx++] = 0x01U;                  /* Type Length = 1 */
    buf[idx++] = (uint8_t)payload_len;   /* Payload Length */
    buf[idx++] = 'T';                    /* Type = Text */

    /* Text Record Payload */
    buf[idx++] = 0x02U;   /* Status: UTF-8, language code length = 2 */
    buf[idx++] = 'e';
    buf[idx++] = 'n';
    memcpy(&buf[idx], text, text_len);
    idx += text_len;

    /* Terminator TLV */
    buf[idx++] = 0xFEU;

    /* 4바이트 경계로 패딩 */
    while ((idx % 4U) != 0U)
        buf[idx++] = 0x00U;

    num_blocks = idx / 4U;
    ret = NFC_NTP53321_WriteMultiBlock(hntag, NFC_NDEF_START_BLOCK,
                                       buf, num_blocks);
    if (ret == NFC_RESULT_OK)
        APP_LOGI("NFC", "NDEF written: \"%s\" (%u blocks)",
                 text, (unsigned int)num_blocks);
    return ret;
}

NFC_Result_t NFC_NTP53321_ReadNDEFText(NFC_NTP53321_Handle_t *hntag,
                                        char *text, uint16_t max_len)
{
    uint8_t        buf[64]     = {0};
    uint16_t       copy_len;
    /* text_start: TLV(2) + RecHeader(3) + Type(1) + status(1) + lang(2) = 9
     * buf[0]=0x03, buf[1]=L, buf[2]=0xD1, buf[3]=0x01, buf[4]=PayLen,
     * buf[5]='T',  buf[6]=0x02, buf[7]='e', buf[8]='n', buf[9~]=text */
    const uint16_t text_start  = 9U;
    NFC_Result_t   ret;

    if (hntag == NULL || text == NULL || max_len == 0U)
        return NFC_RESULT_ERROR_INVALID_PARAM;

    ret = NFC_NTP53321_ReadMultiBlock(hntag, NFC_NDEF_START_BLOCK, buf, 16U);
    if (ret != NFC_RESULT_OK) return ret;

    if (buf[0] != 0x03U) return NFC_RESULT_ERROR;

    /* buf[4] = Payload Length, 텍스트 실제 길이 = PayloadLen - 3 (status+lang) */
    copy_len = (buf[4] > 3U) ? (uint16_t)(buf[4] - 3U) : 0U;
    if (copy_len >= max_len) copy_len = max_len - 1U;

    memcpy(text, &buf[text_start], copy_len);
    text[copy_len] = '\0';

    APP_LOGI("NFC", "NDEF read: \"%s\"", text);
    return NFC_RESULT_OK;
}

/* ============================================================
 * UID
 * ============================================================ */
NFC_Result_t NFC_NTP53321_GetUID(NFC_NTP53321_Handle_t *hntag, uint8_t *uid)
{
    uint8_t      buf[8] = {0};
    NFC_Result_t ret;

    if (hntag == NULL || uid == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;

    ret = NFC_NTP53321_ReadMultiBlock(hntag, NFC_EEPROM_BASE_ADDR + 1U, buf, 2U);
    if (ret != NFC_RESULT_OK) return ret;

    memcpy(uid, buf, 7U);
    if (!hntag->uid_valid) {
        memcpy(hntag->uid, uid, 7U);
        hntag->uid_valid = true;
    }
    APP_LOGI("NFC", "UID: %02X %02X %02X %02X %02X %02X %02X",
             uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
    return NFC_RESULT_OK;
}

/**
 * @brief NTP53321 Standby 모드 진입
 *
 * 레지스터: CONFIG_0_REG (세션, 0x10A1 Byte0)
 * bit0 = AUTO_STANDBY_MODE_EN = 1
 * MASK = 0x01 (bit0만 변경)
 *
 * 주의: Standby 진입 후 I2C 통신 불가.
 *       NFC 필드 감지 또는 HPD 펄스로만 복귀.
 */
NFC_Result_t NFC_NTP53321_EnterStandby(NFC_NTP53321_Handle_t *hntag)
{
    NFC_Result_t ret;

    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;
    if (hntag->state == NFC_STATE_UNINITIALIZED)
        return NFC_RESULT_ERROR_NOT_INIT;

    /* CONFIG_0_REG (0x10A1 Byte0) bit0 = 1 */
    ret = NFC_NTP53321_WriteSessionReg(hntag,
                                       NFC_SESSION_CONFIG_REG_ADDR, /* 0x10A1 */
                                       0U,    /* Byte0 = CONFIG_0_REG */
                                       0x01U, /* MASK: bit0만 변경 */
                                       0x01U);/* VALUE: bit0 = 1 */
    if (ret == NFC_RESULT_OK) {
        hntag->state = NFC_STATE_STOP;
        hntag->sleep_enter_tick = HAL_GetTick();
        APP_LOGI("NFC", "Enter Standby (<6uA). NFC field wakeup OK.");
    } else {
        APP_LOGE("NFC", "Enter Standby FAILED (ret=%d)", ret);
    }
    return ret;
}

/**
 * @brief NTP53321 Standby 복귀 확인
 *
 * Standby에서 NFC 필드로 자동 복귀 후, STATUS_REG(0x10A0 Byte0)의
 * VCC_SUPPLY_OK(bit1) 와 NFC_BOOT_OK(STATUS1 bit6) 확인.
 * I2C 통신이 다시 가능한지 검증.
 */
NFC_Result_t NFC_NTP53321_ExitStandby(NFC_NTP53321_Handle_t *hntag)
{
    NFC_Result_t ret;
    uint8_t      status0 = 0U;
    uint8_t      status1 = 0U;

    if (hntag == NULL) return NFC_RESULT_ERROR_INVALID_PARAM;

    /* STATUS0 읽기 시도 — 성공하면 Standby에서 복귀된 것 */
    ret = NFC_NTP53321_ReadSessionReg(hntag,
                                      NFC_SESSION_STATUS_ADDR, /* 0x10A0 */
                                      0U,                       /* Byte0 = STATUS0 */
                                      &status0);
    if (ret != NFC_RESULT_OK) {
        APP_LOGE("NFC", "ExitStandby: I2C still unavailable (ret=%d)", ret);
        return NFC_RESULT_ERROR_BUSY;
    }

    NFC_NTP53321_ReadSessionReg(hntag, NFC_SESSION_STATUS_ADDR, 1U, &status1);

    APP_LOGI("NFC", "ExitStandby: STATUS0=0x%02X STATUS1=0x%02X", status0, status1);
    APP_LOGI("NFC", "  VCC_OK=%d NFC_FIELD=%d VCC_BOOT=%d NFC_BOOT=%d",
             (status0 & NFC_STATUS0_VCC_SUPPLY_OK) ? 1 : 0,
             (status0 & NFC_STATUS0_NFC_FIELD_OK)  ? 1 : 0,
             (status1 & NFC_STATUS1_VCC_BOOT_OK)   ? 1 : 0,
             (status1 & NFC_STATUS1_NFC_BOOT_OK)   ? 1 : 0);

    /* CONFIG_0_REG bit0 다시 0으로 클리어 (정상 모드 복귀) */
    NFC_NTP53321_WriteSessionReg(hntag,
                                 NFC_SESSION_CONFIG_REG_ADDR,
                                 0U,    /* Byte0 */
                                 0x01U, /* MASK */
                                 0x00U);/* bit0 = 0 */

    hntag->state = NFC_STATE_IDLE;
    hntag->stats.total_sleep_ticks +=
        (HAL_GetTick() - hntag->sleep_enter_tick);
    APP_LOGI("NFC", "ExitStandby OK");
    return NFC_RESULT_OK;
}

/* ============================================================
 * ED / Event Handling
 * ============================================================ */
bool NFC_NTP53321_IsEDTriggered(NFC_NTP53321_Handle_t *hntag)
{
    if (hntag == NULL) return false;
    return hntag->ed_flag;
}

void NFC_NTP53321_ClearEDFlag(NFC_NTP53321_Handle_t *hntag)
{
    if (hntag == NULL) return;
    hntag->ed_flag = false;
}

void NFC_NTP53321_NotifyDeferredEdEvent(NFC_NTP53321_Handle_t *hntag)
{
    if (hntag == NULL) return;
    hntag->ed_flag = true;
    hntag->stats.wakeup_count++;
    hntag->stats.last_wakeup_tick = HAL_GetTick();
}

void NFC_NTP53321_ED_EXTI_IRQHandler(NFC_NTP53321_Handle_t *hntag)
{
    NFC_NTP53321_NotifyDeferredEdEvent(hntag);
}

/* ============================================================
 * Statistics
 * ============================================================ */
void NFC_NTP53321_GetStats(NFC_NTP53321_Handle_t *hntag, NFC_NTP53321_Stats_t *stats)
{
    if (hntag == NULL || stats == NULL) return;
    memcpy(stats, &hntag->stats, sizeof(NFC_NTP53321_Stats_t));
}

void NFC_NTP53321_PrintStats(NFC_NTP53321_Handle_t *hntag)
{
    if (hntag == NULL) return;
    APP_LOGI("NFC", "=== Driver Stats ===");
    APP_LOGI("NFC", "Wakeups    : %lu", (unsigned long)hntag->stats.wakeup_count);
    APP_LOGI("NFC", "I2C errors : %lu", (unsigned long)hntag->stats.i2c_error_count);
    APP_LOGI("NFC", "Active ms  : %lu", (unsigned long)hntag->stats.total_active_ticks);
    APP_LOGI("NFC", "Sleep ms   : %lu", (unsigned long)hntag->stats.total_sleep_ticks);
}

/* ============================================================
 * Private: WRITE MEMORY / READ MEMORY
 * HAL_I2C_Mem_Write/Read → [SL_AD][BL_AD1][BL_AD0][D0..DN]
 * User EEPROM, SRAM, Config Memory 접근에 사용
 * ============================================================ */
static NFC_Result_t nfc_i2c_mem_write(NFC_NTP53321_Handle_t *h,
                                       uint16_t block_addr,
                                       const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef hal_ret;
    uint32_t          hal_err;
    uint8_t           retry = 0U;

    if ((h == NULL) || (h->hi2c == NULL) || (data == NULL) || (len == 0U))
        return NFC_RESULT_ERROR_INVALID_PARAM;

    do {
        hal_ret = HAL_I2C_Mem_Write(h->hi2c,
                                    NFC_NTP53321_I2C_ADDR,
                                    block_addr,
                                    I2C_MEMADD_SIZE_16BIT,
                                    (uint8_t *)data, len,
                                    NFC_NTP53321_I2C_TIMEOUT);
        if (hal_ret == HAL_OK) return NFC_RESULT_OK;

        hal_err = HAL_I2C_GetError(h->hi2c);
        h->stats.i2c_error_count++;
        APP_LOGE("NFC", "Mem_Write err addr=0x%04X len=%u retry=%u hal=%d err=0x%08lX state=%lu",
                 (unsigned int)block_addr,
                 (unsigned int)len,
                 (unsigned int)retry,
                 (int)hal_ret,
                 (unsigned long)hal_err,
                 (unsigned long)HAL_I2C_GetState(h->hi2c));

        if (retry == NFC_NTP53321_I2C_RETRY_MAX - 1U)
            return NFC_RESULT_ERROR_I2C_RETRY;

        HAL_Delay(5U);
        retry++;
    } while (retry < NFC_NTP53321_I2C_RETRY_MAX);

    return NFC_RESULT_ERROR_I2C;
}

static NFC_Result_t nfc_i2c_mem_read(NFC_NTP53321_Handle_t *h,
                                      uint16_t block_addr,
                                      uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef hal_ret;
    uint8_t           retry = 0U;

    do {
        hal_ret = HAL_I2C_Mem_Read(h->hi2c,
                                   NFC_NTP53321_I2C_ADDR,
                                   block_addr,
                                   I2C_MEMADD_SIZE_16BIT,
                                   data, len,
                                   NFC_NTP53321_I2C_TIMEOUT);
        if (hal_ret == HAL_OK) return NFC_RESULT_OK;

        h->stats.i2c_error_count++;
        APP_LOGE("NFC", "Mem_Read  err addr=0x%04X retry=%u hal=%d",
                 block_addr, (unsigned int)retry, (int)hal_ret);

        if (retry == NFC_NTP53321_I2C_RETRY_MAX - 1U)
            return NFC_RESULT_ERROR_I2C_RETRY;

        HAL_Delay(5U);
        retry++;
    } while (retry < NFC_NTP53321_I2C_RETRY_MAX);

    return NFC_RESULT_ERROR_I2C;
}

/* ============================================================
 * Private: WRITE REGISTER / READ REGISTER
 *
 * NXP 데이터시트 패킷 구조 (CMD 바이트 없음):
 *   WRITE: [START][SL_AD+W][BL_AD1][BL_AD0][REGA][MASK][REGDAT][STOP]
 *   READ:  [START][SL_AD+W][BL_AD1][BL_AD0][REGA][rSTART][SL_AD+R][REGDAT][STOP]
 *
 * WRITE REGISTER 패킷은 [BL_AD1][BL_AD0][REGA][MASK][REGDAT] 전체를
 * 그대로 보내야 하므로 HAL_I2C_Master_Transmit() 으로 raw 전송합니다.
 *
 * READ REGISTER 패킷은 [BL_AD1][BL_AD0][REGA] 전송 후,
 * repeated start 로 1바이트를 읽어야 하므로
 * Master_Transmit(3byte) + Master_Receive(1byte) 로 분리 구현합니다.
 * ============================================================ */
static NFC_Result_t nfc_i2c_reg_write(NFC_NTP53321_Handle_t *h,
                                       uint16_t block_addr,
                                       uint8_t  reg_offset,
                                       uint8_t  mask,
                                       uint8_t  value)
{
    HAL_StatusTypeDef hal_ret;
    uint32_t          hal_err;
    uint8_t           pkt[5];
    uint8_t           retry = 0U;

    if ((h == NULL) || (h->hi2c == NULL))
        return NFC_RESULT_ERROR_INVALID_PARAM;

    pkt[0] = (uint8_t)(block_addr >> 8U);
    pkt[1] = (uint8_t)(block_addr & 0xFFU);
    pkt[2] = reg_offset;
    pkt[3] = mask;
    pkt[4] = value;

    do {
        hal_ret = HAL_I2C_Master_Transmit(h->hi2c,
                                          NFC_NTP53321_I2C_ADDR,
                                          pkt, 5U,
                                          NFC_NTP53321_I2C_TIMEOUT);
        if (hal_ret == HAL_OK)
            return NFC_RESULT_OK;

        hal_err = HAL_I2C_GetError(h->hi2c);
        h->stats.i2c_error_count++;
        APP_LOGE("NFC", "Reg_Write err blk=0x%04X rega=0x%02X mask=0x%02X val=0x%02X retry=%u hal=%d err=0x%08lX state=%lu",
                 (unsigned int)block_addr,
                 (unsigned int)reg_offset,
                 (unsigned int)mask,
                 (unsigned int)value,
                 (unsigned int)retry,
                 (int)hal_ret,
                 (unsigned long)hal_err,
                 (unsigned long)HAL_I2C_GetState(h->hi2c));

        if (retry == NFC_NTP53321_I2C_RETRY_MAX - 1U)
            return NFC_RESULT_ERROR_I2C_RETRY;

        HAL_Delay(5U);
        retry++;
    } while (retry < NFC_NTP53321_I2C_RETRY_MAX);

    return NFC_RESULT_ERROR_I2C;
}

static NFC_Result_t nfc_i2c_reg_read(NFC_NTP53321_Handle_t *h,
                                      uint16_t block_addr,
                                      uint8_t  reg_offset,
                                      uint8_t  *out_value)
{
    HAL_StatusTypeDef hal_ret;
    uint32_t          hal_err;
    uint32_t          hal_state;
    uint8_t           cmd[3];
    uint8_t           retry = 0U;

    /*
     * READ REGISTER 패킷:
     * Phase1 (Transmit): [SL_AD+W][BL_AD1][BL_AD0][REGA]
     * Phase2 (Receive):  [rSTART][SL_AD+R][REGDAT]
     *
     * HAL_I2C_Mem_Read 는 MemAddr를 2바이트로 처리하므로
     * [BL_AD1][BL_AD0] 다음에 [REGA]를 따로 전송할 수 없습니다.
     * → Master_Transmit(3byte) + Master_Receive(1byte) 로 분리 구현.
     */
    cmd[0] = (uint8_t)(block_addr >> 8U);    /* BL_AD1 */
    cmd[1] = (uint8_t)(block_addr & 0xFFU);  /* BL_AD0 */
    cmd[2] = reg_offset;                      /* REGA   */

    do {
        /*
        APP_LOGD("NFC", "Reg_Read stage=TX start blk=0x%04X rega=0x%02X retry=%u",
                 (unsigned int)block_addr,
                 (unsigned int)reg_offset,
                 (unsigned int)retry);
        */

        hal_ret = HAL_I2C_Master_Transmit(h->hi2c,
                                          NFC_NTP53321_I2C_ADDR,
                                          cmd, 3U,
                                          NFC_NTP53321_I2C_TIMEOUT);
        if (hal_ret != HAL_OK) {
            hal_err = HAL_I2C_GetError(h->hi2c);
            hal_state = HAL_I2C_GetState(h->hi2c);
            h->stats.i2c_error_count++;
            APP_LOGE("NFC", "Reg_Read TX err blk=0x%04X rega=0x%02X retry=%u hal=%d err=0x%08lX state=%lu",
                     (unsigned int)block_addr,
                     (unsigned int)reg_offset,
                     (unsigned int)retry,
                     (int)hal_ret,
                     (unsigned long)hal_err,
                     (unsigned long)hal_state);
            goto retry_label;
        }

        /*
        APP_LOGD("NFC", "Reg_Read stage=TX ok blk=0x%04X rega=0x%02X retry=%u",
                 (unsigned int)block_addr,
                 (unsigned int)reg_offset,
                 (unsigned int)retry);
        APP_LOGD("NFC", "Reg_Read stage=RX start blk=0x%04X rega=0x%02X retry=%u",
                 (unsigned int)block_addr,
                 (unsigned int)reg_offset,
                 (unsigned int)retry);
        */

        hal_ret = HAL_I2C_Master_Receive(h->hi2c,
                                         NFC_NTP53321_I2C_ADDR,
                                         out_value, 1U,
                                         NFC_NTP53321_I2C_TIMEOUT);
        if (hal_ret == HAL_OK) {
            /*
            APP_LOGD("NFC", "Reg_Read stage=RX ok blk=0x%04X rega=0x%02X retry=%u val=0x%02X",
                     (unsigned int)block_addr,
                     (unsigned int)reg_offset,
                     (unsigned int)retry,
                     (unsigned int)(*out_value));
            */
            return NFC_RESULT_OK;
        }

        hal_err = HAL_I2C_GetError(h->hi2c);
        hal_state = HAL_I2C_GetState(h->hi2c);
        h->stats.i2c_error_count++;
        APP_LOGE("NFC", "Reg_Read RX err blk=0x%04X rega=0x%02X retry=%u hal=%d err=0x%08lX state=%lu",
                 (unsigned int)block_addr,
                 (unsigned int)reg_offset,
                 (unsigned int)retry,
                 (int)hal_ret,
                 (unsigned long)hal_err,
                 (unsigned long)hal_state);

retry_label:
        if (retry == NFC_NTP53321_I2C_RETRY_MAX - 1U)
            return NFC_RESULT_ERROR_I2C_RETRY;

        HAL_Delay(5U);
        retry++;
    } while (retry < NFC_NTP53321_I2C_RETRY_MAX);

    return NFC_RESULT_ERROR_I2C;
}


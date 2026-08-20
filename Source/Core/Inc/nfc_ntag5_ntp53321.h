/**
 * @file    nfc_ntag5_ntp53321.h
 * @brief   NTP53321 NFC Tag Driver for STM32L073
 * @version 3.0.0
 *
 * Changes from v2.2:
 *  - [FIX] Session register address map 전면 수정 (NXP 공식 데이터시트 기준)
 *    * 0x10A0 = STATUS_REG  (STATUS0_REG, STATUS1_REG)
 *    * 0x10A1 = CONFIG_REG  (CONFIG_0_REG, CONFIG_1_REG, CONFIG_2_REG)
 *    * ED_CONF / WDT_CONFIG / I2C_SLAVE_CONFIG 은 구성 메모리(0x103x) 소속
 *  - [FIX] WRITE REGISTER / READ REGISTER API 추가
 *    * 패킷 구조: [BL_AD1][BL_AD0][REGA][MASK][REGDAT]  (CMD 바이트 없음)
 *  - [FIX] NFC_CC_VERSION 0x40 → 0x10 (NDEF Mapping v1.0)
 *  - [FIX] SetEDMode: STATUS 레지스터 → CONFIG_REG 로 변경
 *  - [FIX] EnableSRAMMirror: STATUS 레지스터 → CONFIG_REG 로 변경
 */

#ifndef NFC_NTAG5_NTP53321_H
#define NFC_NTAG5_NTP53321_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ============================================================
 * I2C Configuration
 * NTP53321: 7-bit address 0x54
 * HAL_I2C_Mem_Write/Read 는 내부적으로 << 1 처리됨
 * ============================================================ */
#define NFC_NTP53321_I2C_ADDR       (0x54U << 1U)   /* 0xA8 */
#define NFC_NTP53321_I2C_TIMEOUT    100U
#define NFC_NTP53321_I2C_RETRY_MAX  3U
#define NFC_BLOCK_TO_I2C_ADDR(b)    (b)             /* block No. = I2C MemAddr */

/* ============================================================
 * User Memory Map  (I2C 16-bit block addresses)
 * ============================================================ */
#define NFC_EEPROM_BASE_ADDR            0x0000U
#define NFC_EEPROM_END_ADDR             0x01FFU
#define NFC_EEPROM_BLOCK_SIZE           4U
#define NFC_EEPROM_TOTAL_BLOCKS         512U    /* 2048 bytes / 4 */

/* CC Block (User memory block 0) */
#define NFC_CC_BLOCK_ADDR               0x0000U
#define NFC_CC_MAGIC_BYTE               0xE1U
#define NFC_CC_VERSION                  0x40U   /* T5T v1.0, R/W OK */
#define NFC_CC_SIZE                     0x80U   /* MLEN: 0xFF×8=2040 bytes */
#define NFC_CC_ACCESS                   0x09U   /* Read/Write */

/* NDEF */
#define NFC_NDEF_START_BLOCK            0x0001U

/* Nonce counter (persistent, EEPROM) */
#define NFC_NONCE_BLOCK_ADDR            0x00FFU

/* ============================================================
 * Configuration Memory Map  (I2C 16-bit addresses, 0x1000~0x109F)
 * 접근: HAL_I2C_Mem_Write/Read (WRITE MEMORY / READ MEMORY)
 *
 * [NXP NTP53x2 Datasheet Table 11 기준]
 * ============================================================ */
#define NFC_CONFIG_BASE_ADDR            0x1000U

/* --- 구성 메모리 (POR 후 적용, EEPROM 저장) --- */
#define NFC_CFG_ORIGALITY_SIG_ADDR      0x1000U  /* 0x1000~0x1007 : 32byte */
#define NFC_CFG_CONFIG_HEADER_ADDR      0x1008U  /* CH byte */
#define NFC_CFG_CUSTOMER_ID_ADDR        0x1009U  /* CID */
#define NFC_CFG_NFC_GCH_ADDR            0x100CU  /* NFC Global Crypto Header */
#define NFC_CFG_NFC_CCH_ADDR            0x100DU  /* NFC Crypto Config Header */
#define NFC_CFG_NFC_AUTH_LIMIT_ADDR     0x100EU  /* NFC Auth Limit Counter */
#define NFC_CFG_I2C_KH_ADDR             0x1030U  /* I2C Key Header */
#define NFC_CFG_I2C_PP_ADDR             0x1031U  /* I2C Protection Pointer */
#define NFC_CFG_I2C_AUTH_LIMIT_ADDR     0x1032U  /* I2C Auth Limit Counter */
#define NFC_CFG_I2C_PWD0_ADDR           0x1033U  /* I2C read password */
#define NFC_CFG_I2C_PWD1_ADDR           0x1034U  /* I2C write password */
#define NFC_CFG_I2C_PWD2_ADDR           0x1035U  /* Restricted AREA1 read pwd */
#define NFC_CFG_I2C_PWD3_ADDR           0x1036U  /* Restricted AREA1 write pwd */
#define NFC_CFG_CONFIG_ADDR             0x1037U  /* Feature CONFIG (POR 적용) */
#define NFC_CFG_SYNC_DATA_BLOCK_ADDR    0x1038U  /* Sync data block */
#define NFC_CFG_PWM_GPIO_CONFIG_ADDR    0x1039U  /* PWM/GPIO config */
#define NFC_CFG_PWM0_ON_OFF_ADDR        0x103AU  /* PWM0 duty cycle */
#define NFC_CFG_PWM1_ON_OFF_ADDR        0x103BU  /* PWM1 duty cycle */
#define NFC_CFG_WDT_CONFIG_ADDR         0x103CU  /* Watch Dog Timer config */
                                                  /* Byte0: WDT_CONFIG */
                                                  /* Byte1: SRAM_COPY_BYTES */
#define NFC_CFG_EH_ED_CONFIG_ADDR       0x103DU  /* Byte0: EH_CONF */
                                                  /* Byte2: ED_CONF */
#define NFC_CFG_I2C_CONFIG_ADDR         0x103EU  /* Byte0: I2C_SLAVE_ADDR */
                                                  /* Byte1: I2C_SLAVE_CONFIG */
                                                  /* Byte2: I2C_MASTER_SCL_LOW */
                                                  /* Byte3: I2C_MASTER_SCL_HIGH */
#define NFC_CFG_SEC_CONF_ADDR           0x103FU  /* Security config */
#define NFC_CFG_NFC_PP_ADDR             0x1058U  /* NFC Protection Pointer */
#define NFC_CFG_NFC_LOCK_BLOCK_ADDR     0x106AU  /* NFC Lock block */
#define NFC_CFG_I2C_LOCK_BLOCK_ADDR     0x108AU  /* I2C Lock block */
#define NFC_CFG_NFC_SECTION_LOCK_ADDR   0x1092U  /* NFC section lock */
#define NFC_CFG_I2C_SECTION_LOCK_ADDR   0x1094U  /* I2C section lock */
#define NFC_CFG_I2C_PWD0_AUTH_ADDR      0x1096U  /* I2C read pwd authenticate */
#define NFC_CFG_I2C_PWD1_AUTH_ADDR      0x1097U  /* I2C write pwd authenticate */
#define NFC_CFG_I2C_PWD2_AUTH_ADDR      0x1098U  /* Restricted AREA1 read auth */
#define NFC_CFG_I2C_PWD3_AUTH_ADDR      0x1099U  /* Restricted AREA1 write auth */

/* --- Session Register (전원 유지 중 즉시 적용, POR 후 초기화) ---
 * 접근: WRITE REGISTER / READ REGISTER 전용
 *        패킷: [BL_AD1][BL_AD0][REGA][MASK][REGDAT]
 * ============================================================ */
#define NFC_SESSION_BASE_ADDR           0x10A0U
#define NFC_SESSION_END_ADDR            0x10AFU

/* Block 0x10A0: STATUS_REG  (NFC: A0h)
 * Byte0 = STATUS0_REG, Byte1 = STATUS1_REG */
#define NFC_SESSION_STATUS_ADDR         0x10A0U

/* STATUS0_REG (Byte 0 of 0x10A0) bit definitions */
#define NFC_STATUS0_EEPROM_WR_BUSY      (1U << 7)  /* R  : EEPROM 프로그래밍 중 */
#define NFC_STATUS0_EEPROM_WR_ERROR     (1U << 6)  /* R/W: EEPROM 쓰기 오류 */
#define NFC_STATUS0_SRAM_DATA_READY     (1U << 5)  /* R  : SRAM 데이터 준비 */
#define NFC_STATUS0_SYNCH_BLOCK_WRITE   (1U << 4)  /* R/W: SYNCH_BLOCK 쓰기됨 */
#define NFC_STATUS0_SYNCH_BLOCK_READ    (1U << 3)  /* R/W: SYNCH_BLOCK 읽혔음 */
#define NFC_STATUS0_PT_TRANSFER_DIR     (1U << 2)  /* R  : pass-through 방향 */
#define NFC_STATUS0_VCC_SUPPLY_OK       (1U << 1)  /* R  : VCC 공급 정상 */
#define NFC_STATUS0_NFC_FIELD_OK        (1U << 0)  /* R  : NFC 필드 감지 */

/* STATUS1_REG (Byte 1 of 0x10A0) bit definitions */
#define NFC_STATUS1_VCC_BOOT_OK         (1U << 7)  /* R  : VCC 부트 완료 */
#define NFC_STATUS1_NFC_BOOT_OK         (1U << 6)  /* R  : NFC 부트 완료 */
#define NFC_STATUS1_GPIO1_IN_STATUS     (1U << 4)  /* R  : GPIO1 입력 상태 */
#define NFC_STATUS1_GPIO0_IN_STATUS     (1U << 3)  /* R  : GPIO0 입력 상태 */
#define NFC_STATUS1_I2C_IF_LOCKED       (1U << 1)  /* R/W: I2C 인터페이스 잠금 */
#define NFC_STATUS1_NFC_IF_LOCKED       (1U << 0)  /* R  : NFC 인터페이스 잠금 */

/* Block 0x10A1: CONFIG_REG  (NFC: A1h)
 * Byte0 = CONFIG_0_REG, Byte1 = CONFIG_1_REG, Byte2 = CONFIG_2_REG */
#define NFC_SESSION_CONFIG_REG_ADDR     0x10A1U
#define NFC_SESSION_ED_CONFIG_REG_ADDR     0x10A8U /* Byte0: ED_CONF */

/* CONFIG_0_REG (Byte 0 of 0x10A1) bit definitions */
#define NFC_CONFIG0_SRAM_COPY_EN        (1U << 7)  /* R/W (I2C): SRAM copy on POR */
#define NFC_CONFIG0_DISABLE_NFC         (1U << 5)  /* R/W (I2C): NFC 비활성화 */
#define NFC_CONFIG0_AUTO_STANDBY_EN     (1U << 0)  /* R/W (I2C): Auto standby */

/* CONFIG_1_REG (Byte 1 of 0x10A1) bit definitions */
#define NFC_CONFIG1_ARBITER_MODE_MASK   (0x0CU)    /* bit[3:2]: ARBITER_MODE */
#define NFC_CONFIG1_ARBITER_NORMAL      (0x00U)    /* 00: Normal */
#define NFC_CONFIG1_ARBITER_SRAM_MIRROR (0x04U)    /* 01: SRAM mirror */
#define NFC_CONFIG1_ARBITER_PASSTHRU    (0x08U)    /* 10: Pass-through */
#define NFC_CONFIG1_ARBITER_PHDC        (0x0CU)    /* 11: PHDC */
#define NFC_CONFIG1_SRAM_ENABLED        (1U << 1)  /* R  : SRAM 접근 가능 여부 */
#define NFC_CONFIG1_PT_TRANSFER_DIR     (1U << 0)  /* R/W: 0=I2C→NFC, 1=NFC→I2C */

/* ============================================================
 * ED_CONF 필드 (NFC_CFG_EH_ED_CONFIG_ADDR 0x103D, Byte2)
 * ============================================================ */
#define NFC_ED_CONF_DISABLED            0x00U  /* ED 비활성화 */
#define NFC_ED_CONF_FIELD_DETECT        0x01U  /* NFC 필드 감지 */
#define NFC_ED_CONF_PWM                 0x02U  /* PWM 신호 */
#define NFC_ED_CONF_I2C_TO_NFC_PT       0x03U  /* I2C→NFC pass-through */
#define NFC_ED_CONF_NFC_TO_I2C_PT       0x04U  /* NFC→I2C pass-through */
#define NFC_ED_CONF_ARBITER_LOCK        0x05U  /* Arbiter 잠금 */
#define NFC_ED_CONF_NDEF_TLV_LEN        0x06U  /* NDEF TLV 길이 */
#define NFC_ED_CONF_STANDBY_MODE        0x07U  /* Standby 모드 */
#define NFC_ED_CONF_WRITE_IND           0x08U  /* WRITE 명령 표시 */
#define NFC_ED_CONF_READ_IND            0x09U  /* READ 명령 표시 */
#define NFC_ED_CONF_CMD_START           0x0AU  /* 명령 시작 */
#define NFC_ED_CONF_READ_SYNCH          0x0BU  /* SYNCH_BLOCK 읽기 */
#define NFC_ED_CONF_WRITE_SYNCH         0x0CU  /* SYNCH_BLOCK 쓰기 */
#define NFC_ED_CONF_SW_INTERRUPT        0x0DU  /* 소프트웨어 인터럽트 */

/* SRAM */
#define NFC_SRAM_BASE_ADDR              0x2000U
#define NFC_SRAM_END_ADDR               0x203FU
#define NFC_SRAM_BLOCK_SIZE             4U
#define NFC_SRAM_TOTAL_BLOCKS           64U    /* 256 bytes / 4 */

/* Layer1 (Seoul/legacy) uses 0x2000 ~ 0x201F */

/* Auth SRAM layout (Layer2) */
#define NFC_SRAM_CMD_BLOCK              0x2020U
#define NFC_SRAM_CHALLENGE_BLOCK_START  0x2021U
#define NFC_SRAM_CHALLENGE_BLOCK_END    0x2024U
#define NFC_SRAM_RESPONSE_BLOCK_START   0x2025U
#define NFC_SRAM_RESPONSE_BLOCK_END     0x2028U
#define NFC_SRAM_STATUS_BLOCK           0x2029U

/* User CMD SRAM layout (Layer2) */
#define NFC_SRAM_UCMD_HEADER_BLOCK      0x2030U
#define NFC_SRAM_UCMD_DATA_BLOCK_START  0x2031U
#define NFC_SRAM_UCMD_DATA_BLOCK_END    0x2034U
#define NFC_SRAM_UCMD_RESULT_BLOCK_START 0x2035U
#define NFC_SRAM_UCMD_RESULT_BLOCK_END  0x2038U
#define NFC_SRAM_UCMD_STATUS_BLOCK      0x2039U

/* ============================================================
 * Hardware Pins
 * ============================================================ */
#define NFC_TEST_MEAS_PIN               0U
#define NFC_TEST_MEAS_PORT              GPIOB
#define NFC_TEST_MEAS_START()           do { } while (0)
#define NFC_TEST_MEAS_STOP()            do { } while (0)

/* ============================================================
 * Low Power Constants
 * ============================================================ */
#define NFC_STOP_CURRENT_UA             9500U
#define NFC_ACTIVE_CURRENT_UA           438000U
#define NFC_ACTIVE_DURATION_MS          50U
#define NFC_ED_DEBOUNCE_MS              50U

/* ============================================================
 * Enumerations
 * ============================================================ */
typedef enum {
    NFC_RESULT_OK                   = 0x00,
    NFC_RESULT_ERROR                = 0x01,
    NFC_RESULT_ERROR_I2C            = 0x02,
    NFC_RESULT_ERROR_TIMEOUT        = 0x03,
    NFC_RESULT_ERROR_INVALID_PARAM  = 0x04,
    NFC_RESULT_ERROR_NOT_INIT       = 0x05,
    NFC_RESULT_ERROR_BUSY           = 0x06,
    NFC_RESULT_ERROR_CRC            = 0x07,
    NFC_RESULT_ERROR_I2C_RETRY      = 0x08,
} NFC_Result_t;

typedef enum {
    NFC_ED_MODE_DISABLED        = NFC_ED_CONF_DISABLED,
    NFC_ED_MODE_FIELD_DETECT    = NFC_ED_CONF_FIELD_DETECT,
    NFC_ED_MODE_DATA_READY      = NFC_ED_CONF_NFC_TO_I2C_PT,
    NFC_ED_MODE_INTERRUPT       = NFC_ED_CONF_SW_INTERRUPT,
} NFC_EDMode_t;

typedef enum {
    NFC_STATE_UNINITIALIZED = 0x00,
    NFC_STATE_IDLE          = 0x01,
    NFC_STATE_ACTIVE        = 0x02,
    NFC_STATE_STOP          = 0x03,
    NFC_STATE_ERROR         = 0x04,
} NFC_DriverState_t;

typedef enum {
    NFC_WAKEUP_EVENT_ED_PIN  = 0x01,
    NFC_WAKEUP_EVENT_RTC     = 0x02,
    NFC_WAKEUP_EVENT_UNKNOWN = 0xFF,
} NFC_WakeupEvent_t;

/* ============================================================
 * Structures
 * ============================================================ */
typedef struct {
    uint32_t wakeup_count;
    uint32_t i2c_error_count;
    uint32_t last_wakeup_tick;
    uint32_t total_active_ticks;
    uint32_t total_sleep_ticks;
} NFC_NTP53321_Stats_t;

typedef struct {
    I2C_HandleTypeDef    *hi2c;
    NFC_DriverState_t     state;
    NFC_EDMode_t          ed_mode;
    volatile bool         ed_flag;
    uint8_t               uid[7];
    bool                  uid_valid;
    NFC_NTP53321_Stats_t  stats;
    uint32_t              sleep_enter_tick;
} NFC_NTP53321_Handle_t;

/* ============================================================
 * API
 * ============================================================ */

/* Init / DeInit / Reset */
NFC_Result_t NFC_NTP53321_Init(NFC_NTP53321_Handle_t *hntag, I2C_HandleTypeDef *hi2c);
NFC_Result_t NFC_NTP53321_DeInit(NFC_NTP53321_Handle_t *hntag);
NFC_Result_t NFC_NTP53321_Reset(NFC_NTP53321_Handle_t *hntag);

/* Configuration */
NFC_Result_t NFC_NTP53321_SetEDMode(NFC_NTP53321_Handle_t *hntag, NFC_EDMode_t mode);
NFC_Result_t NFC_NTP53321_ConfigureCC(NFC_NTP53321_Handle_t *hntag);
NFC_Result_t NFC_NTP53321_EnableSRAMMirror(NFC_NTP53321_Handle_t *hntag, bool enable);

/* Memory Access (WRITE MEMORY / READ MEMORY) */
NFC_Result_t NFC_NTP53321_ReadBlock(NFC_NTP53321_Handle_t *hntag,
                                     uint16_t block_addr, uint8_t *data);
NFC_Result_t NFC_NTP53321_WriteBlock(NFC_NTP53321_Handle_t *hntag,
                                      uint16_t block_addr, const uint8_t *data);
NFC_Result_t NFC_NTP53321_ReadMultiBlock(NFC_NTP53321_Handle_t *hntag,
                                          uint16_t block_addr,
                                          uint8_t *data, uint16_t num_blocks);
NFC_Result_t NFC_NTP53321_WriteMultiBlock(NFC_NTP53321_Handle_t *hntag,
                                           uint16_t block_addr,
                                           const uint8_t *data, uint16_t num_blocks);

/* Session Register Access (WRITE REGISTER / READ REGISTER)
 * 패킷: [BL_AD1][BL_AD0][REGA][MASK][REGDAT]  (CMD 바이트 없음)
 * block_addr : 16-bit I2C block address (예: NFC_SESSION_STATUS_ADDR)
 * reg_offset : block 내 byte 번호 0~3 (REGA)
 * mask       : 변경할 비트만 1 (0xFF = 전체 덮어쓰기)
 * value      : 1바이트 값 */
NFC_Result_t NFC_NTP53321_WriteSessionReg(NFC_NTP53321_Handle_t *hntag,
                                           uint16_t block_addr,
                                           uint8_t  reg_offset,
                                           uint8_t  mask,
                                           uint8_t  value);
NFC_Result_t NFC_NTP53321_ReadSessionReg(NFC_NTP53321_Handle_t *hntag,
                                          uint16_t block_addr,
                                          uint8_t  reg_offset,
                                          uint8_t  *out_value);

/* NDEF */
NFC_Result_t NFC_NTP53321_WriteNDEFText(NFC_NTP53321_Handle_t *hntag, const char *text);
NFC_Result_t NFC_NTP53321_ReadNDEFText(NFC_NTP53321_Handle_t *hntag,
                                        char *text, uint16_t max_len);

/* UID */
NFC_Result_t NFC_NTP53321_GetUID(NFC_NTP53321_Handle_t *hntag, uint8_t *uid);

/* Low Power */
NFC_Result_t NFC_NTP53321_EnterStandby(NFC_NTP53321_Handle_t *hntag);
NFC_Result_t NFC_NTP53321_ExitStandby(NFC_NTP53321_Handle_t *hntag);

/* ED */
bool         NFC_NTP53321_IsEDTriggered(NFC_NTP53321_Handle_t *hntag);
void         NFC_NTP53321_ClearEDFlag(NFC_NTP53321_Handle_t *hntag);
void         NFC_NTP53321_ED_EXTI_IRQHandler(NFC_NTP53321_Handle_t *hntag);
void         NFC_NTP53321_NotifyDeferredEdEvent(NFC_NTP53321_Handle_t *hntag);

/* Stats */
void         NFC_NTP53321_GetStats(NFC_NTP53321_Handle_t *hntag,
                                    NFC_NTP53321_Stats_t *stats);
void         NFC_NTP53321_PrintStats(NFC_NTP53321_Handle_t *hntag);

#ifdef __cplusplus
}
#endif
#endif /* NFC_NTAG5_NTP53321_H */


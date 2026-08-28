/**
 * @file    nfc_user_command.h
 * @brief   NFC Layer2 UCMD transport for direct APP_CONTROL commands.
 */

#ifndef NFC_USER_COMMAND_H
#define NFC_USER_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nfc_ntag5_ntp53321.h"
#include "nfc_secure_auth.h"
#include <stdbool.h>
#include <stdint.h>

#define NFC_CMD_MAX_PAYLOAD             32U
#define NFC_CMD_MAX_RESULT              32U
#define NFC_CMD_VERSION_STR             "3.0.0"

/* UCMD indicate block (0x003D): F5 AABl 5F = NFC->I2C, F4 AABl 4F = I2C->NFC */
#define NFC_CMD_IND_NFC_TO_I2C_PREFIX   0xF5U
#define NFC_CMD_IND_NFC_TO_I2C_SUFFIX   0x5FU
#define NFC_CMD_IND_I2C_TO_NFC_PREFIX   0xF4U
#define NFC_CMD_IND_I2C_TO_NFC_SUFFIX   0x4FU
#define NFC_CMD_IND_REQ_ADDR            ((uint8_t)(NFC_SRAM_UCMD_CMD_BLOCK & 0xFFU))
#define NFC_CMD_IND_REQ_BLOCK_LEN_MIN   0x01U
#define NFC_CMD_IND_REQ_BLOCK_LEN_MAX   ((uint8_t)(NFC_SRAM_UCMD_PAYLOAD_BLOCK_END - NFC_SRAM_UCMD_CMD_BLOCK + 1U))
#define NFC_CMD_IND_RSP_ADDR            ((uint8_t)(NFC_SRAM_UCMD_PAYLOAD_BLOCK_START & 0xFFU))
#define NFC_CMD_IND_RSP_BLOCK_LEN_MAX   ((uint8_t)(NFC_SRAM_UCMD_PAYLOAD_BLOCK_END - NFC_SRAM_UCMD_PAYLOAD_BLOCK_START + 1U))

typedef enum {
    NFC_CMD_RESULT_OK            = 0x00,
    NFC_CMD_RESULT_FAIL          = 0x01,
    NFC_CMD_RESULT_NOT_AUTH      = 0x02,
    NFC_CMD_RESULT_INVALID_CMD   = 0x03,
    NFC_CMD_RESULT_INVALID_LEN   = 0x04,
    NFC_CMD_RESULT_INVALID_MAGIC = 0x05,
    NFC_CMD_RESULT_NO_PERM       = 0x06,
    NFC_CMD_RESULT_I2C_ERROR     = 0x07,
    NFC_CMD_RESULT_INVALID_PARAM = 0x08,
} NFC_CMD_Result_t;

#define NFC_CMD_STATUS_IDLE         0x00U
#define NFC_CMD_STATUS_PROCESSING   0x01U
#define NFC_CMD_STATUS_DONE_OK      0x02U
#define NFC_CMD_STATUS_DONE_FAIL    0x03U

#pragma pack(push, 1)
typedef struct {
    uint8_t prefix;
    uint8_t addr;
    uint8_t block_len;
    uint8_t suffix;
} NFC_CMD_Indicate_t;

typedef struct {
    uint8_t cmd[4];
    uint8_t payload[NFC_CMD_MAX_PAYLOAD];
    uint8_t payload_len;
} NFC_CMD_Packet_t;

typedef struct {
    uint8_t status;
    uint8_t op_status;
    uint8_t payload_len;
    uint8_t reserved;
} NFC_CMD_ResultPacket_t;

typedef struct {
    uint16_t temp_threshold_x10;
    uint16_t report_interval_sec;
    uint8_t  reserved[12];
} NFC_CMD_Config_t;
#pragma pack(pop)

typedef struct {
    NFC_NTP53321_Handle_t *hntag;
    NFC_AUTH_Handle_t     *hauth;
    NFC_CMD_Config_t       config;
    uint32_t               cmd_success_count;
    uint32_t               cmd_fail_count;
    uint32_t               cmd_no_auth_count;
    bool                   initialized;
} NFC_CMD_Handle_t;

NFC_CMD_Result_t NFC_CMD_Init(NFC_CMD_Handle_t *hcmd,
                              NFC_NTP53321_Handle_t *hntag,
                              NFC_AUTH_Handle_t *hauth);
NFC_CMD_Result_t NFC_CMD_Process(NFC_CMD_Handle_t *hcmd);
void             NFC_CMD_PrintStats(NFC_CMD_Handle_t *hcmd);

#ifdef __cplusplus
}
#endif
#endif /* NFC_USER_COMMAND_H */

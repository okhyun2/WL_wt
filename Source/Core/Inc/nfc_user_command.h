/**
 * @file    nfc_user_command.h
 * @brief   NFC User Command Processor  [v2.2.0]
 *
 * Changes:
 *  [C4 FIX] NFC_CMD_MAGIC_WORD = 0xAA55U (valid hex, synced with Android)
 *  [M3 FIX] Compile-time struct size assertions added
 */

#ifndef NFC_USER_COMMAND_H
#define NFC_USER_COMMAND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nfc_ntag5_ntp53321.h"
#include "nfc_secure_auth.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Magic Word — must match Android SramProtocol.kt
 *   Android: private const val CMD_MAGIC_WORD: Int = 0xAA55
 * ============================================================ */
#define NFC_CMD_MAGIC_WORD      0xAA55U
#define NFC_CMD_MAX_PAYLOAD     16U
#define NFC_CMD_MAX_RESULT      16U
#define NFC_CMD_VERSION_STR     "2.2.0"

typedef enum {
    NFC_CMD_ID_GET_STATUS       = 0x01,
    NFC_CMD_ID_GET_VERSION      = 0x02,
    NFC_CMD_ID_GET_STATS        = 0x03,
    NFC_CMD_ID_READ_SENSOR      = 0x04,
    NFC_CMD_ID_WRITE_DATA       = 0x05,
    NFC_CMD_ID_READ_EEPROM      = 0x06,
    NFC_CMD_ID_WRITE_EEPROM     = 0x07,
    NFC_CMD_ID_SET_CONFIG       = 0x08,
    NFC_CMD_ID_GET_CONFIG       = 0x09,
    NFC_CMD_ID_SET_THRESHOLD    = 0x0A,
    NFC_CMD_ID_SET_INTERVAL     = 0x0B,
    NFC_CMD_ID_RESET_DEVICE     = 0x10,
    NFC_CMD_ID_FACTORY_RESET    = 0x11,
    NFC_CMD_ID_UPDATE_KEY       = 0x20,
    NFC_CMD_ID_UNLOCK_DEVICE    = 0x21,
    NFC_CMD_ID_SET_AUTH_LIMIT   = 0x22,
    NFC_CMD_ID_MAX              = 0xFF,
} NFC_CMD_ID_t;

#define NFC_CMD_PERM_READ       (1U << 0)
#define NFC_CMD_PERM_WRITE      (1U << 1)
#define NFC_CMD_PERM_CONFIG     (1U << 2)
#define NFC_CMD_PERM_ADMIN      (1U << 3)
#define NFC_CMD_PERM_ALL        (0x0FU)

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
    uint16_t magic;
    uint8_t  cmd_id;
    uint8_t  payload_len;
} NFC_CMD_Header_t;

typedef struct {
    NFC_CMD_Header_t header;
    uint8_t          payload[NFC_CMD_MAX_PAYLOAD];
} NFC_CMD_Packet_t;

typedef struct {
    uint8_t result_code;
    uint8_t data_len;
    uint8_t data[NFC_CMD_MAX_RESULT];
    uint8_t timestamp[2];
} NFC_CMD_ResultPacket_t;

typedef struct {
    uint8_t  fw_version[4];
    uint8_t  auth_state;
    uint8_t  nfc_field_active;
    uint32_t uptime_sec;
    uint32_t wakeup_count;
    uint16_t battery_mv;
} NFC_CMD_MCUStatus_t;              /* 16 bytes total */

typedef struct {
    int16_t  temperature_x10;
    uint16_t humidity_x10;
    uint16_t battery_mv;
    int16_t  signal_level_dbm;
} NFC_CMD_SensorData_t;             /* 8 bytes total */

typedef struct {
    uint16_t temp_threshold_x10;
    uint16_t report_interval_sec;
    uint8_t  auth_max_fail;
    uint8_t  reserved[11];
} NFC_CMD_Config_t;                 /* 16 bytes total */
#pragma pack(pop)

/* ============================================================
 * Compile-time size assertions
 * [M3 FIX] Prevent silent buffer overflow if structs grow
 * ============================================================ */
typedef char _nfc_cmd_status_chk[
    (sizeof(NFC_CMD_MCUStatus_t)  <= NFC_CMD_MAX_RESULT) ? 1 : -1];
typedef char _nfc_cmd_sensor_chk[
    (sizeof(NFC_CMD_SensorData_t) <= NFC_CMD_MAX_RESULT) ? 1 : -1];
typedef char _nfc_cmd_config_chk[
    (sizeof(NFC_CMD_Config_t)     <= NFC_CMD_MAX_RESULT) ? 1 : -1];

typedef NFC_CMD_Result_t (*NFC_CMD_Handler_t)(void *hcmd,
                                               const NFC_CMD_Packet_t *pkt,
                                               NFC_CMD_ResultPacket_t *result);

typedef struct {
    NFC_CMD_ID_t      cmd_id;
    uint8_t           required_perm;
    NFC_CMD_Handler_t handler;
    const char       *name;
} NFC_CMD_TableEntry_t;

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


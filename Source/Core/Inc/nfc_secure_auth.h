/**
 * @file    nfc_secure_auth.h
 * @brief   NFC Secure Authentication Header  [v2.2.0]
 */

#ifndef NFC_SECURE_AUTH_H
#define NFC_SECURE_AUTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nfc_ntag5_ntp53321.h"
#include <stdint.h>
#include <stdbool.h>
#include "app_log.h"
#include "app_clock.h"   /* GetCorrectedTick() 사용을 위해 추가 */

#define NFC_AUTH_KEY_SIZE               16U
#define NFC_AUTH_CHALLENGE_SIZE         16U
#define NFC_AUTH_RESPONSE_SIZE          16U
#define NFC_AUTH_SESSION_TIMEOUT_MS     300000U   /* 5 min */
#define NFC_AUTH_TXN_TIMEOUT_MS         5000U     /* CONNECT~CONFIRM 완료 제한 (한 번의 태깅) */
#define NFC_AUTH_POLL_INTERVAL_MS       30U       /* 트랜잭션 진행 중 SRAM CMD 재확인 주기 */
#define NFC_AUTH_TOKEN_SIZE             16U

/* CMD values */
#define NFC_AUTH_CMD_CONNECT            0x01U
#define NFC_AUTH_CMD_CHALLENGE_READY    0x02U
#define NFC_AUTH_CMD_RESPONSE           0x03U
#define NFC_AUTH_CMD_CONFIRM            0x04U

/* STATUS values */
#define NFC_AUTH_STATUS_IDLE            0x00U
#define NFC_AUTH_STATUS_CONNECTED       0x01U
#define NFC_AUTH_STATUS_CHALLENGE_SENT  0x02U
#define NFC_AUTH_STATUS_SUCCESS         0x03U
#define NFC_AUTH_STATUS_FAIL            0x05U

typedef enum {
    NFC_AUTH_STATE_IDLE          = 0x00,
    NFC_AUTH_STATE_CONNECTING    = 0x01,
    NFC_AUTH_STATE_CHALLENGING   = 0x02,
    NFC_AUTH_STATE_VERIFYING     = 0x03,
    NFC_AUTH_STATE_AUTHENTICATED = 0x04,
    NFC_AUTH_STATE_FAILED        = 0x05,
} NFC_AUTH_State_t;

typedef enum {
    NFC_AUTH_RESULT_OK            = 0x00,
    NFC_AUTH_RESULT_FAIL          = 0x01,
    NFC_AUTH_RESULT_TIMEOUT       = 0x03,
    NFC_AUTH_RESULT_INVALID_STATE = 0x04,
    NFC_AUTH_RESULT_I2C_ERROR     = 0x05,
    NFC_AUTH_RESULT_INVALID_PARAM = 0x06,
} NFC_AUTH_Result_t;

typedef struct {
    uint8_t  challenge[NFC_AUTH_CHALLENGE_SIZE];
    uint8_t  token[NFC_AUTH_TOKEN_SIZE];
    uint32_t start_tick;
    uint32_t timeout_ms;
    bool     active;
} NFC_AUTH_Session_t;

typedef struct {
    uint32_t total_attempts;
    uint32_t success_count;
    uint32_t fail_count;
    uint32_t lock_count;
    uint32_t last_success_tick;
    uint32_t last_fail_tick;
} NFC_AUTH_Stats_t;

typedef struct {
    NFC_NTP53321_Handle_t *hntag;
    NFC_AUTH_State_t       state;
    NFC_AUTH_Session_t     session;
    NFC_AUTH_Stats_t       stats;
    uint8_t                master_key[NFC_AUTH_KEY_SIZE];
    uint8_t                fail_count;
    bool                   initialized;

    bool                   txn_active;      /* CONNECT~CONFIRM 트랜잭션 진행 여부 */
    uint32_t               txn_start_tick;  /* 트랜잭션 시작 tick */

    void (*OnAuthSuccess_Callback)(uint8_t *token);
    void (*OnAuthFail_Callback)(uint8_t fail_count);
} NFC_AUTH_Handle_t;

typedef struct {
    uint8_t  state;
    uint8_t  last_cmd;
    uint8_t  last_result;
    uint8_t  session_valid;
    uint8_t  fail_count;
    uint8_t  reserved[3];
    uint32_t session_elapsed_ms;
    uint32_t session_timeout_ms;
    uint32_t last_event_tick_ms;
} NFC_AUTH_DebugInfo_t;

NFC_AUTH_Result_t NFC_AUTH_Init(NFC_AUTH_Handle_t *hauth,
                                 NFC_NTP53321_Handle_t *hntag,
                                 const uint8_t *master_key);

NFC_AUTH_Result_t NFC_AUTH_ProcessNFCEvent(NFC_AUTH_Handle_t *hauth,
                                            NFC_WakeupEvent_t event);

bool              NFC_AUTH_IsSessionValid(NFC_AUTH_Handle_t *hauth);
NFC_AUTH_Result_t NFC_AUTH_InvalidateSession(NFC_AUTH_Handle_t *hauth);

/* Field-Detect 단일 모드용 트랜잭션 상태 조회 */
bool              NFC_AUTH_IsTransactionActive(NFC_AUTH_Handle_t *hauth);
bool              NFC_AUTH_IsTransactionTimedOut(NFC_AUTH_Handle_t *hauth);

void              NFC_AUTH_RegisterCallbacks(NFC_AUTH_Handle_t *hauth,
                                              void (*OnSuccess)(uint8_t *),
                                              void (*OnFail)(uint8_t));
void              NFC_AUTH_GetStats(NFC_AUTH_Handle_t *hauth,
                                     NFC_AUTH_Stats_t *stats);
void              NFC_AUTH_PrintStats(NFC_AUTH_Handle_t *hauth);
void              NFC_AUTH_Reset(NFC_AUTH_Handle_t *hauth);
void              NFC_AUTH_GetDebugInfo(NFC_AUTH_Handle_t *hauth,
                                        NFC_AUTH_DebugInfo_t *info);

#ifdef __cplusplus
}
#endif
#endif /* NFC_SECURE_AUTH_H */


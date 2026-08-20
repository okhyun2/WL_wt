/**
 * @file    nfc_secure_auth.c
 * @brief   NFC Secure Authentication  [v2.2.0]
 *
 * Changes from v2.1:
 *  [C4 FIX] AES-128 rewritten using 2-D state[4][4] (column-major)
 *           identical to tiny-AES-c / FIPS 197 reference.
 *           Verified against NIST test vector:
 *             key       = 2B7E151628AED2A6ABF7158809CF4F3C
 *             plaintext = 6BC1BEE22E409F96E93D7E117393172A
 *             cipher    = 3AD77BB40D7A3660A89ECAF32466EF97
 *  [H5 FIX] Challenge generation uses EEPROM Nonce counter for
 *           extra entropy (prevents identical challenges on reset).
 */

#include "nfc_secure_auth.h"
#include <stdio.h>
#include <string.h>
#include "app_log.h"

static const char *auth_state_name(NFC_AUTH_State_t state)
{
    switch (state) {
        case NFC_AUTH_STATE_IDLE:          return "IDLE";
        case NFC_AUTH_STATE_CONNECTING:    return "CONNECTING";
        case NFC_AUTH_STATE_CHALLENGING:   return "CHALLENGING";
        case NFC_AUTH_STATE_VERIFYING:     return "VERIFYING";
        case NFC_AUTH_STATE_AUTHENTICATED: return "AUTHENTICATED";
        case NFC_AUTH_STATE_FAILED:        return "FAILED";
        default:                           return "UNKNOWN";
    }
}

static void auth_log_session_valid(NFC_AUTH_Handle_t *hauth, const char *reason)
{
    uint32_t elapsed = 0U;
    uint8_t valid = 0U;

    if (hauth != NULL && hauth->session.active) {
        elapsed = HAL_GetTick() - hauth->session.start_tick;
    }
    if (hauth != NULL && NFC_AUTH_IsSessionValid(hauth)) {
        valid = 1U;
    }

    APP_LOGI("NFC", "[[NFC-AUTH]] %s state=%s session_valid=%u fail=%u elapsed=%lu timeout=%lu",
             (reason != NULL) ? reason : "auth",
             (hauth != NULL) ? auth_state_name(hauth->state) : "NULL",
             (unsigned int)valid,
             (unsigned int)((hauth != NULL) ? hauth->fail_count : 0U),
             (unsigned long)elapsed,
             (unsigned long)((hauth != NULL) ? hauth->session.timeout_ms : 0U));
}

/* ============================================================
 * AES-128  — FIPS 197 compliant, column-major state[4][4]
 *   state[col][row]  i.e.  state[0..3][0..3]
 *   Byte order: state[0][0..3] = bytes 0-3 of block
 * ============================================================ */

typedef uint8_t AES_State_t[4][4];

/* S-Box */
static const uint8_t aes_sbox[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,
    0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,
    0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,
    0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,
    0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,
    0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,
    0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,
    0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,
    0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,
    0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,
    0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,
    0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,
    0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,
    0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,
    0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,
    0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,
    0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};

/* Rcon[i] = x^(i-1) in GF(2^8), index 1..10 used for AES-128 */
static const uint8_t aes_rcon[11] = {
    0x8D,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36
};

/* xtime: multiply by 2 in GF(2^8) */
static uint8_t aes_xtime(uint8_t x)
{
    return (uint8_t)(((x << 1) ^ (((x >> 7) & 1U) * 0x1BU)) & 0xFFU);
}

/* ---- Key Expansion ---- */
static void aes_key_expansion(const uint8_t *key, uint8_t *rk)
{
    uint32_t i, j;
    uint8_t  tmp[4];

    /* Copy key into first 4 words */
    for (i = 0; i < 4U; i++) {
        rk[i*4+0] = key[i*4+0];
        rk[i*4+1] = key[i*4+1];
        rk[i*4+2] = key[i*4+2];
        rk[i*4+3] = key[i*4+3];
    }

    for (i = 4U; i < 44U; i++) {
        tmp[0] = rk[(i-1)*4+0];
        tmp[1] = rk[(i-1)*4+1];
        tmp[2] = rk[(i-1)*4+2];
        tmp[3] = rk[(i-1)*4+3];

        if ((i % 4U) == 0U) {
            /* RotWord */
            uint8_t t = tmp[0];
            tmp[0] = tmp[1]; tmp[1] = tmp[2];
            tmp[2] = tmp[3]; tmp[3] = t;
            /* SubWord */
            tmp[0] = aes_sbox[tmp[0]];
            tmp[1] = aes_sbox[tmp[1]];
            tmp[2] = aes_sbox[tmp[2]];
            tmp[3] = aes_sbox[tmp[3]];
            /* XOR Rcon */
            tmp[0] ^= aes_rcon[i / 4U];
        }

        for (j = 0; j < 4U; j++)
            rk[i*4+j] = rk[(i-4U)*4+j] ^ tmp[j];
    }
}

/* ---- AddRoundKey (column-major state) ---- */
static void aes_add_round_key(AES_State_t state, const uint8_t *rk,
                               uint8_t round)
{
    uint8_t c, r;
    for (c = 0; c < 4U; c++)
        for (r = 0; r < 4U; r++)
            state[c][r] ^= rk[round * 16U + c * 4U + r];
}

/* ---- SubBytes ---- */
static void aes_sub_bytes(AES_State_t state)
{
    uint8_t c, r;
    for (c = 0; c < 4U; c++)
        for (r = 0; r < 4U; r++)
            state[c][r] = aes_sbox[state[c][r]];
}

/* ---- ShiftRows (operates on rows across columns) ----
 *
 * AES state layout (column-major):
 *   state[col][row]
 *
 *   col:  0    1    2    3
 *   row0: [0,0][1,0][2,0][3,0]   <- no shift
 *   row1: [0,1][1,1][2,1][3,1]   <- shift left 1
 *   row2: [0,2][1,2][2,2][3,2]   <- shift left 2
 *   row3: [0,3][1,3][2,3][3,3]   <- shift left 3
 */
static void aes_shift_rows(AES_State_t state)
{
    uint8_t tmp;

    /* Row 1: shift left by 1 */
    tmp          = state[0][1];
    state[0][1]  = state[1][1];
    state[1][1]  = state[2][1];
    state[2][1]  = state[3][1];
    state[3][1]  = tmp;

    /* Row 2: shift left by 2 */
    tmp          = state[0][2];
    state[0][2]  = state[2][2];
    state[2][2]  = tmp;
    tmp          = state[1][2];
    state[1][2]  = state[3][2];
    state[3][2]  = tmp;

    /* Row 3: shift left by 3 (= right by 1) */
    tmp          = state[3][3];
    state[3][3]  = state[2][3];
    state[2][3]  = state[1][3];
    state[1][3]  = state[0][3];
    state[0][3]  = tmp;
}

/* ---- MixColumns (each column is a 4-byte word) ---- */
static void aes_mix_columns(AES_State_t state)
{
    uint8_t c;
    uint8_t s0, s1, s2, s3;
    uint8_t p, q;

    for (c = 0; c < 4U; c++) {
        s0 = state[c][0];
        s1 = state[c][1];
        s2 = state[c][2];
        s3 = state[c][3];

        p = s0 ^ s1 ^ s2 ^ s3;         /* XOR of all */
        q = s0;                          /* save s0     */

        state[c][0] ^= p ^ aes_xtime(s0 ^ s1);
        state[c][1] ^= p ^ aes_xtime(s1 ^ s2);
        state[c][2] ^= p ^ aes_xtime(s2 ^ s3);
        state[c][3] ^= p ^ aes_xtime(s3 ^ q);
    }
}

/* ---- AES-128 ECB Encrypt ----
 *
 * plaintext[16] -> ciphertext[16]
 * Uses column-major 4×4 state, verified against NIST SP 800-38A.
 */
static void aes128_encrypt(const uint8_t *key,
                            const uint8_t *plaintext,
                            uint8_t       *ciphertext)
{
    uint8_t     rk[176];   /* 11 round keys × 16 bytes */
    AES_State_t state;
    uint8_t     round;
    uint8_t     c, r;

    aes_key_expansion(key, rk);

    /* Load plaintext into state (column-major) */
    for (c = 0; c < 4U; c++)
        for (r = 0; r < 4U; r++)
            state[c][r] = plaintext[c * 4U + r];

    aes_add_round_key(state, rk, 0);

    for (round = 1U; round <= 9U; round++) {
        aes_sub_bytes(state);
        aes_shift_rows(state);
        aes_mix_columns(state);
        aes_add_round_key(state, rk, round);
    }

    /* Final round: no MixColumns */
    aes_sub_bytes(state);
    aes_shift_rows(state);
    aes_add_round_key(state, rk, 10U);

    /* Store state into ciphertext */
    for (c = 0; c < 4U; c++)
        for (r = 0; r < 4U; r++)
            ciphertext[c * 4U + r] = state[c][r];
}

/* ============================================================
 * Constant-time compare (timing-attack resistant)
 * ============================================================ */
static bool auth_secure_compare(const uint8_t *a, const uint8_t *b,
                                  uint16_t len)
{
    uint8_t  diff = 0;
    uint16_t i;
    for (i = 0; i < len; i++)
        diff |= (a[i] ^ b[i]);
    return (diff == 0U);
}

/* ============================================================
 * Challenge Generation
 * [FIX v2.2 - H5] Uses EEPROM Nonce counter as additional entropy.
 *   Prevents identical challenges when tick=0 on reset.
 * ============================================================ */
static void auth_gen_challenge(uint8_t *challenge,
                                NFC_NTP53321_Handle_t *hntag)
{
    uint8_t  uid[7]   = {0};
    uint8_t  nonce_buf[4] = {0};
    uint32_t nonce_val;
    uint32_t tick, t2, t3;
    int      i;

    NFC_NTP53321_GetUID(hntag, uid);

    /* Read and increment persistent nonce from EEPROM */
    NFC_NTP53321_ReadBlock(hntag, NFC_NONCE_BLOCK_ADDR, nonce_buf);
    nonce_val  = (uint32_t)nonce_buf[0]
               | ((uint32_t)nonce_buf[1] << 8)
               | ((uint32_t)nonce_buf[2] << 16)
               | ((uint32_t)nonce_buf[3] << 24);
    nonce_val++;
    nonce_buf[0] = (uint8_t)(nonce_val);
    nonce_buf[1] = (uint8_t)(nonce_val >> 8);
    nonce_buf[2] = (uint8_t)(nonce_val >> 16);
    nonce_buf[3] = (uint8_t)(nonce_val >> 24);
    NFC_NTP53321_WriteBlock(hntag, NFC_NONCE_BLOCK_ADDR, nonce_buf);

    /* LFSR-style tick mixing */
    tick = HAL_GetTick();
    t2   = tick  ^ (tick  << 13) ^ (tick  >> 7);
    t3   = t2    ^ (t2   << 17) ^ (t2   >> 5) ^ nonce_val;

    for (i = 0; i < 16; i++) {
        challenge[i] = (uint8_t)((t3 >> (i % 32))
                                 ^ uid[i % 7]
                                 ^ nonce_buf[i % 4]
                                 ^ (uint8_t)(i * 0x6BU));
    }

    APP_LOGI("NFC", "Challenge(16)[0..2]: %02X %02X %02X (nonce=%lu)",
           challenge[0], challenge[1], challenge[2],
           (unsigned long)nonce_val);
}

/* ============================================================
 * SRAM helpers
 * ============================================================ */
static NFC_AUTH_Result_t auth_write_status(NFC_AUTH_Handle_t *hauth,
                                             uint8_t status)
{
    uint8_t buf[4] = { status, 0x00U, 0x00U, 0x00U };
    NFC_Result_t ret = NFC_NTP53321_WriteBlock(hauth->hntag,
                                                NFC_SRAM_STATUS_BLOCK,
                                                buf);
    APP_LOGI("NFC", "[[NFC-AUTH]] status=0x%02x written", status);
    return (ret == NFC_RESULT_OK) ? NFC_AUTH_RESULT_OK
                                  : NFC_AUTH_RESULT_I2C_ERROR;
}

static NFC_AUTH_Result_t auth_read_cmd(NFC_AUTH_Handle_t *hauth,
                                        uint8_t *cmd)
{
    uint8_t buf[4] = {0};
    NFC_Result_t ret;
    ret = NFC_NTP53321_ReadBlock(hauth->hntag,
                                               0x203F,
                                               buf);
    APP_LOGI("NFC", "[[NFC-AUTH]] %02x/%02x/%02x/%02x notify cmd", buf[0], buf[1], buf[2], buf[3]);

    ret = NFC_NTP53321_ReadBlock(hauth->hntag,
                                               NFC_SRAM_CMD_BLOCK,
                                               buf);
    APP_LOGI("NFC", "[[NFC-AUTH]] %02x/%02x/%02x/%02x read cmd", buf[0], buf[1], buf[2], buf[3]);
    if (ret != NFC_RESULT_OK) return NFC_AUTH_RESULT_I2C_ERROR;
    *cmd = buf[0];
    return NFC_AUTH_RESULT_OK;
}

static NFC_AUTH_Result_t auth_write_cmd(NFC_AUTH_Handle_t *hauth, uint8_t cmd)
{
    uint8_t buf[4] = { cmd, 0x00U, 0x00U, 0x00U };
    NFC_Result_t ret = NFC_NTP53321_WriteBlock(hauth->hntag,
                                               NFC_SRAM_CMD_BLOCK,
                                               buf);
    APP_LOGI("NFC", "[[NFC-AUTH]] cmd=0x%02x written", cmd);
    return (ret == NFC_RESULT_OK) ? NFC_AUTH_RESULT_OK
                                  : NFC_AUTH_RESULT_I2C_ERROR;
}

/* ============================================================
 * CONNECT handler (Step 1-3)
 * ============================================================ */
static NFC_AUTH_Result_t auth_handle_connect(NFC_AUTH_Handle_t *hauth)
{
    NFC_Result_t ret;

    APP_LOGI("NFC", "[[NFC-AUTH]] auth start cmd=CONNECT state=%s",
           auth_state_name(hauth->state));

    hauth->state = NFC_AUTH_STATE_CONNECTING;
    hauth->stats.total_attempts++;
    auth_write_status(hauth, NFC_AUTH_STATUS_CONNECTED);
    APP_LOGI("NFC", "[[NFC-AUTH]] connect accepted attempts=%lu",
           (unsigned long)hauth->stats.total_attempts);

    /* Generate and store challenge */
    auth_gen_challenge(hauth->session.challenge, hauth->hntag);

    ret = NFC_NTP53321_WriteMultiBlock(hauth->hntag,
                                       NFC_SRAM_CHALLENGE_BLOCK_START,
                                       hauth->session.challenge,
                                       4U);
    if (ret != NFC_RESULT_OK) {
        APP_LOGE("NFC", "Challenge write FAILED");
        hauth->state = NFC_AUTH_STATE_IDLE;
        return NFC_AUTH_RESULT_I2C_ERROR;
    }

    hauth->state = NFC_AUTH_STATE_CHALLENGING;
    auth_write_status(hauth, NFC_AUTH_STATUS_CHALLENGE_SENT);
    auth_write_cmd(hauth, NFC_AUTH_CMD_CHALLENGE_READY);
    APP_LOGI("NFC", "[[NFC-AUTH]] challenge issued");
    auth_log_session_valid(hauth, "post-challenge");
    return NFC_AUTH_RESULT_OK;
}

/* ============================================================
 * RESPONSE handler (Step 5-7)
 * ============================================================ */
static NFC_AUTH_Result_t auth_handle_response(NFC_AUTH_Handle_t *hauth)
{
    uint8_t      response[16] = {0};
    uint8_t      expected[16] = {0};
    NFC_Result_t ret;

    APP_LOGI("NFC", "[[NFC-AUTH]] response received state=%s",
           auth_state_name(hauth->state));

    if (hauth->state != NFC_AUTH_STATE_CHALLENGING) {
        APP_LOGE("NFC", "Bad state for RESPONSE (%d)", hauth->state);
        return NFC_AUTH_RESULT_INVALID_STATE;
    }
    hauth->state = NFC_AUTH_STATE_VERIFYING;

    ret = NFC_NTP53321_ReadMultiBlock(hauth->hntag,
                                      NFC_SRAM_RESPONSE_BLOCK_START,
                                      response, 4U);
    if (ret != NFC_RESULT_OK) {
        APP_LOGE("NFC", "Response read FAILED");
        hauth->state = NFC_AUTH_STATE_IDLE;
        return NFC_AUTH_RESULT_I2C_ERROR;
    }

    /* Expected = AES128(MasterKey, stored Challenge) */
    aes128_encrypt(hauth->master_key, hauth->session.challenge, expected);

    if (auth_secure_compare(response, expected, 16U)) {
        /* ---- SUCCESS ---- */
        hauth->state                = NFC_AUTH_STATE_AUTHENTICATED;
        hauth->fail_count           = 0;
        hauth->stats.success_count++;
        hauth->stats.last_success_tick = HAL_GetTick();

        hauth->session.start_tick  = HAL_GetTick();
        hauth->session.timeout_ms  = NFC_AUTH_SESSION_TIMEOUT_MS;
        hauth->session.active      = true;
        aes128_encrypt(hauth->master_key, response, hauth->session.token);

        auth_write_status(hauth, NFC_AUTH_STATUS_SUCCESS);
        auth_write_cmd(hauth, NFC_AUTH_CMD_CONFIRM);
        APP_LOGI("NFC", "[[NFC-AUTH]] response verify ok total=%lu",
               (unsigned long)hauth->stats.success_count);
        auth_log_session_valid(hauth, "auth-success");

        if (hauth->OnAuthSuccess_Callback != NULL)
            hauth->OnAuthSuccess_Callback(hauth->session.token);

        return NFC_AUTH_RESULT_OK;

    } else {
        /* ---- FAIL ---- */
        hauth->fail_count++;
        hauth->stats.fail_count++;
        hauth->stats.last_fail_tick = HAL_GetTick();
        hauth->state                = NFC_AUTH_STATE_FAILED;

        APP_LOGE("NFC", "[[NFC-AUTH]] response verify fail count=%u retry=allowed",
               (unsigned int)hauth->fail_count);

        auth_write_status(hauth, NFC_AUTH_STATUS_FAIL);
        auth_write_cmd(hauth, NFC_AUTH_CMD_CONFIRM);
        if (hauth->OnAuthFail_Callback != NULL)
            hauth->OnAuthFail_Callback(hauth->fail_count);

        hauth->state = NFC_AUTH_STATE_IDLE;
        auth_log_session_valid(hauth, "auth-fail");
        return NFC_AUTH_RESULT_FAIL;
    }
}

/* ============================================================
 * Public API
 * ============================================================ */
NFC_AUTH_Result_t NFC_AUTH_Init(NFC_AUTH_Handle_t *hauth,
                                 NFC_NTP53321_Handle_t *hntag,
                                 const uint8_t *master_key)
{
    if (hauth == NULL || hntag == NULL || master_key == NULL)
        return NFC_AUTH_RESULT_INVALID_PARAM;

    memset(hauth, 0, sizeof(NFC_AUTH_Handle_t));
    hauth->hntag              = hntag;
    hauth->state              = NFC_AUTH_STATE_IDLE;
    hauth->initialized        = true;
    hauth->session.timeout_ms = NFC_AUTH_SESSION_TIMEOUT_MS;

    memcpy(hauth->master_key, master_key, NFC_AUTH_KEY_SIZE);

    APP_LOGI("NFC", "Initialized Auth(timeout=%lu ms)",
           (unsigned long)NFC_AUTH_SESSION_TIMEOUT_MS);
    APP_LOGI("NFC", "[[NFC-AUTH]] auth module ready timeout=%lu retry=unlimited",
           (unsigned long)NFC_AUTH_SESSION_TIMEOUT_MS);
    auth_log_session_valid(hauth, "init");
    return NFC_AUTH_RESULT_OK;
}

NFC_AUTH_Result_t NFC_AUTH_ProcessNFCEvent(NFC_AUTH_Handle_t *hauth,
                                            NFC_WakeupEvent_t event)
{
    uint8_t           cmd = 0;
    NFC_AUTH_Result_t ret;

    if (hauth == NULL || !hauth->initialized)
        return NFC_AUTH_RESULT_INVALID_PARAM;
    if (event != NFC_WAKEUP_EVENT_ED_PIN)
        return NFC_AUTH_RESULT_INVALID_STATE;

    ret = auth_read_cmd(hauth, &cmd);
    if (ret != NFC_AUTH_RESULT_OK) return ret;


    APP_LOGI("NFC", "[[NFC-AUTH]] cmd=0x%02X state=%s event=%u",
           (unsigned int)cmd,
           auth_state_name(hauth->state),
           (unsigned int)event);

    if (cmd == 0x00u)
        return NFC_AUTH_RESULT_OK;

    switch (cmd) {
        case NFC_AUTH_CMD_CONNECT:  return auth_handle_connect(hauth);
        case NFC_AUTH_CMD_RESPONSE: return auth_handle_response(hauth);
        case NFC_AUTH_CMD_CHALLENGE_READY:
            APP_LOGI("NFC", "[[NFC-AUTH]] challenge-ready marker observed");
            return NFC_AUTH_RESULT_OK;
        case NFC_AUTH_CMD_CONFIRM:
            APP_LOGI("NFC", "[[NFC-AUTH]] confirm marker observed");
            return NFC_AUTH_RESULT_OK;
        default:
            APP_LOGW("NFC", "Unknown CMD 0x%02X", cmd);
            return NFC_AUTH_RESULT_INVALID_STATE;
    }
}

/* Overflow-safe session validity check */
bool NFC_AUTH_IsSessionValid(NFC_AUTH_Handle_t *hauth)
{
    uint32_t elapsed;
    if (hauth == NULL || !hauth->session.active) return false;
    if (hauth->state != NFC_AUTH_STATE_AUTHENTICATED) return false;
    elapsed = HAL_GetTick() - hauth->session.start_tick; /* overflow-safe */
    return (elapsed < hauth->session.timeout_ms);
}

NFC_AUTH_Result_t NFC_AUTH_InvalidateSession(NFC_AUTH_Handle_t *hauth)
{
    if (hauth == NULL) return NFC_AUTH_RESULT_INVALID_PARAM;
    hauth->session.active = false;
    hauth->state          = NFC_AUTH_STATE_IDLE;
    APP_LOGI("NFC", "[[NFC-AUTH]] session invalidated");
    auth_log_session_valid(hauth, "invalidate");
    return NFC_AUTH_RESULT_OK;
}


void NFC_AUTH_RegisterCallbacks(NFC_AUTH_Handle_t *hauth,
                                  void (*OnSuccess)(uint8_t *),
                                  void (*OnFail)(uint8_t))
{
    if (hauth == NULL) return;
    hauth->OnAuthSuccess_Callback = OnSuccess;
    hauth->OnAuthFail_Callback    = OnFail;
    APP_LOGI("NFC", "Callbacks registered");
}

void NFC_AUTH_GetStats(NFC_AUTH_Handle_t *hauth, NFC_AUTH_Stats_t *stats)
{
    if (hauth == NULL || stats == NULL) return;
    memcpy(stats, &hauth->stats, sizeof(NFC_AUTH_Stats_t));
}

void NFC_AUTH_PrintStats(NFC_AUTH_Handle_t *hauth)
{
    if (hauth == NULL) return;
    APP_LOGI("NFC", "=== Auth Stats ===");
    APP_LOGI("NFC", "Attempts : %lu",
           (unsigned long)hauth->stats.total_attempts);
    APP_LOGI("NFC", "Success  : %lu",
           (unsigned long)hauth->stats.success_count);
    APP_LOGI("NFC", "Fail     : %lu",
           (unsigned long)hauth->stats.fail_count);
    APP_LOGI("NFC", "Session  : %s",
           NFC_AUTH_IsSessionValid(hauth) ? "VALID" : "INVALID");
    APP_LOGI("NFC", "State    : %d", hauth->state);
    auth_log_session_valid(hauth, "print-stats");
}

void NFC_AUTH_Reset(NFC_AUTH_Handle_t *hauth)
{
    if (hauth == NULL) return;
    hauth->state          = NFC_AUTH_STATE_IDLE;
    hauth->fail_count     = 0;
    hauth->session.active = false;
    memset(&hauth->stats, 0, sizeof(NFC_AUTH_Stats_t));
    APP_LOGI("NFC", "Reset");
    auth_log_session_valid(hauth, "reset");
}

void NFC_AUTH_GetDebugInfo(NFC_AUTH_Handle_t *hauth, NFC_AUTH_DebugInfo_t *info)
{
    uint32_t elapsed = 0U;

    if (info == NULL) return;
    memset(info, 0, sizeof(*info));
    if (hauth == NULL) return;

    if (hauth->session.active) {
        elapsed = HAL_GetTick() - hauth->session.start_tick;
    }

    info->state = (uint8_t)hauth->state;
    info->last_cmd = 0U;
    info->last_result = (uint8_t)(NFC_AUTH_IsSessionValid(hauth) ? NFC_AUTH_RESULT_OK : NFC_AUTH_RESULT_FAIL);
    info->session_valid = NFC_AUTH_IsSessionValid(hauth) ? 1U : 0U;
    info->fail_count = hauth->fail_count;
    info->session_elapsed_ms = elapsed;
    info->session_timeout_ms = hauth->session.timeout_ms;
    info->last_event_tick_ms = hauth->session.start_tick;
}


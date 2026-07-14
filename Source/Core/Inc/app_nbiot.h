#ifndef APP_NBIOT_H
#define APP_NBIOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "stm32l0xx_hal.h"
#include "app_build_config.h"
#include "app_clock.h"
#include "app_error.h"

/* ============================================================
 *  Buffer / Timeout 기본값
 * ============================================================ */
#define APP_BC95_AT_RX_BUF_SIZE              (256u)
#define APP_BC95_AT_TX_TIMEOUT_MS            (500u)
#define APP_BC95_AT_RX_TIMEOUT_MS            (3000u)

/* ============================================================
 *  Boot / USIM
 * ============================================================ */
#define APP_BC95_BOOT_BANNER                 "Neul"
#define APP_BC95_BOOT_BANNER_LEN             (4u)
#define APP_BC95_BOOT_WAIT_BANNER_MS         (10000u)
#define APP_BC95_BOOT_PING_TIMEOUT_MS        (500u)
#define APP_BC95_BOOT_PING_INTERVAL_MS       (200u)
#define APP_BC95_BOOT_PING_MAX_RETRY         (30u)
#define APP_BC95_USIM_READY_TIMEOUT_MS       (15000u)
#define APP_BC95_USIM_READY_POLL_MS          (500u)

/* ============================================================
 *  Identity 관련
 * ============================================================ */
#define APP_BC95_IMEI_DIGITS                 (15u)
#define APP_BC95_IMEI_BCD_BYTES              (8u)
#define APP_BC95_IMSI_DIGITS                 (15u)
#define APP_BC95_IMSI_BCD_BYTES              (8u)

#define APP_BC95_IMEI_FETCH_RETRY_MAX        (3u)
#define APP_BC95_IMSI_FETCH_RETRY_MAX        (5u)
#define APP_BC95_QUALITY_FETCH_RETRY_MAX     (5u)
#define APP_BC95_FETCH_RETRY_DELAY_MS        (500u)

#define APP_BC95_QUALITY_BCD_BYTES           (10u)

typedef enum
{
    APP_BC95_AT_OK              = 0,
    APP_BC95_AT_ERR_PARAM       = 1,
    APP_BC95_AT_ERR_TIMEOUT     = 2,
    APP_BC95_AT_ERR_NO_PREFIX   = 3,
    APP_BC95_AT_ERR_FORMAT      = 4,
    APP_BC95_AT_ERR_RANGE       = 5,
    APP_BC95_AT_ERR_AT_ERROR    = 6,
    APP_BC95_AT_ERR_CME_ERROR   = 7,
    APP_BC95_AT_ERR_NO_OK       = 8,
    APP_BC95_AT_ERR_NO_DATA     = 9
} AppBc95AtStatus_t;

typedef struct
{
    int16_t  rssiDbm;
    uint8_t  ber;
    uint16_t pci;
    int16_t  rsrpDbm;
    int16_t  rsrqDbm;
    int16_t  snrDb;
    uint16_t earfcn;
    uint8_t  primaryCell;
    uint8_t  valid;
} AppBc95Quality_t;

/* ============================================================
 *  Boot / Probe
 * ============================================================ */
AppStatus_t App_Bc95AtWaitForBoot(uint32_t bannerTimeoutMs);
AppStatus_t App_Bc95AtPing(uint32_t timeoutMs);
AppStatus_t App_Bc95AtWaitUntilReady(uint32_t totalTimeoutMs);
AppStatus_t App_Bc95AtWaitForUsim(uint32_t timeoutMs);

/* ============================================================
 *  AT 일반
 * ============================================================ */
AppStatus_t        App_Bc95AtSendCommand(const char *p_cmd, uint8_t *p_rxBuf, uint16_t rxBufSize,
                                         uint32_t rxTimeoutMs, uint16_t *p_rxLengthOut);
AppBc95AtStatus_t  App_Bc95AtCheckResponse(const char *p_resp, int32_t *p_cmeErrOut);

AppBc95AtStatus_t  App_Bc95AtExtractImeiString(const char *p_resp, char *p_out, uint32_t outSize);
AppBc95AtStatus_t  App_Bc95AtImeiToBcd(const char *p_imeiStr, uint8_t *p_bcdOut, uint32_t bcdSize);
AppBc95AtStatus_t  App_Bc95AtParseImeiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize);

AppBc95AtStatus_t  App_Bc95AtExtractImsiString(const char *p_resp, char *p_out, uint32_t outSize);
AppBc95AtStatus_t  App_Bc95AtImsiToBcd(const char *p_imsiStr, uint8_t *p_bcdOut, uint32_t bcdSize);
AppBc95AtStatus_t  App_Bc95AtParseImsiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize);

AppStatus_t        App_Bc95AtFetchImei(uint8_t *p_imeiBcd, uint32_t bcdSize);
AppStatus_t        App_Bc95AtFetchImsi(uint8_t *p_imsiBcd, uint32_t bcdSize);
AppStatus_t        App_Bc95AtFetchImeiWithRetry(uint8_t *p_imeiBcd, uint32_t bcdSize, uint32_t maxRetry);
AppStatus_t        App_Bc95AtFetchImsiWithRetry(uint8_t *p_imsiBcd, uint32_t bcdSize, uint32_t maxRetry);

const uint8_t          *App_Bc95AtGetImeiBcd(void);
const uint8_t          *App_Bc95AtGetImsiBcd(void);
const AppBc95Quality_t *App_Bc95AtGetQuality(void);
const uint8_t          *App_Bc95AtGetQualityBcd(void);

const char        *App_Bc95AtGetStatusString(AppBc95AtStatus_t status);

void               App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart);
void               App_Bc95AtOnUartErrorIsr(UART_HandleTypeDef *p_huart);

AppBc95AtStatus_t  App_Bc95AtParseNuestatsCell(const char *p_resp, AppBc95Quality_t *p_quality);
AppStatus_t        App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf, uint32_t bcdBufSize);
AppStatus_t        App_Bc95AtFetchQualityWithRetry(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf,
                                                   uint32_t bcdBufSize, uint32_t maxRetry);

/* ============================================================
 *  네트워크 대기
 * ============================================================ */
#define APP_BC95_NET_POLL_INTERVAL_MS       (2000u)
#define APP_BC95_NET_DEFAULT_TIMEOUT_MS     (120000u)
#define APP_BC95_NET_CMD_RETRY_MAX          (3u)
#define APP_BC95_NET_CMD_RETRY_DELAY_MS     (300u)
#define APP_BC95_NET_DENIED_RETRY_MAX       (3u)
#define APP_BC95_NET_USIM_RETRY_MAX         (3u)
#define APP_BC95_IP_BUF_SIZE                (64u)
#define APP_BC95_AT_CFUN_RESP_TIMEOUT_MS    (90000u)

typedef enum
{
    APP_BC95_CEREG_NOT_REGISTERED   = 0,
    APP_BC95_CEREG_REGISTERED_HOME  = 1,
    APP_BC95_CEREG_SEARCHING        = 2,
    APP_BC95_CEREG_DENIED           = 3,
    APP_BC95_CEREG_UNKNOWN          = 4,
    APP_BC95_CEREG_REGISTERED_ROAM  = 5
} AppBc95CeregStat_t;

typedef enum
{
    APP_BC95_NET_PHASE_INIT          = 0,
    APP_BC95_NET_PHASE_CFUN_OFF      = 1,
    APP_BC95_NET_PHASE_USIM_ERROR    = 2,
    APP_BC95_NET_PHASE_REGISTERING   = 3,
    APP_BC95_NET_PHASE_ATTACHING     = 4,
    APP_BC95_NET_PHASE_WAITING_IP    = 5,
    APP_BC95_NET_PHASE_READY         = 6,
    APP_BC95_NET_PHASE_DENIED        = 7
} AppBc95NetPhase_t;

typedef struct
{
    AppBc95NetPhase_t   phase;
    uint8_t             cfunValue;
    uint8_t             cgattState;
    AppBc95CeregStat_t  ceregStat;
    uint8_t             hasIp;
    char                ipAddr[APP_BC95_IP_BUF_SIZE];
    uint8_t             ready;
    uint32_t            lastUpdateTick;
    uint32_t            pollCount;
    uint32_t            deniedCount;
} AppBc95NetStatus_t;

AppBc95AtStatus_t  App_Bc95AtParseCfun   (const char *p_resp, int32_t *p_funOut);
AppBc95AtStatus_t  App_Bc95AtParseCgatt  (const char *p_resp, int32_t *p_stateOut);
AppBc95AtStatus_t  App_Bc95AtParseCereg  (const char *p_resp, int32_t *p_nOut, int32_t *p_statOut);
AppBc95AtStatus_t  App_Bc95AtParseCgpaddr(const char *p_resp, char *p_ipOut, uint32_t ipBufSize);

AppStatus_t        App_Bc95AtSetFullFunction(void);
AppStatus_t        App_Bc95AtQueryNetStatus (AppBc95NetStatus_t *p_status);
AppStatus_t        App_Bc95AtWaitForNetwork (uint32_t totalTimeoutMs, AppBc95NetStatus_t *p_status);

const char        *App_Bc95AtGetCeregStatString (AppBc95CeregStat_t stat);
const char        *App_Bc95AtGetNetPhaseString  (AppBc95NetPhase_t phase);
const AppBc95NetStatus_t *App_Bc95AtGetLastNetStatus(void);

/* ============================================================
 *  UDP 송신
 * ============================================================ */
#define APP_BC95_DNS_TIMEOUT_MS              (30000u)
#define APP_BC95_DNS_POLL_INTERVAL_MS        (500u)
#define APP_BC95_SOCKET_TIMEOUT_MS           (5000u)
#define APP_BC95_NSOST_CONFIRM_TIMEOUT_MS    (15000u)
#define APP_BC95_UDP_LOCAL_PORT              (0u)
#define APP_BC95_UDP_SEND_RETRY_MAX          (3u)
#define APP_BC95_UDP_SEND_RETRY_DELAY_MS     (1000u)
#define APP_BC95_UDP_MAX_PAYLOAD             (512u)
#define APP_BC95_UDP_SOCKET_ID_MAX           (6)
#define APP_BC95_IP_STR_SIZE                 (64u)

typedef enum
{
    APP_BC95_UDP_STAGE_NONE       = 0,
    APP_BC95_UDP_STAGE_RESOLVE    = 1,
    APP_BC95_UDP_STAGE_CREATE     = 2,
    APP_BC95_UDP_STAGE_SEND       = 3,
    APP_BC95_UDP_STAGE_CONFIRM    = 4,
    APP_BC95_UDP_STAGE_CLOSE      = 5,
    APP_BC95_UDP_STAGE_DONE       = 6
} AppBc95UdpStage_t;

typedef struct
{
    AppBc95UdpStage_t  lastStage;
    int32_t            socketId;
    char               resolvedIp[APP_BC95_IP_STR_SIZE];
    uint16_t           sentBytes;
    uint8_t            seqNumber;
    uint8_t            sendConfirmed;
} AppBc95UdpResult_t;

#define APP_BC95_DNS_CMD_TIMEOUT_MS         (3000u)
#define APP_BC95_DNS_URC_TIMEOUT_MS         (30000u)
#define APP_BC95_DNS_URC_POLL_MS            (100u)
#define APP_BC95_DNS_INTERCMD_DELAY_MS      (200u)
#define APP_BC95_DNS_FAIL_RETRY_MAX         (3u)
#define APP_BC95_DNS_FAIL_RETRY_DELAY_MS    (3000u)
#define APP_BC95_HOSTNAME_MAX_LEN           (128u)

AppStatus_t  App_Bc95AtResolveHost(const char *p_hostname, char *p_ipOut, uint32_t ipBufSize);
AppStatus_t  App_Bc95AtResolveHostRobust(const char *p_hostname, char *p_ipOut, uint32_t ipBufSize);

AppStatus_t  App_Bc95AtCreateUdpSocket(uint16_t localPort, int32_t *p_socketOut);
AppStatus_t  App_Bc95AtUdpSendAndConfirm(int32_t socketId, const char *p_ip, uint16_t port,
                                         const uint8_t *p_data, uint16_t length,
                                         uint8_t seqNum, uint32_t confirmTimeoutMs);
AppStatus_t  App_Bc95AtCloseSocket(int32_t socketId);
void         App_Bc95AtCloseAllSockets(void);

AppStatus_t  App_Bc95AtUdpSendOnce(const char *p_host, uint16_t port,
                                   const uint8_t *p_data, uint16_t length,
                                   AppBc95UdpResult_t *p_result);

/* ============================================================
 *  시간 정보 (네트워크 → RTC 동기화)
 * ============================================================ */
#define APP_BC95_AT_CMD_CCLK_QUERY           "AT+CCLK?\r\n"
#define APP_BC95_AT_CMD_CTZU_ENABLE          "AT+CTZU=1\r\n"
#define APP_BC95_AT_CCLK_PREFIX              "+CCLK:"
#define APP_BC95_AT_CCLK_PREFIX_LEN          (6u)

#define APP_BC95_TIME_SYNC_RETRY_MAX         (10u)
#define APP_BC95_TIME_SYNC_RETRY_DELAY_MS    (1000u)
#define APP_BC95_TIME_MIN_VALID_YEAR         (2020u)

typedef struct
{
    AppDateTime_t dateTime;
    int8_t   tzQuarterHour; /* -48 ~ +56 (15분 단위), 예: +36 = UTC+9 */
    uint8_t  valid;
} AppBc95Time_t;

/* 응답 파서 */
AppBc95AtStatus_t  App_Bc95AtParseCclk(const char *p_resp, AppBc95Time_t *p_time);

/* 시간 조회 */
AppStatus_t  App_Bc95AtFetchTime(AppBc95Time_t *p_time);
AppStatus_t  App_Bc95AtFetchTimeWithRetry(AppBc95Time_t *p_time, uint32_t maxRetry);

/* 자동 타임존 동기화 활성화 (부팅 시 1회 호출 권장) */
AppStatus_t  App_Bc95AtEnableAutoTimezone(void);

/* 통합: 모듈에서 가져와 RTC 에 바로 적용 */
AppStatus_t  App_Bc95AtSyncTimeToRtc(void);

/* 접근자 */
const AppBc95Time_t *App_Bc95AtGetLastTime(void);

/* Application level */
AppStatus_t App_NBIoTSyncTime(void);

/* 외부 RTC 함수 (다른 파일에 구현되어 있는 기존 함수) */
extern void RTC_SetTime(int year, int month, int date, int hour, int min, int sec);

/* ============================================================
 *  Application level
 * ============================================================ */
#ifndef APP_NBIOT_BRINGUP_MAX_RESET
#define APP_NBIOT_BRINGUP_MAX_RESET         (3u)
#endif

AppStatus_t App_NBIoTAtInit(void);
AppStatus_t App_NBIoTBringUp(void);
AppStatus_t App_NBIoTBringUpWithReset(uint8_t maxResetRetry);
AppStatus_t App_NBIoTNetworkBringUp(void);
AppStatus_t App_NBIoTReadIdentity(uint8_t bSaveInfo);
AppStatus_t App_NBIoTReadQuality(uint8_t bSaveInfo);
AppStatus_t App_NBIoTTransmitUdp(void);

AppStatus_t App_ClockSyncFromNbiot(const AppBc95Time_t *nbTime);

#ifdef __cplusplus
}
#endif

#endif /* APP_NBIOT_H */

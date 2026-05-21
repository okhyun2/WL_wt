#ifndef APP_NBIOT_H
#define APP_NBIOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "stm32l0xx_hal.h"
#include "app_build_config.h"
#include "app_error.h"

#define APP_BC95_AT_RX_BUF_SIZE              (256u)
#define APP_BC95_AT_TX_TIMEOUT_MS            (500u)
#define APP_BC95_AT_RX_TIMEOUT_MS            (3000u)

#define APP_BC95_BOOT_BANNER             "Neul"
#define APP_BC95_BOOT_BANNER_LEN         (4u)
#define APP_BC95_BOOT_WAIT_BANNER_MS     (10000u)   /* 부팅 배너 대기 (콜드 부팅 최대) */
#define APP_BC95_BOOT_PING_TIMEOUT_MS    (500u)    /* AT 핑 1회 응답 대기 */
#define APP_BC95_BOOT_PING_INTERVAL_MS   (200u)    /* AT 핑 간격 */
#define APP_BC95_BOOT_PING_MAX_RETRY     (30u)     /* 핑 최대 시도 횟수 */
#define APP_BC95_USIM_READY_TIMEOUT_MS      (15000u)
#define APP_BC95_USIM_READY_POLL_MS         (500u)

#define APP_BC95_IMEI_DIGITS                 (15u)
#define APP_BC95_IMEI_BCD_BYTES              (8u)
#define APP_BC95_IMSI_DIGITS                 (15u)
#define APP_BC95_IMSI_BCD_BYTES              (8u)

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
    int16_t  rssiDbm;        /* dBm, AT+NUESTATS=CELL의 rssi 필드 (centibel/10) */
    uint8_t  ber;            /* reserved, 항상 0 */
    uint16_t pci;            /* Physical Cell ID (0~503) — CID 필드로 사용 */
    int16_t  rsrpDbm;        /* dBm (centibel/10) */
    int16_t  rsrqDbm;        /* dB  (centibel/10) */
    int16_t  snrDb;          /* dB  (centibel/10) */
    uint16_t earfcn;         /* 참고용, 직렬화에는 미포함 */
    uint8_t  primaryCell;    /* 참고용 */
    uint8_t  valid;          /* APP_TRUE if 모든 필드 채워짐 */
} AppBc95Quality_t;

AppStatus_t App_Bc95AtWaitForBoot(uint32_t bannerTimeoutMs);
AppStatus_t App_Bc95AtPing(uint32_t timeoutMs);
AppStatus_t App_Bc95AtWaitUntilReady(uint32_t totalTimeoutMs);
AppStatus_t App_Bc95AtWaitForUsim(uint32_t timeoutMs);

AppStatus_t        App_Bc95AtSendCommand(const char *p_cmd, uint8_t *p_rxBuf, uint16_t rxBufSize, uint32_t rxTimeoutMs, uint16_t *p_rxLengthOut);

AppBc95AtStatus_t  App_Bc95AtCheckResponse(const char *p_resp, int32_t *p_cmeErrOut);

AppBc95AtStatus_t  App_Bc95AtExtractImeiString(const char *p_resp, char *p_out, uint32_t outSize);
AppBc95AtStatus_t  App_Bc95AtImeiToBcd(const char *p_imeiStr, uint8_t *p_bcdOut, uint32_t bcdSize);
AppBc95AtStatus_t  App_Bc95AtParseImeiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize);

AppBc95AtStatus_t  App_Bc95AtExtractImsiString(const char *p_resp, char *p_out, uint32_t outSize);
AppBc95AtStatus_t  App_Bc95AtImsiToBcd(const char *p_imsiStr, uint8_t *p_bcdOut, uint32_t bcdSize);
AppBc95AtStatus_t  App_Bc95AtParseImsiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize);

AppStatus_t        App_Bc95AtFetchImei(uint8_t *p_imeiBcd, uint32_t bcdSize);
AppStatus_t        App_Bc95AtFetchImsi(uint8_t *p_imsiBcd, uint32_t bcdSize);

const uint8_t     *App_Bc95AtGetImeiBcd(void);
const uint8_t     *App_Bc95AtGetImsiBcd(void);
const AppBc95Quality_t *App_Bc95AtGetQuality(void);
const uint8_t *App_Bc95AtGetQualityBcd(void);

const char        *App_Bc95AtGetStatusString(AppBc95AtStatus_t status);

void               App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart);
void               App_Bc95AtOnUartErrorIsr(UART_HandleTypeDef *p_huart);

AppBc95AtStatus_t  App_Bc95AtParseNuestatsCell(const char *p_resp, AppBc95Quality_t *p_quality);

AppStatus_t        App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf, uint32_t bcdBufSize);


/* ============================================================
 *  네트워크 대기 관련 정의
 * ============================================================ */
#define APP_BC95_NET_POLL_INTERVAL_MS       (2000u)    /* 폴링 간격 */
#define APP_BC95_NET_DEFAULT_TIMEOUT_MS     (120000u)  /* 기본 2분 */
#define APP_BC95_NET_CMD_RETRY_MAX          (3u)       /* 단일 명령 재시도 */
#define APP_BC95_NET_CMD_RETRY_DELAY_MS     (300u)
#define APP_BC95_NET_DENIED_RETRY_MAX       (3u)       /* CEREG=3 허용 횟수 */
#define APP_BC95_NET_USIM_RETRY_MAX         (3u)       /* USIM 일시 오류 허용 */
#define APP_BC95_IP_BUF_SIZE                (64u)
#define APP_BC95_AT_CFUN_RESP_TIMEOUT_MS    (90000u)   /* AT+CFUN=1 응답 (매뉴얼 85s) */

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
    APP_BC95_NET_PHASE_CFUN_OFF      = 1,    /* CFUN=0 -> CFUN=1 필요 */
    APP_BC95_NET_PHASE_USIM_ERROR    = 2,    /* USIM 미인식/오류 (영구) */
    APP_BC95_NET_PHASE_REGISTERING   = 3,    /* CEREG 검색 중 */
    APP_BC95_NET_PHASE_ATTACHING     = 4,    /* 등록 됐지만 CGATT=0 */
    APP_BC95_NET_PHASE_WAITING_IP    = 5,    /* CGATT=1 됐지만 IP 미할당 */
    APP_BC95_NET_PHASE_READY         = 6,    /* 송수신 가능 */
    APP_BC95_NET_PHASE_DENIED        = 7     /* 등록 거부 누적 (영구) */
} AppBc95NetPhase_t;

typedef struct
{
    AppBc95NetPhase_t   phase;
    uint8_t             cfunValue;           /* 0=min, 1=full, 0xFF=unknown */
    uint8_t             cgattState;          /* 0=detached, 1=attached, 0xFF=unknown */
    AppBc95CeregStat_t  ceregStat;
    uint8_t             hasIp;
    char                ipAddr[APP_BC95_IP_BUF_SIZE];
    uint8_t             ready;               /* APP_TRUE = 송수신 가능 */
    uint32_t            lastUpdateTick;
    uint32_t            pollCount;
    uint32_t            deniedCount;
} AppBc95NetStatus_t;

/* --- 응답 파서 --- */
AppBc95AtStatus_t  App_Bc95AtParseCfun   (const char *p_resp, int32_t *p_funOut);
AppBc95AtStatus_t  App_Bc95AtParseCgatt  (const char *p_resp, int32_t *p_stateOut);
AppBc95AtStatus_t  App_Bc95AtParseCereg  (const char *p_resp, int32_t *p_nOut, int32_t *p_statOut);
AppBc95AtStatus_t  App_Bc95AtParseCgpaddr(const char *p_resp, char *p_ipOut, uint32_t ipBufSize);

/* --- 단일 동작 --- */
AppStatus_t        App_Bc95AtSetFullFunction(void);
AppStatus_t        App_Bc95AtQueryNetStatus (AppBc95NetStatus_t *p_status);

/* --- 통합 대기 --- */
AppStatus_t        App_Bc95AtWaitForNetwork (uint32_t totalTimeoutMs, AppBc95NetStatus_t *p_status);

/* --- 문자열/접근자 --- */
const char        *App_Bc95AtGetCeregStatString (AppBc95CeregStat_t stat);
const char        *App_Bc95AtGetNetPhaseString  (AppBc95NetPhase_t phase);
const AppBc95NetStatus_t *App_Bc95AtGetLastNetStatus(void);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////
AppStatus_t App_NBIoTAtInit(void);
AppStatus_t App_NBIoTBringUp(void);
AppStatus_t App_NBIoTNetworkBringUp(void);
AppStatus_t App_NBIoTReadIdentity(void);
AppStatus_t App_NBIoTReadQuality(void);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* APP_NBIOT_H */

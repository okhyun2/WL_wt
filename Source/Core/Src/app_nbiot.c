#include <string.h>
#include <strings.h>   /* strncasecmp */
#include <stdlib.h>
#include <stdio.h>

#include "app_build_config.h"
#include "main.h"
#include "app_hw.h"
#include "app_nbiot.h"
#include "boot_udp_dl.h"   /* [FW-UPD] BootDlMeta_t / BOOT_DL_META_PAGE2 공유 */
#include "app_gpio_lp.h"
#include "app_log.h"
#include "app_clock.h"
#include "app_fsm.h"
#include "app_meter_storage.h"
#include "app_meter_server_format.h"
#include "app_comm_param.h"
#include "app_aux.h"
#include "app_selftest.h"

#define APP_NBIOT_AT_RAW_RX_DEBUG   APP_FALSE //TODO. debug:APP_TRUE, PRODUCT: APP_FALSE

#define APP_BC95_AT_CMD_PING          "AT\r\n"
#define APP_BC95_AT_CMD_IMEI          "AT+CGSN=1\r\n"
#define APP_BC95_AT_CMD_IMSI          "AT+CIMI\r\n"
#define APP_BC95_AT_CMD_NUESTATS_CELL "AT+NUESTATS=CELL\r\n"

#define APP_BC95_AT_CGSN_PREFIX_LEN          (6u)
#define APP_BC95_AT_CME_ERROR_PREFIX         "+CME ERROR:"
#define APP_BC95_AT_CME_ERROR_PREFIX_LEN     (11u)
#define APP_BC95_AT_ERROR_TOKEN              "ERROR"
#define APP_BC95_AT_ERROR_TOKEN_LEN          (5u)
#define APP_BC95_AT_OK_TOKEN                 "OK"
#define APP_BC95_AT_OK_TOKEN_LEN             (2u)
#define APP_BC95_AT_BCD_PAD_NIBBLE           (0x0Fu)

#define APP_BC95_AT_NUESTATS_CELL_PREFIX     "NUESTATS:CELL,"
#define APP_BC95_AT_NUESTATS_CELL_PREFIX_LEN (14u)
#define APP_BC95_AT_NUESTATS_CELL_FIELDS     (7u)
#define APP_BC95_AT_DBM_ABS_MAX              (150)

#define APP_BC95_AT_CMD_TX_BUF_SIZE          (1400u)
#define APP_BC95_AT_QDNS_PREFIX              "+QDNS:"
#define APP_BC95_AT_QDNS_PREFIX_LEN          (6u)
#define APP_BC95_AT_NSOSTR_PREFIX            "+NSOSTR:"
#define APP_BC95_AT_NSOSTR_PREFIX_LEN        (8u)
#define APP_BC95_AT_NSORF_PREFIX             "+NSORF:"
#define APP_BC95_AT_NSORF_PREFIX_LEN         (7u)

#define APP_BC95_AT_CMD_NSOCR_UDP_FMT        "AT+NSOCR=DGRAM,17,%u,1,AF_INET\r\n"
#define APP_BC95_AT_CMD_NSOCL_FMT            "AT+NSOCL=%ld\r\n"
#define APP_BC95_AT_CMD_NSORF_FMT            "AT+NSORF=%ld,%u\r\n"

#define APP_BC95_AT_CMD_CFUN_QUERY           "AT+CFUN?\r\n"
#define APP_BC95_AT_CMD_CFUN_SET_FULL        "AT+CFUN=1\r\n"
#define APP_BC95_AT_CMD_CFUN_SET_MIN         "AT+CFUN=0\r\n"
#define APP_BC95_AT_CMD_CGATT_QUERY          "AT+CGATT?\r\n"
#define APP_BC95_AT_CMD_CGATT_ATTACH         "AT+CGATT=1\r\n"
#define APP_BC95_AT_CMD_CGATT_DETACH         "AT+CGATT=0\r\n"
#define APP_BC95_AT_CMD_CMEE_ON              "AT+CMEE=1\r\n"
#define APP_BC95_AT_CMD_CEREG_SET_3          "AT+CEREG=3\r\n"
#define APP_BC95_AT_CMD_CEREG_QUERY          "AT+CEREG?\r\n"
#define APP_BC95_AT_CMD_CGPADDR_QUERY        "AT+CGPADDR\r\n"
#define APP_BC95_AT_CMD_CPSMS_QUERY          "AT+CPSMS?\r\n"
#define APP_BC95_AT_CMD_NCCID_QUERY          "AT+NCCID?\r\n"

#define APP_BC95_AT_CMD_QGMR_QUERY           "AT+QGMR\r\n"
#define APP_BC95_AT_CMD_CPIN_QUERY           "AT+CPIN?\r\n"
#define APP_BC95_AT_CMD_NCONFIG_QUERY        "AT+NCONFIG?\r\n"
#define APP_BC95_AT_CMD_NCONFIG_AUTOCONNECT_OFF "AT+NCONFIG=AUTOCONNECT,FALSE\r\n"
#define APP_BC95_AT_CMD_NCONFIG_AUTOCONNECT_ON  "AT+NCONFIG=AUTOCONNECT,TRUE\r\n"
#define APP_BC95_AT_CMD_NCONFIG_CELL_RESELECTION_ON "AT+NCONFIG=CELL_RESELECTION,TRUE\r\n"
#define APP_BC95_AT_CMD_NCONFIG_SCRAMBLEING  "AT+NCONFIG=CR_0354_0338_SCRAMBLING,TRUE\r\n"
#define APP_BC95_AT_CMD_QREGSWT_QUERY        "AT+QREGSWT?\r\n"
#define APP_BC95_AT_CMD_QREGSWT_SET_1        "AT+QREGSWT=1\r\n"
#define APP_BC95_AT_CMD_QREGSWT_SET_2        "AT+QREGSWT=2\r\n"
#define APP_BC95_AT_CMD_QLWFOTAIND_OFF       "AT+QLWFOTAIND=0\r\n"
#define APP_BC95_AT_CMD_NRB                  "AT+NRB\r\n"
#define APP_BC95_AT_CMD_QLWSERVERIP_QUERY    "AT+QLWSERVERIP?\r\n"
#define APP_BC95_AT_CMD_QLWEPNS_QUERY        "AT+QLWEPNS?\r\n"
#define APP_BC95_AT_CMD_QLWMBSPS_QUERY       "AT+QLWMBSPS?\r\n"
#define APP_BC95_AT_CMD_QBOOTSTRAPHOLDOFF_QUERY "AT+QBOOTSTRAPHOLDOFF?\r\n"
#define APP_BC95_AT_CMD_NBAND_QUERY          "AT+NBAND?\r\n"
#define APP_BC95_AT_CMD_NBAND_SET            "AT+NBAND=" APP_BC95_PLATFORM_LGU_NBAND_STRING "\r\n"
#define APP_BC95_AT_CMD_LGU_QLWSERVERIP_SET_BS   "AT+QLWSERVERIP=" APP_BC95_PLATFORM_LGU_BOOTSTRAP_MODE "," APP_BC95_PLATFORM_LGU_SERVER_IP_STRING "," APP_BC95_PLATFORM_LGU_SERVER_PORT_STRING "\r\n"
#define APP_BC95_AT_CMD_LGU_QLWSERVERIP_SET_LWM2M "AT+QLWSERVERIP=" APP_BC95_PLATFORM_LGU_LWM2M_MODE "," APP_BC95_PLATFORM_LGU_SERVER_IP_STRING "," APP_BC95_PLATFORM_LGU_SERVER_PORT_STRING "\r\n"
#define APP_BC95_AT_CMD_LGU_QBOOTSTRAPHOLDOFF_SET_0 "AT+QBOOTSTRAPHOLDOFF=0\r\n"
#define APP_BC95_AT_CMD_LGU_QLWEPNS_MANU_SET      "AT+QLWEPNS=1," APP_BC95_PLATFORM_LGU_EPNS_STRING "\r\n"
#define APP_BC95_AT_CMD_LGU_QLWEPNS_AUTO_SET      "AT+QLWEPNS=0," APP_BC95_PLATFORM_LGU_SERVCODE_STRING "\r\n"
//realtime 생성.
//#define APP_BC95_AT_CMD_LGU_QLWMBSPS_SET     "AT+QLWMBSPS=" APP_BC95_PLATFORM_LGU_MBSPS_STRING "\r\n"
#define APP_BC95_AT_CMD_CSQ                  "AT+CSQ\r\n"

#define APP_BC95_SERVICE_READY_WAKE_SETTLE_MS        (2500u)
#define APP_BC95_SERVICE_READY_WAKE_CME_THRESHOLD    (2u)
#define APP_BC95_SERVICE_READY_POST_CFUN_SETTLE_MS   (2500u)
#define APP_BC95_SERVICE_READY_WAKE_READY_STREAK     (2u)

/*
 * wakeup fast-path에서 service-ready probe로 너무 빨리 들어가면
 * CPIN/USIM 준비 전이라 CME=524가 반복될 수 있다.
 * -> 최근 NRB ready 힌트로 skip-boot-wait를 타는 세션에 한해
 *    고정 지연 + CPIN READY gate를 한 번 먼저 수행한다.
 */
#define APP_BC95_WAKE_CPIN_GUARD_DELAY_MS            (3000u)
#define APP_BC95_WAKE_CPIN_GUARD_RETRY_DELAY_MS      (1000u)
#define APP_BC95_WAKE_CPIN_GUARD_TIMEOUT_MS          (30000u)
#define APP_BC95_WAKE_CPIN_GUARD_POLL_MS             (500u)

/* CME 524 반복 후 attach 진입 전 추가 안정화 */
#define APP_BC95_CME524_ATTACH_STABILIZE_DELAY_MS    (1500u)
#define APP_BC95_CME524_ATTACH_READY_RECHECK_MS      (4000u)
#define APP_BC95_CME524_ATTACH_RECHECK_RETRY_MAX     (1u)

/* 현재 소스 추가 보강안: 524는 더 늦게 recovery로 넘기고, recovery 직후 net reset 1회 생략 */
#define APP_BC95_CME524_WAKE_GRACE_MS                (4000u)
#define APP_BC95_CME524_WAKE_RECOVERY_THRESHOLD      (4u)
#define APP_BC95_CME524_GRACE_POLL_MS                (1000u)
#define APP_BC95_CME524_ATTACH_WINDOW_MS             (15000u)

#define APP_BC95_AT_CMD_QLWSREGIND_REGISTER  "AT+QLWSREGIND=0\r\n"
#define APP_BC95_AT_CMD_QLWULDATASTATUS_QUERY "AT+QLWULDATASTATUS?\r\n"
/* [NNMI 래치 2026-11-04] 서비스 서버 애플리케이션 ACK는 LwM2M DL로 푸시된다
 * (BC95-GV LG U+ App Note v1.0.3 §1.5: "+NNMI:<len>,<hex>" — 별도 읽기 명령 없음).
 * QLWULDATASTATUS=4는 플랫폼(CoAP) 1차 ACK일 뿐이며, 서버 ACK는 +NNMI URC로만 확인.
 * EVTIND와 동일한 3-경로(응답 혼입 스캔 / DMA 잔량 스캔 / 폴링 강제 스캔)로 래치한다. */
#define APP_BC95_AT_NNMI_PREFIX              "+NNMI:"
#define APP_BC95_AT_NNMI_PREFIX_LEN          (6u)
#define APP_BC95_NNMI_LATCH_MAX              (64u)   /* 래치 보관 최대 바이트 */
#define APP_BC95_NNMI_LINE_MAX               (160u)  /* +NNMI:<len>,<hex> 라인 최대 (2*64+여유) */
#define APP_BC95_AT_QLWEVTIND_PREFIX         "+QLWEVTIND:"
#define APP_BC95_AT_QLWEVTIND_PREFIX_LEN     (11u)
#define APP_BC95_AT_QLWULDATASTATUS_PREFIX   "+QLWULDATASTATUS:"
#define APP_BC95_AT_QLWULDATASTATUS_PREFIX_LEN (17u)
#define APP_BC95_PLATFORM_EVENT_TIMEOUT_MS   (30000u)
#define APP_BC95_PLATFORM_STATUS_TIMEOUT_MS  (30000u)
#define APP_BC95_PLATFORM_STATUS_POLL_MS     (1000u)
/* fast-path: 부팅 직후 AUTOCONNECT=TRUE + NRB로 LwM2M 엔진이 자기 등록을 시작하므로
 * EVTIND(0/3) 도착을 기다린다. 실측 11:40 로그에서 2.5s 만에 timeout -> QLWSREGIND=CME4 실패였으므로
 * 10s로 확장 (observe 요구 정책 REQUIRE_OBSERVE 그대로 유지) */
/* [2026-11-05 실측 기반 단축] 139세션 분석: 정상 세션 EVTIND 4→3(ready) 평균 2.0s,
 * 최대(4→8) 24s. 비정상(3/8 미도래) 세션 5건은 120s 대기로도 회복 불가(NRB 필요).
 * → fast-path 120s→30s 단축(최대 실측 24s + 마진 6s). 정상 무손실, 비정상 세션당
 * 90s 절약(시간당 세션 수 증가에 따른 전체 회수율 개선). attempt2(NRB 후)도 동일 30s. */
#define APP_BC95_PLATFORM_FAST_PATH_TIMEOUT_MS (30*1000u) //best: 30sec
#define APP_BC95_PLATFORM_REGISTER_RETRY_MAX  (2u)
#define APP_BC95_PLATFORM_SEND_RETRY_MAX      (3u)
#define APP_BC95_PLATFORM_RETRY_BACKOFF_MS    (1000u)
#define APP_BC95_POST_PROVISION_SETTLE_MS     (1000u)
#define APP_BC95_POST_PROVISION_SERVICE_READY_TIMEOUT_MS (12000u)
#define APP_BC95_POST_PROVISION_REATTACH_TIMEOUT_MS      (30000u)

#define APP_BC95_AT_CFUN_PREFIX              "+CFUN:"
#define APP_BC95_AT_CFUN_PREFIX_LEN          (6u)
#define APP_BC95_AT_CGATT_PREFIX             "+CGATT:"
#define APP_BC95_AT_CGATT_PREFIX_LEN         (7u)
#define APP_BC95_AT_CEREG_PREFIX             "+CEREG:"
#define APP_BC95_AT_CEREG_PREFIX_LEN         (7u)
#define APP_BC95_AT_CGPADDR_PREFIX           "+CGPADDR:"
#define APP_BC95_AT_CGPADDR_PREFIX_LEN       (9u)

#define APP_BC95_AT_RX_IDLE_GAP_MS           (40u)
#define APP_BC95_AT_RX_POST_TERM_WAIT_MS     (20u)
#define APP_BC95_AT_DRAIN_GUARD_MS           (5u)
#define APP_BC95_AT_LINE_QUEUE_DEPTH         (12u)
#define APP_BC95_AT_LINE_MAX_LEN             (96u)
#define APP_BC95_SERVICE_READY_SETTLE_MS     (500u)
#define APP_BC95_SERVICE_READY_RECOVERABLE_CME_THRESHOLD (4u)
#define APP_BC95_SERVICE_READY_RECOVER_SETTLE_MS (1500u)
#define APP_BC95_NET_REGISTERING_GRACE_MS    (120000u)
#define APP_BC95_NET_REGISTERING_GRACE_MS_FAST_RECOVERY (30000u)
#define APP_BC95_NET_REGISTERING_STUCK_RECOVERY_MAX (1u)
#define APP_BC95_NET_REGISTERING_STUCK_MIN_POLLS (8u)
#define APP_BC95_NET_REGISTERING_STUCK_MIN_POLLS_FAST_RECOVERY (4u)
#define APP_NBIOT_NVM_WRITE_GAP_MS           (100u)
#define APP_NBIOT_NCONFIG_QUERY_SETTLE_MS    (500u)
/* CFUN=0 재전송 간격 단축: detach 처리 중 모듈은 바쁘므로 300ms idle-gap 후 재시도 */
#define APP_NBIOT_POWEROFF_CFUN0_RETRY_DELAY_MS (1000u)
/* NCONFIG? 응답 ~500B@9600bps ~= 520ms + 마진.
 * 255B 스냅샷 캡으로 최종 OK는 구조적으로 못 받아 timeout 경로로 skip 판정하므로
 * 전체 응답을 기다릴 필요가 없다 - 부팅당 1.5s 절감 */
#define APP_NBIOT_NCONFIG_QUERY_RX_TIMEOUT_MS (1500u)
#define APP_NBIOT_SILENCE_REBOOT_EXTRA_WAIT_MS (3000u)

#define APP_NBIOT_LOG_PREATTACH         "[PreAttach]"
#define APP_NBIOT_LOG_DNS               "[DNS]"
#define APP_NBIOT_LOG_READYPROBE        "[ReadyProbe]"

#define APP_NBIOT_REPORT_LOG_ATTACH         "[[Attach]]"
#define APP_NBIOT_REPORT_LOG_RESET          "[[Reset]]"
#define APP_NBIOT_REPORT_LOG_DETACH         "[[Detach]]"
#define APP_NBIOT_REPORT_LOG_POWEROFF       "[[PowerOff]]"

#define APP_NBIOT_REJECT_CAUSE_SUSPEND        (19)
#define APP_NBIOT_REJECT_CAUSE_TERMINATED_1   (8)
#define APP_NBIOT_REJECT_CAUSE_TERMINATED_2   (2)
#define APP_NBIOT_REJECT_CAUSE_TERMINATED_3   (26)

/* [NNMI] 서비스 서버 ACK 대기 (2차 ACK): QLWULDATASTATUS=4(1차, 플랫폼 CoAP) 이후 호출.
 * 서버 응답은 +NNMI 푸시이므로 NSORF 폴링이 아닌 래치 카운터 폴링으로 대기(문서 §1.5).
 * 타임아웃 시에도 전송 자체는 성공(STATUS=4)이므로 OK 반환 — 재전송에 의한 이중화 방지. */
/* ============================================================================
 * [DL 명령 실행 2026-11-04] +NNMI로 수신한 A3 명령 프레임의 실제 동작 수행
 *   0x50 reset  : 단말 리셋(6.5) — 시스템 소프트 리셋 지연 수행(로그 플러시 시간 확보)
 *   0x51 period : 주기 변경(6.6) — payload [측정h][보고h] → 옵션 저장(NVM) 후 반영
 *   0x52 spread : 보고 시간간격 분산(6.7) — payload [분산h] → 옵션 저장(NVM) 후 반영
 * 프레임은 App_DlAckClassifyFrame(헤더/msgLen/checksum)을 통과한 것만 전달된다.
 * 실행 결과는 APP_LOGN으로 남기며, period/spread 저장 실패 시에도 수신 자체는
 * ack 카운트된다(수신-실행 분리).
 * ========================================================================== */
#define APP_DL_CMD_RESET                      (0x50u)
#define APP_DL_CMD_PERIOD                     (0x51u)
#define APP_DL_CMD_SPREAD                     (0x52u)
/* [FW-UPD 6.9] 앱 업데이트 시작: [ver(2)][size(4)][crc32(4)][ip(4)][port(2)] 수신 ->
 * DlMeta(META page2) 기록 후 소프트 리셋. 부트가 BOOT UDP 다운로드를 수행한다. */
#define APP_DL_CMD_FW_BEGIN                    (0x54u)
#define APP_DL_CMD_RESET_DELAY_MS             (1500u)   /* 로그 플러시 후 리셋 */
/* [mgmt reserved 2026-11-04] mgmt 전용 cmd 예약 영역 0x60~0x7F — 향후 확장용.
 * 등록은 App_DlAckRegisterCmd(cmd, "이름"), 동작은 case 추가로 확장. */
#define APP_DL_CMD_MGMT_RESERVED_MIN          (0x60u)
#define APP_DL_CMD_MGMT_RESERVED_MAX          (0x7Fu)

/* ============================================================
 *  내부 상태
 * ============================================================ */
typedef struct
{
    UART_HandleTypeDef *p_huart;
    uint8_t *p_buffer;
    uint16_t bufferSize;
    volatile uint16_t receivedLength;
    volatile uint8_t active;
    volatile uint8_t completed;
    volatile uint8_t error;
} AppBc95AtRxContext_t;

typedef struct
{
    char     lines[APP_BC95_AT_LINE_QUEUE_DEPTH][APP_BC95_AT_LINE_MAX_LEN];
    char     partial[APP_BC95_AT_LINE_MAX_LEN];
    uint8_t  head;
    uint8_t  tail;
    uint8_t  count;
    uint16_t partialLen;
    uint16_t parsedLen;
} AppBc95AtLineQueue_t;

static AppBc95AtRxContext_t g_appBc95AtRxContext;
static uint8_t  g_appBc95AtRxBuf[APP_BC95_AT_RX_BUF_SIZE];

static uint8_t  g_appBc95AtInitialized;
static uint8_t  g_appBc95AtImeiBcd[APP_BC95_IMEI_BCD_BYTES];
static uint8_t  g_appBc95AtImsiBcd[APP_BC95_IMSI_BCD_BYTES];
static uint8_t  g_appBc95AtQualityBcd[APP_BC95_QUALITY_BCD_BYTES];
static AppBc95Quality_t g_appBc95AtQuality;
/* [QUALITY-REPORT 2026-11-09] 품질리포트 상태(RAM, STOP 유지).
 * 리셋 시 lastValid=0 -> 부팅 후 첫 만료 세션에서 부팅후 리포트(UE INFO bit0=1) 1회. */
static uint8_t  g_appBc95UdpSkipAckWait   = APP_FALSE;
static uint8_t  g_appQualityReportLastValid = APP_FALSE;
static uint32_t g_appQualityReportLastDays  = 0u;
static AppBc95NetStatus_t g_appBc95AtNetStatus;
static AppNbiotCarrierContext_t g_appNbiotCarrierContext;
static uint8_t g_appBc95UsimFatal = APP_FALSE;
static uint8_t g_appBc95SkipNextAttachBootWait = APP_FALSE;
static uint8_t g_appBc95ShortRetryBootBannerWaitOnce = APP_FALSE;
static uint8_t g_appBc95ForceSkipAttachBootWaitOnce = APP_FALSE;
static uint8_t g_appBc95RegisteringStuckRecoveryUsed = APP_FALSE;
static uint8_t g_appBc95WakeNeedsPreAttachNetReset = APP_FALSE;
static uint8_t g_appBc95WakeNeedsCme524AttachBackoff = APP_FALSE;
static uint8_t g_appBc95LastRecoverableCmeWas524 = APP_FALSE;
static uint8_t g_appBc95WakeCme524GraceUsed = APP_FALSE;
static uint8_t g_appBc95WakeSkipPreAttachNetResetOnce = APP_FALSE;
static uint8_t g_appBc95Cme524AttachRecheckCount = 0u;
static uint32_t g_appBc95LastRecoverableCmeTick = 0u;
/* "rxLen=0 완전 침묵" 감지 스위치: boot-config sequence 동안만 활성 */
static uint8_t g_appBc95SilenceAbortEnable  = APP_FALSE;
static uint8_t g_appBc95TotalSilenceDetected = APP_FALSE;
/* AT+NCONFIG? 응답은 256B를 초과(실측 len=255+TRUNC)하므로 전용 대형 버퍼 사용 */
static uint8_t g_appBc95NcfgQueryBuf[1024];

/* [NRB 최적화 1단계] 마지막 NCONFIG 동기화에서 실제 수행한 NVM set 횟수
 * (0=이미 원하는 값 -> 부팅 NRB 생략 근거). 쿼리 실패 시 conservative set 2회. */
static uint8_t g_appBc95NconfigSyncSetCount = 0u;
/* [NCONFIG-QUERY-SKIP] BKP4R 기반 직전 세션 AUTOCONNECT 상태 캐시:
 * BKP4R = 매직(0x4E43 <<16 | state), state: 1=FALSE(스티어링 완료), 2=TRUE(provisioning 실행).
 * RTC BKP는 POR 시 클리어되므로 매직 불일치 -> 폴백 쿼리(기존 경로).
 * 기존 BKP4R 사용(Get_BootInfo 마킹 0x00040004)은 부팅 즉시 소비/무사용이라 충돌 없음. */
#define APP_NBIOT_NCFG_BKP_MAGIC_STATE_FALSE (0x4E430001u)
#define APP_NBIOT_NCFG_BKP_MAGIC_STATE_TRUE  (0x4E430002u)
static uint8_t App_NbiotNcfgBkpGetState(uint8_t *p_isFalse);
static uint8_t App_NbiotProvSkipGet(uint8_t *p_due);   /* [PROV-SKIP] */
static void App_NbiotProvSkipSet(uint8_t done);
static void App_NbiotNcfgBkpSetState(uint8_t isFalse);
/* [NRB 최적화 2단계] 이번 세션에 provisioning(QREGSWT=1+AUTOCONNECT=TRUE+NRB)을
 * 실행했는지. TRUE로 남은 NVM을 전원 off 전에 FALSE로 되돌리기 위한 표시. */
static uint8_t s_appNbiotProvisionedThisSession = APP_FALSE;
/* [PROV-SKIP] BKP3R 상태 캐시: provisioning 완료 플래그.
 * BKP3R = 0x50524F4B(PROVK)면 provisioning 파라미터 유효 -> 후속 세션은
 * provisioning(쿼리+set+QREGSWT=1+AUTOCONNECT=TRUE+NRB)을 생략하고
 * TRUE 상태 전원온 자동 attach로 진행. POR 시 소실 -> 폴백 풀 provisioning.
 * 주기 재검증: skipCount >= APP_PROV_SKIP_RECHECK_SESSIONS면 1회 풀 실행. */
#define APP_NBIOT_PROV_BKP_MAGIC             (0x50524F4Bu)
#define APP_PROV_SKIP_RECHECK_SESSIONS       (24u)
static uint16_t s_provSkipCount = 0u;
 
/* UDP seq 단조 증가 카운터 (stale URC 매칭 방지) */
static uint8_t g_appBc95UdpSeqCounter = 0u;

/* service(플랫폼) 전송 통계: mgmt 트레일러(tag 0xD3)에 실어 보고한다.
 * - serviceTxAttempts: QLWULDATAEX 전송 시도 누적(성공/실패 무관)
 * - serviceSuccessCount  : CON 성공 URC(+QLWULDATASTATUS:4) 수신 누적
 * mgmt 전송(부팅 live 전송 포함)에 실려 보고된 뒤 0으로 리셋된다 -
 * 트레일러 값은 "직전 mgmt 보고 이후의 service 전송 결과"를 뜻한다. */  
static volatile uint32_t s_appNbiotServiceTxAttempts = 0u;
static volatile uint32_t s_appNbiotServiceSuccessCount   = 0u;
/* [DL-ACK 카운트 2026-11-04] 서버 애플리케이션 ACK(+NNMI) 수신 누적 -
 * mgmt 트레일러(tag 0xD3) 3바째 확장용: service(시도/성공/ack) 보고.
 * ACK가 오지 않는 명령은 카운트 없이 타임아웃으로 정상 종료(ack 부재 허용). */
static volatile uint32_t s_appNbiotServiceAppAckCount = 0u;

static AppStatus_t App_Bc95AtIcasePrefixMatch(const char *p_text, const char *p_token, uint16_t tokenLen);
static uint8_t App_Bc95AtNconfigTokenHasValue(const char *p_resp, const char *p_key, const char *p_value);
static AppStatus_t App_Bc95AtNconfigSyncIfNeeded(const char *p_key1, const char *p_val1,
                                                 const char *p_cmd1, const char *p_label1,
                                                 const char *p_key2, const char *p_val2,
                                                 const char *p_cmd2, const char *p_label2);
static AppStatus_t App_Bc95AtNconfigSetTrueIfNeeded(void);
static void App_NbiotSteerAutoconnectFalseAtPowerOff(void);
static AppStatus_t App_NbiotRunBootConfigurationSequenceCore(void);

#ifdef DEBUG
typedef struct
{
    volatile uint32_t errorCode;
    volatile uint16_t errorAtIndex;
    volatile uint8_t  errorCount;
} AppBc95AtRxErrorInfo_t;
static AppBc95AtRxErrorInfo_t g_appBc95AtRxErrorInfo;
#endif

static AppStatus_t App_Bc95AtSendSimpleOkCommand(const char *p_cmd,
                                                 uint32_t rxTimeoutMs,
                                                 const char *p_cmdLabel,
                                                 const char *p_logPrefix);
static AppStatus_t App_Bc95AtWaitForServiceReady(uint32_t totalTimeoutMs);

static AppStatus_t App_Bc95AtSendBestEffortQuery(const char *p_cmd,
                                                 uint32_t rxTimeoutMs,
                                                 const char *p_cmdLabel,
                                                 const char *p_logPrefix);
static AppStatus_t App_Bc95AtSendBestEffortSimpleOk(const char *p_cmd,
                                                    uint32_t rxTimeoutMs,
                                                    const char *p_cmdLabel,
                                                    const char *p_logPrefix);
static AppStatus_t App_Bc95AtSendRebootBestEffort(void);
static uint8_t App_Bc95AtIsUsimFatalCme(int32_t cmeErr);
static uint8_t App_Bc95AtIsRecoverableServiceReadyCme(int32_t cmeErr);
static AppStatus_t App_Bc95AtRecoverFromServiceReadyCme(int32_t cmeErr);
static AppStatus_t App_NbiotCarrierRecoverRegisteringStuck(void);
static AppStatus_t App_NbiotRunBootConfigurationSequence(void);
static AppStatus_t App_NbiotRunBoardAttachVerificationSequence(void);
static AppStatus_t App_NbiotRunBoardProvisioningSequence(void);
static AppStatus_t App_NbiotRunPostProvisionFinalizeSequence(void);
static AppStatus_t App_NbiotSyncTimeSoft(void);
static AppStatus_t App_NbiotRunPlatformPreflight(void);
static AppStatus_t App_Bc95AtWaitForLinePrefix(const char *p_prefix,
                                                uint32_t timeoutMs,
                                                char *p_rxSnapshot,
                                                uint16_t snapshotSize);
static AppBc95AtStatus_t App_Bc95AtParseQlwevtind(const char *p_resp, int *p_eventType);
static uint8_t App_Bc95AtPlatformEventNeedsRegisterRetry(int eventType);
static AppStatus_t App_Bc95AtWaitForPlatformReadyWithPolicy(uint32_t timeoutMs, uint8_t *p_needRegisterRetry);
static AppStatus_t App_Bc95AtRunShortCampProbe(AppBc95NetStatus_t *p_status);
static AppStatus_t App_Bc95AtConfirmWeakCellAfterProbe(uint8_t *p_confirmed, AppBc95Quality_t *p_quality, uint8_t *p_noCellAfterProbe);
static AppBc95AtStatus_t App_Bc95AtParseQlwuldataStatus(const char *p_resp, int *p_statusOut, int *p_seqOut);
static AppStatus_t App_Bc95AtPlatformRegister(void);
static AppStatus_t App_Bc95AtPlatformSendAndConfirm(const uint8_t *p_data, uint16_t length, uint8_t seqNum);
static AppStatus_t App_Bc95AtWaitUdpAck(int32_t socketId, uint32_t timeoutMs, uint32_t pollMs);
static AppStatus_t App_NBIoTTransmitPlatformInternal(const char *p_logTag,
                                                     uint8_t deleteStorage,
                                                     uint8_t unsentOnly,
                                                     const AppMeterStorageRecord_t *p_liveRecord);
static void App_Bc95AtBytesToHex(const uint8_t *p_in, uint32_t inLen, char *p_out);
static uint8_t App_Bc95AtNextUdpSeq(void);
static void App_Bc95AtDelayWithFeed(uint32_t ms);
static AppStatus_t App_NbiotAppendMgmtEnvTrailer(uint8_t *p_packet, uint16_t packetCapacity, AppMeterServerFormatResult_t *p_result);
static AppStatus_t App_NbiotAppendMgmtSelfTestTrailer(uint8_t *p_packet, uint16_t packetCapacity, AppMeterServerFormatResult_t *p_result);
static AppStatus_t App_NbiotAppendMgmtServiceStatTrailer(uint8_t *p_packet, uint16_t packetCapacity, AppMeterServerFormatResult_t *p_result);
static AppStatus_t App_NbiotQualityReportMaybeSend(void);

static int s_bc95LastEVTIND = -1;
/* [STALL-GUARD 2026-11-09] AT 채널 완전 소실 감지.
 * 실측(3일 745세션): 16세션(2.1%)에서 QLW* 쿼리 응답 0건 + CGATT/CEREG/EVTIND 모두
 * 수십초 무응답(모뎀 AT 스톨, RSSI -75~-78 정상, 다음 세션 회복). 기존은 30초
 * 대기 창을 2 attempt 모두 소진해 세션당 ~2분 지연. 대기 중 연속 무응답 청크를
 * 세고, 프로브 AT 1회 송신 후에도 침묵하면 만료를 기다리지 않고 즉시 NRB escalate. */
/* 청크=5s(APP_BC95_AT_WAIT_CHUNK_MS 기준). 2청크(10s) 무응답 시 프로브,
 * 3청크(15s) 침묵 지속 시 스톨 확정. */
#define APP_BC95_AT_STALL_PROBE_CHUNKS       (2u)
#define APP_BC95_AT_STALL_SILENT_CHUNKS      (3u)
static volatile uint32_t s_bc95AtStallEpisodes = 0u;   /* 스톨 에피소드 누적(로그용) */
/* [STALL-CLUSTER 2026-11-09] 연속 스톨 에피소드(클러스터) 추적.
 * 실측(11-09 두 로그): 스톨 클러스터 내 attempt 2(NRB)도 회복 불가 - NRB 직후
 * 에피소드 4건(2,3,5,6) 재발. 클러스터 판정(연속 2회+) 시 attempt 2의 NRB를
 * 건너뛰고 즉시 세션 포기(다음 세션 회복에 맡김)로 NRB 1회/배터리 절감.
 * 정상 라인 수신 시 리셋(스톨 종료 판정). */
static volatile uint32_t s_bc95AtStallStreak = 0u;     /* 연속 스톨 에피소드(클러스터 내) */
#define APP_BC95_AT_STALL_CLUSTER_NRB_SKIP_MIN (2u)    /* streak>=2면 NRB 스킵 */
static void App_Bc95AtUpdateEVTINDLatch(const char *p_line);
static uint8_t App_Bc95AtEVTINDMaskReady(uint16_t seenMask);
static void App_Bc95AtScanBufferForEVTIND(const char *p_buf, uint16_t len);
static void App_Bc95AtScanBufferForEVTINDCont(void);
static uint8_t App_Bc95AtEVTINDMaskReadyFallbackOnly(uint16_t seenMask);

/* --- NNMI 래치 상태 (main/IRQ 혼재 쓰기 → PRIMASK 보호) --- */
static volatile uint32_t s_bc95NnmiCount   = 0u;   /* 신규 NNMI 라인 누적(중복 제외) */
static volatile uint16_t s_bc95LastNnmiLen = 0u;   /* 래치된 프레임 길이(0=없음) */
static uint8_t  s_bc95LastNnmiBuf[APP_BC95_NNMI_LATCH_MAX];
static char     s_bc95NnmiLastLine[APP_BC95_NNMI_LINE_MAX];  /* 중복 라인 제거용 */
static uint16_t s_bc95NnmiDmaScanPos = 0u;         /* DMA 버퍼 순차 스캔 커서 */
static void App_Bc95AtUpdateNnmiLatch(const char *p_line);
static void App_Bc95AtScanBufferForNnmi(const char *p_buf, uint16_t len);
static void App_Bc95AtNnmiScanDmaBuf(void);
static AppStatus_t App_Bc95AtWaitServiceAckNnmi(uint8_t seqNum, uint32_t timeoutMs, uint32_t pollMs);
static void App_DlCmdExecute(const uint8_t *p_frame, uint16_t len, uint8_t isServicePath);

/* [v19 NNMI 래치 리셋 2026-11-04] 중복라인 캐시/스캔 커서/래치 길이를 세션 단위로 리셋.
 * 근본 원인: s_bc95NnmiLastLine(중복 판정 캐시)이 wake 사이에 유지되어, 운영 중
 * 이전 wake와 동일한 DL 응답 프레임(같은 명령의 동일 ACK/에코)이 도착하면
 * 재스캔 중복으로 판정되어 래치 카운터가 증가하지 않는다. 부팅 직후는 캐시가
 * 비어 있어 항상 정상(실측: 부팅 직후 DL은 회수), 운영 wake에서는 동일 프레임이
 * 전부 유실됐다. DMA 스캔 커서도 이전 wake 값으로 남아 신규 유입분 판정이
 * 어긋날 수 있어 함께 리셋한다. s_bc95NnmiCount는 단조 유지(대기 루프가
 * 변화분만 소비)하므로 리셋하지 않는다. */
static void App_Bc95AtNnmiLatchReset(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_bc95NnmiLastLine[0] = 0;
    s_bc95LastNnmiLen = 0u;
    s_bc95NnmiDmaScanPos = 0u;
    if (primask == 0u) { __enable_irq(); }
}

/* ============================================================
 *  Pre-attach radio power check (AT+NUESTATS) + deferred abort
 * ============================================================ */
#define APP_BC95_AT_CMD_NUESTATS_RADIO        "AT+NUESTATS\r\n"
#define APP_BC95_AT_NUESTATS_SIGNAL_TAG       "Signal power:"
#define APP_BC95_AT_NUESTATS_TOTAL_TAG        "Total power:"
#define APP_BC95_AT_NUESTATS_INVALID_RAW      (-32768L)
/* raw 는 0.1 dBm 단위: APP_BC95_AT_DBM_ABS_MAX(150 dBm) -> 1500 */
#define APP_BC95_AT_NUESTATS_RAW_ABS_MAX      ((int32_t)APP_BC95_AT_DBM_ABS_MAX * 10)

#ifndef APP_BC95_PRE_ATTACH_SIGNAL_WEAK_DBM
#define APP_BC95_PRE_ATTACH_SIGNAL_WEAK_DBM       (-115)   /* Signal power(RSRP 계열) 기준, 현장 튜닝 */
#endif
#ifndef APP_BC95_PRE_ATTACH_POWER_POLL_MS
#define APP_BC95_PRE_ATTACH_POWER_POLL_MS         (1500u)  /* 1~2초 간격 */
#endif
#ifndef APP_BC95_PRE_ATTACH_POWER_TIMEOUT_MS
#define APP_BC95_PRE_ATTACH_POWER_TIMEOUT_MS      (9000u)  /* 이 시간 동안 -32768 이면 음영 의심 */
#endif
#ifndef APP_BC95_PRE_ATTACH_POWER_COMM_FAIL_MAX
#define APP_BC95_PRE_ATTACH_POWER_COMM_FAIL_MAX   (3u)
#endif
#ifndef APP_BC95_PRE_ATTACH_CGATT_KICK_TIMEOUT_MS
#define APP_BC95_PRE_ATTACH_CGATT_KICK_TIMEOUT_MS (5000u)
#endif

/* ============================================================
 *  SIM info log: PLMN / MDN(est) / ICCID / modem FW (log only)
 * ============================================================ */
#define APP_BC95_SIM_INFO_IMSI_DIGITS       (15u)
#define APP_BC95_SIM_INFO_PLMN_LEN          (5u)
#define APP_BC95_SIM_INFO_PLMN_LGUP         "45006"
#define APP_BC95_SIM_INFO_MDN_BUF_SIZE      (APP_NBIOT_SIM_MDN_BUF_SIZE)
#define APP_BC95_SIM_INFO_ICCID_MAX_LEN     (APP_NBIOT_SIM_ICCID_BUF_SIZE - 1u)
#define APP_BC95_SIM_INFO_ICCID_MIN_LEN     (18u)
/* Printed serial = ICCID digits [10..17] (0-based) of a 19-digit ICCID; the last digit is the check digit */
#define APP_BC95_SIM_INFO_ICCID_LEN_SERIAL  (19u)
#define APP_BC95_SIM_INFO_SERIAL_START      (10u)
#define APP_BC95_SIM_INFO_SERIAL_LEN        (8u)
#define APP_BC95_SIM_INFO_FW_MAX_LEN        (32u)
#define APP_BC95_SIM_INFO_NCCID_PREFIX      "+NCCID:"
#define APP_BC95_SIM_INFO_NCCID_PREFIX_LEN  (7u)

_Static_assert((APP_BC95_IMSI_BCD_BYTES * 2u) >= APP_BC95_SIM_INFO_IMSI_DIGITS,
               "IMSI BCD buffer too small");

/* RAM-only cache. Never written to EEPROM. Cleared on every refresh. */
typedef struct
{
    uint8_t iccidValid;
    uint8_t mdnValid;
    uint8_t serialValid;
    char    iccid[APP_NBIOT_SIM_ICCID_BUF_SIZE];
    char    mdn[APP_NBIOT_SIM_MDN_BUF_SIZE];
    char    serial[APP_NBIOT_SIM_SERIAL_BUF_SIZE];
} AppBc95SimInfoCache_t;

static AppBc95SimInfoCache_t g_appBc95SimInfo;
/* IMSI BCD(8B, 15 digits + pad nibble) -> ASCII 15 digits */
static uint8_t App_Bc95SimImsiBcdToDigits(const uint8_t *p_bcd, char *p_out, size_t outSize)
{
    uint8_t i;
    uint8_t nibble;

    if ((p_bcd == NULL) || (p_out == NULL) ||
        (outSize < (APP_BC95_SIM_INFO_IMSI_DIGITS + 1u)))
    {
        return APP_FALSE;
    }
    for (i = 0u; i < APP_BC95_SIM_INFO_IMSI_DIGITS; i++)
    {
        nibble = ((i & 1u) == 0u) ? (uint8_t)(p_bcd[i >> 1] >> 4)
                                  : (uint8_t)(p_bcd[i >> 1] & 0x0Fu);
        if (nibble > 9u) { return APP_FALSE; }
        p_out[i] = (char)('0' + nibble);
    }
    p_out[APP_BC95_SIM_INFO_IMSI_DIGITS] = '\0';
    /* MCC never starts with '0': rejects an unfilled (all-zero) buffer */
    return (p_out[0] != '0') ? APP_TRUE : APP_FALSE;
}

/* LG U+ (45006) only: MDN(est) = '0' + IMSI[5..14]. Estimated value, not verified. */
static uint8_t App_Bc95SimEstimateMdn(const char *p_imsi, char *p_out, size_t outSize)
{
    if ((p_imsi == NULL) || (p_out == NULL) ||
        (outSize < APP_BC95_SIM_INFO_MDN_BUF_SIZE))
    {
        return APP_FALSE;
    }
    if (strncmp(p_imsi, APP_BC95_SIM_INFO_PLMN_LGUP, APP_BC95_SIM_INFO_PLMN_LEN) != 0)
    {
        return APP_FALSE;
    }
    p_out[0] = '0';
    (void)memcpy(&p_out[1], &p_imsi[APP_BC95_SIM_INFO_PLMN_LEN],
                 APP_BC95_SIM_INFO_IMSI_DIGITS - APP_BC95_SIM_INFO_PLMN_LEN);
    p_out[1u + (APP_BC95_SIM_INFO_IMSI_DIGITS - APP_BC95_SIM_INFO_PLMN_LEN)] = '\0';
    return APP_TRUE;
}

/* AT+NCCID? -> "+NCCID: <iccid>" (best-effort, logs and caches in RAM) */
static void App_Bc95SimLogIccid(void)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    const char *p_pos;
    char iccid[APP_BC95_SIM_INFO_ICCID_MAX_LEN + 1u];
    uint8_t n = 0u;
    char ch;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NCCID_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "ICCID query fail (status=%d)", (int)status);
        return;
    }

    p_pos = strstr((const char *)g_appBc95AtRxBuf, APP_BC95_SIM_INFO_NCCID_PREFIX);
    if (p_pos == NULL)
    {
        APP_LOGD("NBIOT", "ICCID n/a (no +NCCID in response)");
        return;
    }
    p_pos += APP_BC95_SIM_INFO_NCCID_PREFIX_LEN;
    while ((*p_pos == ' ') || (*p_pos == '\t')) { p_pos++; }

    while (n < APP_BC95_SIM_INFO_ICCID_MAX_LEN)
    {
        ch = *p_pos++;
        if (((ch >= '0') && (ch <= '9')) ||
            ((ch >= 'A') && (ch <= 'F')) ||
            ((ch >= 'a') && (ch <= 'f')))
        {
            iccid[n++] = ch;
        }
        else
        {
            break;
        }
    }
    iccid[n] = '\0';

    if (n < APP_BC95_SIM_INFO_ICCID_MIN_LEN)
    {
        APP_LOGD("NBIOT", "ICCID parse fail (len=%u)", (unsigned)n);
        return;
    }
    APP_LOGD("NBIOT", "ICCID    : %s", iccid);

    (void)memcpy(g_appBc95SimInfo.iccid, iccid, (size_t)n + 1u);
    g_appBc95SimInfo.iccidValid = APP_TRUE;

    if (n == APP_BC95_SIM_INFO_ICCID_LEN_SERIAL)
    {
        uint8_t i;
        uint8_t allDigits = APP_TRUE;

        for (i = 0u; i < n; i++)
        {
            if ((iccid[i] < '0') || (iccid[i] > '9')) { allDigits = APP_FALSE; }
        }
        if (allDigits == APP_TRUE)
        {
            (void)memcpy(g_appBc95SimInfo.serial,
                         &iccid[APP_BC95_SIM_INFO_SERIAL_START],
                         APP_BC95_SIM_INFO_SERIAL_LEN);
            g_appBc95SimInfo.serial[APP_BC95_SIM_INFO_SERIAL_LEN] = '\0';
            g_appBc95SimInfo.serialValid = APP_TRUE;
            APP_LOGD("NBIOT", "Serial   : %s", g_appBc95SimInfo.serial);
        }
    }
}

/* AT+QGMR (also sent in BootTrack) -> first payload line (best-effort, log only) */
static void App_Bc95SimLogModemFw(void)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    const char *p_line;
    char fw[APP_BC95_SIM_INFO_FW_MAX_LEN];
    size_t len;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_QGMR_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "FW query fail (status=%d)", (int)status);
        return;
    }

    if (App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL) != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "FW n/a (not OK)");
        return;
    }

    p_line = (const char *)g_appBc95AtRxBuf;
    while (*p_line != '\0')
    {
        while ((*p_line == '\r') || (*p_line == '\n') || (*p_line == ' '))
        {
            p_line++;
        }
        if (*p_line == '\0') { break; }

        len = 0u;
        while ((p_line[len] != '\0') && (p_line[len] != '\r') && (p_line[len] != '\n'))
        {
            len++;
        }

        /* [FIX-EVTIND-5] 응답에 혼입된 EVTIND URC는 FW 값 오염의 원인
         * (실측: "Modem FW : +QLWEVTIND:0"). URC는 래치 스캔 경로가
         * 이미 처리하므로 여기서는 건너뛰고 다음 라인에서 FW를 찾는다. */
        if (strncmp(p_line, APP_BC95_AT_QLWEVTIND_PREFIX,
                    APP_BC95_AT_QLWEVTIND_PREFIX_LEN) == 0)
        {
            p_line += len;
            continue;
        }

        /* skip command echo (AT...) and the final "OK" */
        if ((strncmp(p_line, "AT", 2) != 0) &&
            !((len == 2u) && (strncmp(p_line, "OK", 2) == 0)))
        {
            if (len >= sizeof(fw)) { len = sizeof(fw) - 1u; }
            (void)memcpy(fw, p_line, len);
            fw[len] = '\0';
            APP_LOGD("NBIOT", "Modem FW : %s", fw);
            return;
        }
        p_line += len;
    }
    APP_LOGD("NBIOT", "FW n/a (no payload line)");
}

static void App_Bc95LogSimInfo(const uint8_t *p_imsiBcd)
{
    char imsi[APP_BC95_SIM_INFO_IMSI_DIGITS + 1u];
    char plmn[APP_BC95_SIM_INFO_PLMN_LEN + 1u];
    char mdnEst[APP_BC95_SIM_INFO_MDN_BUF_SIZE];
    /* drop values from a previous SIM / failed read */
    (void)memset(&g_appBc95SimInfo, 0, sizeof(g_appBc95SimInfo));

    if (App_Bc95SimImsiBcdToDigits(p_imsiBcd, imsi, sizeof(imsi)) == APP_TRUE)
    {
        (void)memcpy(plmn, imsi, APP_BC95_SIM_INFO_PLMN_LEN);
        plmn[APP_BC95_SIM_INFO_PLMN_LEN] = '\0';
        APP_LOGD("NBIOT", "PLMN     : %s", plmn);

        if (App_Bc95SimEstimateMdn(imsi, mdnEst, sizeof(mdnEst)) == APP_TRUE)
        {
            APP_LOGD("NBIOT", "MDN(est) : %s", mdnEst);
            (void)memcpy(g_appBc95SimInfo.mdn, mdnEst, sizeof(g_appBc95SimInfo.mdn));
            g_appBc95SimInfo.mdnValid = APP_TRUE;
        }
        else
        {
            APP_LOGD("NBIOT", "MDN(est) : n/a (PLMN != %s)", APP_BC95_SIM_INFO_PLMN_LGUP);
        }
    }
    else
    {
        APP_LOGD("NBIOT", "IMSI BCD decode fail");
    }

    App_Bc95SimLogIccid();
    App_Bc95SimLogModemFw();
}

uint8_t App_NBIoTGetSimIccid(char *p_out, size_t outSize)
{
    if ((p_out == NULL) || (outSize < APP_NBIOT_SIM_ICCID_BUF_SIZE)) { return APP_FALSE; }
    p_out[0] = '\0';
    if (g_appBc95SimInfo.iccidValid != APP_TRUE) { return APP_FALSE; }
    (void)memcpy(p_out, g_appBc95SimInfo.iccid, APP_NBIOT_SIM_ICCID_BUF_SIZE);
    return APP_TRUE;
}

uint8_t App_NBIoTGetSimMdnEst(char *p_out, size_t outSize)
{
    if ((p_out == NULL) || (outSize < APP_NBIOT_SIM_MDN_BUF_SIZE)) { return APP_FALSE; }
    p_out[0] = '\0';
    if (g_appBc95SimInfo.mdnValid != APP_TRUE) { return APP_FALSE; }
    (void)memcpy(p_out, g_appBc95SimInfo.mdn, APP_NBIOT_SIM_MDN_BUF_SIZE);
    return APP_TRUE;
}

uint8_t App_NBIoTGetSimSerial(char *p_out, size_t outSize)
{
    if ((p_out == NULL) || (outSize < APP_NBIOT_SIM_SERIAL_BUF_SIZE)) { return APP_FALSE; }
    p_out[0] = '\0';
    if (g_appBc95SimInfo.serialValid != APP_TRUE) { return APP_FALSE; }
    (void)memcpy(p_out, g_appBc95SimInfo.serial, APP_NBIOT_SIM_SERIAL_BUF_SIZE);
    return APP_TRUE;
}

static int16_t App_Bc95AtCentibelToDbm(int32_t centibel);

typedef struct
{
    uint8_t          pending;
    uint8_t          qualityValid;
    AppBc95Quality_t quality;
    int16_t          rssiDbm;      /* 0 = 측정 없음 (App_CommOnPreAttachAbort 규약) */
    uint8_t          noCell;
    uint8_t          attemptNo;
} AppNbiotPreAttachAbortPending_t;

/* "Tag:-800" 형태에서 정수 추출 */
static uint8_t App_Bc95AtParseTaggedInt(const char *p_resp, const char *p_tag, int32_t *p_out)
{
    const char *p_pos;
    char *p_end = NULL;
    long value;

    if ((p_resp == NULL) || (p_tag == NULL) || (p_out == NULL)) { return APP_FALSE; }

    p_pos = strstr(p_resp, p_tag);
    if (p_pos == NULL) { return APP_FALSE; }

    p_pos += strlen(p_tag);
    while ((*p_pos == ' ') || (*p_pos == '\t')) { p_pos++; }

    value = strtol(p_pos, &p_end, 10);
    if (p_end == p_pos) { return APP_FALSE; }

    *p_out = (int32_t)value;
    return APP_TRUE;
}

/*
 * AUTOCONNECT=TRUE idempotent set (v9):
 *  - 프로비저닝 시퀀스의 원래 위치(QREGSWT=1 이후)를 유지하면서
 *    NVM에 이미 AUTOCONNECT=TRUE가 있으면 NVM 쓰기를 생략한다.
 *  - boot-config가 FALSE로 초기화한 뒤 프로비저닝이 TRUE로 전환하는
 *    벤더 플로우의 특성상, 정상 콜드부팅에서는 쓰기가 1회 발생할 수 있다.
 *    (FALSE->TRUE 전환은 의도된 동작)
 *  - 이득: 프로비저닝 재진입/재시도 경로에서 이미 TRUE면 쓰기 0회,
 *    웨지 유형 지점(쓰기 직후)의 노출 횟수 자체를 줄인다.
 *  - query는 전용 1024B 버퍼 + 1500ms 타임아웃 (NCONFIG? 대형 응답 대응)
 *  - 판정 실패 시 기존대로 set (conservative fallback)
 */
static AppStatus_t App_Bc95AtNconfigSetTrueIfNeeded(void)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    uint16_t safeLen;

    APP_LOGN("NBIOT", "[[BootTrack]] TX AT+NCONFIG? (autoconnect idem)");
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NCONFIG_QUERY,
                                   g_appBc95NcfgQueryBuf,   /* 수신 = 파싱 같은 버퍼 */
                                   (uint16_t)sizeof(g_appBc95NcfgQueryBuf),
                                   APP_NBIOT_NCONFIG_QUERY_RX_TIMEOUT_MS,
                                   &rxLen);
    APP_LOGN("NBIOT", "[[BootTrack]] NCONFIG? rx status=%d rxLen=%u",
             (int)status, (unsigned)rxLen);

    if ((status == APP_STATUS_OK) ||
        ((status == APP_STATUS_UART_TIMEOUT) && (rxLen > 0u)))
    {
        safeLen = (rxLen < (uint16_t)(sizeof(g_appBc95NcfgQueryBuf) - 1u))
                      ? rxLen : (uint16_t)(sizeof(g_appBc95NcfgQueryBuf) - 1u);
        g_appBc95NcfgQueryBuf[safeLen] = (uint8_t)'\0';

        if (App_Bc95AtNconfigTokenHasValue((const char *)g_appBc95NcfgQueryBuf,
                                           "AUTOCONNECT", "TRUE") == APP_TRUE)
        {
            APP_LOGN("NBIOT", "[[BootTrack]] AT+NCONFIG=AUTOCONNECT,TRUE already TRUE -> skip NVM write");
            App_Bc95AtDelayWithFeed(APP_NBIOT_NCONFIG_QUERY_SETTLE_MS);
            return APP_STATUS_OK;
        }
    }
    else
    {
        APP_LOGW("NBIOT", "[[BootTrack]] AT+NCONFIG? query failed (status=%d rxLen=%u) -> proceed to set",
                 (int)status, (unsigned)rxLen);
    }

    App_Bc95AtDelayWithFeed(APP_NBIOT_NCONFIG_QUERY_SETTLE_MS);
    App_Bc95AtDelayWithFeed(APP_NBIOT_NVM_WRITE_GAP_MS);
    return App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_NCONFIG_AUTOCONNECT_ON,
                                         APP_BC95_AT_RX_TIMEOUT_MS,
                                         "AT+NCONFIG=AUTOCONNECT,TRUE",
                                         "[[BootTrack]]");
}

static uint8_t App_Bc95AtNuestatsRawIsValid(int32_t raw)
{
    if (raw == APP_BC95_AT_NUESTATS_INVALID_RAW) { return APP_FALSE; }
    if ((raw > APP_BC95_AT_NUESTATS_RAW_ABS_MAX) ||
        (raw < -APP_BC95_AT_NUESTATS_RAW_ABS_MAX)) { return APP_FALSE; }
    return APP_TRUE;
}

/* ============================================================
 *  RAW RX 디버그 덤프 (임시 진단용 - 끝나면 APP_FALSE로)
 * ============================================================ */
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
static void App_Bc95AtLogRawRx(const char *p_tag,
                               const uint8_t *p_buf,
                               uint16_t len)
{
    /* EVTIND 조기 도착 래치 캡처 (경로 1):
     * 모든 수신은 이 RAW 로그를 drain보다 먼저 통과한다. 부팅 트랙(자가진단/계량기
     * 프로브) 동안 도착하는 +QLWEVTIND:0/3/8 URC를 여기서 래치해 유실을 막는다.
     * 주의: 이 함수는 APP_NBIOT_AT_RAW_RX_DEBUG 플래그로 감싸져 있으므로,
     * 플래그 FALSE 빌드에서는 (경로 2/3) drain 전후 스캔이 백업. */
    {
        char evtScan[160];
        uint16_t scanLen = (len < (uint16_t)(sizeof(evtScan) - 1u)) ? len : (uint16_t)(sizeof(evtScan) - 1u);
        uint16_t k;
        for (k = 0u; k < scanLen; k++)
        {
            evtScan[k] = (char)p_buf[k];
        }
        evtScan[scanLen] = '\0';
        App_Bc95AtScanBufferForEVTIND(evtScan, scanLen);
    }

    char     text[400];
    uint16_t i;
    uint16_t out = 0u;
    uint16_t dumpLen = len;
    static const char kHex[] = "0123456789ABCDEF";

    if (dumpLen > 96u)
    {
        dumpLen = 96u;   /* 스택 보호: 앞 96바이트만 덤프 */
    }

    for (i = 0u; (i < dumpLen) && (out < (uint16_t)(sizeof(text) - 8u)); i++)
    {
        uint8_t b = p_buf[i];
        if (b == (uint8_t)'\r')
        {
            text[out++] = '\\'; text[out++] = 'r';
        }
        else if (b == (uint8_t)'\n')
        {
            text[out++] = '\\'; text[out++] = 'n';
        }
        else if (b == (uint8_t)'\\')
        {
            text[out++] = '\\'; text[out++] = '\\';
        }
        else if ((b >= 0x20u) && (b <= 0x7Eu))
        {
            text[out++] = (char)b;
        }
        else
        {
            text[out++] = '\\'; text[out++] = 'x';
            text[out++] = kHex[(b >> 4) & 0x0Fu];
            text[out++] = kHex[b & 0x0Fu];
        }
    }
    text[out] = '\0';

    APP_LOGD("NBIOT", "RAW %s len=%u%s [%s]",
             (p_tag != NULL) ? p_tag : "rx",
             (unsigned)len,
             (len > dumpLen) ? "+TRUNC" : "",
             text);
}
/* ============================================================
 *  CME ERROR 진단 보고 (CME 22/512/4 원인 특정용, 임시 진단)
 * ============================================================ */
static void App_Bc95AtLogCmeIfPresent(const char *p_tag,
                                      uint8_t *p_rxBuf,
                                      uint16_t rxLen)
{
    const char *p_cme;
    const char *p_end;
    long cmeVal;

    if ((p_rxBuf == NULL) || (rxLen == 0u))
    {
        return;
    }
    p_rxBuf[rxLen] = (uint8_t)'\0'; /* SendCommand가 이미 종단 보장하므로 방어적 재기입 */
    p_cme = strstr((const char *)p_rxBuf, "+CME ERROR:");
    if (p_cme == NULL)
    {
        return;
    }
    cmeVal = strtol(p_cme + 11, (char **)&p_end, 10);
    APP_LOGD("NBIOT", "CME-DIAG %s cme=%ld rxLen=%u",
             (p_tag != NULL) ? p_tag : "cmd",
             cmeVal,
             (unsigned)rxLen);
}
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)

/* CFUN=1 이 아니면 NUESTATS 값이 올라오지 않으므로 사전 확인 */
static AppStatus_t App_Bc95AtEnsureCfunFull(void)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    const char *p_cfun;
    int cfun = -1;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CFUN_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK) { return status; }

    p_cfun = strstr((const char *)g_appBc95AtRxBuf, APP_BC95_AT_CFUN_PREFIX);
    if ((p_cfun != NULL) &&
        (sscanf(p_cfun + APP_BC95_AT_CFUN_PREFIX_LEN, "%d", &cfun) == 1) &&
        (cfun == 1))
    {
        return APP_STATUS_OK;
    }

    APP_LOGI("NBIOT", APP_NBIOT_LOG_PREATTACH " CFUN=%d -> set CFUN=1", cfun);
    return App_Bc95AtSetFullFunction();
}

/*
 * AUTOCONNECT=FALSE 이므로 CFUN=1 만으로는 셀 탐색이 시작되지 않는 것으로 추정된다.
 * 신호 측정 전에 CGATT=1 로 탐색/Attach 를 명시적으로 시작한다.
 */
static void App_Bc95AtKickAttachIfNeeded(void)
{
    uint16_t rxLen = 0u;
    const char *p_cgatt;
    int attached = -1;

    if (App_Bc95AtSendCommand(APP_BC95_AT_CMD_CGATT_QUERY,
                              g_appBc95AtRxBuf,
                              (uint16_t)sizeof(g_appBc95AtRxBuf),
                              APP_BC95_AT_RX_TIMEOUT_MS,
                              &rxLen) == APP_STATUS_OK)
    {
        p_cgatt = strstr((const char *)g_appBc95AtRxBuf, APP_BC95_AT_CGATT_PREFIX);
        if (p_cgatt != NULL)
        {
            (void)sscanf(p_cgatt + APP_BC95_AT_CGATT_PREFIX_LEN, "%d", &attached);
        }
    }

    if (attached == 1) { return; }

    APP_LOGI("NBIOT", APP_NBIOT_LOG_PREATTACH " CGATT=%d -> kick attach (CGATT=1)", attached);
    (void)App_Bc95AtSendBestEffortSimpleOk(APP_BC95_AT_CMD_CGATT_ATTACH,
                                           APP_BC95_PRE_ATTACH_CGATT_KICK_TIMEOUT_MS,
                                           "AT+CGATT=1",
                                           "[[Attach]]");
}

/*
 * AT+NUESTATS 1회 조회.
 *  - 통신 성공(또는 부분 수신) 시 APP_STATUS_OK, *p_valid 로 유효 여부 전달
 *  - Signal power / Total power 둘 다 -32768 이 아니어야 유효
 *  - 응답이 RX 버퍼(256B)보다 길어 UART_TIMEOUT 이 나도, 앞쪽 두 줄이 들어왔으면 파싱한다.
 */
static AppStatus_t App_Bc95AtReadNuestatsPowerOnce(int16_t *p_signalDbm,
                                                   int16_t *p_totalDbm,
                                                   uint8_t *p_valid)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int32_t cmeErr = 0;
    int32_t sigRaw = APP_BC95_AT_NUESTATS_INVALID_RAW;
    int32_t totRaw = APP_BC95_AT_NUESTATS_INVALID_RAW;
    uint8_t sigFound;
    uint8_t totFound;

    APP_RETURN_IF_FALSE((p_signalDbm != NULL) && (p_totalDbm != NULL) && (p_valid != NULL),
                        APP_STATUS_INVALID_PARAM);
    *p_signalDbm = 0;
    *p_totalDbm  = 0;
    *p_valid     = APP_FALSE;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NUESTATS_RADIO,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    App_Bc95AtLogCmeIfPresent("NUESTATS(pre)", g_appBc95AtRxBuf, rxLen);   /* CME 22 특정 */
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)

    if ((status != APP_STATUS_OK) && (rxLen == 0u))
    {
        APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH " NUESTATS tx/rx fail st=%d", (int)status);
        return status;
    }

    if (status == APP_STATUS_OK)
    {
        atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
        if (atStatus != APP_BC95_AT_OK)
        {
            APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH " NUESTATS !OK parse=%s cme=%ld",
                     App_Bc95AtGetStatusString(atStatus), (long)cmeErr);
            return (atStatus == APP_BC95_AT_ERR_CME_ERROR) ? APP_STATUS_UART_TIMEOUT
                                                           : APP_STATUS_FATAL;
        }
    }

    sigFound = App_Bc95AtParseTaggedInt((const char *)g_appBc95AtRxBuf,
                                        APP_BC95_AT_NUESTATS_SIGNAL_TAG, &sigRaw);
    totFound = App_Bc95AtParseTaggedInt((const char *)g_appBc95AtRxBuf,
                                        APP_BC95_AT_NUESTATS_TOTAL_TAG, &totRaw);
    if ((sigFound != APP_TRUE) || (totFound != APP_TRUE))
    {
        APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH " NUESTATS parse fail (sig=%u tot=%u st=%d)",
                 (unsigned)sigFound, (unsigned)totFound, (int)status);
        return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
    }

    APP_LOGD("NBIOT", APP_NBIOT_LOG_PREATTACH " NUESTATS raw sig=%ld tot=%ld",
             (long)sigRaw, (long)totRaw);

    if ((App_Bc95AtNuestatsRawIsValid(sigRaw) == APP_TRUE) &&
        (App_Bc95AtNuestatsRawIsValid(totRaw) == APP_TRUE))
    {
        *p_signalDbm = App_Bc95AtCentibelToDbm(sigRaw);
        *p_totalDbm  = App_Bc95AtCentibelToDbm(totRaw);
        *p_valid     = APP_TRUE;
    }
    return APP_STATUS_OK;
}

/*
 * 망 접속 이전 안테나/음영 판별용:
 *  - CFUN=1 확인 + CGATT=1 로 탐색 시작 후 AT+NUESTATS 를 POLL_MS 간격으로 반복 조회
 *  - 유효값이 올라오면 APP_STATUS_OK (*p_signalDbm=Signal power, *p_totalDbm=Total power)
 *  - TIMEOUT_MS 동안 계속 -32768 이면 APP_STATUS_UART_TIMEOUT (음영 의심)
 *  - 통신 오류가 연속 발생하면 해당 status 반환
 */
static AppStatus_t App_Bc95AtReadPreAttachSignalDbm(int16_t *p_signalDbm, int16_t *p_totalDbm)
{
    AppStatus_t status;
    uint32_t startTick;
    uint32_t attempt = 0u;
    uint32_t commFail = 0u;
    int16_t signalDbm = 0;
    int16_t totalDbm = 0;
    uint8_t valid = APP_FALSE;

    APP_RETURN_IF_FALSE((p_signalDbm != NULL) && (p_totalDbm != NULL), APP_STATUS_INVALID_PARAM);
    *p_signalDbm = 0;
    *p_totalDbm  = 0;

    status = App_Bc95AtEnsureCfunFull();
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH " CFUN=1 ensure fail st=%d", (int)status);
        return status;
    }

    App_Bc95AtKickAttachIfNeeded();

    startTick = HAL_GetTick();
    while (1)
    {
        attempt++;
        status = App_Bc95AtReadNuestatsPowerOnce(&signalDbm, &totalDbm, &valid);

        if (status == APP_STATUS_OK)
        {
            commFail = 0u;
            if (valid == APP_TRUE)
            {
                *p_signalDbm = signalDbm;
                *p_totalDbm  = totalDbm;
                APP_LOGI("NBIOT", APP_NBIOT_LOG_PREATTACH
                         " radio power ok sig=%d dBm tot=%d dBm a=%lu t=%lums",
                         (int)signalDbm, (int)totalDbm,
                         (unsigned long)attempt,
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }
        }
        else
        {
            commFail++;
            if (commFail >= APP_BC95_PRE_ATTACH_POWER_COMM_FAIL_MAX) { return status; }
        }

        if ((HAL_GetTick() - startTick) >= APP_BC95_PRE_ATTACH_POWER_TIMEOUT_MS)
        {
            APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH
                     " radio power invalid(-32768) for %lums a=%lu -> shadow suspected",
                     (unsigned long)(HAL_GetTick() - startTick),
                     (unsigned long)attempt);
            return APP_STATUS_UART_TIMEOUT;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_PRE_ATTACH_POWER_POLL_MS);
    }
}

/* abort 정보 보관: 같은 사이클에서 여러 번 발생하면 마지막 것으로 덮어쓴다 */
static void App_NbiotPreAttachAbortDefer(AppNbiotPreAttachAbortPending_t *p_pending,
                                         const AppBc95Quality_t *p_quality,
                                         int16_t rssiDbm,
                                         uint8_t noCell,
                                         uint8_t attemptNo)
{
    if (p_pending == NULL) { return; }

    (void)memset(p_pending, 0, sizeof(*p_pending));
    if ((p_quality != NULL) && (p_quality->valid == APP_TRUE))
    {
        p_pending->quality = *p_quality;
        p_pending->qualityValid = APP_TRUE;
    }
    p_pending->rssiDbm   = rssiDbm;
    p_pending->noCell    = noCell;
    p_pending->attemptNo = attemptNo;
    p_pending->pending   = APP_TRUE;
}

/* 최종 실패 시에만 학습 모듈(cost/badStreak/extremeWeak/nightOnly)에 1회 반영 */
static void App_NbiotPreAttachAbortFlush(AppNbiotPreAttachAbortPending_t *p_pending)
{
    if ((p_pending == NULL) || (p_pending->pending != APP_TRUE)) { return; }

    APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH
             " apply deferred abort (attempt=%u rssi=%d noCell=%u)",
             (unsigned)p_pending->attemptNo,
             (int)p_pending->rssiDbm,
             (unsigned)p_pending->noCell);

    App_CommOnPreAttachAbort(
        (p_pending->qualityValid == APP_TRUE) ? &p_pending->quality : NULL,
        p_pending->rssiDbm,
        p_pending->noCell);

    p_pending->pending = APP_FALSE;
}

/* 이후 시도에서 attach 성공 시 보관해 둔 abort 폐기 */
static void App_NbiotPreAttachAbortDiscard(AppNbiotPreAttachAbortPending_t *p_pending,
                                           uint8_t successAttemptNo)
{
    if ((p_pending == NULL) || (p_pending->pending != APP_TRUE)) { return; }

    APP_LOGI("NBIOT", APP_NBIOT_LOG_PREATTACH
             " discard deferred abort (abort attempt=%u, recovered at attempt=%u)",
             (unsigned)p_pending->attemptNo,
             (unsigned)successAttemptNo);
    p_pending->pending = APP_FALSE;
}

#define APP_BC95_SERVICE_READY_CPIN_STABLE_DELAY_MS   (200u)
#define APP_BC95_SERVICE_READY_POST_RECOVER_POLL_MS   (1200u)

static uint8_t g_appBc95ServiceReadyRecoveryActive = APP_FALSE;
static uint8_t g_appBc95ShortRetryCpinGuardOnce = APP_FALSE;

typedef struct
{
    uint8_t  valid;
    uint32_t tickMs;
    char     host[APP_BC95_HOSTNAME_MAX_LEN];
    char     ip[APP_BC95_IP_STR_SIZE];
} AppBc95DnsCacheEntry_t;

static AppBc95DnsCacheEntry_t g_appBc95DnsCache[2];

static uint8_t App_NbiotIsIpv4Literal(const char *p_host)
{
    uint8_t dots = 0u;
    if ((p_host == NULL) || (*p_host == '\0')) { return APP_FALSE; }
    while (*p_host != '\0')
    {
        if (*p_host == '.') { dots++; }
        else if ((*p_host < '0') || (*p_host > '9')) { return APP_FALSE; }
        p_host++;
    }
    return (dots == 3u) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_NbiotDnsCacheLookup(const char *p_host, char *p_ipOut, uint32_t ipBufSize)
{
    uint32_t i;
    uint32_t now = HAL_GetTick();
    for (i = 0u; i < (sizeof(g_appBc95DnsCache) / sizeof(g_appBc95DnsCache[0])); i++)
    {
        if ((g_appBc95DnsCache[i].valid == APP_TRUE) &&
            (strncmp(g_appBc95DnsCache[i].host, p_host, sizeof(g_appBc95DnsCache[i].host)) == 0) &&
            ((now - g_appBc95DnsCache[i].tickMs) < APP_BC95_DNS_CACHE_TTL_MS))
        {
            (void)strncpy(p_ipOut, g_appBc95DnsCache[i].ip, ipBufSize - 1u);
            p_ipOut[ipBufSize - 1u] = '\\0';
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

static void App_NbiotDnsCacheStore(const char *p_host, const char *p_ip)
{
    AppBc95DnsCacheEntry_t *p_slot = &g_appBc95DnsCache[0];
    (void)memset(p_slot, 0, sizeof(*p_slot));
    p_slot->valid = APP_TRUE;
    p_slot->tickMs = HAL_GetTick();
    (void)strncpy(p_slot->host, p_host, sizeof(p_slot->host) - 1u);
    (void)strncpy(p_slot->ip, p_ip, sizeof(p_slot->ip) - 1u);
}

static void App_NbiotDnsCacheInvalidate(const char *p_host)
{
    uint32_t i;
    for (i = 0u; i < (sizeof(g_appBc95DnsCache) / sizeof(g_appBc95DnsCache[0])); i++)
    {
        if ((g_appBc95DnsCache[i].valid == APP_TRUE) &&
            (strncmp(g_appBc95DnsCache[i].host, p_host, sizeof(g_appBc95DnsCache[i].host)) == 0))
        {
            g_appBc95DnsCache[i].valid = APP_FALSE;
        }
    }
}

static AppStatus_t App_Bc95AtResolveHostRobustInternal(const char *p_hostname,
                                                       char *p_ipOut,
                                                       uint32_t ipBufSize,
                                                       uint8_t *p_attemptsOut,
                                                       uint32_t *p_elapsedMsOut)
{
    AppStatus_t status;
    uint32_t attempt;
    uint32_t totalAttempts = 0u;
    uint32_t uartErrCount  = 0u;
    uint32_t startTick = HAL_GetTick();

    if (p_attemptsOut != NULL) { *p_attemptsOut = 0u; }
    if (p_elapsedMsOut != NULL) { *p_elapsedMsOut = 0u; }
    (void)memset(p_ipOut, 0, ipBufSize);

    APP_LOGD("NBIOT", "==== DNS resolve start: %s ====", p_hostname);

    for (attempt = 1u; attempt <= APP_BC95_DNS_FAIL_RETRY_MAX; attempt++)
    {
        totalAttempts++;
        status = App_Bc95AtResolveHost(p_hostname, p_ipOut, ipBufSize);
        if (status == APP_STATUS_OK)
        {
            if (p_attemptsOut != NULL) { *p_attemptsOut = (uint8_t)totalAttempts; }
            if (p_elapsedMsOut != NULL) { *p_elapsedMsOut = HAL_GetTick() - startTick; }
            APP_LOGD("NBIOT", "==== DNS resolved: %s -> %s (attempts=%lu, elapsed=%lums) ====",
                     p_hostname, p_ipOut,
                     (unsigned long)totalAttempts,
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }
        if (status == APP_STATUS_FATAL) { return APP_STATUS_FATAL; }
        if (status == APP_STATUS_UART_RX_FAILED)
        {
            uartErrCount++;
            if (uartErrCount >= 3u) { return APP_STATUS_UART_RX_FAILED; }
        }
        else
        {
            uartErrCount = 0u;
        }
        if (attempt < APP_BC95_DNS_FAIL_RETRY_MAX)
        {
            App_Bc95AtDelayWithFeed(APP_BC95_DNS_FAIL_RETRY_DELAY_MS);
        }
    }

    if (p_attemptsOut != NULL) { *p_attemptsOut = (uint8_t)totalAttempts; }
    if (p_elapsedMsOut != NULL) { *p_elapsedMsOut = HAL_GetTick() - startTick; }
    return APP_STATUS_UART_TIMEOUT;
}

#if (APP_TEST_SERVER_TX_BUZZER_ENABLE == APP_TRUE)
static void App_NbiotTestNotifyServerTxBuzzer(void)
{
    HAL_GPIO_WritePin(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin, GPIO_PIN_SET);
    HAL_Delay(APP_TEST_SERVER_TX_BUZZER_ON_MS);
    HAL_GPIO_WritePin(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin, GPIO_PIN_RESET);
    HAL_Delay(APP_TEST_SERVER_TX_BUZZER_OFF_MS);
}
#endif

static uint8_t App_NbiotResolveDeleteAfterSend(uint8_t requestedDeleteStorage,
                                               const AppMeterServerFormatOptions_t *p_options)
{
    APP_RETURN_IF_FALSE(p_options != NULL, requestedDeleteStorage);
    return (uint8_t)((requestedDeleteStorage == APP_TRUE) ||
                     (p_options->deleteAfterSend == APP_TRUE));
}

static AppStatus_t App_NbiotBuildMeterPacket(const char *p_logTag,
                                             uint8_t deleteStorage,
                                             uint8_t unsentOnly,
                                             const AppMeterStorageRecord_t *p_liveRecord,
                                             uint8_t appendMgmtTrailers,
                                             AppMeterServerFormatOptions_t *p_optOut,
                                             AppMeterServerFormatResult_t *p_buildResultOut,
                                             uint8_t *p_packet,
                                             uint32_t packetCapacity,
                                             uint8_t *p_effectiveDeleteStorageOut)
{
    AppStatus_t status;
    AppStatus_t envAppendStatus;
    AppStatus_t diagAppendStatus;
    AppStatus_t statAppendStatus;
    AppMeterServerFormatOptions_t opt;
    AppMeterServerFormatResult_t buildResult;
    uint8_t effectiveDeleteStorage;

    APP_RETURN_IF_FALSE((p_logTag != NULL) &&
                        (p_buildResultOut != NULL) &&
                        (p_packet != NULL) &&
                        (p_effectiveDeleteStorageOut != NULL),
                        APP_STATUS_INVALID_PARAM);

    App_MeterServerOptionsLoad(&opt);
    App_MeterServerOptionsDump(&opt);

    effectiveDeleteStorage = App_NbiotResolveDeleteAfterSend(deleteStorage, &opt);

    if (p_liveRecord != NULL)
    {
        status = App_MeterServerFormatBuildFromRecord(&opt,
                                                      p_liveRecord,
                                                      p_packet,
                                                      packetCapacity,
                                                      &buildResult);
    }
    else
    {
        status = (unsentOnly == APP_TRUE)
               ? App_MeterServerFormatBuildFromUnsentStorage(&opt,
                                                             p_packet,
                                                             packetCapacity,
                                                             &buildResult)
               : App_MeterServerFormatBuildFromStorage(&opt,
                                                      p_packet,
                                                      packetCapacity,
                                                      &buildResult);
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    if (appendMgmtTrailers == APP_TRUE)
    {
        envAppendStatus = App_NbiotAppendMgmtEnvTrailer(p_packet, packetCapacity, &buildResult);
        if (envAppendStatus != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "%s env trailer skipped (status=%d, len=%u)",
                     p_logTag, (int)envAppendStatus, (unsigned int)buildResult.packetLength);
        }

        diagAppendStatus = App_NbiotAppendMgmtSelfTestTrailer(p_packet, packetCapacity, &buildResult);
        if (diagAppendStatus != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "%s selftest trailer skipped (status=%d, len=%u)",
                     p_logTag, (int)diagAppendStatus, (unsigned int)buildResult.packetLength);
        }

        /* selftest 트레일러(0xD2) 뒤: service 전송 통계 트레일러(tag 0xD3) */
        statAppendStatus = App_NbiotAppendMgmtServiceStatTrailer(p_packet, packetCapacity, &buildResult);
        if (statAppendStatus != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "%s service-stat trailer skipped (status=%d, len=%u)",
                     p_logTag, (int)statAppendStatus, (unsigned int)buildResult.packetLength);
        }
    }

    if (p_optOut != NULL)
    {
        *p_optOut = opt;
    }
    *p_buildResultOut = buildResult;
    *p_effectiveDeleteStorageOut = effectiveDeleteStorage;
    return APP_STATUS_OK;
}

static uint8_t App_NbiotResolveWaitServerAck(const AppMeterServerFormatOptions_t *p_options)
{
    APP_RETURN_IF_FALSE(p_options != NULL, (uint8_t)APP_POLICY_WAIT_MGMT_ACK_ENABLE);
    return App_MeterServerOptionsValidate((AppMeterServerFormatOptions_t *)p_options) == APP_STATUS_OK
           ? p_options->ackWaitEnabled
           : (uint8_t)APP_POLICY_WAIT_MGMT_ACK_ENABLE;
}

/* [ACK 분리 2026-11-04] service 경로용 런타임 스위치 — service 전용 필드 사용 */
static uint8_t App_NbiotResolveWaitServiceAck(const AppMeterServerFormatOptions_t *p_options)
{
    APP_RETURN_IF_FALSE(p_options != NULL, (uint8_t)APP_POLICY_WAIT_SERVICE_ACK_ENABLE);
    return App_MeterServerOptionsValidate((AppMeterServerFormatOptions_t *)p_options) == APP_STATUS_OK
           ? p_options->svcAckWaitEnabled
           : (uint8_t)APP_POLICY_WAIT_SERVICE_ACK_ENABLE;
}

static void App_HwNbiotPowerCycle(void)
{
    APP_LOGD("NBIOT", "App_HwNbiotPowerCycle");
    App_GpioLpSetNbiotPowered(APP_FALSE); /* off */
    /*
     * Hold power-off briefly so the modem performs a clean reboot, but do not
     * wait after power-on here; the next boot-wait path must start immediately
     * so the boot banner can be captured.
     */
    HAL_Delay(200u);
    App_GpioLpSetNbiotPowered(APP_TRUE);  /* on */
}

static void App_NbiotPutU16Le(uint8_t *p_dst, uint16_t value)
{
    if (p_dst == NULL) { return; }
    p_dst[0] = (uint8_t)(value & 0xFFu);
    p_dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void App_NbiotPutS16Le(uint8_t *p_dst, int16_t value)
{
    if (p_dst == NULL) { return; }
    p_dst[0] = (uint8_t)((uint16_t)value & 0xFFu);
    p_dst[1] = (uint8_t)(((uint16_t)value >> 8) & 0xFFu);
}

static uint8_t App_NbiotChecksum8(const uint8_t *p_data, uint16_t startIndex, uint16_t endIndex)
{
    uint16_t i;
    uint8_t sum = 0u;

    if (p_data == NULL)
    {
        return 0u;
    }

    for (i = startIndex; i < endIndex; i++)
    {
        sum = (uint8_t)(sum + p_data[i]);
    }
    return sum;
}

static AppStatus_t App_NbiotAppendMgmtSelfTestTrailer(uint8_t *p_packet, uint16_t packetCapacity, AppMeterServerFormatResult_t *p_result)
{
#if (APP_MGMT_TX_APPEND_SELFTEST_ENABLE == APP_TRUE)
    const AppSelfTestContext_t *p_ctx;
    uint16_t executedMask;
    uint16_t passedMask;
    uint16_t cursor;
    uint8_t diag[APP_MGMT_TX_SELFTEST_PAYLOAD_LEN];

    APP_RETURN_IF_FALSE((p_packet != NULL) && (p_result != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_result->packetLength >= 3u, APP_STATUS_INVALID_PARAM);

    /* 기존 checksum 위치부터 다시 덮어쓴다 */
    cursor = (uint16_t)(p_result->packetLength - 1u);

    /* self-test TLV: tag(D2) + len + payload(7) + checksum */
    APP_RETURN_IF_FALSE(((uint32_t)cursor + 2u + APP_MGMT_TX_SELFTEST_PAYLOAD_LEN + 1u) <= packetCapacity,
                        APP_STATUS_BUFFER_OVERFLOW);

    p_ctx = App_SelfTestGetContext();
    executedMask = 0u;
    passedMask   = 0u;
    (void)memset(diag, 0, sizeof(diag));

    if (p_ctx != NULL)
    {
        uint8_t i;
        for (i = 0u; i < (uint8_t)APP_SELFTEST_ITEM_COUNT; i++)
        {
            if (p_ctx->items[i].executed != 0u)
            {
                executedMask |= (uint16_t)(1u << i);
                if (p_ctx->items[i].passed != 0u)
                {
                    passedMask |= (uint16_t)(1u << i);
                }
            }
        }

        App_NbiotPutU16Le(&diag[0], executedMask);
        App_NbiotPutU16Le(&diag[2], passedMask);
        diag[4] = (uint8_t)(p_ctx->failCount & 0xFFu);
        App_NbiotPutU16Le(&diag[5], (uint16_t)p_ctx->lastSequenceStatus);
    }

    p_packet[cursor++] = APP_MGMT_TX_SELFTEST_TAG;
    p_packet[cursor++] = APP_MGMT_TX_SELFTEST_PAYLOAD_LEN;
    (void)memcpy(&p_packet[cursor], diag, sizeof(diag));
    cursor = (uint16_t)(cursor + sizeof(diag));

    /* length = byte[2]부터 checksum 직전까지 */
    p_packet[1] = (uint8_t)(cursor - 2u);
    p_result->checksum = App_NbiotChecksum8(p_packet, 2u, cursor);
    p_packet[cursor++] = p_result->checksum;

    p_result->payloadLength = p_packet[1];
    p_result->packetLength  = cursor;

    APP_LOGN("NBIOT", "[[MgmtTx]] selftest trailer appended exec=0x%04X pass=0x%04X fail=%u st=%u len=%u",
             (unsigned int)executedMask,
             (unsigned int)passedMask,
             (unsigned int)diag[4],
             (unsigned int)((uint16_t)diag[5] | ((uint16_t)diag[6] << 8)),
             (unsigned int)p_result->packetLength);

    return APP_STATUS_OK;
#else
    (void)p_packet;
    (void)packetCapacity;
    (void)p_result;
    return APP_STATUS_OK;
#endif
}
 
/* mgmt 트레일러 tag 0xD3: service(플랫폼) 전송 통계
 * payload(2바이트): attempts U8, ack(QLWULDATASTATUS:4) U8
 * 각 카운터는 255에서 포화(모듈러 래핑 없음) - 1바이트 보고 형식. */
static AppStatus_t App_NbiotAppendMgmtServiceStatTrailer(uint8_t *p_packet, uint16_t packetCapacity, AppMeterServerFormatResult_t *p_result)
{
    uint16_t cursor;
    uint8_t stat[APP_MGMT_TX_SERVICE_STAT_PAYLOAD_LEN];

    APP_RETURN_IF_FALSE((p_packet != NULL) && (p_result != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_result->packetLength >= 3u, APP_STATUS_INVALID_PARAM);

    /* 기존 checksum 위치부터 다시 덮어쓴다 (selftest 트레일러 0xD2 뒤에 이어 붙임) */
    cursor = (uint16_t)(p_result->packetLength - 1u);

    APP_RETURN_IF_FALSE(((uint32_t)cursor + 2u + APP_MGMT_TX_SERVICE_STAT_PAYLOAD_LEN + 1u) <= packetCapacity,
                        APP_STATUS_BUFFER_OVERFLOW);

    /* 255 포화: 1바이트 필드이므로 값을 넘기지 않고 최댓값에 고정 */
    stat[0] = (s_appNbiotServiceTxAttempts > 255u) ? 255u : (uint8_t)s_appNbiotServiceTxAttempts;
    stat[1] = (s_appNbiotServiceSuccessCount   > 255u) ? 255u : (uint8_t)s_appNbiotServiceSuccessCount;
    /* [DL-ACK] stat[2] = 서버 애플리케이션 ACK(+NNMI) 수신 수 */
    stat[2] = (s_appNbiotServiceAppAckCount > 255u) ? 255u : (uint8_t)s_appNbiotServiceAppAckCount;

    p_packet[cursor++] = APP_MGMT_TX_SERVICE_STAT_TAG;
    p_packet[cursor++] = APP_MGMT_TX_SERVICE_STAT_PAYLOAD_LEN;
    (void)memcpy(&p_packet[cursor], stat, sizeof(stat));
    cursor = (uint16_t)(cursor + sizeof(stat));

    /* length = byte[2]부터 checksum 직전까지 */
    p_packet[1] = (uint8_t)(cursor - 2u);
    p_result->checksum = App_NbiotChecksum8(p_packet, 2u, cursor);
    p_packet[cursor++] = p_result->checksum;

    p_result->payloadLength = p_packet[1];
    p_result->packetLength  = cursor;

    APP_LOGN("NBIOT", "[[MgmtTx]] service-stat trailer appended tx=%u ok=%u ack=%u len=%u",
             (unsigned int)stat[0],
             (unsigned int)stat[1],
             (unsigned int)stat[2],
             (unsigned int)p_result->packetLength);

    /* 보고 완료 - 다음 mgmt 보고까지의 구간 통계를 위해 리셋한다.
     * (패킷에는 이미 값이 복사되어 있으므로 리셋이 전송 내용에 영향 없음) */
    s_appNbiotServiceTxAttempts = 0u;
    s_appNbiotServiceSuccessCount   = 0u;
    s_appNbiotServiceAppAckCount = 0u;

    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotAppendMgmtEnvTrailer(uint8_t *p_packet, uint16_t packetCapacity, AppMeterServerFormatResult_t *p_result)
{
#if (APP_MGMT_TX_APPEND_ENV_ENABLE == APP_TRUE)
    uint16_t cursor;
    uint8_t env[APP_MGMT_TX_ENV_PAYLOAD_LEN];
    SHTC3_Data_t th;
    uint8_t validFlags = 0u;
    int16_t tempX100 = 0;
    uint16_t rhX100 = 0u;

    APP_RETURN_IF_FALSE((p_packet != NULL) && (p_result != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_result->packetLength >= 3u, APP_STATUS_INVALID_PARAM);

    /* 기존 checksum 위치부터 다시 덮어쓴다 */
    cursor = (uint16_t)(p_result->packetLength - 1u);

    /* tag + len + payload(5) + checksum */
    APP_RETURN_IF_FALSE(((uint32_t)cursor + 2u + APP_MGMT_TX_ENV_PAYLOAD_LEN + 1u) <= packetCapacity,
                        APP_STATUS_BUFFER_OVERFLOW);

    (void)memset(env, 0, sizeof(env));

    if (SHTC3_ReadTempHumidity(APP_I2C_AUX_HANDLE, &th) == HAL_OK)
    {
        float tempScaled = th.temperature * 100.0f;
        float rhScaled   = th.humidity * 100.0f;

        if (tempScaled > 32767.0f) { tempScaled = 32767.0f; }
        if (tempScaled < -32768.0f) { tempScaled = -32768.0f; }
        if (rhScaled < 0.0f) { rhScaled = 0.0f; }
        if (rhScaled > 65535.0f) { rhScaled = 65535.0f; }

        tempX100 = (int16_t)tempScaled;
        rhX100   = (uint16_t)rhScaled;
        validFlags = 0x03u; /* bit0=temp valid, bit1=humidity valid */

        env[0] = validFlags;
        App_NbiotPutS16Le(&env[1], tempX100);
        App_NbiotPutU16Le(&env[3], rhX100);
    }
    else
    {
        env[0] = 0x00u;
        App_NbiotPutS16Le(&env[1], 0);
        App_NbiotPutU16Le(&env[3], 0u);
    }

    p_packet[cursor++] = APP_MGMT_TX_ENV_TAG;
    p_packet[cursor++] = APP_MGMT_TX_ENV_PAYLOAD_LEN;
    (void)memcpy(&p_packet[cursor], env, sizeof(env));
    cursor = (uint16_t)(cursor + sizeof(env));

    /* length = byte[2]부터 checksum 직전까지 */
    p_packet[1] = (uint8_t)(cursor - 2u);
    p_result->checksum = App_NbiotChecksum8(p_packet, 2u, cursor);
    p_packet[cursor++] = p_result->checksum;

    p_result->payloadLength = p_packet[1];
    p_result->packetLength  = cursor;

    APP_LOGN("NBIOT", "[[MgmtTx]] env trailer appended flags=0x%02X tempX100=%d rhX100=%u len=%u",
             (unsigned int)env[0],
             (int)tempX100,
             (unsigned int)rhX100,
             (unsigned int)p_result->packetLength);

    return APP_STATUS_OK;
#else
    (void)p_packet;
    (void)packetCapacity;
    (void)p_result;
    return APP_STATUS_OK;
#endif
}

/* ============================================================
 *  PowerOff Fast-Bail: 모듈 무응답 연속 감지
 * ============================================================ */
/* [ADD] 파일 상단, static 헬퍼 선언들 근처에 추가 */
/* [ADD] 개설(추적) 중인 UDP 소켓 비트마스크: bit N = 소켓 id N (MAX=6).
 * 근거: LGU 커스텀 FW는 열려있지 않은 소켓 NSOCL에 +CME ERROR: 530 반환 (10/30 실측:
 * detach 소켓정리 7회 -> 530 7회, "sock clean c=0 ac=0 sf=0 of=7" 대응).
 * 개설된 소켓에만 NSOCL을 보내기 위해 추적. 소켓 수명은 세션 내부이므로
 * modem reset 시 AtInit에서 0으로 리셋. */
static uint32_t s_nbiotUdpSocketOpenMask = 0u;

static uint8_t s_nbiotDetachNoRespStreak = 0u;

#ifndef APP_NBIOT_DETACH_NORESP_BAIL_THRESHOLD
#define APP_NBIOT_DETACH_NORESP_BAIL_THRESHOLD   (3u)
#endif

static void App_NBIoTDetachNoteOutcome(AppStatus_t st)
{
    if ((st == APP_STATUS_UART_TIMEOUT) ||
        (st == APP_STATUS_UART_RX_FAILED) ||
        (st == APP_STATUS_UART_TX_FAILED))
    {
        if (s_nbiotDetachNoRespStreak < 0xFFu) s_nbiotDetachNoRespStreak++;
    }
    else
    {
        s_nbiotDetachNoRespStreak = 0u;
    }
}

static uint8_t App_NBIoTDetachModuleDead(void)
{
    return (s_nbiotDetachNoRespStreak >= APP_NBIOT_DETACH_NORESP_BAIL_THRESHOLD) ? APP_TRUE : APP_FALSE;
}

/* ============================================================
 *  유틸: WDT-feed 분할 지연
 * ============================================================ */
static void App_Bc95AtDelayWithFeed(uint32_t ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t step  = 100u;

    while ((HAL_GetTick() - start) < ms)
    {
        APP_WWDGFeed();
        if ((ms - (HAL_GetTick() - start)) < step)
        {
            step = ms - (HAL_GetTick() - start);
            if (step == 0u) break;
        }
        HAL_Delay(step);
    }
    APP_WWDGFeed();
}

static uint16_t App_Bc95AtCopySnapshot(const uint8_t *p_src, uint16_t srcLen,
                                       char *p_dst, uint16_t dstSize)
{
    uint16_t copyLen = srcLen;

    if ((p_src == NULL) || (p_dst == NULL) || (dstSize == 0u))
    {
        return 0u;
    }

    if (copyLen >= dstSize)
    {
        copyLen = (uint16_t)(dstSize - 1u);
    }

    if (copyLen > 0u)
    {
        (void)memcpy(p_dst, p_src, copyLen);
    }
    p_dst[copyLen] = '\0';
    return copyLen;
}

static void App_Bc95AtLineQueueReset(AppBc95AtLineQueue_t *p_queue)
{
    if (p_queue == NULL) return;
    (void)memset(p_queue, 0, sizeof(*p_queue));
}

static void App_Bc95AtLineQueuePush(AppBc95AtLineQueue_t *p_queue, const char *p_line)
{
    uint8_t index;

    if ((p_queue == NULL) || (p_line == NULL) || (p_line[0] == '\0'))
    {
        return;
    }

    index = p_queue->tail;
    (void)strncpy(p_queue->lines[index], p_line, (size_t)(APP_BC95_AT_LINE_MAX_LEN - 1u));
    p_queue->lines[index][APP_BC95_AT_LINE_MAX_LEN - 1u] = '\0';

    p_queue->tail = (uint8_t)((p_queue->tail + 1u) % APP_BC95_AT_LINE_QUEUE_DEPTH);
    if (p_queue->count < APP_BC95_AT_LINE_QUEUE_DEPTH)
    {
        p_queue->count++;
    }
    else
    {
        p_queue->head = (uint8_t)((p_queue->head + 1u) % APP_BC95_AT_LINE_QUEUE_DEPTH);
    }
}

static void App_Bc95AtLineQueueFeed(AppBc95AtLineQueue_t *p_queue,
                                    const char *p_snapshot,
                                    uint16_t snapshotLen)
{
    uint16_t i;
    char ch;

    if ((p_queue == NULL) || (p_snapshot == NULL))
    {
        return;
    }

    if (snapshotLen < p_queue->parsedLen)
    {
        p_queue->parsedLen = 0u;
        p_queue->partialLen = 0u;
        p_queue->partial[0] = '\0';
    }

    for (i = p_queue->parsedLen; i < snapshotLen; i++)
    {
        ch = p_snapshot[i];
        if ((ch == '\r') || (ch == '\n'))
        {
            if (p_queue->partialLen > 0u)
            {
                p_queue->partial[p_queue->partialLen] = '\0';
                App_Bc95AtLineQueuePush(p_queue, p_queue->partial);
                p_queue->partialLen = 0u;
                p_queue->partial[0] = '\0';
            }
            continue;
        }

        if (p_queue->partialLen < (APP_BC95_AT_LINE_MAX_LEN - 1u))
        {
            p_queue->partial[p_queue->partialLen++] = ch;
            p_queue->partial[p_queue->partialLen] = '\0';
        }
    }

    p_queue->parsedLen = snapshotLen;
}

static uint8_t App_Bc95AtLineQueueContainsExact(const AppBc95AtLineQueue_t *p_queue,
                                                const char *p_line)
{
    uint8_t i;
    uint8_t index;

    if ((p_queue == NULL) || (p_line == NULL))
    {
        return APP_FALSE;
    }

    for (i = 0u; i < p_queue->count; i++)
    {
        index = (uint8_t)((p_queue->head + i) % APP_BC95_AT_LINE_QUEUE_DEPTH);
        if (strcmp(p_queue->lines[index], p_line) == 0)
        {
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

static uint8_t App_Bc95AtLineQueueContainsPrefix(const AppBc95AtLineQueue_t *p_queue,
                                                 const char *p_prefix)
{
    uint8_t i;
    uint8_t index;
    size_t prefixLen;

    if ((p_queue == NULL) || (p_prefix == NULL))
    {
        return APP_FALSE;
    }

    prefixLen = strlen(p_prefix);
    for (i = 0u; i < p_queue->count; i++)
    {
        index = (uint8_t)((p_queue->head + i) % APP_BC95_AT_LINE_QUEUE_DEPTH);
        if (strncmp(p_queue->lines[index], p_prefix, prefixLen) == 0)
        {
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

static uint8_t App_Bc95AtLineQueueContainsSubstring(const AppBc95AtLineQueue_t *p_queue,
                                                    const char *p_token)
{
    uint8_t i;
    uint8_t index;

    if ((p_queue == NULL) || (p_token == NULL) || (p_token[0] == '\0'))
    {
        return APP_FALSE;
    }

    for (i = 0u; i < p_queue->count; i++)
    {
        index = (uint8_t)((p_queue->head + i) % APP_BC95_AT_LINE_QUEUE_DEPTH);
        if (strstr(p_queue->lines[index], p_token) != NULL)
        {
            return APP_TRUE;
        }
    }

    if (strstr(p_queue->partial, p_token) != NULL)
    {
        return APP_TRUE;
    }
    return APP_FALSE;
}

static uint8_t App_Bc95AtLineQueueHasFinalResponse(const AppBc95AtLineQueue_t *p_queue)
{
    if (p_queue == NULL)
    {
        return APP_FALSE;
    }

    if (App_Bc95AtLineQueueContainsExact(p_queue, APP_BC95_AT_OK_TOKEN) == APP_TRUE)
    {
        return APP_TRUE;
    }
    if (App_Bc95AtLineQueueContainsExact(p_queue, APP_BC95_AT_ERROR_TOKEN) == APP_TRUE)
    {
        return APP_TRUE;
    }
    if (App_Bc95AtLineQueueContainsPrefix(p_queue, APP_BC95_AT_CME_ERROR_PREFIX) == APP_TRUE)
    {
        return APP_TRUE;
    }
    return APP_FALSE;
}

/* ============================================================
 *  URC 모니터 (근본 해결 - ISR 레벨 EVTIND 래치)
 *    문제: FinishRxIt 이 명령 완료마다 HAL_UART_AbortReceive_IT 로 수신을
 *          끊고, 다음 명령이 나오기 전까지 재무장이 없다. 부팅 트랙의
 *          자가진단(수 초) 동안은 active==FALSE 이므로
 *          App_Bc95AtOnUartRxCompleteIsr 이 조기 반환하며 HW RX 레지스터
 *          1~2바이트만 남고 URC 전체가 오버런 유실된다(boot10 실측:
 *          :0/:3/:8 이 RAW 에도 도달하지 못함).
 *    해결: 명령 공백에도 1바이트 수신 IT 를 항상 유지하고, ISR 에서
 *          바이트 단위 FSM 으로 "+QLWEVTIND:" URC 만 골라 래치한다.
 *          로그/큐 처리 없는 최소 ISR 비용, 다른 URC 는 무시.
 * ============================================================ */
typedef struct
{
    UART_HandleTypeDef *p_huart;
    uint8_t            byte;       /* 1바이트 IT 수신 대상 버퍼 */
    volatile uint8_t   armed;      /* 모니터 무장 상태(ISR 에서 참조) */
    uint8_t            matchIdx;   /* 접두어 매칭 진행 인덱스 */
    uint8_t            inNumber;   /* 이벤트 번호 파싱 중 */
    uint16_t           number;     /* 파싱 중인 이벤트 번호 */
} App_Bc95AtUrcMonitor_t;

static App_Bc95AtUrcMonitor_t s_bc95UrcMonitor;
static volatile uint8_t s_bc95LatchIsrPending = 0u;
/* [FIX-EVTIND-4] ready 판정 타임아웃 시 last∈{3,8}이면 마스크 불완전해도
 * 실제 전송 1회로 등록을 검증한다(NRB 에스컬레이션 대신). 호출부가
 * 소비 후 0으로 되돌린다. */
static uint8_t s_bc95EvtindReadyVerifyPending = 0u;
/* 지금까지 본 EVTIND 종류의 비트마스크(bit n = EVTIND:n 관측).
 * 마지막 값(last)만 저장하면 :0(등록완료)이 :3/:8에 덮여 유실되므로
 * ready 판정은 마스크 기준으로 한다. */
static volatile uint16_t s_bc95EVTINDSeenMask = 0u;
/* [FIX-EVTIND-3] 스캔 경로 부분 URC continuation:
 * 버퍼 경계에서 잘린 "+QLWEVTIND:" 접두어/라인을 다음 스캔에서 이어 붙여
 * 파싱한다. 비어 있음은 tailLen==0 으로 판정. */
static char s_bc95EvtScanTail[16];
static uint16_t s_bc95EvtScanTailLen = 0u;
/* 요약 시점에 이미 로그를 남긴 이벤트 비트마스크(ISR 캡처분 중복 로그 방지) */
static uint16_t s_bc95LoggedMask = 0u; /* ISR 래치 갱신 표지(메인 루프 로그용) */
/* [2단계-1차] HW 수신 오류 카운터: 유실 원인(오버런 유무) 실증용.
 * drain 시작 시 플래그 폴링으로 적산하고, ISR 모니터 분기에서는 ORE 발견 즉시
 * 클리어(자동복구)하며 적산한다. register 실패/래치 캡처 로그에 함께 출력되며
 * 카운터는 세션 누적이므로 웨이크 간 증가분으로 판독한다. */
static volatile uint32_t s_bc95RxErrOreCount = 0u;
static volatile uint32_t s_bc95RxErrFeCount  = 0u;
static volatile uint32_t s_bc95RxErrNeCount  = 0u;
static volatile uint32_t s_bc95RxErrPeCount  = 0u;

/* ============================================================
 *  [PROBE] RX blind-window instrumentation (diagnostic only, no behavior change)
 *    - all variables are updated from main context only (no ISR access)
 * ============================================================ */
#ifdef NBIOT_TRACE_DEBUG
#define APP_BC95_TRC_MARK_PROBE_BLIND_RXNE (33)
#define APP_BC95_TRC_MARK_PROBE_BLIND_ORE  (34)
#endif /* NBIOT_TRACE_DEBUG */

#ifdef NBIOT_TRACE_DEBUG
static volatile uint32_t s_probeCmds;
static volatile uint32_t s_probeDmaOnAtDrain;
static volatile uint32_t s_probeDmaOffAtDrain;
static volatile uint32_t s_probeDmaFlagMismatch;
static volatile uint32_t s_probeDisarmDmaOff;
static volatile uint32_t s_probeBlindOre;
static volatile uint32_t s_probeBlindRxne;
static volatile uint32_t s_probeSplitSuspect;
static volatile uint32_t s_probeBlindLastMs;
static volatile uint32_t s_probeBlindMaxMs;
static volatile uint32_t s_probeDrainTick;
static volatile uint32_t s_probeBaud;
static volatile uint8_t  s_probeBlindLastByte;
#endif /* NBIOT_TRACE_DEBUG */

static uint8_t App_Bc95RxProbeTailIsPartialUrc(const char *p_buf, uint16_t len)
{
    uint16_t plen = (uint16_t)APP_BC95_AT_QLWEVTIND_PREFIX_LEN;
    uint16_t k;
    uint16_t i;
    uint16_t j;

    /* (a) buffer ends with a leading part of "+QLWEVTIND:" */
    for (k = 1u; (k < plen) && (k <= len); k++)
    {
        if (memcmp(&p_buf[len - k], APP_BC95_AT_QLWEVTIND_PREFIX, k) == 0)
        {
            return 1u;
        }
    }
    /* (b) full prefix present but no CR/LF after it */
    for (i = 0u; ((uint32_t)i + plen) <= len; i++)
    {
        if (memcmp(&p_buf[i], APP_BC95_AT_QLWEVTIND_PREFIX, plen) == 0)
        {
            j = (uint16_t)(i + plen);
            while ((j < len) && (p_buf[j] != '\r') && (p_buf[j] != '\n'))
            {
                j++;
            }
            if (j >= len)
            {
                return 1u;
            }
        }
    }
    return 0u;
}

#ifdef NBIOT_TRACE_DEBUG
static void App_Bc95RxProbeDump(const char *p_where)
{
    APP_LOGN("NBIOT", "[[RxProbe]] %s cmds=%lu dma(on/off/mis)=%lu/%lu/%lu disarmOff=%lu",
             p_where,
             (unsigned long)s_probeCmds,
             (unsigned long)s_probeDmaOnAtDrain,
             (unsigned long)s_probeDmaOffAtDrain,
             (unsigned long)s_probeDmaFlagMismatch,
             (unsigned long)s_probeDisarmDmaOff);
    APP_LOGN("NBIOT", "[[RxProbe]] blind(ore/rxne)=%lu/%lu lastB=0x%02X blindMs(last/max)=%lu/%lu split=%lu baud=%lu",
             (unsigned long)s_probeBlindOre,
             (unsigned long)s_probeBlindRxne,
             (unsigned int)s_probeBlindLastByte,
             (unsigned long)s_probeBlindLastMs,
             (unsigned long)s_probeBlindMaxMs,
             (unsigned long)s_probeSplitSuspect,
             (unsigned long)s_probeBaud);
}
#endif /* NBIOT_TRACE_DEBUG */

/* ============================================================
 *  [INSTR] EVTIND 이벤트 추적 링버퍼 (진단 전용, 동작 변경 없음)
 *    - 어느 경로(ISR/DMA/라인스캔/구제스캔)가 언제 이벤트를 캡처했는지 기록
 *    - Add 는 ISR 안전(PRIMASK, 로그 호출 없음). Dump 는 메인 컨텍스트 전용
 * ============================================================ */
#ifdef NBIOT_TRACE_DEBUG
#define APP_BC95_TRC_DEPTH        (32u)   /* 2의 거듭제곱 */
#define APP_BC95_TRC_KIND_EVT     (0u)    /* val = EVTIND 번호 */
#define APP_BC95_TRC_KIND_DMA     (1u)    /* val = DMA idle 수신 바이트 수 */
#define APP_BC95_TRC_KIND_RESCUE  (2u)    /* val = Disarm 시 CNDTR 구제 바이트 수 */
#define APP_BC95_TRC_KIND_MARK    (3u)    /* val = 마커 ID */
#define APP_BC95_TRC_SRC_NONE     (0u)
#define APP_BC95_TRC_SRC_ISR      (1u)    /* 1바이트 모니터 FSM */
#define APP_BC95_TRC_SRC_DMA      (2u)    /* RxEvent 콜백 */
#define APP_BC95_TRC_SRC_LINE     (3u)    /* 라인/버퍼 스캔 (UpdateEVTINDLatch) */
#define APP_BC95_TRC_SRC_RESCUE   (4u)    /* Disarm CNDTR 구제 스캔 */
#define APP_BC95_TRC_SRC_COUNT    (5u)
#define APP_BC95_TRC_MARK_LATCHRESET (1)
#endif /* NBIOT_TRACE_DEBUG */

#ifdef NBIOT_TRACE_DEBUG
typedef struct
{
    uint32_t tick;
    int16_t  val;
    uint16_t mask;     /* 기록 시점의 s_bc95EVTINDSeenMask */
    uint8_t  kind;
    uint8_t  src;
    uint8_t  dup;      /* EVT: 이미 mask 에 있던 이벤트를 다시 캡처 */
    uint8_t  pad;
} AppBc95EvtTrace_t;
#endif /* NBIOT_TRACE_DEBUG */

#ifdef NBIOT_TRACE_DEBUG
static AppBc95EvtTrace_t s_bc95Trc[APP_BC95_TRC_DEPTH];
static volatile uint32_t s_bc95TrcHead = 0u;
static uint32_t          s_bc95TrcDumped = 0u;
static volatile uint8_t  s_bc95TrcSrcHint = 0u;
static volatile uint16_t s_bc95TrcSrcCnt[APP_BC95_TRC_SRC_COUNT];
static volatile uint16_t s_bc95TrcDupCnt = 0u;
#endif /* NBIOT_TRACE_DEBUG */

#ifdef NBIOT_TRACE_DEBUG
static void App_Bc95EvtTraceAdd(uint8_t kind, uint8_t src, int16_t val, uint8_t dup)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t idx;

    __disable_irq();
    if ((kind == APP_BC95_TRC_KIND_EVT) && (dup != 0u))
    {
        /* 중복 캡처: 링 범람 방지를 위해 저장하지 않고 카운터만 증가 */
        if (src < APP_BC95_TRC_SRC_COUNT) { s_bc95TrcSrcCnt[src]++; }
        s_bc95TrcDupCnt++;
        __set_PRIMASK(primask);
        return;
    }
    idx = s_bc95TrcHead & (APP_BC95_TRC_DEPTH - 1u);
    s_bc95Trc[idx].tick = HAL_GetTick();
    s_bc95Trc[idx].val  = val;
    s_bc95Trc[idx].mask = s_bc95EVTINDSeenMask;
    s_bc95Trc[idx].kind = kind;
    s_bc95Trc[idx].src  = src;
    s_bc95Trc[idx].dup  = dup;
    s_bc95Trc[idx].pad  = 0u;
    s_bc95TrcHead++;
    if (kind == APP_BC95_TRC_KIND_EVT)
    {
        if (src < APP_BC95_TRC_SRC_COUNT) { s_bc95TrcSrcCnt[src]++; }
        if (dup != 0u) { s_bc95TrcDupCnt++; }
    }
    __set_PRIMASK(primask);
}
#endif /* NBIOT_TRACE_DEBUG */

/* 메인 컨텍스트 전용(로그 호출). 마지막 Dump 이후 기록분만 출력하고 커서를 전진 */
#ifdef NBIOT_TRACE_DEBUG
static void App_Bc95EvtTraceDump(const char *p_tag)
{
    static const char *const kKind[] = { "EVT", "DMA", "RESCUE", "MARK" };
    static const char *const kSrc[]  = { "-", "ISR", "DMA", "LINE", "RESCUE" };
    uint32_t head = s_bc95TrcHead;
    uint32_t from = s_bc95TrcDumped;
    uint32_t lost = 0u;
    uint32_t i;

    if ((head - from) > APP_BC95_TRC_DEPTH)
    {
        lost = (head - from) - APP_BC95_TRC_DEPTH;
        from = head - APP_BC95_TRC_DEPTH;
    }
    APP_LOGN("NBIOT",
             "[[EvtTrace]] %s n=%lu lost=%lu last=%d mask=0x%04X cnt(isr=%u dma=%u line=%u rescue=%u dup=%u) ore=%lu fe=%lu",
             p_tag,
             (unsigned long)(head - from), (unsigned long)lost,
             s_bc95LastEVTIND, (unsigned int)s_bc95EVTINDSeenMask,
             (unsigned int)s_bc95TrcSrcCnt[APP_BC95_TRC_SRC_ISR],
             (unsigned int)s_bc95TrcSrcCnt[APP_BC95_TRC_SRC_DMA],
             (unsigned int)s_bc95TrcSrcCnt[APP_BC95_TRC_SRC_LINE],
             (unsigned int)s_bc95TrcSrcCnt[APP_BC95_TRC_SRC_RESCUE],
             (unsigned int)s_bc95TrcDupCnt,
             (unsigned long)s_bc95RxErrOreCount,
             (unsigned long)s_bc95RxErrFeCount);
    for (i = from; i != head; i++)
    {
        const AppBc95EvtTrace_t *p_e = &s_bc95Trc[i & (APP_BC95_TRC_DEPTH - 1u)];
        APP_LOGN("NBIOT", "[[EvtTrace]]  t=%lu.%03lu %s src=%s v=%d%s mask=0x%04X",
                 (unsigned long)(p_e->tick / 1000u), (unsigned long)(p_e->tick % 1000u),
                 (p_e->kind < 4u) ? kKind[p_e->kind] : "?",
                 (p_e->src < APP_BC95_TRC_SRC_COUNT) ? kSrc[p_e->src] : "?",
                 (int)p_e->val,
                 (p_e->dup != 0u) ? " DUP" : "",
                 (unsigned int)p_e->mask);
    }
    s_bc95TrcDumped = head;
}
#endif /* NBIOT_TRACE_DEBUG */


/* ============================================================
 *  [2단계-2차] URC 모니터 idle-DMA 전환 (ORE 증가 확정 후 적용 권장)
 *    - 공백 구간(명령 미진행) 수신을 1바이트 IT -> idle-line DMA 로 전환.
 *      버스트 URC(EVTIND 4줄)는 ISR 개입 없이 HW가 연속 수신하므로
 *      ISR 지연 유발 오버런(ORE)에 의한 EVTIND 유실이 근본 제거된다.
 *    - 응답 캡처 체인(명령 진행 중 1바이트 IT)은 유지: 명령 시작 시
 *      DMA 수신분을 구제-스캔한 뒤 중단하고, 명령 종료(FinishRxIt)에서 재무장.
 *    - DMA1_Channel3 은 코드에서 직접 초기화하므로 CubeMX .ioc 수정 불필요.
 *    - DMA 채널 NVIC 인터럽트는 의도적으로 enable 하지 않는다(stm32l0xx_it.c 에
 *      핸들러가 없어 TC/HT 인터럽트 발생 시 Default_Handler 행 hang 위험).
 *      대신 수신 완료 판정은 UART IDLE 이벤트(UART IRQ 기존 경로)와
 *      DisarmUrcMonitor 의 CNDTR 기반 구제-스캔이 담당한다.
 * ============================================================ */
#define APP_BC95_AT_RX_DMA_BUF_SIZE        (256u)
/* [초안-검증필수] LPUART1_RX 의 DMA1 CSELR 요청 코드(DMA_REQUEST_5 임시값).
 * CubeMX에서 LPUART1_RX 에 DMA(RX, DMA1_Channel3)를 추가하면 자동 생성되는
 * Init.Request 값을 그대로 복사하는 것이 가장 확실하다. 잘못된 값은 CH3 가
 * 다른 주변장치 요청에 매핑되어 RX DMA 가 전혀 동작하지 않는다(벤치에서
 * EVTIND 래치 전무로 즉시 확인됨 - 침묵 고장이 아님). */
#ifndef APP_BC95_RX_DMA_REQUEST
#define APP_BC95_RX_DMA_REQUEST            (DMA_REQUEST_5)
#endif

static DMA_HandleTypeDef s_bc95RxDmaHandle;
static uint8_t           s_bc95RxDmaBuf[APP_BC95_AT_RX_DMA_BUF_SIZE];
static volatile uint8_t  s_bc95RxDmaArmed     = 0u;
static volatile uint32_t s_bc95RxDmaRearmFail = 0u;

/* DMA 수신분 EVTIND 스캔: len = 이미 수신된 바이트 수 */
static void App_Bc95AtRxDmaScanAndLatch(uint16_t len)
{
    uint16_t safeLen;

    if (len == 0u)
    {
        return;
    }
    safeLen = (len > (uint16_t)APP_BC95_AT_RX_DMA_BUF_SIZE)
              ? (uint16_t)APP_BC95_AT_RX_DMA_BUF_SIZE : len;
    App_Bc95AtScanBufferForEVTIND((const char *)s_bc95RxDmaBuf, safeLen);
}

/* idle-DMA (재)기동: 실패 시 disarm + 카운터 (재무장 실패 은폐 방지) */
static void App_Bc95AtRxDmaStart(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(s_bc95UrcMonitor.p_huart,
                                     s_bc95RxDmaBuf,
                                     APP_BC95_AT_RX_DMA_BUF_SIZE) != HAL_OK)
    {
        s_bc95RxDmaArmed       = APP_FALSE;
        s_bc95UrcMonitor.armed = APP_FALSE;
        s_bc95RxDmaRearmFail++;
    }
}

/* RxEvent 콜백(idle/HT/TC): 수신분 EVTIND 래치 후 즉시 재무장 */
static void App_Bc95AtRxDmaRxEventCb(UART_HandleTypeDef *huart, uint16_t Size)
{
    if ((huart != APP_UART_NBIOT_HANDLE) || (s_bc95RxDmaArmed != APP_TRUE))
    {
        return;
    }
#ifdef NBIOT_TRACE_DEBUG
    if (Size != 0u)
    {
        App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_DMA, APP_BC95_TRC_SRC_DMA, (int16_t)Size, 0u);
    }
    s_bc95TrcSrcHint = APP_BC95_TRC_SRC_DMA;
#endif /* NBIOT_TRACE_DEBUG */
    App_Bc95AtRxDmaScanAndLatch(Size);
#ifdef NBIOT_TRACE_DEBUG
    s_bc95TrcSrcHint = APP_BC95_TRC_SRC_NONE;
#endif /* NBIOT_TRACE_DEBUG */
    App_Bc95AtRxDmaStart();
}

#if (USE_HAL_UART_REGISTER_CALLBACKS == 1U)
/* 등록형 콜백: ArmUrcMonitor 에서 HAL_UART_RegisterRxEventCallback 로 등록한다 */
#else
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    App_Bc95AtRxDmaRxEventCb(huart, Size);
}
#endif
 
static void App_Bc95AtArmUrcMonitor(UART_HandleTypeDef *p_huart)
{
    if (p_huart == NULL)
    {
        return;
    }
    s_bc95UrcMonitor.p_huart = p_huart;
    if (s_bc95RxDmaArmed == APP_TRUE)
    {
        /* [FIX-RX-2] A TRUE flag alone does not mean the DMA channel is enabled
         * (FinishRxIt abort, HAL error abort). If it is really running, only resync
         * the monitor flag (ErrorIsr clears it) and return; otherwise heal the stale
         * flag and fall through to the normal DMA start. */
        if ((s_bc95RxDmaHandle.Instance != NULL) &&
            ((s_bc95RxDmaHandle.Instance->CCR & DMA_CCR_EN) != 0u))
        {
            s_bc95UrcMonitor.armed = APP_TRUE;
            return;
        }
#ifdef NBIOT_TRACE_DEBUG
        App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_MARK, APP_BC95_TRC_SRC_NONE, 35, 0u);
#endif /* NBIOT_TRACE_DEBUG */
        s_bc95RxDmaArmed = APP_FALSE;
    }
    if (s_bc95RxDmaHandle.Instance == NULL)
    {
        /* [2단계-2차] DMA1 + LPUART1_RX 채널 1회 초기화.
         * STM32L0 DMA는 CSELR 매핑이므로 Init.Request 값이 곧 채널 요청 선택이다. */
        __HAL_RCC_DMA1_CLK_ENABLE();
        (void)memset(&s_bc95RxDmaHandle, 0, sizeof(s_bc95RxDmaHandle));
        s_bc95RxDmaHandle.Instance                 = DMA1_Channel3;
        s_bc95RxDmaHandle.Init.Request             = APP_BC95_RX_DMA_REQUEST;
        s_bc95RxDmaHandle.Init.Direction           = DMA_PERIPH_TO_MEMORY;
        s_bc95RxDmaHandle.Init.PeriphInc           = DMA_PINC_DISABLE;
        s_bc95RxDmaHandle.Init.MemInc              = DMA_MINC_ENABLE;
        s_bc95RxDmaHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        s_bc95RxDmaHandle.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        s_bc95RxDmaHandle.Init.Mode                = DMA_NORMAL;
        s_bc95RxDmaHandle.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
        if (HAL_DMA_Init(&s_bc95RxDmaHandle) != HAL_OK)
        {
            s_bc95UrcMonitor.armed = APP_FALSE;
            return;
        }
        __HAL_LINKDMA(p_huart, hdmarx, s_bc95RxDmaHandle);
#if (USE_HAL_UART_REGISTER_CALLBACKS == 1U)
        if (HAL_UART_RegisterRxEventCallback(p_huart, App_Bc95AtRxDmaRxEventCb) != HAL_OK)
        {
            s_bc95UrcMonitor.armed = APP_FALSE;
            return;
        }
#endif
    }
    s_bc95UrcMonitor.armed = APP_TRUE;
    s_bc95RxDmaArmed       = APP_TRUE;
    App_Bc95AtRxDmaStart();
}

static void App_Bc95AtDisarmUrcMonitor(UART_HandleTypeDef *p_huart)
{
    if ((p_huart == NULL) || (s_bc95UrcMonitor.armed == APP_FALSE))
    {
        return;
    }
    if ((s_bc95UrcMonitor.p_huart != NULL) && (s_bc95UrcMonitor.p_huart != p_huart))
    {
        return;
    }
    /* [2단계-2차] 중단 전 DMA 수신분을 스캔해 EVTIND를 구제한다 -
     * 명령 직전에 도착한 URC가 기존 1바이트 모니터/플러시 경계에서
     * 유실되던 boot14/boot20 패턴을 DMA 버퍼 째로 회수한다. */
    {
        uint16_t rescueLen = (uint16_t)(APP_BC95_AT_RX_DMA_BUF_SIZE -
                                        s_bc95RxDmaHandle.Instance->CNDTR);
        /* [PROBE-3] DMA hardware state at Disarm, and suspected split URC in the rescued bytes */
#ifdef NBIOT_TRACE_DEBUG
        if ((s_bc95RxDmaHandle.Instance->CCR & DMA_CCR_EN) == 0u)
        {
            s_probeDisarmDmaOff++;
        }
        if ((rescueLen != 0u) &&
            (App_Bc95RxProbeTailIsPartialUrc((const char *)s_bc95RxDmaBuf, rescueLen) != 0u))
        {
            s_probeSplitSuspect++;
        }

        if (rescueLen != 0u)
        {
            App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_RESCUE, APP_BC95_TRC_SRC_RESCUE, (int16_t)rescueLen, 0u);
        }
        s_bc95TrcSrcHint = APP_BC95_TRC_SRC_RESCUE;
#endif /* NBIOT_TRACE_DEBUG */
        App_Bc95AtRxDmaScanAndLatch(rescueLen);
#ifdef NBIOT_TRACE_DEBUG
        s_bc95TrcSrcHint = APP_BC95_TRC_SRC_NONE;
#endif /* NBIOT_TRACE_DEBUG */
    }
    (void)HAL_UART_AbortReceive(p_huart);

    s_bc95UrcMonitor.armed = APP_FALSE;
    s_bc95RxDmaArmed       = APP_FALSE;
}

/* ============================================================
 *  RX 컨텍스트 종료 (경쟁 조건 방지를 위한 통합 헬퍼)
 *    - active 를 먼저 false 로 만든 뒤 abort 호출 → ISR 이
 *      추가 IT 등록을 못 하도록 보장
 * ============================================================ */
#ifndef APP_BC95_RX_DMA_RUNNING
/* TRUE when the idle-DMA channel linked to this UART is really enabled */
#define APP_BC95_RX_DMA_RUNNING(h) \
    (((h)->hdmarx != NULL) && ((h)->hdmarx->Instance != NULL) && \
     (((h)->hdmarx->Instance->CCR & DMA_CCR_EN) != 0u))
#endif

static void App_Bc95AtFinishRxIt(UART_HandleTypeDef *p_huart)
{
    g_appBc95AtRxContext.active = APP_FALSE;
    if (p_huart != NULL)
    {
        /* [FIX-RX-1] If the idle-DMA is really running it already owns the receiver:
         * do not abort it (that opened the blind gap between commands). */
        if (APP_BC95_RX_DMA_RUNNING(p_huart))
        {
            return;
        }
        (void)HAL_UART_AbortReceive_IT(p_huart);
        /* 근본 해결: 명령 없는 공백(자가진단/계량기 프로브 등)에도
         * 1바이트 URC 모니터를 유지해 EVTIND 의 HW 레벨 유실을 막는다. */
        App_Bc95AtArmUrcMonitor(p_huart);
    }
}

/* ============================================================
 *  RX 라인 드레인 (송신 직전 호출)
 * ============================================================ */
/* ============================================================================
 * [DFOTA 1.6.3] LwM2M Module DFOTA flow 대응 (bc95 LGU 매뉴얼 1.6절)
 *  +QLWEVTIND:5 = 서버가 DFOTA 개시를 통보하고 delta 다운로드가 시작된다.
 *  매뉴얼 경고: 이 URC 수신 후에는 모듈이 firmware update를 마칠 때까지
 *  AT command 전송/리부트 등 모듈 제어를 멈춰야 한다(REBOOT_CAUSE_SECURITY_
 *  FOTA_UPGRADE 로 모듈이 스스로 리부트).
 *  대응: EVTIND:5 수신 시 DFOTA hold 상태 진입 ->
 *    - 플랫폼 UDP 송신(App_Bc95AtUdpSendOnce) 억제
 *    - 모뎀 SW/HW 리셋(CFUN 토글/전원 차단) 억제
 *    - hold는 모듈 재초기화(App_NBIoTAtInit) 또는 만료 시 해제되며,
 *      부팅 재진입 시 기존 BootTrack이 AT+QGMR 로 버전을 확인한다.
 *  hold 상한은 APP_BC95_DFOTA_HOLD_TIMEOUT_MS (delta 수신 지연 만료 대비).
 * ========================================================================== */
#define APP_BC95_DFOTA_HOLD_TIMEOUT_MS      (30u * 60000u)   /* 30 min */

static volatile uint8_t s_bc95DfotaHoldActive  = 0u;
static volatile uint32_t s_bc95DfotaHoldStartMs = 0u;
static volatile uint8_t  s_bc95DfotaHoldTimeoutLogged = 0u;

static void App_Bc95AtDfotaHoldEnter(void);
static uint8_t App_Bc95AtDfotaHoldActive(void);
static void App_Bc95AtDfotaHoldClear(const char *p_reason);

static void App_Bc95AtDfotaHoldEnter(void)
{
    if (s_bc95DfotaHoldActive == 0u)
    {
        s_bc95DfotaHoldActive = 1u;
        s_bc95DfotaHoldStartMs = HAL_GetTick();
        s_bc95DfotaHoldTimeoutLogged = 0u;
        APP_LOGW("NBIOT", "[[DFOTA]] EVTIND:5 firmware download started -> hold module control (timeout=%lums)",
                 (unsigned long)APP_BC95_DFOTA_HOLD_TIMEOUT_MS);
    }
    else
    {
        APP_LOGD("NBIOT", "[[DFOTA]] EVTIND:5 re-enter while hold already active");
    }
}

static uint8_t App_Bc95AtDfotaHoldActive(void)
{
    if (s_bc95DfotaHoldActive == 0u)
    {
        return APP_FALSE;
    }
    if ((HAL_GetTick() - s_bc95DfotaHoldStartMs) >= APP_BC95_DFOTA_HOLD_TIMEOUT_MS)
    {
        /* 만료: 다운로드가 비정상 지연된 경우. 제어를 회복해 폴백 복구가
         * 가능하도록 한다(모듈이 이미 죽어있으면 기존 부팅 복구 경로가 처리). */
        App_Bc95AtDfotaHoldClear("timeout");
        return APP_FALSE;
    }
    return APP_TRUE;
}

static void App_Bc95AtDfotaHoldClear(const char *p_reason)
{
    if (s_bc95DfotaHoldActive != 0u)
    {
        s_bc95DfotaHoldActive = 0u;
        (void)p_reason;
        APP_LOGN("NBIOT", "[[DFOTA]] hold cleared (%s)", (p_reason != NULL) ? p_reason : "unknown");
    }
}

/* EVTIND 조기 도착 래치:
 * 부팅 트랙(자가진단/계량기 프로브, 수 초) 동안 LwM2M 엔진의 EVTIND:0/3/8 URC가
 * 모뎀에서 먼저 도착할 수 있다. 기존 로직은 ServicePlatformWake의 fast-path 대기가
 * 시작될 때 DrainRxLine으로 수신 버퍼를 비우므로, 그 전에 도착한 EVTIND는 유실되어
 * fast-path가 시간초과 -> QLWSREGIND=0(자동 모드에서 CME 4)로 빠졌다.
 * DrainRxLine이 라인을 버리기 전 EVTIND를 스캔해 마지막 이벤트를 래치하고,
 * fast-path 대기 직전 래치 검사로 즉시 준비완료 판정한다. */
static void App_Bc95AtUpdateEVTINDLatch(const char *p_line)
{
    int eventType = -1;
    if ((p_line == NULL) || (p_line[0] == '\0'))
    {
        return;
    }
    if (App_Bc95AtParseQlwevtind(p_line, &eventType) == APP_BC95_AT_OK)
    {
        /* 스캔 경로(RAW 혼입/루프 스캔)로 수신된 URC도 관측 이력에 반영한다.
         * 명령 진행 중 도착한 URC는 ISR 모니터를 지나지 않으므로, 이 갱신이
         * 없으면 mask에서 해당 이벤트 비트가 누락된다(boot13 실측:
         * :0/:4 가 RAW 혼입 수신됐는데 mask=0x0108 로 비트0/4 누락). */
        {
            /* [FIX-EVTIND-2] Cortex-M0+ 에서 |= 는 load-modify-store 라 비원자적이다.
             * DMA RxEvent(UART IRQ)와 메인 루프 스캔 경로가 같은 mask 를 갱신하므로 보호한다. */
            uint32_t primask = __get_PRIMASK();
#ifdef NBIOT_TRACE_DEBUG
            uint16_t prevMask;
#endif /* NBIOT_TRACE_DEBUG */
            __disable_irq();
#ifdef NBIOT_TRACE_DEBUG
            prevMask = s_bc95EVTINDSeenMask;
#endif /* NBIOT_TRACE_DEBUG */
            if ((eventType >= 0) && (eventType < 16))
            {
                s_bc95EVTINDSeenMask |= (uint16_t)(1u << eventType);
            }
            s_bc95LastEVTIND = eventType;
#ifdef NBIOT_TRACE_DEBUG
            App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_EVT,
                                (s_bc95TrcSrcHint != 0u) ? s_bc95TrcSrcHint : APP_BC95_TRC_SRC_LINE,
                                (int16_t)eventType,
                                (uint8_t)(((eventType >= 0) && (eventType < 16) &&
                                           ((prevMask & (uint16_t)(1u << eventType)) != 0u)) ? 1u : 0u));
#endif /* NBIOT_TRACE_DEBUG */
            __set_PRIMASK(primask);
        }
        /* [DFOTA 1.6.3] EVTIND:5 = DFOTA 다운로드 개시 URC -> hold 진입.
         * 스캔/래치/대기 루프 모든 경로가 이 래치를 거치므로 여기서 1회 처리. */
        if (eventType == 5)
        {
            App_Bc95AtDfotaHoldEnter();
        }
    }
}

static void App_Bc95AtScanBufferForEVTIND(const char *p_buf, uint16_t len)
{
    uint16_t i;
    if ((p_buf == NULL) || (len == 0u))
    {
        return;
    }
    /* [NNMI] EVTIND 스캔과 동일한 경로(응답 혼입/ DMA 잔량)에서 NNMI도 함께 회수 */
    App_Bc95AtScanBufferForNnmi(p_buf, len);
    /* [FIX-EVTIND-3] 이전 버퍼 끝에서 잘린 EVTIND 라인이 있으면 현재 버퍼
     * 선두와 이어 붙여 먼저 판정한다(경계 분할 URC 회수). */
    if (s_bc95EvtScanTailLen > 0u)
    {
        char contBuf[64];
        uint16_t contLen = s_bc95EvtScanTailLen;
        uint16_t k;
        (void)memcpy(contBuf, s_bc95EvtScanTail, s_bc95EvtScanTailLen);
        for (k = 0u; (k < len) && (contLen < (uint16_t)(sizeof(contBuf) - 1u)) &&
             (p_buf[k] != '\r') && (p_buf[k] != '\n'); k++)
        {
            contBuf[contLen++] = p_buf[k];
        }
        contBuf[contLen] = '\0';
        if ((contLen > (uint16_t)APP_BC95_AT_QLWEVTIND_PREFIX_LEN) &&
            (memcmp(contBuf, APP_BC95_AT_QLWEVTIND_PREFIX,
                    APP_BC95_AT_QLWEVTIND_PREFIX_LEN) == 0))
        {
            App_Bc95AtUpdateEVTINDLatch(contBuf);
            if (k > 0u) { i = (uint16_t)(k - 1u); }  /* 소비분 건너뛰기(아래 ++i) */
        }
        s_bc95EvtScanTailLen = 0u;
    }

    /* 라인 단위로 훑어 EVTIND 접두어를 찾는다(버퍼 내 NUL 보존 가정 없음) */
    for (i = 0u; (i + APP_BC95_AT_QLWEVTIND_PREFIX_LEN) <= len; i++)
    {
        if (strncmp(&p_buf[i], APP_BC95_AT_QLWEVTIND_PREFIX,
                    APP_BC95_AT_QLWEVTIND_PREFIX_LEN) == 0)
        {
            char lineBuf[48];
            uint16_t copyLen = 0u;
            /* 접두어(+QLWEVTIND:)부터 복사한다. ParseQlwevtind 는 strstr 로
             * 접두어를 요구하므로, 숫자만 건네면 APP_BC95_AT_ERR_NO_PREFIX 로
             * 항상 실패해 스캔 경로(경로 1/2/3)의 캡처가 전무했다
             * (boot14/15 실측: RAW 혼입 수신된 :4 가 마스크에 누락). */
            uint16_t j = i;
            while ((j < len) && (copyLen < (sizeof(lineBuf) - 1u)) &&
                   (p_buf[j] != '\r') && (p_buf[j] != '\n'))
            {
                lineBuf[copyLen++] = p_buf[j];
                j++;
            }
            lineBuf[copyLen] = '\0';
            /* 뒤에 숫자가 있는지 확인 후 래치 갱신 */
            if (copyLen > 0u)
            {
                App_Bc95AtUpdateEVTINDLatch(lineBuf);
                i = j;  /* 이벤트 값은 숫자 하나뿐이므로 건너뜀 */
            }
        }
    }
    /* [FIX-EVTIND-3] 버퍼 끝이 부분 URC(잘린 접두어 또는 접두어+미완성 본문)면
     * 다음 스캔에서 완성 판정할 수 있게 보존한다. 완성 라인은 위 루프에서
     * 이미 처리되므로 여기는 미완성 꼬리만 남는다. */
    if (App_Bc95RxProbeTailIsPartialUrc(p_buf, len) == 1u)
    {
        uint16_t k;
        uint16_t start = len;
        /* 마지막 라인 시작 위치(직전 CR/LF 뒤)를 찾아 라인 단위로 보존 */
        for (k = len; k > 0u; k--)
        {
            if ((p_buf[k - 1u] == '\r') || (p_buf[k - 1u] == '\n'))
            {
                start = k;
                break;
            }
        }
        if (len > start)
        {
            uint16_t copyLen = (uint16_t)(len - start);
            if (copyLen > (uint16_t)(sizeof(s_bc95EvtScanTail) - 1u))
            {
                copyLen = (uint16_t)(sizeof(s_bc95EvtScanTail) - 1u);
            }
            (void)memcpy(s_bc95EvtScanTail, &p_buf[len - copyLen], copyLen);
            s_bc95EvtScanTailLen = copyLen;
        }
    }
    else
    {
        s_bc95EvtScanTailLen = 0u;
    }
}

/* [FIX-EVTIND-3] 스캔 경로 외부(세션 경계 등)에서 tail을 비워
 * 이전 세션 조각이 다음 세션 판정을 오염시키지 않게 한다. */
static void App_Bc95AtScanBufferForEVTINDCont(void)
{
    s_bc95EvtScanTailLen = 0u;

}

/* ============================================================================
 * [DL-ACK 분류 2026-11-04] service/mgmt 공용 DL ACK 분류·카운트 루틴
 * 프레임 구조: [0xA3][msgLen][cmd][payload...][checksum]
 *   - msgLen = cmd + payload 길이 (전체 길이 - 3)
 *   - checksum = header부터 payload까지 합산 하위 8비트
 * +NNMI로 수신되는 모든 프레임(대기 중 여부 무관, unsolicited 포함)을
 * 수신 시점에 1회 검증·분류해 cmd별 카운터에 누적한다. service와 mgmt가
 * 같은 테이블을 공유하며, mgmt 쪽 추가 ack 명령은 App_DlAckRegisterCmd로
 * 등록만 하면 카운트된다. A3 프레임이 아닌 응답(예: 에코 페이로드)은
 * 분류되지 않고 카운트되지 않는다(ack 부재 = 정상 경로).
 * ========================================================================== */
#define APP_DL_ACK_TABLE_MAX                (8u)
#define APP_DL_ACK_NAME_MAX                 (12u)
#define APP_DL_ACK_FRAME_HEADER             (0xA3u)

typedef struct
{
    uint8_t           cmd[APP_DL_ACK_TABLE_MAX];
    char              name[APP_DL_ACK_TABLE_MAX][APP_DL_ACK_NAME_MAX];
    volatile uint32_t count[APP_DL_ACK_TABLE_MAX];
    uint8_t           used;
} AppDlAckTable_t;

static AppDlAckTable_t s_appDlAckTable;
static uint8_t         s_appDlAckInitDone = 0u;

/* 기본 등록(service): 6.5 리셋 / 6.6 주기 변경 / 6.7 보고 시간간격 분산.
 * mgmt 명령은 부팅 초기화 시점에 App_DlAckRegisterCmd로 추가 등록한다. */
void App_DlAckInit(void)
{
    (void)memset((void *)&s_appDlAckTable, 0, sizeof(s_appDlAckTable));
    s_appDlAckInitDone = 1u;
    (void)App_DlAckRegisterCmd(0x10u, "ack");      /* [2026-11-05] 6.8 서버 ACK — 응답 확인용, 실행 없음 */
    (void)App_DlAckRegisterCmd(0x50u, "reset");
    (void)App_DlAckRegisterCmd(0x51u, "period");
    (void)App_DlAckRegisterCmd(0x52u, "spread");
    (void)App_DlAckRegisterCmd(APP_DL_CMD_FW_BEGIN, "fw_begin");   /* [FW-UPD] 6.9 */
}

AppStatus_t App_DlAckRegisterCmd(uint8_t cmd, const char *p_name)
{
    uint8_t i;

    if (s_appDlAckInitDone == 0u)
    {
        /* 재귀 방지: Init 내부 등록은 initDone 선행 설정으로 직접 통과 */
        App_DlAckInit();
    }
    if (p_name == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }
    for (i = 0u; i < s_appDlAckTable.used; i++)
    {
        if (s_appDlAckTable.cmd[i] == cmd)
        {
            (void)strncpy(s_appDlAckTable.name[i], p_name, APP_DL_ACK_NAME_MAX - 1u);
            s_appDlAckTable.name[i][APP_DL_ACK_NAME_MAX - 1u] = '\0';
            return APP_STATUS_OK;   /* 재등록 = 이름 갱신 (카운트 유지) */
        }
    }
    if (s_appDlAckTable.used >= APP_DL_ACK_TABLE_MAX)
    {
        return APP_STATUS_BUFFER_OVERFLOW;
    }
    s_appDlAckTable.cmd[s_appDlAckTable.used] = cmd;
    (void)strncpy(s_appDlAckTable.name[s_appDlAckTable.used], p_name, APP_DL_ACK_NAME_MAX - 1u);
    s_appDlAckTable.name[s_appDlAckTable.used][APP_DL_ACK_NAME_MAX - 1u] = '\0';
    s_appDlAckTable.used++;
    return APP_STATUS_OK;
}

/* 프레임 구조 검증 + cmd 추출. 유효하지 않으면 오류 반환(카운트 안 함). */
AppStatus_t App_DlAckClassifyFrame(const uint8_t *p_frame, uint16_t len, uint8_t *p_cmdOut)
{
    uint16_t i;
    uint8_t  checksum = 0u;

    if ((p_frame == NULL) || (p_cmdOut == NULL) || (len < 4u))
    {
        return APP_STATUS_INVALID_PARAM;   /* 최소: header+msgLen+cmd+checksum */
    }
    if (p_frame[0] != APP_DL_ACK_FRAME_HEADER)
    {
        return APP_STATUS_INVALID_PARAM;   /* A3 프레임 아님 (에코/기타 응답) */
    }
    if (p_frame[1] != (uint8_t)(len - 3u))
    {
        return APP_STATUS_INVALID_PARAM;   /* msgLen 불일치 (절단/변형) */
    }
    for (i = 0u; i < (uint16_t)(len - 1u); i++)
    {
        checksum = (uint8_t)(checksum + p_frame[i]);
    }
    if (checksum != p_frame[len - 1u])
    {
        return APP_STATUS_INVALID_PARAM;   /* checksum 오류 */
    }
    *p_cmdOut = p_frame[2];
    return APP_STATUS_OK;
}

/* 수신 프레임 1회 카운트. 분류 실패 시 무시.
 * isServicePath=APP_TRUE일 때만 service appAck 총계에도 누적한다 -
 * mgmt 경로에서 호출하면 cmd별 카운트만 올라가고 service 통계는 불변.
 * IRQ 컨텍스트에서 호출될 수 있으므로 PRIMASK 보호(기존 래치 패턴 동일). */
void App_DlAckCountFrame(const uint8_t *p_frame, uint16_t len, uint8_t isServicePath)
{
    uint8_t  cmd = 0u;
    uint8_t  i;
    uint32_t primask;

    if (s_appDlAckInitDone == 0u)
    {
        App_DlAckInit();
    }
    if (App_DlAckClassifyFrame(p_frame, len, &cmd) != APP_STATUS_OK)
    {
        return;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0u; i < s_appDlAckTable.used; i++)
    {
        if (s_appDlAckTable.cmd[i] == cmd)
        {
            s_appDlAckTable.count[i]++;
            break;
        }
    }
    if (isServicePath == APP_TRUE)
    {
        s_appNbiotServiceAppAckCount++;
    }
    if (primask == 0u) { __enable_irq(); }
}

uint32_t App_DlAckGetCount(uint8_t cmd)
{
    uint8_t i;

    for (i = 0u; i < s_appDlAckTable.used; i++)
    {
        if (s_appDlAckTable.cmd[i] == cmd)
        {
            return s_appDlAckTable.count[i];
        }
    }
    return 0u;
}

const char *App_DlAckNameOf(uint8_t cmd)
{
    uint8_t i;

    for (i = 0u; i < s_appDlAckTable.used; i++)
    {
        if (s_appDlAckTable.cmd[i] == cmd)
        {
            return s_appDlAckTable.name[i];
        }
    }
    return "n/a";
}

/* [NNMI] 라인 파서: "+NNMI:<len>,<hex>" → 바이트 래치.
 * RxEvent 콜백(UART IRQ)에서도 호출되므로 래치 갱신은 PRIMASK 보호(FIX-EVTIND-2와 동일).
 * 중복 라인은 마지막 라인과 비교해 무시 — DMA 재스캔/응답 버퍼 반복 스캔 중복 방지.
 * DMA 버퍼 경계에서 URC가 갈라지는 경우는 미회수(기존 EVTIND 스캔과 동일 한계). */
static void App_Bc95AtUpdateNnmiLatch(const char *p_line)
{
    const char *p_data;
    long parsedLen;
    int hexLen;
    int byteIdx;
    uint8_t decoded[APP_BC95_NNMI_LATCH_MAX];

    if ((p_line == NULL) || (p_line[0] == '\0'))
    {
        return;
    }
    p_data = strstr(p_line, APP_BC95_AT_NNMI_PREFIX);
    if (p_data == NULL)
    {
        return;
    }
    p_data += APP_BC95_AT_NNMI_PREFIX_LEN;
    parsedLen = strtol(p_data, (char **)&p_data, 10);   /* <len> */
    if ((parsedLen <= 0) || (parsedLen > (long)APP_BC95_NNMI_LATCH_MAX))
    {
        return;   /* 비정상 길이 — 무시 */
    }
    if (p_data[0] != ',')
    {
        return;
    }
    p_data++;
    hexLen = 0;
    while ((p_data[hexLen] != '\0') && (p_data[hexLen] != '\r') && (p_data[hexLen] != '\n'))
    {
        hexLen++;
    }
    if (hexLen < (int)(2 * parsedLen))
    {
        return;   /* 데이터 잘림 — 불완전 라인 폐기(이후 스캔에서 완성본 회수) */
    }
    byteIdx = 0;
    while (byteIdx < (int)parsedLen)
    {
        char hexPair[3];
        hexPair[0] = p_data[2 * byteIdx];
        hexPair[1] = p_data[(2 * byteIdx) + 1];
        hexPair[2] = '\0';
        decoded[byteIdx] = (uint8_t)strtoul(hexPair, NULL, 16);
        byteIdx++;
    }

    {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        if ((s_bc95NnmiLastLine[0] != '\0') &&
            (strncmp(p_line, s_bc95NnmiLastLine, APP_BC95_NNMI_LINE_MAX) == 0))
        {
            if (primask == 0u) { __enable_irq(); }
            return;   /* 동일 라인 재스캔 — 무시 */
        }
        (void)strncpy(s_bc95NnmiLastLine, p_line, APP_BC95_NNMI_LINE_MAX - 1u);
        s_bc95NnmiLastLine[APP_BC95_NNMI_LINE_MAX - 1u] = '\0';
        (void)memcpy((void *)s_bc95LastNnmiBuf, decoded, (uint32_t)parsedLen);
        s_bc95LastNnmiLen = (uint16_t)parsedLen;
        s_bc95NnmiCount++;
        /* [DL-ACK] cmd별 분리 카운트 — service/mgmt 공용, 대기 중이 아니어도 집계 */
        App_DlAckCountFrame(decoded, (uint16_t)parsedLen, APP_TRUE);
        if (primask == 0u) { __enable_irq(); }
    }
}

/* [NNMI] 버퍼 스캐너: EVTIND 스캐너와 동일 구조, 접두어만 +NNMI: */
static void App_Bc95AtScanBufferForNnmi(const char *p_buf, uint16_t len)
{
    uint16_t i;
    if ((p_buf == NULL) || (len == 0u))
    {
        return;
    }
    for (i = 0u; (i + APP_BC95_AT_NNMI_PREFIX_LEN) <= len; i++)
    {
        if (strncmp(&p_buf[i], APP_BC95_AT_NNMI_PREFIX, APP_BC95_AT_NNMI_PREFIX_LEN) == 0)
        {
            char lineBuf[APP_BC95_NNMI_LINE_MAX];
            uint16_t copyLen = 0u;
            uint16_t j = i;
            while ((j < len) && (copyLen < (sizeof(lineBuf) - 1u)) &&
                   (p_buf[j] != '\r') && (p_buf[j] != '\n'))
            {
                lineBuf[copyLen++] = p_buf[j];
                j++;
            }
            lineBuf[copyLen] = '\0';
            if (copyLen > APP_BC95_AT_NNMI_PREFIX_LEN)
            {
                App_Bc95AtUpdateNnmiLatch(lineBuf);
                i = j;   /* 라인 끝까지 건너뜀 (hex 데이터 재매칭 방지) */
            }
        }
    }
}

/* [NNMI] 대기 루프용 강제 스캔: idle-DMA 버퍼 신규 유입분만 순차 스캔.
 * RxEvent(idle) 콜백이 놓치는 구간(바이트 연속 유입 중)까지 커버. */
static void App_Bc95AtNnmiScanDmaBuf(void)
{
    uint16_t len;
    if ((s_bc95RxDmaHandle.Instance == NULL) || (s_bc95RxDmaArmed != APP_TRUE))
    {
        return;
    }
    len = (uint16_t)(APP_BC95_AT_RX_DMA_BUF_SIZE - s_bc95RxDmaHandle.Instance->CNDTR);
    if (len > (uint16_t)APP_BC95_AT_RX_DMA_BUF_SIZE)
    {
        len = (uint16_t)APP_BC95_AT_RX_DMA_BUF_SIZE;
    }
    if (len > s_bc95NnmiDmaScanPos)
    {
        App_Bc95AtScanBufferForNnmi((const char *)s_bc95RxDmaBuf, len);
        s_bc95NnmiDmaScanPos = len;
    }
    else if (len < s_bc95NnmiDmaScanPos)
    {
        /* DMA 재무장(랩어라운드): 커서/중복라인 캐시 리셋 */
        s_bc95NnmiDmaScanPos = 0u;
        {
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            s_bc95NnmiLastLine[0] = '\0';
            if (primask == 0u) { __enable_irq(); }
        }
        if (len > 0u)
        {
            App_Bc95AtScanBufferForNnmi((const char *)s_bc95RxDmaBuf, len);
            s_bc95NnmiDmaScanPos = len;
        }
    }
}

/* [FW-UPD 6.9] BootDlMeta를 META page2(0x0802F000)에 기록. 부트와 구조체 공유
 * (Boot/Inc/boot_udp_dl.h — include path에 Boot/Inc 존재). 페이지 지움+word 프로그램. */
static AppStatus_t App_DlFwBeginSaveMeta(const BootDlMeta_t *p_meta)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t pageError = 0u;
    const uint32_t *p_word = (const uint32_t *)p_meta;
    uint32_t i;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = BOOT_DL_META_PAGE2;
    erase.NbPages = 1u;
    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        APP_LOGE("NBIOT", "[[FW-UPD]] DlMeta erase fail (err=%lu)", (unsigned long)pageError);
        return APP_STATUS_INIT_FAILED;
    }
    for (i = 0u; i < (sizeof(BootDlMeta_t) / 4u); i++)
    {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, BOOT_DL_META_PAGE2 + (i * 4u), p_word[i]) != HAL_OK)
        {
            (void)HAL_FLASH_Lock();
            APP_LOGE("NBIOT", "[[FW-UPD]] DlMeta program fail (idx=%lu)", (unsigned long)i);
            return APP_STATUS_INIT_FAILED;
        }
    }
    (void)HAL_FLASH_Lock();
    return APP_STATUS_OK;
}

static void App_DlCmdExecute(const uint8_t *p_frame, uint16_t len, uint8_t isServicePath)
{
    uint8_t cmd = 0u;
    AppMeterServerFormatOptions_t opt;
    AppStatus_t optStatus;

    if (App_DlAckClassifyFrame(p_frame, len, &cmd) != APP_STATUS_OK)
    {
        return;   /* 분류 실패 프레임은 실행하지 않는다(카운트만) */
    }

    switch (cmd)
    {
        case APP_DL_CMD_RESET:
            APP_LOGN("NBIOT", "[[DlCmd]] reset(6.5) requested -> system reset in %u ms",
                     (unsigned)APP_DL_CMD_RESET_DELAY_MS);
            App_Bc95AtDelayWithFeed(APP_DL_CMD_RESET_DELAY_MS);
            HAL_NVIC_SystemReset();   /* 반환 없음 */
            break;   /* 도달하지 않음(방어) */

        case APP_DL_CMD_PERIOD:
            if (len < 6u)
            {
                APP_LOGW("NBIOT", "[[DlCmd]] period(6.6) payload short (len=%u) -> ignore", (unsigned)len);
                break;
            }
            optStatus = App_MeterServerOptionsLoad(&opt);
            if ((optStatus != APP_STATUS_OK) && (optStatus != APP_STATUS_NOT_INITIALIZED))
            {
                APP_LOGW("NBIOT", "[[DlCmd]] period(6.6) options load fail (%d) -> ignore", (int)optStatus);
                break;
            }
            opt.meteringPeriodHours = p_frame[3];   /* 측정 주기(h) */
            opt.reportingPeriodHours = p_frame[4];  /* 보고 주기(h) */
            if (App_MeterServerOptionsSave(&opt) == APP_STATUS_OK)
            {
                APP_LOGN("NBIOT", "[[DlCmd]] period(6.6) applied meter=%uh report=%uh (saved)",
                         (unsigned)opt.meteringPeriodHours, (unsigned)opt.reportingPeriodHours);
            }
            else
            {
                APP_LOGW("NBIOT", "[[DlCmd]] period(6.6) save FAILED (meter=%uh report=%uh)",
                         (unsigned)opt.meteringPeriodHours, (unsigned)opt.reportingPeriodHours);
            }
            break;

        case APP_DL_CMD_SPREAD:
            if (len < 5u)
            {
                APP_LOGW("NBIOT", "[[DlCmd]] spread(6.7) payload short (len=%u) -> ignore", (unsigned)len);
                break;
            }
            optStatus = App_MeterServerOptionsLoad(&opt);
            if ((optStatus != APP_STATUS_OK) && (optStatus != APP_STATUS_NOT_INITIALIZED))
            {
                APP_LOGW("NBIOT", "[[DlCmd]] spread(6.7) options load fail (%d) -> ignore", (int)optStatus);
                break;
            }
            opt.reportingSpreadHours = p_frame[3];  /* 분산 간격(h) */
            if (opt.reportingSpreadHours > opt.reportingPeriodHours)
            {
                opt.reportingSpreadHours = opt.reportingPeriodHours;   /* 명세 규칙: 보고주기 이하 */
            }
            if (App_MeterServerOptionsSave(&opt) == APP_STATUS_OK)
            {
                APP_LOGN("NBIOT", "[[DlCmd]] spread(6.7) applied spread=%uh (saved)",
                         (unsigned)opt.reportingSpreadHours);
            }
            else
            {
                APP_LOGW("NBIOT", "[[DlCmd]] spread(6.7) save FAILED (spread=%uh)",
                         (unsigned)opt.reportingSpreadHours);
            }
            break;

        case APP_DL_CMD_FW_BEGIN:
        {
            /* 프레임: [0xA3][msgLen=17][0x54][ver2][size4][crc4][ip4][port2][crc8] */
            if (len < 20u)
            {
                APP_LOGW("NBIOT", "[[DlCmd]] fw_begin(6.9) payload short (len=%u) -> ignore", (unsigned)len);
                break;
            }
            {
                BootDlMeta_t meta;
                uint8_t i;
                (void)memset((void *)&meta, 0, sizeof(meta));
                meta.magic = BOOT_DL_MAGIC;
                meta.versionMajor = p_frame[3];
                meta.versionMinor = p_frame[4];
                meta.imageSize = ((uint32_t)p_frame[5] << 24) | ((uint32_t)p_frame[6] << 16) |
                                 ((uint32_t)p_frame[7] << 8) | (uint32_t)p_frame[8];
                meta.imageCrc = ((uint32_t)p_frame[9] << 24) | ((uint32_t)p_frame[10] << 16) |
                                ((uint32_t)p_frame[11] << 8) | (uint32_t)p_frame[12];
                for (i = 0u; i < 4u; i++) { meta.serverIp[i] = p_frame[13 + i]; }
                meta.serverPort = (uint16_t)(((uint16_t)p_frame[17] << 8) | (uint16_t)p_frame[18]);
                meta.active = 1u;
                meta.failCount = 0u;
                meta.recvOffset = 0u;   /* FW_BEGIN 중복 수신 시 풀재시도 */
                if (App_DlFwBeginSaveMeta(&meta) != APP_STATUS_OK)
                {
                    APP_LOGE("NBIOT", "[[DlCmd]] fw_begin(6.9) meta save FAIL -> ignore (no reset)");
                    break;
                }
                APP_LOGN("NBIOT", "[[DlCmd]] fw_begin(6.9) v%u.%u size=%lu crc=0x%08lX ip=%u.%u.%u.%u:%u -> reset in %u ms",
                         (unsigned)meta.versionMajor, (unsigned)meta.versionMinor,
                         (unsigned long)meta.imageSize, (unsigned long)meta.imageCrc,
                         meta.serverIp[0], meta.serverIp[1], meta.serverIp[2], meta.serverIp[3],
                         (unsigned)meta.serverPort, (unsigned)APP_DL_CMD_RESET_DELAY_MS);
                App_Bc95AtDelayWithFeed(APP_DL_CMD_RESET_DELAY_MS);
                HAL_NVIC_SystemReset();   /* 반환 없음 */
            }
            break;   /* 도달하지 않음(방어) */
        }

        default:
            if ((cmd >= APP_DL_CMD_MGMT_RESERVED_MIN) && (cmd <= APP_DL_CMD_MGMT_RESERVED_MAX))
            {
                /* [mgmt reserved] 예약 영역 cmd - 동작 미정의, 로그만 남기고 무동작 */
                APP_LOGN("NBIOT", "[[DlCmd]] mgmt reserved cmd=0x%02X(%s) len=%u -> no-op",
                         (unsigned)cmd, App_DlAckNameOf(cmd), (unsigned)len);
                break;
            }
            APP_LOGN("NBIOT", "[[DlCmd]] unhandled cmd=0x%02X(%s) len=%u",
                     (unsigned)cmd, App_DlAckNameOf(cmd), (unsigned)len);
            break;
    }
}

static AppStatus_t App_Bc95AtWaitServiceAckNnmi(uint8_t seqNum, uint32_t timeoutMs, uint32_t pollMs)
{
#if (APP_POLICY_WAIT_SERVICE_ACK_ENABLE != APP_TRUE)
    (void)seqNum;
    APP_LOGW("NBIOT", "[[ServicePlatformWake]] skip service ACK(+NNMI) wait");
    return APP_STATUS_OK;
#else
    uint32_t startTick;
    uint32_t beforeCount = s_bc95NnmiCount;
    uint16_t frameLen = 0u;
    uint8_t  frameBuf[APP_BC95_NNMI_LATCH_MAX];
    char     hexLog[(APP_BC95_NNMI_LATCH_MAX * 2u) + 1u];
    char     waitLineBuf[APP_BC95_AT_RX_BUF_SIZE];
    uint16_t i;

    APP_LOGD("NBIOT", "[[ServicePlatformWake]] wait service ACK(+NNMI) timeout=%lu ms poll=%lu ms",
             (unsigned long)timeoutMs, (unsigned long)pollMs);
    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < timeoutMs)
    {
        APP_WWDGFeed();
        App_Bc95AtNnmiScanDmaBuf();   /* idle-DMA 신규분 강제 스캔 */
        if (s_bc95NnmiCount != beforeCount)
        {
            /* 래치 소비 — IRQ 보호 복사 */
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            frameLen = s_bc95LastNnmiLen;
            if (frameLen > (uint16_t)APP_BC95_NNMI_LATCH_MAX)
            {
                frameLen = (uint16_t)APP_BC95_NNMI_LATCH_MAX;
            }
            (void)memcpy(frameBuf, (const void *)s_bc95LastNnmiBuf, (uint32_t)frameLen);
            s_bc95LastNnmiLen = 0u;
            if (primask == 0u) { __enable_irq(); }

            for (i = 0u; (i < frameLen) && (((i * 2u) + 2u) < sizeof(hexLog)); i++)
            {
                (void)snprintf(&hexLog[i * 2u], 3u, "%02X", (unsigned)frameBuf[i]);
            }
            hexLog[i * 2u] = '\0';
            {
                uint8_t ackCmd = 0u;
                if (App_DlAckClassifyFrame(frameBuf, frameLen, &ackCmd) == APP_STATUS_OK)
                {
                    /* [DL 명령 실행] 분류 통과 프레임 = 실제 명령 - 즉시 수행 */
                    App_DlCmdExecute(frameBuf, frameLen, APP_TRUE);
                    /* [SERVICE-STAT-LOG] ACK 수신 성공판 (요약은 결과 로그 뒤) */
                    APP_LOGN("NBIOT", "[[ServicePlatformWake]] service ACK received len=%u cmd=0x%02X(%s) data=%s",
                             (unsigned)frameLen, (unsigned)ackCmd, App_DlAckNameOf(ackCmd), hexLog);
                }
                else
                {
                    /* A3 프레임이 아닌 응답(에코 등) — 카운트 없이 원본만 기록 */
                    APP_LOGN("NBIOT", "[[ServicePlatformWake]] service ACK received len=%u cmd=n/a data=%s",
                             (unsigned)frameLen, hexLog);

                    /* [SERVICE-STAT-LOG] 트랜잭션 단위 service 통계 요약 (결과 로그 뒤 1행):
                     * try=누적 전송 시도, platform4=플랫폼 1차 ACK(QLWULDATASTATUS=4) 수신 여부,
                     * appAck=서버 애플리케이션 ACK(+NNMI) 수신 여부. */
                    APP_LOGN("NBIOT", "[[ServiceStat]] try=%u seq=%u platform4=1 appAck=1",
                             (unsigned)(s_appNbiotServiceTxAttempts > 0u ? s_appNbiotServiceTxAttempts : 1u),
                             (unsigned)seqNum);
                }
            }
            return APP_STATUS_OK;
        }
        /* [v19 NNMI 유실 방지 2026-11-04] 기존 DelayWithFeed(poll) 동안은
         * Receive_IT가 비활성이라 +NNMI 바이트가 HW에 쌓이다 ORE로 유실됐다.
         * 라인 대기로 전환해 대기 시간 전체를 수신 가능 구간으로 만든다.
         * WaitForLinePrefix는 (1) 선두에서 잔량 스캔→drain (2) 대기 중 모든 라인에서
         * EVTIND/NNMI 래치 스캔을 수행하므로, NNMI 라인은 도착 즉시 래치되고
         * 래치 카운터 폴링(위)이 회수한다. 타임아웃은 외부 while이 담당. */
        (void)App_Bc95AtWaitForLinePrefix(APP_BC95_AT_NNMI_PREFIX, pollMs,
                                          waitLineBuf, (uint16_t)sizeof(waitLineBuf));
    }
    APP_LOGW("NBIOT", "[[ServicePlatformWake]] service ACK(+NNMI) timeout (platform OK seq=%u, app ACK absent)", (unsigned)seqNum);
    /* [SERVICE-STAT-LOG] 트랜잭션 단위 service 통계 요약 (결과 로그 뒤 1행) */
    APP_LOGN("NBIOT", "[[ServiceStat]] try=%u seq=%u platform4=1 appAck=0",
             (unsigned)(s_appNbiotServiceTxAttempts > 0u ? s_appNbiotServiceTxAttempts : 1u),
             (unsigned)seqNum);
    return APP_STATUS_OK;
#endif
}


/* ISR 바이트 단위 FSM: "+QLWEVTIND:<n>" 만 골라 래치. ISR 안에서
 * 로그/문자열 함수를 부르지 않는다(비용 최소화). */
static void App_Bc95AtUrcMonitorFeedIsr(uint8_t byteValue)
{
    const char *prefix = APP_BC95_AT_QLWEVTIND_PREFIX;

    if (s_bc95UrcMonitor.inNumber != 0u)
    {
        if ((byteValue >= (uint8_t)'0') && (byteValue <= (uint8_t)'9'))
        {
            s_bc95UrcMonitor.number = (uint16_t)((s_bc95UrcMonitor.number * 10u) +
                                                 (uint16_t)(byteValue - (uint8_t)'0'));
            if (s_bc95UrcMonitor.number > 999u)
            {
                /* 비정상적으로 긴 번호 - FSM 리셋 */
                s_bc95UrcMonitor.inNumber = 0u;
                s_bc95UrcMonitor.matchIdx = 0u;
                s_bc95UrcMonitor.number   = 0u;
            }
            return;
        }
        if ((byteValue == '\r') || (byteValue == '\n'))
        {
#ifdef NBIOT_TRACE_DEBUG
            uint16_t prevMaskIsr = s_bc95EVTINDSeenMask;
#endif /* NBIOT_TRACE_DEBUG */
            s_bc95LastEVTIND = (int)s_bc95UrcMonitor.number;
            if (s_bc95UrcMonitor.number < 16u)
            {
                s_bc95EVTINDSeenMask |= (uint16_t)(1u << s_bc95UrcMonitor.number);
            }
#ifdef NBIOT_TRACE_DEBUG
            App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_EVT, APP_BC95_TRC_SRC_ISR,
                                (int16_t)s_bc95UrcMonitor.number,
                                (uint8_t)(((s_bc95UrcMonitor.number < 16u) &&
                                           ((prevMaskIsr & (uint16_t)(1u << s_bc95UrcMonitor.number)) != 0u)) ? 1u : 0u));
            s_bc95LatchIsrPending = 1u;
#endif /* NBIOT_TRACE_DEBUG */
        }
        s_bc95UrcMonitor.inNumber = 0u;
        s_bc95UrcMonitor.matchIdx = 0u;
        s_bc95UrcMonitor.number   = 0u;
        return;
    }

    if (byteValue == (uint8_t)prefix[s_bc95UrcMonitor.matchIdx])
    {
        s_bc95UrcMonitor.matchIdx++;
        if (s_bc95UrcMonitor.matchIdx >= APP_BC95_AT_QLWEVTIND_PREFIX_LEN)
        {
            s_bc95UrcMonitor.matchIdx = 0u;
            s_bc95UrcMonitor.inNumber = 1u;
            s_bc95UrcMonitor.number   = 0u;
        }
    }
    else
    {
        /* 리싱크 내성: 명령 시작 시점의 모니터 해제(abort)+플러시로 URC 선두
         * 바이트(예: '+')가 유실될 수 있다(boot14 실측: :4 만 래치 누락).
         * 접두어 문자가 모두 고유하므로, 현재 바이트가 접두어 내 어느 위치와
         * 일치하는지 뒤에서부터 찾아 그 다음 위치부터 재개한다 - 선두 유실에도
         * 매칭이 살아난다. 최대 11회 비교, ISR 비용 미미. */
        uint8_t j = APP_BC95_AT_QLWEVTIND_PREFIX_LEN;
        s_bc95UrcMonitor.matchIdx = 0u;
        while (j > 0u)
        {
            j--;
            if (byteValue == (uint8_t)prefix[j])
            {
                s_bc95UrcMonitor.matchIdx = (uint8_t)(j + 1u);
                break;
            }
        }
    }
}

static void App_Bc95AtDrainRxLine(UART_HandleTypeDef *p_huart, uint32_t drainMs)
{
    uint32_t guardMs;
    uint8_t  rescueIdx;
    uint8_t  dmaOwnsRx;

    if (p_huart == NULL) return;

    App_Bc95AtFinishRxIt(p_huart);
    /* [FIX-RX-4] After FinishRxIt the idle-DMA either kept running (no abort) or was
     * restarted by it, so it owns the receiver: skip the RDR rescue and FLUSH below. */
    dmaOwnsRx = APP_BC95_RX_DMA_RUNNING(p_huart) ? APP_TRUE : APP_FALSE;

    /* [2단계-1차] 클리어 전 오류 플래그 적산 - 유실 웨이크에서 ORE 유무 실증 */
    if (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_ORE) != RESET) { s_bc95RxErrOreCount++; }
    if (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_FE)  != RESET) { s_bc95RxErrFeCount++;  }
    if (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_NE)  != RESET) { s_bc95RxErrNeCount++;  }
    if (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_PE)  != RESET) { s_bc95RxErrPeCount++;  }

    /* [PROBE-1] right after FinishRxIt (abort): is the DMA channel really enabled? flag says armed. */
#ifdef NBIOT_TRACE_DEBUG
    s_probeDrainTick = HAL_GetTick();
    if (s_probeDrainTick == 0u) { s_probeDrainTick = 1u; }
    s_probeCmds++;
    if ((s_bc95RxDmaHandle.Instance != NULL) &&
        ((s_bc95RxDmaHandle.Instance->CCR & DMA_CCR_EN) != 0u))
    {
        s_probeDmaOnAtDrain++;
    }
    else
    {
        s_probeDmaOffAtDrain++;
        if (s_bc95RxDmaArmed == APP_TRUE)
        {
            s_probeDmaFlagMismatch++;
        }
    }
#endif /* NBIOT_TRACE_DEBUG */

    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);

    /* [보완3] 플러시 전에 HW 수신기(RDR)에 남은 바이트를 구제해 URC 모니터에
     * 먹인다 - abort 와 flush 사이에 남은 EVTIND 선두/본체 바이트가 플러시로
     * 버려지는 것(boot14 실측, resync 주석의 선두 유실)을 줄인다.
     * ISR 이 먼저 가져갔으면 RXNE 가 이미 클리어되어 무동작(이중 소비 없음). */
    /* [2단계-1차] 1바이트 -> 최대 16바이트 루프 확장: 오버런 직전 RDR에
     * 여러 바이트가 쌓인 경우에도 EVTIND 본체 전체가 살아난다.
     * ISR 이 먼저 가져갔으면 RXNE 가 클리어되어 즉시 종료(이중 소비 없음). */
    for (rescueIdx = 0u; rescueIdx < 16u; rescueIdx++)
    {
        if ((dmaOwnsRx == APP_TRUE) ||
            (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_RXNE) == RESET))
        {
            break;
        }
        App_Bc95AtUrcMonitorFeedIsr((uint8_t)(p_huart->Instance->RDR & 0xFFu));
    }

    /* [FIX-RX-4] With the DMA running, FLUSH would drop bytes the DMA should receive. */
    if (dmaOwnsRx == APP_FALSE)
    {
        __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
    }

    g_appBc95AtRxContext.error = APP_FALSE;
    guardMs = drainMs;
    if (guardMs > APP_BC95_AT_DRAIN_GUARD_MS)
    {
        guardMs = APP_BC95_AT_DRAIN_GUARD_MS;
    }
    if (guardMs > 0u)
    {
        App_Bc95AtDelayWithFeed(guardMs);
    }
}

/* ============================================================
 *  AT 응답 토큰 검출 헬퍼
 * ============================================================ */
static uint8_t App_Bc95AtIsLineStart(const char *p_buf, const char *p_pos)
{
    if (p_pos == p_buf) return APP_TRUE;
    if ((*(p_pos - 1) == '\r') || (*(p_pos - 1) == '\n')) return APP_TRUE;
    return APP_FALSE;
}

static uint8_t App_Bc95AtIsLineEnd(char ch)
{
    if ((ch == '\0') || (ch == '\r') || (ch == '\n')) return APP_TRUE;
    return APP_FALSE;
}

static const char *App_Bc95AtFindOkBoundary(const char *p_resp)
{
    const char *p_scan = p_resp;
    const char *p_token;

    while (*p_scan != '\0')
    {
        p_token = strstr(p_scan, APP_BC95_AT_OK_TOKEN);
        if (p_token == NULL) return NULL;

        if ((App_Bc95AtIsLineStart(p_resp, p_token) == APP_TRUE) &&
            (App_Bc95AtIsLineEnd(*(p_token + APP_BC95_AT_OK_TOKEN_LEN)) == APP_TRUE))
        {
            return p_token;
        }
        p_scan = p_token + APP_BC95_AT_OK_TOKEN_LEN;
    }
    return NULL;
}

static uint8_t App_Bc95AtHasErrorToken(const char *p_resp)
{
    const char *p_scan = p_resp;
    const char *p_token;

    while (*p_scan != '\0')
    {
        p_token = strstr(p_scan, APP_BC95_AT_ERROR_TOKEN);
        if (p_token == NULL) return APP_FALSE;

        if ((App_Bc95AtIsLineStart(p_resp, p_token) == APP_TRUE) &&
            (App_Bc95AtIsLineEnd(*(p_token + APP_BC95_AT_ERROR_TOKEN_LEN)) == APP_TRUE))
        {
            return APP_TRUE;
        }
        p_scan = p_token + APP_BC95_AT_ERROR_TOKEN_LEN;
    }
    return APP_FALSE;
}

static const char *App_Bc95AtFindCgsnPrefix(const char *p_resp)
{
    const char *p_scan = p_resp;
    while (*p_scan != '\0')
    {
        if ((p_scan[0] == '+') &&
            ((p_scan[1] == 'C') || (p_scan[1] == 'c')) &&
            ((p_scan[2] == 'G') || (p_scan[2] == 'g')) &&
            ((p_scan[3] == 'S') || (p_scan[3] == 's')) &&
            ((p_scan[4] == 'N') || (p_scan[4] == 'n')) &&
            (p_scan[5] == ':'))
        {
            return (p_scan + APP_BC95_AT_CGSN_PREFIX_LEN);
        }
        p_scan++;
    }
    return NULL;
}

static uint8_t App_Bc95AtIsResponseTerminated(const uint8_t *p_buf, uint16_t length)
{
    const char *p_str;

    if ((p_buf == NULL) || (length < 6u)) return APP_FALSE;

    p_str = (const char *)p_buf;

    if (App_Bc95AtFindOkBoundary(p_str) != NULL)    return APP_TRUE;
    if (App_Bc95AtHasErrorToken(p_str) == APP_TRUE) return APP_TRUE;

    if (strstr(p_str, APP_BC95_AT_CME_ERROR_PREFIX) != NULL)
    {
        if ((strchr(p_str, '\n') != NULL) || (strchr(p_str, '\r') != NULL))
        {
            return APP_TRUE;
        }
    }
    return APP_FALSE;
}

/* ============================================================
 *  BCD 변환 (IMEI/IMSI 공통 로직, 길이 파라미터로 분리)
 * ============================================================ */
static AppBc95AtStatus_t App_Bc95AtDigitsToBcd(const char *p_digits, uint32_t digitCount,
                                                uint8_t *p_bcdOut, uint32_t bcdSize,
                                                uint32_t requiredDigits, uint32_t requiredBcdBytes)
{
    uint8_t nibble[16];
    uint32_t index;
    uint32_t pairIndex;

    if ((p_digits == NULL) || (p_bcdOut == NULL))   return APP_BC95_AT_ERR_PARAM;
    if (digitCount != requiredDigits)               return APP_BC95_AT_ERR_RANGE;
    if (bcdSize < requiredBcdBytes)                 return APP_BC95_AT_ERR_PARAM;
    if (requiredBcdBytes > sizeof(nibble) / 2u)     return APP_BC95_AT_ERR_PARAM;

    for (index = 0u; index < digitCount; index++)
    {
        if ((p_digits[index] < '0') || (p_digits[index] > '9'))
            return APP_BC95_AT_ERR_FORMAT;
        nibble[index] = (uint8_t)(p_digits[index] - '0');
    }
    nibble[(requiredBcdBytes * 2u) - 1u] = APP_BC95_AT_BCD_PAD_NIBBLE;

    for (pairIndex = 0u; pairIndex < requiredBcdBytes; pairIndex++)
    {
        p_bcdOut[pairIndex] = (uint8_t)((nibble[2u * pairIndex] << 4u) |
                                        (nibble[(2u * pairIndex) + 1u] & 0x0Fu));
    }
    return APP_BC95_AT_OK;
}

/* ============================================================
 *  AT 응답 1차 분류
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtCheckResponse(const char *p_resp, int32_t *p_cmeErrOut)
{
    const char *p_cme;
    const char *p_ok;

    if (p_cmeErrOut != NULL) *p_cmeErrOut = 0;

    if ((p_resp == NULL) || (p_resp[0] == '\0'))
        return APP_BC95_AT_ERR_TIMEOUT;

    p_cme = strstr(p_resp, APP_BC95_AT_CME_ERROR_PREFIX);
    if (p_cme != NULL)
    {
        if (p_cmeErrOut != NULL)
        {
            const char *p_num = p_cme + APP_BC95_AT_CME_ERROR_PREFIX_LEN;
            while ((*p_num == ' ') || (*p_num == '\t')) p_num++;
            *p_cmeErrOut = (int32_t)atoi(p_num);
        }
        return APP_BC95_AT_ERR_CME_ERROR;
    }

    if (App_Bc95AtHasErrorToken(p_resp) == APP_TRUE)
        return APP_BC95_AT_ERR_AT_ERROR;

    p_ok = App_Bc95AtFindOkBoundary(p_resp);
    if (p_ok == NULL) return APP_BC95_AT_ERR_NO_OK;

    return APP_BC95_AT_OK;
}

/* ============================================================
 *  UART IT 기반 가변 길이 AT 응답 수신
 * ============================================================ */
static AppStatus_t App_Bc95AtUartReceiveResponse(UART_HandleTypeDef *p_huart,
                                                 uint8_t *p_buffer,
                                                 uint16_t bufferSize,
                                                 uint32_t timeoutMs,
                                                 uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint32_t lastRxTick;
    uint32_t termTick = 0u;
    uint16_t curLen;
    uint16_t lastCheckedLen = 0u;
    uint16_t safeLen;
    char     rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    AppBc95AtLineQueue_t lineQueue;
    uint8_t  termSeen = APP_FALSE;
    uint8_t  dmaWasRunning = APP_FALSE;

    APP_RETURN_IF_FALSE((p_huart != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buffer != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufferSize > 1u), APP_STATUS_INVALID_PARAM);

    /* [FIX-RX-5] snapshot at entry: TRUE when the idle-DMA owns the receiver */
    dmaWasRunning = APP_BC95_RX_DMA_RUNNING(p_huart) ? APP_TRUE : APP_FALSE;

    /* EVTIND 조기 도착 래치 캡처 (경로 6 - 명령 시작 전 잔량 스캔):
     * [v17 FIX] 스캔은 반드시 memset 전에 실행한다. 기존 순서는
     * memset(p_buffer)이 g_appBc95AtRxContext.p_buffer(=동일 버퍼
     * g_appBc95AtRxBuf)를 먼저 0으로 지워버려 스캔이 항상 빈 버퍼를
     * 읽는 no-op였다. 실측(10:21:08 세션): ev=4가 활성 CEREG 응답 버퍼
     * 꼬리에 합쳐 도착한 뒤 다음 명령 시작 memset으로 소멸해
     * mask=0x0109(비트4 누락) -> platform-ready wait timeout.
     * 이전 명령 응답이 폐기되며 버퍼에 남은 URC 혼입분을 버리기 전에
     * 회수한다(boot20 실측: preflight 중 status=13 폐기 구간 URC 유실). */
    if ((g_appBc95AtRxContext.p_buffer != NULL) &&
        (g_appBc95AtRxContext.active != APP_TRUE) &&
        (g_appBc95AtRxContext.receivedLength > 0u))
    {
        App_Bc95AtScanBufferForEVTIND((const char *)g_appBc95AtRxContext.p_buffer,
                                      g_appBc95AtRxContext.receivedLength);
    }

    (void)memset(p_buffer, 0, bufferSize);
    if (p_rxLengthOut != NULL) *p_rxLengthOut = 0u;

    g_appBc95AtRxContext.p_huart        = p_huart;
    g_appBc95AtRxContext.p_buffer       = p_buffer;
    g_appBc95AtRxContext.bufferSize     = (uint16_t)(bufferSize - 1u);
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    /* [PROBE-2] just before arming RX: bytes that arrived after DrainRxLine and sit in RDR/ORE (FLUSH discards them next) */
#ifdef NBIOT_TRACE_DEBUG
    if (s_probeDrainTick != 0u)
    {
        uint32_t probeBlindMs = HAL_GetTick() - s_probeDrainTick;

        s_probeDrainTick = 0u;
        s_probeBlindLastMs = probeBlindMs;
        s_probeBaud = p_huart->Init.BaudRate;
        if (probeBlindMs > s_probeBlindMaxMs)
        {
            s_probeBlindMaxMs = probeBlindMs;
        }
        if (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_ORE) != RESET)
        {
            s_probeBlindOre++;
            App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_MARK, APP_BC95_TRC_SRC_NONE, APP_BC95_TRC_MARK_PROBE_BLIND_ORE, 0u);
        }
        /* [FIX-RX-5] Reading RDR here would steal a byte from the running DMA. */
        if ((dmaWasRunning == APP_FALSE) &&
            (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_RXNE) != RESET))
        {
            s_probeBlindRxne++;
            s_probeBlindLastByte = (uint8_t)(p_huart->Instance->RDR & 0xFFu);
            App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_MARK, APP_BC95_TRC_SRC_NONE, APP_BC95_TRC_MARK_PROBE_BLIND_RXNE, 0u);
        }
    }
#endif /* NBIOT_TRACE_DEBUG */

    __HAL_UART_CLEAR_FLAG(p_huart,
        UART_CLEAR_OREF | UART_CLEAR_FEF |
        UART_CLEAR_NEF  | UART_CLEAR_PEF);
    /* [FIX-RX-5] FLUSH only when the DMA was not running. Disarm below stops the DMA
     * and rescues its data (EVTIND scan), so nothing needs flushing in that case. */
    if (dmaWasRunning == APP_FALSE)
    {
        __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);
    }

    App_Bc95AtDisarmUrcMonitor(p_huart);
    halStatus = HAL_UART_Receive_IT(p_huart, &p_buffer[0], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(p_huart);
        return APP_STATUS_UART_RX_FAILED;
    }

    App_Bc95AtLineQueueReset(&lineQueue);
    startTick  = HAL_GetTick();
    lastRxTick = startTick;
    while (1)
    {
        APP_WWDGFeed();

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            curLen = g_appBc95AtRxContext.receivedLength;
            if (curLen >= g_appBc95AtRxContext.bufferSize)
            {
                curLen = g_appBc95AtRxContext.bufferSize;
            }
            if (curLen < bufferSize)
            {
                p_buffer[curLen] = (uint8_t)'\0';
            }
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("err", p_buffer, curLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtFinishRxIt(p_huart);
            if (p_rxLengthOut != NULL) *p_rxLengthOut = curLen;
            return APP_STATUS_UART_RX_FAILED;
        }

        curLen = g_appBc95AtRxContext.receivedLength;
        if (curLen != lastCheckedLen)
        {
            lastCheckedLen = curLen;
            lastRxTick = HAL_GetTick();
            termSeen = APP_FALSE;
        }

        safeLen = curLen;
        if (safeLen >= g_appBc95AtRxContext.bufferSize)
        {
            safeLen = g_appBc95AtRxContext.bufferSize;
        }
        safeLen = App_Bc95AtCopySnapshot(p_buffer, safeLen, rxSnapshot, (uint16_t)sizeof(rxSnapshot));
        /* EVTIND 래치 (경로 3): AT 명령 응답 대기 중 혼입 URC 캡처 */
        App_Bc95AtScanBufferForEVTIND(rxSnapshot, safeLen);
        App_Bc95AtLineQueueFeed(&lineQueue, rxSnapshot, safeLen);

        if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
            (App_Bc95AtIsResponseTerminated((const uint8_t *)rxSnapshot, safeLen) == APP_TRUE))
        {
            if (termSeen != APP_TRUE)
            {
                termSeen = APP_TRUE;
                termTick = HAL_GetTick();
            }

            if (((HAL_GetTick() - lastRxTick) >= APP_BC95_AT_RX_IDLE_GAP_MS) &&
                ((HAL_GetTick() - termTick) >= APP_BC95_AT_RX_POST_TERM_WAIT_MS))
            {
                App_Bc95AtFinishRxIt(p_huart);
                if (safeLen < bufferSize)
                {
                    p_buffer[safeLen] = (uint8_t)'\0';
                }
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("ok", p_buffer, safeLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                if (p_rxLengthOut != NULL) *p_rxLengthOut = safeLen;
                return APP_STATUS_OK;
            }
        }

        if (curLen >= g_appBc95AtRxContext.bufferSize)
        {
            App_Bc95AtFinishRxIt(p_huart);
            if (safeLen < bufferSize)
            {
                p_buffer[safeLen] = (uint8_t)'\0';
            }
            if (p_rxLengthOut != NULL) *p_rxLengthOut = safeLen;
            if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
                (App_Bc95AtIsResponseTerminated((const uint8_t *)rxSnapshot, safeLen) == APP_TRUE))
            {
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("full-ok", p_buffer, safeLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                APP_LOGD("NBIOT", "Response completed at full buffer (rxLen=%u)",
                         (unsigned)safeLen);
                return APP_STATUS_OK;
            }
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("full-to", p_buffer, safeLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            return APP_STATUS_UART_TIMEOUT;
        }

        if ((HAL_GetTick() - startTick) >= timeoutMs)
        {
            App_Bc95AtFinishRxIt(p_huart);
            __HAL_UART_CLEAR_FLAG(p_huart,
                UART_CLEAR_OREF | UART_CLEAR_FEF |
                UART_CLEAR_NEF  | UART_CLEAR_PEF);
            if (safeLen < bufferSize)
            {
                p_buffer[safeLen] = (uint8_t)'\0';
            }
            if (p_rxLengthOut != NULL) *p_rxLengthOut = safeLen;

            if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
                (App_Bc95AtIsResponseTerminated((const uint8_t *)rxSnapshot, safeLen) == APP_TRUE))
            {
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("latel-ok", p_buffer, safeLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                APP_LOGD("NBIOT", "Late-detected response on timeout (rxLen=%u)",
                         (unsigned)safeLen);
                return APP_STATUS_OK;
            }
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("timeout", p_buffer, safeLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            return APP_STATUS_UART_TIMEOUT;
        }

        HAL_Delay(1u);
    }
}

/* ============================================================
 *  명령 송신 + 응답 수신
 * ============================================================ */
AppStatus_t App_Bc95AtSendCommand(const char *p_cmd, uint8_t *p_rxBuf, uint16_t rxBufSize,
                                  uint32_t rxTimeoutMs, uint16_t *p_rxLengthOut)
{
    HAL_StatusTypeDef halStatus;
    AppStatus_t status;
    uint16_t cmdLen;

    APP_WWDGFeed();
    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_cmd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_rxBuf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((rxBufSize > 1u), APP_STATUS_INVALID_PARAM);

    cmdLen = (uint16_t)strlen(p_cmd);
    APP_RETURN_IF_FALSE((cmdLen > 0u), APP_STATUS_INVALID_PARAM);

    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)p_cmd, cmdLen,
                                  APP_BC95_AT_TX_TIMEOUT_MS);
    APP_WWDGFeed();
    APP_RETURN_IF_FALSE((halStatus == HAL_OK), APP_STATUS_UART_TX_FAILED);

    status = App_Bc95AtUartReceiveResponse(APP_UART_NBIOT_HANDLE,
                                           p_rxBuf, rxBufSize,
                                           rxTimeoutMs, p_rxLengthOut);
    APP_WWDGFeed();
    return status;
}

/* ============================================================
 *  Ping / Boot wait
 * ============================================================ */
AppStatus_t App_Bc95AtPing(uint32_t timeoutMs)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int32_t cmeErr;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_PING,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   timeoutMs, &rxLen);
    if (status != APP_STATUS_OK) return status;

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK) return APP_STATUS_FATAL;
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtWaitForBoot(uint32_t bannerTimeoutMs)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint16_t curLen;
    uint16_t lastCheckedLen = 0u;
    uint16_t bufferCap;
    char     rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    AppBc95AtLineQueue_t lineQueue;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    /* EVTIND 래치 (경로 6): 부트 대기 시작 전 이전 버퍼 잔량 회수
     * [v17 FIX] memset 전에 스캔(UartReceiveResponse와 동일 순서 버그) */
    if ((g_appBc95AtRxContext.p_buffer != NULL) &&
        (g_appBc95AtRxContext.active != APP_TRUE) &&
        (g_appBc95AtRxContext.receivedLength > 0u))
    {
        App_Bc95AtScanBufferForEVTIND((const char *)g_appBc95AtRxContext.p_buffer,
                                      g_appBc95AtRxContext.receivedLength);
    }

    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));
    bufferCap = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = bufferCap;
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    App_Bc95AtDisarmUrcMonitor(APP_UART_NBIOT_HANDLE);
    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE, &g_appBc95AtRxBuf[0], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    App_Bc95AtLineQueueReset(&lineQueue);
    startTick = HAL_GetTick();
    while (1)
    {
        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("banner-err", (const uint8_t *)g_appBc95AtRxBuf,
                               (uint16_t)g_appBc95AtRxContext.receivedLength);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_RX_FAILED;
        }

        curLen = g_appBc95AtRxContext.receivedLength;
        if (curLen != lastCheckedLen)
        {
            if (curLen >= bufferCap)
            {
                curLen = bufferCap;
            }
            (void)App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, curLen, rxSnapshot, (uint16_t)sizeof(rxSnapshot));
            App_Bc95AtLineQueueFeed(&lineQueue, rxSnapshot, curLen);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("banner+", (const uint8_t *)&rxSnapshot[lastCheckedLen],
                               (uint16_t)(curLen - lastCheckedLen));
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)

            if ((App_Bc95AtLineQueueContainsSubstring(&lineQueue, APP_BC95_BOOT_BANNER) == APP_TRUE) ||
                (strstr(rxSnapshot, APP_BC95_BOOT_BANNER) != NULL))
            {
                uint32_t completeWaitStart = HAL_GetTick();
                while ((HAL_GetTick() - completeWaitStart) < 500u)
                {
                    curLen = g_appBc95AtRxContext.receivedLength;
                    if (curLen >= bufferCap)
                    {
                        curLen = bufferCap;
                    }
                    (void)App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, curLen, rxSnapshot, (uint16_t)sizeof(rxSnapshot));
                    App_Bc95AtLineQueueFeed(&lineQueue, rxSnapshot, curLen);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                    App_Bc95AtLogRawRx("banner+", (const uint8_t *)&rxSnapshot[lastCheckedLen],
                               (uint16_t)(curLen - lastCheckedLen));
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                    if ((App_Bc95AtLineQueueHasFinalResponse(&lineQueue) == APP_TRUE) ||
                        (App_Bc95AtFindOkBoundary(rxSnapshot) != NULL)) break;
                    APP_WWDGFeed();
                    HAL_Delay(10u);
                }
                App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
                if (curLen < sizeof(g_appBc95AtRxBuf))
                {
                    g_appBc95AtRxBuf[curLen] = (uint8_t)'\0';
                }
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                App_Bc95AtLogRawRx("banner-ok", (const uint8_t *)g_appBc95AtRxBuf,
                                   (uint16_t)g_appBc95AtRxContext.receivedLength);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
                APP_LOGD("NBIOT", "Boot banner detected (len=%u)", (unsigned)curLen);
                return APP_STATUS_OK;
            }
            lastCheckedLen = curLen;
        }

        if (curLen >= bufferCap)
        {
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("banner-full", (const uint8_t *)g_appBc95AtRxBuf,
                               (uint16_t)g_appBc95AtRxContext.receivedLength);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_TIMEOUT;
        }

        if ((HAL_GetTick() - startTick) >= bannerTimeoutMs)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF | UART_CLEAR_PEF);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            App_Bc95AtLogRawRx("banner-to", (const uint8_t *)g_appBc95AtRxBuf,
                               (uint16_t)g_appBc95AtRxContext.receivedLength);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_WWDGFeed();
        HAL_Delay(1u);
    }
}


AppStatus_t App_Bc95AtWaitUntilReady(uint32_t totalTimeoutMs)
{
    AppStatus_t status;
    uint32_t startTick;
    uint32_t elapsedMs;
    uint32_t bannerWaitMs;
    uint32_t retryCount;
    uint8_t  bannerSeen = APP_FALSE;
    uint8_t  skipBootWaitOnce = g_appBc95SkipNextAttachBootWait;
    uint8_t  shortRetryBannerWait = g_appBc95ShortRetryBootBannerWaitOnce;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    startTick = HAL_GetTick();

    bannerWaitMs = (totalTimeoutMs > APP_BC95_BOOT_WAIT_BANNER_MS)
                       ? APP_BC95_BOOT_WAIT_BANNER_MS
                       : totalTimeoutMs;

    if ((shortRetryBannerWait == APP_TRUE) &&
        (skipBootWaitOnce != APP_TRUE) &&
        (g_appNbiotCarrierContext.lastResetType == APP_NBIOT_CARRIER_RESET_NONE))
    {
        if (bannerWaitMs > APP_BC95_ATTACH_RETRY_BANNER_WAIT_MS)
        {
            bannerWaitMs = APP_BC95_ATTACH_RETRY_BANNER_WAIT_MS;
        }
        g_appBc95ShortRetryBootBannerWaitOnce = APP_FALSE;
        APP_LOGD("NBIOT", "short boot wait for attach retry %lums",
                 (unsigned long)bannerWaitMs);
    }

    if (skipBootWaitOnce == APP_TRUE)
    {
        APP_LOGD("NBIOT", "skip boot wait after NRB, probe AT");
    }
    else if (g_appNbiotCarrierContext.lastResetType == APP_NBIOT_CARRIER_RESET_SW)
    {
        APP_LOGD("NBIOT", "skip boot wait after SW reset");
    }
    else
    {
        APP_LOGD("NBIOT", "wait boot banner %lums", (unsigned long)bannerWaitMs);
        status = App_Bc95AtWaitForBoot(bannerWaitMs);
        if (status == APP_STATUS_OK)
        {
            bannerSeen = APP_TRUE;
            APP_LOGD("NBIOT", "boot banner rx");
        }
        else
        {
            APP_LOGD("NBIOT", "no boot banner, probe AT");
        }
    }

    retryCount = 0u;
    while (1)
    {
        elapsedMs = HAL_GetTick() - startTick;
        if (elapsedMs >= totalTimeoutMs)
        {
            APP_LOGD("NBIOT", "ready timeout b=%u r=%lu",
                     (unsigned)bannerSeen, (unsigned long)retryCount);
            return APP_STATUS_UART_TIMEOUT;
        }

        status = App_Bc95AtPing(APP_BC95_BOOT_PING_TIMEOUT_MS);
        if (status == APP_STATUS_OK)
        {
            g_appBc95SkipNextAttachBootWait = APP_TRUE;
            APP_LOGD("NBIOT", "ready b=%u r=%lu t=%lums",
                     (unsigned)bannerSeen, (unsigned long)retryCount,
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }

        retryCount++;
        if (retryCount >= APP_BC95_BOOT_PING_MAX_RETRY)
        {
            APP_LOGD("NBIOT", "Ready check exhausted retries (last=%d)", (int)status);
            return APP_STATUS_UART_TIMEOUT;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_BOOT_PING_INTERVAL_MS);
    }
}

/* ============================================================
 *  USIM 준비 대기
 * ============================================================ */
AppStatus_t App_Bc95AtWaitForUsim(uint32_t timeoutMs)
{
    AppStatus_t st;
    AppBc95AtStatus_t atSt;
    uint32_t startTick;
    uint32_t elapsed;
    uint32_t attempt = 0u;
    uint16_t rxLen;
    int32_t cmeErr;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    g_appBc95UsimFatal = APP_FALSE;
    startTick = HAL_GetTick();
    APP_LOGD("NBIOT", "wait USIM %lums", (unsigned long)timeoutMs);

    while (1)
    {
        attempt++;
        rxLen = 0u;
        st = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);

        if (st == APP_STATUS_OK)
        {
            atSt = App_Bc95AtExtractImsiString((const char *)g_appBc95AtRxBuf,
                                               imsiStr, sizeof(imsiStr));
            if (atSt == APP_BC95_AT_OK)
            {
                APP_LOGD("NBIOT", "USIM ready a=%lu t=%lums",
                         (unsigned long)attempt,
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }

            if (atSt == APP_BC95_AT_ERR_CME_ERROR)
            {
                cmeErr = 0;
                (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
                if ((cmeErr == 10) || (cmeErr == 13) || (cmeErr == 15) ||
                    (cmeErr == 16) || (cmeErr == 311) || (cmeErr == 313) ||
                    (cmeErr == 315) || (cmeErr == 317) || (cmeErr == 318))
                {
                    g_appBc95UsimFatal = APP_TRUE;
                    APP_LOGD("NBIOT", "USIM fatal (CME=%ld)", (long)cmeErr);
                    return APP_STATUS_FATAL;
                }
                APP_LOGD("NBIOT", "USIM !ready cme=%ld a=%lu",
                         (long)cmeErr, (unsigned long)attempt);
            }
            else
            {
                APP_LOGD("NBIOT", "USIM !ready parse=%s a=%lu",
                         App_Bc95AtGetStatusString(atSt), (unsigned long)attempt);
            }
        }
        else
        {
            APP_LOGD("NBIOT", "USIM probe uart st=%d a=%lu",
                     (int)st, (unsigned long)attempt);
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= timeoutMs)
        {
            g_appBc95UsimFatal = APP_TRUE;
            APP_LOGD("NBIOT", "USIM fatal timeout a=%lu t=%lums",
                     (unsigned long)attempt, (unsigned long)elapsed);
            return APP_STATUS_FATAL;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_USIM_READY_POLL_MS);
    }
}

static uint8_t App_Bc95AtResponseHasCpinReady(const char *p_resp)
{
    const char *p_cpin;

    if (p_resp == NULL)
    {
        return APP_FALSE;
    }

    p_cpin = strstr(p_resp, "+CPIN:");
    if (p_cpin == NULL)
    {
        return APP_FALSE;
    }

    if (strstr(p_cpin, "READY") != NULL)
    {
        return APP_TRUE;
    }

    return APP_FALSE;
}

static AppStatus_t App_Bc95AtWakeDelayAndWaitForCpinReady(void)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint32_t startTick;
    uint32_t elapsedMs;
    uint32_t fixedDelayMs = APP_BC95_WAKE_CPIN_GUARD_DELAY_MS;
    uint16_t rxLen = 0u;
    int32_t cmeErr = 0;

    /*
     * wakeup fast-path(recent NRB ready 기반 skip boot wait)일 때만 적용.
     * cold boot / 일반 경로는 그대로 둔다.
     */
    if (g_appBc95SkipNextAttachBootWait != APP_TRUE)
    {
        return APP_STATUS_OK;
    }

    if (g_appBc95ShortRetryCpinGuardOnce == APP_TRUE)
    {
        fixedDelayMs = APP_BC95_WAKE_CPIN_GUARD_RETRY_DELAY_MS;
        g_appBc95ShortRetryCpinGuardOnce = APP_FALSE;
    }

    APP_LOGW("NBIOT",
             "[[ReadyProbe]] wake CPIN guard start fixedDelay=%lums timeout=%lums poll=%lums",
             (unsigned long)fixedDelayMs,
             (unsigned long)APP_BC95_WAKE_CPIN_GUARD_TIMEOUT_MS,
             (unsigned long)APP_BC95_WAKE_CPIN_GUARD_POLL_MS);

    App_Bc95AtDelayWithFeed(fixedDelayMs);

    (void)App_Bc95AtSendBestEffortSimpleOk(APP_BC95_AT_CMD_CMEE_ON,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CMEE=1",
                                           "[[ReadyProbe]]");

    startTick = HAL_GetTick();

    while (1)
    {
        APP_WWDGFeed();
        rxLen = 0u;
        cmeErr = 0;

        status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CPIN_QUERY,
                                       g_appBc95AtRxBuf,
                                       (uint16_t)sizeof(g_appBc95AtRxBuf),
                                       APP_BC95_AT_RX_TIMEOUT_MS,
                                       &rxLen);

        if (status == APP_STATUS_OK)
        {
            atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);

            if ((atStatus == APP_BC95_AT_OK) &&
                (App_Bc95AtResponseHasCpinReady((const char *)g_appBc95AtRxBuf) == APP_TRUE))
            {
                APP_LOGN("NBIOT",
                         "[[ReadyProbe]] wake CPIN guard passed (elapsed=%lums)",
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }

            if (atStatus == APP_BC95_AT_ERR_CME_ERROR)
            {
                APP_LOGW("NBIOT",
                         "[[ReadyProbe]] wake CPIN guard waiting (CME=%ld, elapsed=%lums)",
                         (long)cmeErr,
                         (unsigned long)(HAL_GetTick() - startTick));
            }
            else if (atStatus == APP_BC95_AT_OK)
            {
                APP_LOGW("NBIOT",
                         "[[ReadyProbe]] wake CPIN guard waiting (OK but READY not visible, elapsed=%lums)",
                         (unsigned long)(HAL_GetTick() - startTick));
            }
            else
            {
                APP_LOGW("NBIOT",
                         "[[ReadyProbe]] wake CPIN guard parse=%s elapsed=%lums",
                         App_Bc95AtGetStatusString(atStatus),
                         (unsigned long)(HAL_GetTick() - startTick));
            }
        }
        else
        {
            APP_LOGW("NBIOT",
                     "[[ReadyProbe]] wake CPIN guard UART issue status=%d elapsed=%lums",
                     (int)status,
                     (unsigned long)(HAL_GetTick() - startTick));
        }

        elapsedMs = HAL_GetTick() - startTick;
        if (elapsedMs >= APP_BC95_WAKE_CPIN_GUARD_TIMEOUT_MS)
        {
            APP_LOGW("NBIOT",
                     "[[ReadyProbe]] wake CPIN guard timeout (elapsed=%lums) -> force conservative path",
                     (unsigned long)elapsedMs);
            return APP_STATUS_UART_TIMEOUT;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_WAKE_CPIN_GUARD_POLL_MS);
    }
}

AppStatus_t App_Bc95AtProbeServiceReady(AppBc95ServiceReadyProbe_t *p_probe)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int32_t cmeErr = 0;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_probe != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(p_probe, 0, sizeof(*p_probe));
    p_probe->lastStatus = APP_STATUS_INVALID_PARAM;

    status = App_Bc95AtPing(APP_BC95_BOOT_PING_TIMEOUT_MS);
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGN("NBIOT", "[[ReadyProbe]] AT ping fail (status=%d)", (int)status);
        return status;
    }
    p_probe->atReady = APP_TRUE;

    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CMEE_ON,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CMEE=1",
                                           "[[ReadyProbe]]");
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "[[ReadyProbe]] CMEE enable fail (status=%d)", (int)status);
        return status;
    }
    p_probe->cmeeReady = APP_TRUE;

    /*
     * 1차 게이트: CPIN READY 확인
     */
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CPIN_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "[[ReadyProbe]] CPIN probe UART issue (status=%d)", (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus == APP_BC95_AT_ERR_CME_ERROR)
    {
        p_probe->lastCmeError = cmeErr;
        p_probe->lastStatus = APP_STATUS_UART_TIMEOUT;
        APP_LOGE("NBIOT", "[[ReadyProbe]] CPIN not ready yet (AT=1, CMEE=1, USIM=0, CME=%ld)",
                 (long)cmeErr);
        return APP_STATUS_UART_TIMEOUT;
    }
    if (atStatus != APP_BC95_AT_OK)
    {
        p_probe->lastStatus = APP_STATUS_FATAL;
        APP_LOGE("NBIOT", "[[ReadyProbe]] CPIN parse fail (status=%s)",
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    if (App_Bc95AtResponseHasCpinReady((const char *)g_appBc95AtRxBuf) != APP_TRUE)
    {
        p_probe->lastStatus = APP_STATUS_UART_TIMEOUT;
        p_probe->lastCmeError = 0;
        APP_LOGW("NBIOT", "[[ReadyProbe]] CPIN response is OK but READY not yet visible");
        return APP_STATUS_UART_TIMEOUT;
    }

    /*
     * recoverable-CME recovery 직후에만
     * CPIN READY를 한 번 더 확인해서 READY 흔들림을 줄인다.
     */
    if (g_appBc95ServiceReadyRecoveryActive == APP_TRUE)
    {
        App_Bc95AtDelayWithFeed(APP_BC95_SERVICE_READY_CPIN_STABLE_DELAY_MS);

        status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CPIN_QUERY,
                                       g_appBc95AtRxBuf,
                                       (uint16_t)sizeof(g_appBc95AtRxBuf),
                                       APP_BC95_AT_RX_TIMEOUT_MS,
                                       &rxLen);
        p_probe->lastStatus = status;
        if (status != APP_STATUS_OK)
        {
            APP_LOGE("NBIOT", "[[ReadyProbe]] CPIN re-check UART issue after READY (status=%d)", (int)status);
            return status;
        }

        atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
        if (atStatus == APP_BC95_AT_ERR_CME_ERROR)
        {
            p_probe->lastCmeError = cmeErr;
            p_probe->lastStatus = APP_STATUS_UART_TIMEOUT;
            APP_LOGW("NBIOT", "[[ReadyProbe]] CPIN READY unstable after wake/recovery (CME=%ld)",
                     (long)cmeErr);
            return APP_STATUS_UART_TIMEOUT;
        }
        if (atStatus != APP_BC95_AT_OK)
        {
            p_probe->lastStatus = APP_STATUS_FATAL;
            APP_LOGE("NBIOT", "[[ReadyProbe]] CPIN re-check parse fail (status=%s)",
                     App_Bc95AtGetStatusString(atStatus));
            return APP_STATUS_FATAL;
        }
        if (App_Bc95AtResponseHasCpinReady((const char *)g_appBc95AtRxBuf) != APP_TRUE)
        {
            p_probe->lastStatus = APP_STATUS_UART_TIMEOUT;
            p_probe->lastCmeError = 0;
            APP_LOGW("NBIOT", "[[ReadyProbe]] CPIN READY not stable on re-check");
            return APP_STATUS_UART_TIMEOUT;
        }
    }

    /*
     * recoverable-CME recovery 직후에는 SIM path를 한 번 더 깨워준다.
     * 실패해도 fatal로 보지 않고 best-effort로만 수행한다.
     */
    if (g_appBc95ServiceReadyRecoveryActive == APP_TRUE)
    {
        (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_NCCID_QUERY,
                                            APP_BC95_AT_RX_TIMEOUT_MS,
                                            "AT+NCCID?",
                                            "[[ReadyProbe]]");
    }

    /*
     * 2차 게이트: READY 이후에만 IMSI 확인
     */
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    p_probe->lastStatus = status;
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", "[[ReadyProbe]] IMSI probe UART issue after CPIN READY (status=%d)", (int)status);
        return status;
    }

    atStatus = App_Bc95AtExtractImsiString((const char *)g_appBc95AtRxBuf,
                                           imsiStr,
                                           sizeof(imsiStr));
    if (atStatus == APP_BC95_AT_OK)
    {
        p_probe->usimReady = APP_TRUE;
        p_probe->lastStatus = APP_STATUS_OK;
        APP_LOGN("NBIOT", "[[ReadyProbe]] service ready (AT=1, CMEE=1, CPIN=READY, USIM=1)");
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_CME_ERROR)
    {
        (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
        p_probe->lastCmeError = cmeErr;
        p_probe->lastStatus = APP_STATUS_UART_TIMEOUT;
        APP_LOGE("NBIOT", "[[ReadyProbe]] IMSI not ready after CPIN READY (CME=%ld)",
                 (long)cmeErr);
        return APP_STATUS_UART_TIMEOUT;
    }

    p_probe->lastStatus = APP_STATUS_FATAL;
    APP_LOGE("NBIOT", "[[ReadyProbe]] IMSI parse fail after CPIN READY (status=%s)",
             App_Bc95AtGetStatusString(atStatus));
    return APP_STATUS_FATAL;
}

static AppStatus_t App_Bc95AtWaitForServiceReady(uint32_t totalTimeoutMs)
{
    AppStatus_t status;
    AppStatus_t recoverStatus;
    uint32_t startTick;
    uint32_t elapsedMs;
    uint32_t remainingMs;
    uint32_t settleMs;
    uint32_t pollMs;
    uint8_t recoverableCmeCount = 0u;
    uint8_t recoverableCmeThreshold;
    uint8_t requiredReadyStreak;
    uint8_t readyStreak = 0u;
    uint8_t effectiveRecoverableCmeThreshold;
    int32_t recoverableCme = -1;
    AppBc95ServiceReadyProbe_t probe;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    if (g_appBc95ServiceReadyRecoveryActive == APP_TRUE)
    {
        settleMs = APP_BC95_SERVICE_READY_POST_CFUN_SETTLE_MS;
        pollMs = APP_BC95_SERVICE_READY_POST_RECOVER_POLL_MS;
        recoverableCmeThreshold = 1u;
        requiredReadyStreak = APP_BC95_SERVICE_READY_WAKE_READY_STREAK;
    }
    else
    {
        settleMs = APP_BC95_SERVICE_READY_SETTLE_MS;
        pollMs = APP_BC95_USIM_READY_POLL_MS;
        recoverableCmeThreshold = APP_BC95_SERVICE_READY_RECOVERABLE_CME_THRESHOLD;
        requiredReadyStreak = 1u;
    }

    startTick = HAL_GetTick();
    g_appBc95WakeCme524GraceUsed = APP_FALSE;

    APP_LOGD("NBIOT", "wait srv-ready t=%lums s=%lums p=%lums pr=%u c=%u rs=%u",
             (unsigned long)totalTimeoutMs,
             (unsigned long)settleMs,
             (unsigned long)pollMs,
             (unsigned)g_appBc95ServiceReadyRecoveryActive,
             (unsigned)recoverableCmeThreshold,
             (unsigned)requiredReadyStreak);

    if (settleMs != 0u)
    {
        App_Bc95AtDelayWithFeed(settleMs);
    }

    elapsedMs = HAL_GetTick() - startTick;
    if (elapsedMs >= totalTimeoutMs)
    {
        APP_LOGD("NBIOT", "srv-ready timeout before USIM t=%lums",
                 (unsigned long)elapsedMs);
        g_appBc95UsimFatal = APP_TRUE;
        return APP_STATUS_FATAL;
    }

    while (1)
    {
        status = App_Bc95AtProbeServiceReady(&probe);
        if (status == APP_STATUS_OK)
        {
            readyStreak++;
            APP_LOGD("NBIOT", "srv-ready streak=%u/%u t=%lums",
                     (unsigned)readyStreak,
                     (unsigned)requiredReadyStreak,
                     (unsigned long)(HAL_GetTick() - startTick));

            if (readyStreak < requiredReadyStreak)
            {
                App_Bc95AtDelayWithFeed(pollMs);
                continue;
            }

            g_appBc95ServiceReadyRecoveryActive = APP_FALSE;

            APP_LOGD("NBIOT", "srv-ready ok t=%lums",
                     (unsigned long)(HAL_GetTick() - startTick));
            return APP_STATUS_OK;
        }

        readyStreak = 0u;

        if ((probe.lastStatus == APP_STATUS_FATAL) ||
            (App_Bc95AtIsUsimFatalCme(probe.lastCmeError) == APP_TRUE))
        {
            g_appBc95UsimFatal = APP_TRUE;
            APP_LOGD("NBIOT", "srv-ready fatal st=%d cme=%ld",
                     (int)probe.lastStatus,
                     (long)probe.lastCmeError);
            return APP_STATUS_FATAL;
        }

        if (App_Bc95AtIsRecoverableServiceReadyCme(probe.lastCmeError) == APP_TRUE)
        {
            effectiveRecoverableCmeThreshold = recoverableCmeThreshold;

            if ((probe.lastCmeError == 524) &&
                 (g_appBc95ServiceReadyRecoveryActive == APP_TRUE))
            {
                if ((g_appBc95WakeCme524GraceUsed != APP_TRUE) &&
                    ((HAL_GetTick() - startTick) < APP_BC95_CME524_WAKE_GRACE_MS))
                {
                    g_appBc95WakeCme524GraceUsed = APP_TRUE;
                    recoverableCmeCount = 0u;
                    recoverableCme = -1;

                    APP_LOGD("NBIOT", "srv-ready 524 grace t=%lums<%lums, reprobe",
                             (unsigned long)(HAL_GetTick() - startTick),
                             (unsigned long)APP_BC95_CME524_WAKE_GRACE_MS);

                    elapsedMs = HAL_GetTick() - startTick;
                    if (elapsedMs < totalTimeoutMs)
                    {
                        remainingMs = totalTimeoutMs - elapsedMs;
                        App_Bc95AtDelayWithFeed((remainingMs > APP_BC95_CME524_GRACE_POLL_MS) ?
                                                APP_BC95_CME524_GRACE_POLL_MS : remainingMs);
                    }
                    continue;
                }

                effectiveRecoverableCmeThreshold = APP_BC95_CME524_WAKE_RECOVERY_THRESHOLD;
            }

            if (probe.lastCmeError == recoverableCme)
            {
                if (recoverableCmeCount < 0xFFu)
                {
                    recoverableCmeCount++;
                }
            }
            else
            {
                recoverableCme = probe.lastCmeError;
                recoverableCmeCount = 1u;
            }

            APP_LOGD("NBIOT", "srv-ready recover cme=%ld streak=%u/%u",
                     (long)recoverableCme,
                     (unsigned)recoverableCmeCount,
                     (unsigned)effectiveRecoverableCmeThreshold);


            if (recoverableCmeCount >= effectiveRecoverableCmeThreshold)
            {
                recoverStatus = App_Bc95AtRecoverFromServiceReadyCme(recoverableCme);
                if (recoverStatus == APP_STATUS_OK)
                {
                    recoverableCmeCount = 0u;
                    recoverableCme = -1;
                    readyStreak = 0u;

                    g_appBc95ServiceReadyRecoveryActive = APP_TRUE;

                    App_Bc95AtDelayWithFeed(APP_BC95_SERVICE_READY_POST_CFUN_SETTLE_MS);
                    continue;
                }

                APP_LOGD("NBIOT",
                         "srv-ready recover fail st=%d cme=%ld -> attach",
                         (int)recoverStatus,
                         (long)recoverableCme);
                return APP_STATUS_UART_TIMEOUT;
            }
        }
        else
        {
            recoverableCmeCount = 0u;
            recoverableCme = -1;
        }

        elapsedMs = HAL_GetTick() - startTick;
        if (elapsedMs >= totalTimeoutMs)
        {
            if (App_Bc95AtIsRecoverableServiceReadyCme(probe.lastCmeError) == APP_TRUE)
            {
                APP_LOGD("NBIOT",
                         "srv-ready timeout after recover cme=%ld t=%lums -> attach",
                         (long)probe.lastCmeError,
                         (unsigned long)elapsedMs);
                return APP_STATUS_UART_TIMEOUT;
            }

            g_appBc95UsimFatal = APP_TRUE;
            APP_LOGD("NBIOT", "srv-ready timeout t=%lums st=%d cme=%ld",
                     (unsigned long)elapsedMs,
                     (int)probe.lastStatus,
                     (long)probe.lastCmeError);
            return APP_STATUS_FATAL;
        }

        remainingMs = totalTimeoutMs - elapsedMs;
        if (remainingMs > pollMs)
        {
            App_Bc95AtDelayWithFeed(pollMs);
        }
        else
        {
            App_Bc95AtDelayWithFeed(remainingMs);
        }
    }
}

/* ============================================================
 *  IMEI / IMSI 추출
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtExtractImeiString(const char *p_resp, char *p_out, uint32_t outSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    uint32_t digitCount;

    if ((p_resp == NULL) || (p_out == NULL) || (outSize <= (uint32_t)APP_BC95_IMEI_DIGITS))
        return APP_BC95_AT_ERR_PARAM;
    p_out[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = App_Bc95AtFindCgsnPrefix(p_resp);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;

    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;

    digitCount = 0u;
    while ((*p_payload != '\0') && (digitCount <= (uint32_t)APP_BC95_IMEI_DIGITS))
    {
        if ((*p_payload >= '0') && (*p_payload <= '9'))
        {
            if (digitCount < (uint32_t)APP_BC95_IMEI_DIGITS)
                p_out[digitCount] = *p_payload;
            digitCount++;
        }
        else if ((*p_payload == '\r') || (*p_payload == '\n'))
        {
            break;
        }
        p_payload++;
    }

    if (digitCount > (uint32_t)APP_BC95_IMEI_DIGITS)
    {
        p_out[APP_BC95_IMEI_DIGITS] = '\0';
        return APP_BC95_AT_ERR_RANGE;
    }
    p_out[digitCount] = '\0';
    if (digitCount != (uint32_t)APP_BC95_IMEI_DIGITS) return APP_BC95_AT_ERR_RANGE;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtImeiToBcd(const char *p_imeiStr, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    if ((p_imeiStr == NULL) || (p_bcdOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    if (strlen(p_imeiStr) != (size_t)APP_BC95_IMEI_DIGITS) return APP_BC95_AT_ERR_RANGE;
    return App_Bc95AtDigitsToBcd(p_imeiStr, (uint32_t)APP_BC95_IMEI_DIGITS,
                                 p_bcdOut, bcdSize,
                                 (uint32_t)APP_BC95_IMEI_DIGITS,
                                 (uint32_t)APP_BC95_IMEI_BCD_BYTES);
}

AppBc95AtStatus_t App_Bc95AtParseImeiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    AppBc95AtStatus_t status;
    char imeiStr[APP_BC95_IMEI_DIGITS + 1u];

    if ((p_bcdOut == NULL) || (bcdSize < (uint32_t)APP_BC95_IMEI_BCD_BYTES))
        return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_bcdOut, 0, APP_BC95_IMEI_BCD_BYTES);

    status = App_Bc95AtExtractImeiString(p_resp, imeiStr, sizeof(imeiStr));
    if (status != APP_BC95_AT_OK) return status;

    status = App_Bc95AtImeiToBcd(imeiStr, p_bcdOut, bcdSize);
    if (status == APP_BC95_AT_OK)
        (void)memcpy(g_appBc95AtImeiBcd, p_bcdOut, APP_BC95_IMEI_BCD_BYTES);
    return status;
}

AppBc95AtStatus_t App_Bc95AtExtractImsiString(const char *p_resp, char *p_out, uint32_t outSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_okBoundary;
    const char *p_end;
    const char *p_scan;
    const char *p_digitStart;
    uint32_t digitLen;

    if ((p_resp == NULL) || (p_out == NULL) || (outSize <= (uint32_t)APP_BC95_IMSI_DIGITS))
        return APP_BC95_AT_ERR_PARAM;
    p_out[0] = '\0';

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_okBoundary = App_Bc95AtFindOkBoundary(p_resp);
    p_end = (p_okBoundary != NULL) ? p_okBoundary : (p_resp + strlen(p_resp));

    p_scan = p_resp;
    p_digitStart = NULL;
    digitLen = 0u;

    while (p_scan < p_end)
    {
        if (App_Bc95AtIsLineStart(p_resp, p_scan) == APP_TRUE)
        {
            const char *p_lineStart = p_scan;
            while ((p_lineStart < p_end) && ((*p_lineStart == ' ') || (*p_lineStart == '\t')))
                p_lineStart++;

            if ((p_lineStart < p_end) && (*p_lineStart >= '0') && (*p_lineStart <= '9'))
            {
                const char *p_lineEnd = p_lineStart;
                uint32_t count = 0u;
                while ((p_lineEnd < p_end) && (*p_lineEnd >= '0') && (*p_lineEnd <= '9'))
                {
                    count++;
                    p_lineEnd++;
                }
                if ((p_lineEnd == p_end) || (*p_lineEnd == '\r') || (*p_lineEnd == '\n'))
                {
                    p_digitStart = p_lineStart;
                    digitLen = count;
                    break;
                }
            }
        }
        p_scan++;
    }

    if (p_digitStart == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    if (digitLen != (uint32_t)APP_BC95_IMSI_DIGITS) return APP_BC95_AT_ERR_RANGE;

    (void)memcpy(p_out, p_digitStart, APP_BC95_IMSI_DIGITS);
    p_out[APP_BC95_IMSI_DIGITS] = '\0';
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtImsiToBcd(const char *p_imsiStr, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    if ((p_imsiStr == NULL) || (p_bcdOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    if (strlen(p_imsiStr) != (size_t)APP_BC95_IMSI_DIGITS) return APP_BC95_AT_ERR_RANGE;
    return App_Bc95AtDigitsToBcd(p_imsiStr, (uint32_t)APP_BC95_IMSI_DIGITS,
                                 p_bcdOut, bcdSize,
                                 (uint32_t)APP_BC95_IMSI_DIGITS,
                                 (uint32_t)APP_BC95_IMSI_BCD_BYTES);
}

AppBc95AtStatus_t App_Bc95AtParseImsiToBcd(const char *p_resp, uint8_t *p_bcdOut, uint32_t bcdSize)
{
    AppBc95AtStatus_t status;
    char imsiStr[APP_BC95_IMSI_DIGITS + 1u];

    if ((p_bcdOut == NULL) || (bcdSize < (uint32_t)APP_BC95_IMSI_BCD_BYTES))
        return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_bcdOut, 0, APP_BC95_IMSI_BCD_BYTES);

    status = App_Bc95AtExtractImsiString(p_resp, imsiStr, sizeof(imsiStr));
    if (status != APP_BC95_AT_OK) return status;

    status = App_Bc95AtImsiToBcd(imsiStr, p_bcdOut, bcdSize);
    if (status == APP_BC95_AT_OK)
        (void)memcpy(g_appBc95AtImsiBcd, p_bcdOut, APP_BC95_IMSI_BCD_BYTES);
    return status;
}

/* ============================================================
 *  Fetch IMEI/IMSI (단일/재시도)
 * ============================================================ */
AppStatus_t App_Bc95AtFetchImei(uint8_t *p_imeiBcd, uint32_t bcdSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_imeiBcd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bcdSize >= (uint32_t)APP_BC95_IMEI_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMEI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK) return status;

    atStatus = App_Bc95AtParseImeiToBcd((const char *)g_appBc95AtRxBuf, p_imeiBcd, bcdSize);
    if (atStatus != APP_BC95_AT_OK) return APP_STATUS_FATAL;
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchImsi(uint8_t *p_imsiBcd, uint32_t bcdSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_imsiBcd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bcdSize >= (uint32_t)APP_BC95_IMSI_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_IMSI,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK) return status;

    atStatus = App_Bc95AtParseImsiToBcd((const char *)g_appBc95AtRxBuf, p_imsiBcd, bcdSize);
    if (atStatus != APP_BC95_AT_OK) return APP_STATUS_FATAL;
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchImeiWithRetry(uint8_t *p_imeiBcd, uint32_t bcdSize, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchImei(p_imeiBcd, bcdSize);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;

        APP_LOGD("NBIOT", "IMEI retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);

        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_FETCH_RETRY_DELAY_MS);
    }
    return st;
}

AppStatus_t App_Bc95AtFetchImsiWithRetry(uint8_t *p_imsiBcd, uint32_t bcdSize, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchImsi(p_imsiBcd, bcdSize);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;

        APP_LOGD("NBIOT", "IMSI retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);

        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_FETCH_RETRY_DELAY_MS);
    }
    return st;
}

/* ============================================================
 *  Getter / 문자열 변환
 * ============================================================ */
const uint8_t          *App_Bc95AtGetImeiBcd(void)     { return g_appBc95AtImeiBcd; }
const uint8_t          *App_Bc95AtGetImsiBcd(void)     { return g_appBc95AtImsiBcd; }
const AppBc95Quality_t *App_Bc95AtGetQuality(void)     { return &g_appBc95AtQuality; }
const uint8_t          *App_Bc95AtGetQualityBcd(void)  { return g_appBc95AtQualityBcd; }

const char *App_Bc95AtGetStatusString(AppBc95AtStatus_t status)
{
    switch (status)
    {
        case APP_BC95_AT_OK:            return "ok";
        case APP_BC95_AT_ERR_PARAM:     return "param";
        case APP_BC95_AT_ERR_TIMEOUT:   return "timeout";
        case APP_BC95_AT_ERR_NO_PREFIX: return "no_prefix";
        case APP_BC95_AT_ERR_FORMAT:    return "format";
        case APP_BC95_AT_ERR_RANGE:     return "range";
        case APP_BC95_AT_ERR_AT_ERROR:  return "at_error";
        case APP_BC95_AT_ERR_CME_ERROR: return "cme_error";
        case APP_BC95_AT_ERR_NO_OK:     return "no_ok";
        case APP_BC95_AT_ERR_NO_DATA:   return "no_data";
        default:                        return "unknown";
    }
}

/* ============================================================
 *  UART ISR Callbacks
 * ============================================================ */
void App_Bc95AtOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
    HAL_StatusTypeDef halStatus;
    uint16_t nextIndex;

    if (p_huart == NULL) return;

    /* 명령 진행 중이 아니면(자가진단/계량기 프로브 등) URC 모니터로
     * 래치하고 즉시 재무장 - RX 무무장 공백을 없앤다. */
    if ((g_appBc95AtRxContext.active != APP_TRUE) ||
        (p_huart != g_appBc95AtRxContext.p_huart))
    {
        if ((s_bc95UrcMonitor.armed == APP_TRUE) &&
            (p_huart == s_bc95UrcMonitor.p_huart))
        {
            /* [2단계-2차] 공백 구간 수신은 idle-DMA 가 담당한다.
             * 1바이트 IT 는 DMA 와 충돌하므로 여기서 재무장하지 않는다.
             * ORE 카운트/복구(2단계-1차)만 유지한다. */
            if (__HAL_UART_GET_FLAG(p_huart, UART_FLAG_ORE) != RESET)
            {
                s_bc95RxErrOreCount++;
                __HAL_UART_CLEAR_FLAG(p_huart, UART_CLEAR_OREF);
            }
        }
        return;
    }
 
    nextIndex = (uint16_t)(g_appBc95AtRxContext.receivedLength + 1u);
    g_appBc95AtRxContext.receivedLength = nextIndex;

    if (nextIndex >= g_appBc95AtRxContext.bufferSize)
    {
        /* [보완2] 버퍼 만료로 수신 체인이 끊기면 그 뒤 바이트(NCONFIG? 실측 255B+
         * 초과분과 그 뒤에 붙는 EVTIND)가 전부 유실된다. 응답 캡처를 종료하고
         * URC 모니터로 우회해 EVTIND만이라도 래치한다(응답 파싱은 수신분으로 가능). */
        g_appBc95AtRxContext.active = APP_FALSE;
        App_Bc95AtArmUrcMonitor(p_huart);
        return;
    }

    /* active 한 번 더 확인 (메인 루프에서 finish 한 경우 IT 재등록 금지) */
    if (g_appBc95AtRxContext.active != APP_TRUE) return;

    halStatus = HAL_UART_Receive_IT(p_huart,
                                    &g_appBc95AtRxContext.p_buffer[nextIndex], 1u);
    if (halStatus != HAL_OK)
    {
        /* [보완1] 재무장 실패 시 1회 재시도. 그래도 실패하면 응답 캡처를 종료하고
         * URC 모니터로 전환한다 - 조용히 죽은 RX 체인 뒤로 도착하는 EVTIND가
         * 전부 버려지는 것을 막는다(모뎀은 체인 사망과 무관하게 출력을 계속함).
         * 실측(2026-10-30 001035 wake): 버스트 중 :4/:3/:8 만 래치(mask=0x0118),
         * :0 만 유실 - 응답 완료 직후 체인 사망 구간에서 URC가 통째로 버려지는 패턴. */
        halStatus = HAL_UART_Receive_IT(p_huart,
                                        &g_appBc95AtRxContext.p_buffer[nextIndex], 1u);
        if (halStatus != HAL_OK)
        {
            g_appBc95AtRxContext.error    = APP_TRUE;
            g_appBc95AtRxContext.active   = APP_FALSE;
            App_Bc95AtArmUrcMonitor(p_huart);
        }
    }
}

void App_Bc95AtOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
    uint8_t monitorWasArmed = APP_FALSE;

    if (p_huart == NULL) return;

    if ((g_appBc95AtRxContext.active == APP_TRUE) &&
        (p_huart == g_appBc95AtRxContext.p_huart))
    {
        /* 활성 RX 컨텍스트의 에러 - 기존 처리 유지(아래) */
    }
    else if ((s_bc95UrcMonitor.armed == APP_TRUE) &&
             (p_huart == s_bc95UrcMonitor.p_huart))
    {
        monitorWasArmed = APP_TRUE;
    }
    else
    {
        return;
    }
 
#ifdef DEBUG
    g_appBc95AtRxErrorInfo.errorCode    = p_huart->ErrorCode;
    g_appBc95AtRxErrorInfo.errorAtIndex = g_appBc95AtRxContext.receivedLength;
    g_appBc95AtRxErrorInfo.errorCount++;
#endif

    __HAL_UART_CLEAR_FLAG(p_huart,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(p_huart, UART_RXDATA_FLUSH_REQUEST);

    if (monitorWasArmed == APP_TRUE)
    {
        /* 모니터 모드 에러: HAL 이 수신을 중단했으므로 즉시 재무장 */
        s_bc95UrcMonitor.armed = APP_FALSE;
        App_Bc95AtArmUrcMonitor(p_huart);
    }
    else
    {
        g_appBc95AtRxContext.error = APP_TRUE;
    }
}

/* ============================================================
 *  Quality (NUESTATS=CELL)
 * ============================================================ */
static int16_t App_Bc95AtCentibelToDbm(int32_t centibel)
{
    int32_t result;
    if (centibel >= 0) result = (centibel + 5) / 10;
    else               result = (centibel - 5) / 10;
    return (int16_t)result;
}

static uint16_t App_Bc95AtDbmToAbsU16(int16_t dbm)
{
    int32_t absVal = (dbm >= 0) ? 0 : (int32_t)(-dbm);
    if (absVal > APP_BC95_AT_DBM_ABS_MAX) absVal = APP_BC95_AT_DBM_ABS_MAX;
    return (uint16_t)absVal;
}

static uint8_t App_Bc95AtDbmToAbsByte(int16_t dbm)
{
    return (uint8_t)App_Bc95AtDbmToAbsU16(dbm);
}

AppBc95AtStatus_t App_Bc95AtParseNuestatsCell(const char *p_resp, AppBc95Quality_t *p_quality)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_scan;
    const char *p_lineStart;
    const char *p_picked = NULL;
    int parsedFields;
    int earfcn = 0, pci = 0, primary = 0;
    int rsrpCb = 0, rsrqCb = 0, rssiCb = 0, snrCb = 0;

    if ((p_resp == NULL) || (p_quality == NULL)) return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_quality, 0, sizeof(*p_quality));

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if ((status != APP_BC95_AT_OK) && (status != APP_BC95_AT_ERR_NO_OK)) return status;

    p_scan = p_resp;
    while ((p_lineStart = strstr(p_scan, APP_BC95_AT_NUESTATS_CELL_PREFIX)) != NULL)
    {
        const char *p_payload = p_lineStart + APP_BC95_AT_NUESTATS_CELL_PREFIX_LEN;
        parsedFields = sscanf(p_payload, "%d,%d,%d,%d,%d,%d,%d",
                              &earfcn, &pci, &primary,
                              &rsrpCb, &rsrqCb, &rssiCb, &snrCb);
        if (parsedFields == (int)APP_BC95_AT_NUESTATS_CELL_FIELDS)
        {
            if (p_picked == NULL) p_picked = p_payload;
            if (primary == 1) { p_picked = p_payload; break; }
        }
        p_scan = p_payload;
    }

    if (p_picked == NULL) return APP_BC95_AT_ERR_NO_DATA;

    parsedFields = sscanf(p_picked, "%d,%d,%d,%d,%d,%d,%d",
                          &earfcn, &pci, &primary,
                          &rsrpCb, &rsrqCb, &rssiCb, &snrCb);
    if (parsedFields != (int)APP_BC95_AT_NUESTATS_CELL_FIELDS) return APP_BC95_AT_ERR_FORMAT;

    p_quality->earfcn      = (uint16_t)((earfcn < 0) ? 0 : (earfcn & 0xFFFF));
    p_quality->pci         = (uint16_t)((pci    < 0) ? 0 : (pci    & 0xFFFF));
    p_quality->primaryCell = (uint8_t)((primary == 1) ? 1u : 0u);
    p_quality->rsrpDbm     = App_Bc95AtCentibelToDbm((int32_t)rsrpCb);
    p_quality->rsrqDbm     = App_Bc95AtCentibelToDbm((int32_t)rsrqCb);
    p_quality->rssiDbm     = App_Bc95AtCentibelToDbm((int32_t)rssiCb);
    p_quality->snrDb       = App_Bc95AtCentibelToDbm((int32_t)snrCb);
    p_quality->ber         = 0u;
    p_quality->valid       = APP_TRUE;
    return APP_BC95_AT_OK;
}

AppStatus_t App_Bc95AtQualityToBcd(AppBc95Quality_t *pQuality, uint8_t *p_buf, uint32_t bufSize)
{
    uint16_t rsrpAbs, rsrqAbs;
    int16_t  snrVal;

    APP_RETURN_IF_FALSE((pQuality != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_buf != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((bufSize >= (uint32_t)APP_BC95_QUALITY_BCD_BYTES), APP_STATUS_INVALID_PARAM);

    (void)memset(p_buf, 0, APP_BC95_QUALITY_BCD_BYTES);
    p_buf[0] = App_Bc95AtDbmToAbsByte(pQuality->rssiDbm);
    p_buf[1] = pQuality->ber;
    p_buf[2] = (uint8_t)(pQuality->pci & 0xFFu);
    p_buf[3] = (uint8_t)((pQuality->pci >> 8u) & 0xFFu);
    rsrpAbs = App_Bc95AtDbmToAbsU16(pQuality->rsrpDbm);
    p_buf[4] = (uint8_t)(rsrpAbs & 0xFFu);
    p_buf[5] = (uint8_t)((rsrpAbs >> 8u) & 0xFFu);
    rsrqAbs = App_Bc95AtDbmToAbsU16(pQuality->rsrqDbm);
    p_buf[6] = (uint8_t)(rsrqAbs & 0xFFu);
    p_buf[7] = (uint8_t)((rsrqAbs >> 8u) & 0xFFu);
    snrVal = pQuality->snrDb;
    p_buf[8] = (uint8_t)((uint16_t)snrVal & 0xFFu);
    p_buf[9] = (uint8_t)(((uint16_t)snrVal >> 8u) & 0xFFu);
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtFetchQuality(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf, uint32_t bcdBufSize)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_quality != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_quality, 0, sizeof(*p_quality));

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NUESTATS_CELL,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    App_Bc95AtLogCmeIfPresent("NUESTATS-CELL", g_appBc95AtRxBuf, rxLen);   /* CME 22 특정 */
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_UART_TIMEOUT))
    {
        APP_LOGD("NBIOT", "NUESTATS send fail (status=%d, rxLen=%u)",
                 (int)status, (unsigned)rxLen);
        return status;
    }

    if (status == APP_STATUS_UART_TIMEOUT)
    {
        /* 부분 데이터라도 NUESTATS:CELL 라인이 있으면 파싱 가능 */
        if (rxLen < 30u)
        {
            /* 너무 짧으면 파싱 불가 */
            APP_LOGD("NBIOT", "NUESTATS timeout, too short (rxLen=%u)",
                     (unsigned)rxLen);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_LOGD("NBIOT", "NUESTATS timeout but data present (rxLen=%u), try parse",
                 (unsigned)rxLen);
        /* OK 토큰이 없을 수 있으므로 ParseNuestatsCell 가 통과할지 확인 */
    }

    atStatus = App_Bc95AtParseNuestatsCell((const char *)g_appBc95AtRxBuf, p_quality);
    if (atStatus == APP_BC95_AT_OK)
    {
        (void)memcpy(&g_appBc95AtQuality, p_quality, sizeof(g_appBc95AtQuality));

        if (p_bcdBuf != NULL)
        {
            status = App_Bc95AtQualityToBcd(p_quality, p_bcdBuf, bcdBufSize);
            if (status != APP_STATUS_OK)
            {
                APP_LOGD("NBIOT", "quality to bcd failed");
                return APP_STATUS_FATAL;
            }
        }
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_NO_DATA)
    {
        APP_LOGD("NBIOT", "NUESTATS: no cell info yet (not camped)");
        return APP_STATUS_UART_TIMEOUT;   /* 일시 실패 */
    }

    APP_LOGD("NBIOT", "NUESTATS parse fail (status=%d, rxLen=%u)",
             (int)atStatus, (unsigned)rxLen);
    return APP_STATUS_FATAL;
}

AppStatus_t App_Bc95AtFetchQualityWithRetry(AppBc95Quality_t *p_quality, uint8_t *p_bcdBuf,
                                            uint32_t bcdBufSize, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchQuality(p_quality, p_bcdBuf, bcdBufSize);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;
        if (st == APP_STATUS_FATAL) return st;   /* 영구 실패는 즉시 */

        APP_LOGD("NBIOT", "Quality retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);

        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_FETCH_RETRY_DELAY_MS);
    }
    return st;
}

/* ============================================================
 *  Print helpers
 * ============================================================ */
#define APP_BC95_PRINT_TEMP_LEN     (64u)

static void App_Bc95PrintBcd(const char *p_label, const uint8_t *p_bcd, uint32_t length)
{
    char tempChar[APP_BC95_PRINT_TEMP_LEN];
    uint32_t index, offset = 0u, remaining;
    int written;

    if ((p_label == NULL) || (p_bcd == NULL) || (length == 0u)) return;
    tempChar[0] = '\0';

    for (index = 0u; index < length; index++)
    {
        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u)) break;
        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        written = snprintf(&tempChar[offset], remaining, "%02X%s",
                           p_bcd[index],
                           (index < (length - 1u)) ? "-" : "");
        if (written <= 0) break;
        if ((uint32_t)written >= remaining)
        {
            offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u;
            break;
        }
        offset += (uint32_t)written;
    }
    APP_LOGD("NBIOT", "%s BCD: %s", p_label, tempChar);
}

static void App_Bc95PrintDecoded(const char *p_label, const uint8_t *p_bcd, uint32_t length)
{
    char tempChar[APP_BC95_PRINT_TEMP_LEN];
    uint32_t index, offset = 0u, remaining;
    uint8_t highNibble, lowNibble;
    int written;

    if ((p_label == NULL) || (p_bcd == NULL) || (length == 0u)) return;
    tempChar[0] = '\0';

    for (index = 0u; index < length; index++)
    {
        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u)) break;

        highNibble = (uint8_t)((p_bcd[index] >> 4u) & 0x0Fu);
        lowNibble  = (uint8_t)(p_bcd[index] & 0x0Fu);

        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        if (highNibble <= 9u)
            written = snprintf(&tempChar[offset], remaining, "%u", (unsigned)highNibble);
        else if (highNibble != 0x0Fu)
            written = snprintf(&tempChar[offset], remaining, "?");
        else
            written = 0;
        if (written < 0) break;
        if ((uint32_t)written >= remaining)
        { offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u; break; }
        offset += (uint32_t)written;

        if (offset >= (APP_BC95_PRINT_TEMP_LEN - 1u)) break;
        remaining = (uint32_t)APP_BC95_PRINT_TEMP_LEN - offset;
        if (lowNibble <= 9u)
            written = snprintf(&tempChar[offset], remaining, "%u", (unsigned)lowNibble);
        else if (lowNibble != 0x0Fu)
            written = snprintf(&tempChar[offset], remaining, "?");
        else
            written = 0;
        if (written < 0) break;
        if ((uint32_t)written >= remaining)
        { offset = (uint32_t)APP_BC95_PRINT_TEMP_LEN - 1u; break; }
        offset += (uint32_t)written;
    }
    APP_LOGD("NBIOT", "%s     : %s", p_label, tempChar);
}

static void App_NBIoTPrintQuality(const AppBc95Quality_t *p_q)
{
    if (p_q == NULL) return;
    APP_LOGD("NBIOT", "EARFCN: %u, PCI: %u, Primary: %u",
             (unsigned)p_q->earfcn, (unsigned)p_q->pci, (unsigned)p_q->primaryCell);
    APP_LOGD("NBIOT", "RSSI : %d dBm", (int)p_q->rssiDbm);
    APP_LOGD("NBIOT", "RSRP : %d dBm", (int)p_q->rsrpDbm);
    APP_LOGD("NBIOT", "RSRQ : %d dB",  (int)p_q->rsrqDbm);
    APP_LOGD("NBIOT", "SNR  : %d dB",  (int)p_q->snrDb);
    APP_LOGD("NBIOT", "BER  : %u (reserved)", (unsigned)p_q->ber);
}

/* ============================================================
 *  네트워크 응답 파서
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtParseCfun(const char *p_resp, int32_t *p_funOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedFun;

    if ((p_resp == NULL) || (p_funOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    *p_funOut = 0;
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CFUN_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CFUN_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;
    if (sscanf(p_payload, "%d", &parsedFun) != 1) return APP_BC95_AT_ERR_FORMAT;
    *p_funOut = (int32_t)parsedFun;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCgatt(const char *p_resp, int32_t *p_stateOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedState;

    if ((p_resp == NULL) || (p_stateOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    *p_stateOut = 0;
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CGATT_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CGATT_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;
    if (sscanf(p_payload, "%d", &parsedState) != 1) return APP_BC95_AT_ERR_FORMAT;
    *p_stateOut = (int32_t)parsedState;
    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCereg(const char *p_resp, int32_t *p_nOut, int32_t *p_statOut, int32_t *p_rejectCauseOut)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    const char *p_lineEnd;
    const char *p_lastComma;
    int parsedN, parsedStat;

    if ((p_resp == NULL) || (p_statOut == NULL)) return APP_BC95_AT_ERR_PARAM;
    if (p_nOut != NULL) *p_nOut = 0;
    if (p_rejectCauseOut != NULL) *p_rejectCauseOut = -1;
    *p_statOut = 0;
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CEREG_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CEREG_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '	')) p_payload++;
    if (sscanf(p_payload, "%d,%d", &parsedN, &parsedStat) != 2) return APP_BC95_AT_ERR_FORMAT;
    if (p_nOut != NULL) *p_nOut = (int32_t)parsedN;
    *p_statOut = (int32_t)parsedStat;

    if ((p_rejectCauseOut != NULL) && (parsedStat == APP_BC95_CEREG_DENIED))
    {
        p_lineEnd = p_payload;
        while ((*p_lineEnd != '\0') && (*p_lineEnd != ' ') && (*p_lineEnd != ' '))
        {
            p_lineEnd++;
        }

        p_lastComma = p_lineEnd;
        while ((p_lastComma > p_payload) && (*p_lastComma != ','))
        {
            p_lastComma--;
        }

        if ((*p_lastComma == ',') && ((p_lastComma + 1) < p_lineEnd))
        {
            char *p_endptr = NULL;
            long parsedReject = strtol(p_lastComma + 1, &p_endptr, 10);
            if ((p_endptr != (p_lastComma + 1)) && (parsedReject >= 0L) && (parsedReject <= 65535L))
            {
                *p_rejectCauseOut = (int32_t)parsedReject;
            }
        }
    }

    return APP_BC95_AT_OK;
}

AppBc95AtStatus_t App_Bc95AtParseCgpaddr(const char *p_resp, char *p_ipOut, uint32_t ipBufSize)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    const char *p_comma;
    uint32_t i;

    if ((p_resp == NULL) || (p_ipOut == NULL) || (ipBufSize == 0u)) return APP_BC95_AT_ERR_PARAM;
    p_ipOut[0] = '\0';
    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CGPADDR_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CGPADDR_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t')) p_payload++;

    p_comma = strchr(p_payload, ',');
    if (p_comma == NULL) return APP_BC95_AT_OK;  /* IP 미할당 */
    p_comma++;
    while ((*p_comma == ' ') || (*p_comma == '\t') || (*p_comma == '"')) p_comma++;

    i = 0u;
    while ((i < (ipBufSize - 1u)) && (*p_comma != '\0') &&
           (*p_comma != '\r') && (*p_comma != '\n') &&
           (*p_comma != '"')  && (*p_comma != ','))
    {
        p_ipOut[i++] = *p_comma++;
    }
    p_ipOut[i] = '\0';
    return APP_BC95_AT_OK;
}

/* ============================================================
 *  명령 송신 (UART 일시 오류 재시도)
 * ============================================================ */
static AppStatus_t App_Bc95AtSendWithRetry(const char *p_cmd, uint8_t *p_rxBuf,
                                           uint16_t rxBufSize, uint32_t rxTimeoutMs,
                                           uint16_t *p_rxLenOut, uint32_t maxRetry)
{
    AppStatus_t status = APP_STATUS_FATAL;
    uint32_t attempt;

    for (attempt = 0u; attempt < maxRetry; attempt++)
    {
        status = App_Bc95AtSendCommand(p_cmd, p_rxBuf, rxBufSize, rxTimeoutMs, p_rxLenOut);
        if (status == APP_STATUS_OK) return APP_STATUS_OK;

        if ((g_appBc95SilenceAbortEnable == APP_TRUE) &&
            (status == APP_STATUS_UART_TIMEOUT) &&
            ((p_rxLenOut == NULL) || (*p_rxLenOut == 0u)))
        {
            g_appBc95TotalSilenceDetected = APP_TRUE;
            APP_LOGD("NBIOT", "cmd(%s) total silence (rxLen=0) -> abort retries", p_cmd);
            return status;
        }

        if ((status == APP_STATUS_UART_RX_FAILED) ||
            (status == APP_STATUS_UART_TX_FAILED) ||
            (status == APP_STATUS_UART_TIMEOUT))
        {
            APP_LOGW("NBIOT", "cmd(%s) retry %lu/%lu (status=%d)",
                     p_cmd,
                     (unsigned long)(attempt + 1u),
                     (unsigned long)maxRetry, (int)status);
            App_Bc95AtDelayWithFeed(APP_BC95_NET_CMD_RETRY_DELAY_MS);
            continue;
        }
        break;
    }
    return status;
}

AppStatus_t App_Bc95AtSetFullFunction(void)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_LOGD("NBIOT", "Set CFUN=1 (may take up to 85s)...");

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CFUN_SET_FULL,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_CFUN_RESP_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "CFUN=1 send fail (status=%d)", (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
#ifdef NBIoT_SIMULATION_CODE
    APP_LOGD("NBIOT", "Forcely err timeout CFUN=1 test..."); // NBIoT simulation test
    atStatus = APP_BC95_AT_ERR_TIMEOUT;
#endif // NBIoT_SIMULATION_CODE
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "CFUN=1 not OK (parse=%s)",
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }
    APP_LOGD("NBIOT", "CFUN=1 done");
    return APP_STATUS_OK;
}

static uint8_t App_Bc95AtIsSuspendRejectCause(int32_t rejectCause)
{
    return (rejectCause == APP_NBIOT_REJECT_CAUSE_SUSPEND) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_Bc95AtIsTerminatedRejectCause(int32_t rejectCause)
{
    return ((rejectCause == APP_NBIOT_REJECT_CAUSE_TERMINATED_1) ||
            (rejectCause == APP_NBIOT_REJECT_CAUSE_TERMINATED_2) ||
            (rejectCause == APP_NBIOT_REJECT_CAUSE_TERMINATED_3)) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_Bc95AtIsUsimFatalCme(int32_t cmeErr)
{
    switch (cmeErr)
    {
        case 10: case 13: case 15: case 16:
        case 311: case 313: case 315: case 317: case 318:
            return APP_TRUE;
        default:
            return APP_FALSE;
    }
}

static uint8_t App_Bc95AtIsRecoverableServiceReadyCme(int32_t cmeErr)
{
    if ((cmeErr <= 0) || (App_Bc95AtIsUsimFatalCme(cmeErr) == APP_TRUE))
    {
        return APP_FALSE;
    }
    return APP_TRUE;
}

AppStatus_t App_Bc95AtQueryNetStatus(AppBc95NetStatus_t *p_status)
{
    AppStatus_t st;
    AppBc95AtStatus_t atSt;
    uint16_t rxLen;
    int32_t funVal = -1, cgattVal = -1;
    int32_t ceregN = -1, ceregStat = -1;
    int32_t rejectCause = -1;
    int32_t cmeErr = 0;

    APP_RETURN_IF_FALSE((p_status != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_status, 0, sizeof(*p_status));
    p_status->phase      = APP_BC95_NET_PHASE_INIT;
    p_status->cfunValue  = 0xFFu;
    p_status->cgattState = 0xFFu;
    p_status->ceregStat  = APP_BC95_CEREG_NOT_REGISTERED;
    p_status->rejectCause = 0u;
    p_status->rejectCauseValid = APP_FALSE;

    /* CFUN? */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CFUN_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    App_Bc95AtLogCmeIfPresent("CFUN?", g_appBc95AtRxBuf, rxLen);   /* CME 22 특정 */
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCfun((const char *)g_appBc95AtRxBuf, &funVal);
        if (atSt == APP_BC95_AT_OK) p_status->cfunValue = (uint8_t)funVal;
        else if (atSt == APP_BC95_AT_ERR_CME_ERROR)
        {
            cmeErr = 0;
            (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
            if (App_Bc95AtIsUsimFatalCme(cmeErr) == APP_TRUE)
            {
                APP_LOGD("NBIOT", "CFUN? USIM fatal (CME=%ld)", (long)cmeErr);
                p_status->phase = APP_BC95_NET_PHASE_USIM_ERROR;
                p_status->lastUpdateTick = HAL_GetTick();
                return APP_STATUS_FATAL;
            }
        }
    }

    if ((funVal >= 0) && (funVal != 1))
    {
        p_status->phase = APP_BC95_NET_PHASE_CFUN_OFF;
        p_status->lastUpdateTick = HAL_GetTick();
        return APP_STATUS_OK;
    }

    /* CEREG? */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CEREG_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCereg((const char *)g_appBc95AtRxBuf, &ceregN, &ceregStat, &rejectCause);
#ifdef NBIoT_SIMULATION_CODE
        APP_LOGD("NBIOT", "Forcely err timeout CEREG:0 test..."); // NBIoT simulation test
        atSt = APP_BC95_AT_ERR_FORMAT;
#endif // NBIoT_SIMULATION_CODE
        if (atSt == APP_BC95_AT_OK)
        {
            p_status->ceregStat = (AppBc95CeregStat_t)ceregStat;
            if ((rejectCause >= 0) && (rejectCause <= 65535))
            {
                p_status->rejectCause = (uint16_t)rejectCause;
                p_status->rejectCauseValid = APP_TRUE;
            }
        }
    }

    /* CGATT? */
    rxLen = 0u;
    st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CGATT_QUERY,
                                 g_appBc95AtRxBuf,
                                 (uint16_t)sizeof(g_appBc95AtRxBuf),
                                 APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                 APP_BC95_NET_CMD_RETRY_MAX);
    if (st == APP_STATUS_OK)
    {
        atSt = App_Bc95AtParseCgatt((const char *)g_appBc95AtRxBuf, &cgattVal);
#ifdef NBIoT_SIMULATION_CODE
        APP_LOGD("NBIOT", "Forcely err timeout CGATT:0 test..."); // NBIoT simulation test
        atSt = APP_BC95_AT_ERR_FORMAT;
#endif // NBIoT_SIMULATION_CODE
        if (atSt == APP_BC95_AT_OK)
            p_status->cgattState = (uint8_t)cgattVal;
    }

    /* CGPADDR (등록 시에만) */
    if ((p_status->ceregStat == APP_BC95_CEREG_REGISTERED_HOME) ||
        (p_status->ceregStat == APP_BC95_CEREG_REGISTERED_ROAM))
    {
        rxLen = 0u;
        st = App_Bc95AtSendWithRetry(APP_BC95_AT_CMD_CGPADDR_QUERY,
                                     g_appBc95AtRxBuf,
                                     (uint16_t)sizeof(g_appBc95AtRxBuf),
                                     APP_BC95_AT_RX_TIMEOUT_MS, &rxLen,
                                     APP_BC95_NET_CMD_RETRY_MAX);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
        App_Bc95AtLogCmeIfPresent("CGPADDR", g_appBc95AtRxBuf, rxLen);   /* CME 22 특정 */
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
        if (st == APP_STATUS_OK)
        {
            atSt = App_Bc95AtParseCgpaddr((const char *)g_appBc95AtRxBuf,
                                          p_status->ipAddr,
                                          (uint32_t)sizeof(p_status->ipAddr));
            /* 패치: "0.0.0.0" 거르기 */
            if ((atSt == APP_BC95_AT_OK) &&
                (p_status->ipAddr[0] != '\0') &&
                (strcmp(p_status->ipAddr, "0.0.0.0") != 0))
            {
                p_status->hasIp = APP_TRUE;
            }
        }
    }

    /* phase 종합 */
    if ((p_status->cfunValue != 0xFFu) && (p_status->cfunValue != 1u))
        p_status->phase = APP_BC95_NET_PHASE_CFUN_OFF;
    else if ((p_status->ceregStat == APP_BC95_CEREG_NOT_REGISTERED) ||
             (p_status->ceregStat == APP_BC95_CEREG_SEARCHING) ||
             (p_status->ceregStat == APP_BC95_CEREG_UNKNOWN))
        p_status->phase = APP_BC95_NET_PHASE_REGISTERING;
    else if (p_status->ceregStat == APP_BC95_CEREG_DENIED)
    {
        if ((p_status->rejectCauseValid == APP_TRUE) &&
            (App_Bc95AtIsSuspendRejectCause((int32_t)p_status->rejectCause) == APP_TRUE))
        {
            p_status->phase = APP_BC95_NET_PHASE_USIM_SUSPEND;
        }
        else if ((p_status->rejectCauseValid == APP_TRUE) &&
                 (App_Bc95AtIsTerminatedRejectCause((int32_t)p_status->rejectCause) == APP_TRUE))
        {
            p_status->phase = APP_BC95_NET_PHASE_USIM_TERMINATED;
        }
        else
        {
            p_status->phase = APP_BC95_NET_PHASE_DENIED;
        }
    }
    else if ((p_status->ceregStat == APP_BC95_CEREG_REGISTERED_HOME) ||
             (p_status->ceregStat == APP_BC95_CEREG_REGISTERED_ROAM))
    {
        if (p_status->cgattState != 1u)        p_status->phase = APP_BC95_NET_PHASE_ATTACHING;
        else if (p_status->hasIp != APP_TRUE)  p_status->phase = APP_BC95_NET_PHASE_WAITING_IP;
        else { p_status->phase = APP_BC95_NET_PHASE_READY; p_status->ready = APP_TRUE; }
    }
    else
        p_status->phase = APP_BC95_NET_PHASE_REGISTERING;

    p_status->lastUpdateTick = HAL_GetTick();
    (void)memcpy(&g_appBc95AtNetStatus, p_status, sizeof(g_appBc95AtNetStatus));
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtWaitForNetwork(uint32_t totalTimeoutMs, AppBc95NetStatus_t *p_status)
{
    AppStatus_t st;
    AppBc95NetStatus_t snapshot;
    uint32_t startTick;
    uint32_t elapsed;
    uint32_t effectiveTimeoutMs;
    uint32_t pollCount   = 0u;
    uint32_t deniedCount = 0u;
    uint32_t registeringPollCount = 0u;
    uint32_t registeringGraceMs = APP_BC95_NET_REGISTERING_GRACE_MS;
    uint8_t  cfunFixDone = APP_FALSE;
    uint8_t  registeringGraceUsed = APP_FALSE;
    uint8_t  registeringMinPolls = APP_BC95_NET_REGISTERING_STUCK_MIN_POLLS;
    AppBc95NetPhase_t lastPhase = APP_BC95_NET_PHASE_INIT;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);

    if (p_status != NULL) (void)memset(p_status, 0, sizeof(*p_status));
    (void)memset(&snapshot, 0, sizeof(snapshot));

    if ((g_appBc95LastRecoverableCmeWas524 == APP_TRUE) ||
        (g_appBc95ServiceReadyRecoveryActive == APP_TRUE) ||
        (g_appBc95WakeNeedsCme524AttachBackoff == APP_TRUE))
    {
        registeringGraceMs = APP_BC95_NET_REGISTERING_GRACE_MS_FAST_RECOVERY;
        registeringMinPolls = APP_BC95_NET_REGISTERING_STUCK_MIN_POLLS_FAST_RECOVERY;
    }

    startTick = HAL_GetTick();
    effectiveTimeoutMs = totalTimeoutMs;

    APP_LOGI("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
             " registering-stuck policy grace=%lums minPolls=%u last524=%u recoveryActive=%u cme524Backoff=%u",
             (unsigned long)registeringGraceMs,
             (unsigned)registeringMinPolls,
             (unsigned)g_appBc95LastRecoverableCmeWas524,
             (unsigned)g_appBc95ServiceReadyRecoveryActive,
             (unsigned)g_appBc95WakeNeedsCme524AttachBackoff);

    while (1)
    {
        elapsed = HAL_GetTick() - startTick;
        st = App_Bc95AtQueryNetStatus(&snapshot);
        snapshot.pollCount   = ++pollCount;
        snapshot.deniedCount = deniedCount;

        if (st == APP_STATUS_FATAL)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " Net query fatal (phase=%s)",
                     App_Bc95AtGetNetPhaseString(snapshot.phase));
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_FATAL;
        }

        if (st != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "poll #%lu: query failed (%d)",
                     (unsigned long)pollCount, (int)st);
        }
        else
        {
            if (snapshot.phase != lastPhase)
            {
                APP_LOGD("NBIOT", "poll #%lu: phase=%s, CFUN=%u, CEREG=%s, CGATT=%u, rejectCause=%u, IP='%s'",
                         (unsigned long)pollCount,
                         App_Bc95AtGetNetPhaseString(snapshot.phase),
                         (unsigned)snapshot.cfunValue,
                         App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                         (unsigned)snapshot.cgattState,
                         (unsigned)snapshot.rejectCause,
                         snapshot.ipAddr);
                lastPhase = snapshot.phase;
            }

            if ((snapshot.phase == APP_BC95_NET_PHASE_REGISTERING) &&
                (snapshot.rejectCauseValid != APP_TRUE) &&
                (snapshot.cfunValue == 1u))
            {
                if (registeringPollCount < 0xFFFFFFFFu)
                {
                    registeringPollCount++;
                }
            }
            else
            {
                registeringPollCount = 0u;
            }

            if (snapshot.phase == APP_BC95_NET_PHASE_READY)
            {
                APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                    " Network ready (elapsed=%lums, ip=%s, poll=%lu)",
                         (unsigned long)elapsed, snapshot.ipAddr, (unsigned long)pollCount);
                if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                return APP_STATUS_OK;
            }

            if (snapshot.phase == APP_BC95_NET_PHASE_USIM_ERROR)
            {
                APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                    " USIM permanent error, give up");
                if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                return APP_STATUS_FATAL;
            }

            if (snapshot.phase == APP_BC95_NET_PHASE_USIM_SUSPEND)
            {
                APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                    " USIM suspended by network (rejectCause=%u)",
                         (unsigned)snapshot.rejectCause);
                if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                return APP_STATUS_FATAL;
            }

            if (snapshot.phase == APP_BC95_NET_PHASE_USIM_TERMINATED)
            {
                APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                    " USIM terminated by network (rejectCause=%u)",
                         (unsigned)snapshot.rejectCause);
                if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                return APP_STATUS_FATAL;
            }

            if (snapshot.phase == APP_BC95_NET_PHASE_CFUN_OFF)
            {
                if (cfunFixDone != APP_TRUE)
                {
                    cfunFixDone = APP_TRUE;
                    APP_LOGD("NBIOT", "CFUN=0, send AT+CFUN=1");
                    (void)App_Bc95AtSetFullFunction();
                }
            }
            else if (snapshot.phase == APP_BC95_NET_PHASE_DENIED)
            {
                deniedCount++;
                if (deniedCount >= APP_BC95_NET_DENIED_RETRY_MAX)
                {
                    APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                        " Registration denied %lu times, give up",
                             (unsigned long)deniedCount);
                    if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
                    return APP_STATUS_FATAL;
                }
                APP_LOGD("NBIOT", "Denied (%lu/%lu), keep polling",
                         (unsigned long)deniedCount,
                         (unsigned long)APP_BC95_NET_DENIED_RETRY_MAX);
            }

            if ((registeringGraceUsed != APP_TRUE) &&
                (elapsed >= totalTimeoutMs) &&
                (snapshot.phase == APP_BC95_NET_PHASE_REGISTERING) &&
                (snapshot.rejectCauseValid != APP_TRUE) &&
                (registeringPollCount >= registeringMinPolls))
            {
                registeringGraceUsed = APP_TRUE;
                effectiveTimeoutMs = totalTimeoutMs + registeringGraceMs;
                APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                         " registering timeout relaxed by %lums (poll=%lu streak=%lu newTimeout=%lums)",
                         (unsigned long)registeringGraceMs,
                         (unsigned long)pollCount,
                         (unsigned long)registeringPollCount,
                         (unsigned long)effectiveTimeoutMs);
            }
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= effectiveTimeoutMs)
        {
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                " Network wait timeout (poll=%lu, lastPhase=%s, elapsed=%lums, registeringStreak=%lu, grace=%u)",
                     (unsigned long)pollCount,
                     App_Bc95AtGetNetPhaseString(lastPhase),
                     (unsigned long)elapsed,
                     (unsigned long)registeringPollCount,
                     (unsigned)registeringGraceUsed);
            if (p_status != NULL) (void)memcpy(p_status, &snapshot, sizeof(*p_status));
            return APP_STATUS_UART_TIMEOUT;
        }

        App_Bc95AtDelayWithFeed(APP_BC95_NET_POLL_INTERVAL_MS);
    }
}

const char *App_Bc95AtGetCeregStatString(AppBc95CeregStat_t stat)
{
    switch (stat)
    {
        case APP_BC95_CEREG_NOT_REGISTERED:   return "not_registered";
        case APP_BC95_CEREG_REGISTERED_HOME:  return "home";
        case APP_BC95_CEREG_SEARCHING:        return "searching";
        case APP_BC95_CEREG_DENIED:           return "denied";
        case APP_BC95_CEREG_UNKNOWN:          return "unknown";
        case APP_BC95_CEREG_REGISTERED_ROAM:  return "roaming";
        default:                              return "invalid";
    }
}

const char *App_Bc95AtGetNetPhaseString(AppBc95NetPhase_t phase)
{
    switch (phase)
    {
        case APP_BC95_NET_PHASE_INIT:         return "init";
        case APP_BC95_NET_PHASE_CFUN_OFF:     return "cfun_off";
        case APP_BC95_NET_PHASE_USIM_ERROR:   return "usim_error";
        case APP_BC95_NET_PHASE_REGISTERING:  return "registering";
        case APP_BC95_NET_PHASE_ATTACHING:    return "attaching";
        case APP_BC95_NET_PHASE_WAITING_IP:   return "waiting_ip";
        case APP_BC95_NET_PHASE_READY:           return "ready";
        case APP_BC95_NET_PHASE_DENIED:          return "denied";
        case APP_BC95_NET_PHASE_USIM_SUSPEND:    return "usim_suspend";
        case APP_BC95_NET_PHASE_USIM_TERMINATED: return "usim_terminated";
        default:                                 return "invalid";
    }
}

const AppBc95NetStatus_t *App_Bc95AtGetLastNetStatus(void) { return &g_appBc95AtNetStatus; }

static const char *App_NbiotCarrierAttachStateString(AppNbiotCarrierAttachState_t state)
{
    switch (state)
    {
        case APP_NBIOT_ATTACH_STATE_IDLE:         return "idle";
        case APP_NBIOT_ATTACH_STATE_BOOT_WAIT:    return "boot_wait";
        case APP_NBIOT_ATTACH_STATE_NETWORK_WAIT: return "network_wait";
        case APP_NBIOT_ATTACH_STATE_READY:        return "ready";
        case APP_NBIOT_ATTACH_STATE_SW_RESET:     return "sw_reset";
        case APP_NBIOT_ATTACH_STATE_HW_RESET:     return "hw_reset";
        case APP_NBIOT_ATTACH_STATE_ABORT:        return "abort";
        default:                                  return "invalid";
    }
}

static const char *App_NbiotCarrierPowerOffStateString(AppNbiotCarrierPowerOffState_t state)
{
    switch (state)
    {
        case APP_NBIOT_POWEROFF_STATE_IDLE:        return "idle";
        case APP_NBIOT_POWEROFF_STATE_DETACH_REQ:  return "detach_req";
        case APP_NBIOT_POWEROFF_STATE_WAIT_CEREG0: return "wait_cereg0";
        case APP_NBIOT_POWEROFF_STATE_FORCE_OFF:   return "force_off";
        case APP_NBIOT_POWEROFF_STATE_DONE:        return "done";
        default:                                   return "invalid";
    }
}

static const char *App_NbiotCarrierResetTypeString(AppNbiotCarrierResetType_t type)
{
    switch (type)
    {
        case APP_NBIOT_CARRIER_RESET_NONE: return "none";
        case APP_NBIOT_CARRIER_RESET_SW:   return "sw";
        case APP_NBIOT_CARRIER_RESET_HW:   return "hw";
        default:                           return "invalid";
    }
}

static AppStatus_t App_Bc95AtSendSimpleOkCommand(const char *p_cmd,
                                                 uint32_t rxTimeoutMs,
                                                 const char *p_cmdLabel,
                                                 const char *p_logPrefix)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_cmd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_cmdLabel != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_logPrefix != NULL), APP_STATUS_INVALID_PARAM);

    APP_LOGD("NBIOT", "%s TX %s", p_logPrefix, p_cmdLabel);
    status = App_Bc95AtSendWithRetry(p_cmd,
                                     g_appBc95AtRxBuf,
                                     (uint16_t)sizeof(g_appBc95AtRxBuf),
                                     rxTimeoutMs,
                                     &rxLen,
                                     APP_NBIOT_AT_NORESP_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "%s %s send fail. rxLen=%u (status=%d)", p_logPrefix, p_cmdLabel, (unsigned)rxLen, (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "%s %s not OK (parse=%s)",
                 p_logPrefix, p_cmdLabel, App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGD("NBIOT", "%s %s OK", p_logPrefix, p_cmdLabel);
    return APP_STATUS_OK;
}

static uint8_t App_Bc95AtIcasePrefixMatch(const char *p_text, const char *p_token, uint16_t tokenLen)
{
    uint16_t i;

    if ((p_text == NULL) || (p_token == NULL)) return APP_FALSE;

    for (i = 0u; i < tokenLen; i++)
    {
        char a = p_text[i];
        char b = p_token[i];

        if ((a >= 'A') && (a <= 'Z')) a = (char)(a + ('a' - 'A'));
        if ((b >= 'A') && (b <= 'Z')) b = (char)(b + ('a' - 'A'));
        if (a != b) return APP_FALSE;
    }
    return APP_TRUE;
}

static uint8_t App_Bc95AtNconfigTokenHasValue(const char *p_resp,
                                              const char *p_key,
                                              const char *p_value)
{
    size_t   keyLen;
    size_t   valLen;
    const char *p_pos;

    if ((p_resp == NULL) || (p_key == NULL) || (p_value == NULL)) return APP_FALSE;

    keyLen = strlen(p_key);
    valLen = strlen(p_value);
    if ((keyLen == 0u) || (valLen == 0u)) return APP_FALSE;

    p_pos = p_resp;
    while ((p_pos = strstr(p_pos, p_key)) != NULL)
    {
        const char *p_colon = p_pos + keyLen;

        while ((*p_colon == ' ') || (*p_colon == '\t') ||
               (*p_colon == '\r') || (*p_colon == '\n'))
        {
            p_colon++;
        }
        /* 실측: 응답 포맷은 "+NCONFIG:KEY,VALUE" - 구분자는 콤마(콜론도 허용) */
        if ((*p_colon == ':') || (*p_colon == ','))
        {
            const char *p_val = p_colon + 1u;
            char boundary;

            while ((*p_val == ' ') || (*p_val == '\t')) p_val++;
            if (App_Bc95AtIcasePrefixMatch(p_val, p_value, (uint16_t)valLen) == APP_TRUE)
            {
                boundary = p_val[valLen];
                if ((boundary == '\0') || (boundary == '\r') || (boundary == '\n') ||
                    (boundary == ',')  || (boundary == ' ')  || (boundary == '\t'))
                {
                    return APP_TRUE;
                }
            }
        }
        p_pos += keyLen;
    }
    return APP_FALSE;
}

/*
 * NCONFIG 쓰기 idempotent화 (v2):
 *  1) AT+NCONFIG? 를 전용 640B 버퍼로 수신 (응답이 256B 초과 - 실측)
 *  2) OK 뿐 아니라 timeout+부분수신(rxLen>0)에서도 토큰 판정
 *     (AUTOCONNECT는 응답 첫 줄이라 부분 수신만으로 판정 가능)
 *  3) 이미 원하는 값이면 NVM 쓰기 생략 (크래시 지점 우회 + 플래시 수명 절감)
 *  4) 모든 TX 전에 500ms 정적 지연 - 응답 꼬리가 흐르는 동안 보낸 AT 입력을
 *     모듈이 드랍하는 것이 실측되었으므로(set TX 직후 len=20 잔여 꼬리 + 이후 침묵),
 *     스트림이 끝난 뒤에만 명령을 보낸다
 *  5) query 실패/키 미발견이면 기존대로 set 진행 (conservative fallback)
 */
/*
 * NCONFIG 동기화 (v3) - 쿼리 1회로 두 키를 모두 판정:
 *  1) AT+NCONFIG? 를 전용 640B 버퍼로 "수신하고 그 같은 버퍼를" 파싱
 *     (수신 버퍼 != 파싱 버퍼면 스킵이 절대 발동하지 않음 - v2 실측)
 *  2) OK 뿐 아니라 timeout+부분수신(rxLen>0)에서도 파싱
 *     (AUTOCONNECT는 응답 첫 줄이라 부분 수신만으로 판정 가능)
 *  3) 판정 결과를 로그로 명시: already -> skip / not confirmed -> will set
 *  4) 필요한 set만 수행. 둘 다 스킵이면 NVM 쓰기 0회 - 웨지 지점 우회
 *  5) 모든 TX 전 500ms+100ms 정적 - 응답 꼬리가 흐르는 중 AT 입력은 모듈이 드랍(실측)
 *  6) query 자체 실패 시 기존대로 둘 다 set (conservative fallback)
 */
static AppStatus_t App_Bc95AtNconfigSyncIfNeeded(const char *p_key1, const char *p_val1,
                                                 const char *p_cmd1, const char *p_label1,
                                                 const char *p_key2, const char *p_val2,
                                                 const char *p_cmd2, const char *p_label2)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    uint16_t safeLen;
    uint8_t  need1 = APP_TRUE;
    uint8_t  need2 = APP_TRUE;

    APP_LOGN("NBIOT", "[[BootTrack]] TX AT+NCONFIG? (sync)");
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NCONFIG_QUERY,
                                   g_appBc95NcfgQueryBuf,   /* 수신 = 파싱 같은 버퍼 */
                                   (uint16_t)sizeof(g_appBc95NcfgQueryBuf),
                                   APP_NBIOT_NCONFIG_QUERY_RX_TIMEOUT_MS,
                                   &rxLen);
    APP_LOGN("NBIOT", "[[BootTrack]] NCONFIG? rx status=%d rxLen=%u",
             (int)status, (unsigned)rxLen);

    if ((status == APP_STATUS_OK) ||
        ((status == APP_STATUS_UART_TIMEOUT) && (rxLen > 0u)))
    {
        safeLen = (rxLen < (uint16_t)(sizeof(g_appBc95NcfgQueryBuf) - 1u))
                      ? rxLen : (uint16_t)(sizeof(g_appBc95NcfgQueryBuf) - 1u);
        g_appBc95NcfgQueryBuf[safeLen] = (uint8_t)'\0';

        if (App_Bc95AtNconfigTokenHasValue((const char *)g_appBc95NcfgQueryBuf,
                                           p_key1, p_val1) == APP_TRUE)
        {
            APP_LOGN("NBIOT", "[[BootTrack]] %s already %s -> skip set", p_label1, p_val1);
            need1 = APP_FALSE;
        }
        else
        {
            APP_LOGW("NBIOT", "[[BootTrack]] %s not confirmed (rxLen=%u) -> will set",
                     p_label1, (unsigned)rxLen);
        }

        if (App_Bc95AtNconfigTokenHasValue((const char *)g_appBc95NcfgQueryBuf,
                                           p_key2, p_val2) == APP_TRUE)
        {
            APP_LOGN("NBIOT", "[[BootTrack]] %s already %s -> skip set", p_label2, p_val2);
            need2 = APP_FALSE;
        }
        else
        {
            APP_LOGW("NBIOT", "[[BootTrack]] %s not confirmed (rxLen=%u) -> will set",
                     p_label2, (unsigned)rxLen);
        }
    }
    else
    {
        APP_LOGW("NBIOT", "[[BootTrack]] AT+NCONFIG? query failed (status=%d rxLen=%u) -> will set both",
                 (int)status, (unsigned)rxLen);
    }

    /* [NRB 최적화 1단계] 이번 동기화에서 실제 set한 키 개수(0=이미 원하는 값).
     * 부팅 설정은 이 카운트가 0이면 NRB를 생략한다. */
    g_appBc95NconfigSyncSetCount = (uint8_t)(((need1 == APP_TRUE) ? 1u : 0u) +
                                             ((need2 == APP_TRUE) ? 1u : 0u));

    if ((need1 == APP_FALSE) && (need2 == APP_FALSE))
    {
        /* NVM 쓰기 0회: 웨지 지점(쓰기 직후)에 아예 진입하지 않는다 */
        App_Bc95AtDelayWithFeed(APP_NBIOT_NCONFIG_QUERY_SETTLE_MS);
        return APP_STATUS_OK;
    }

    /* 응답 꼬리가 모두 흐른 뒤 set을 보낸다 (TX-during-TX 드랍 방지) */
    App_Bc95AtDelayWithFeed(APP_NBIOT_NCONFIG_QUERY_SETTLE_MS);
    App_Bc95AtDelayWithFeed(APP_NBIOT_NVM_WRITE_GAP_MS);

    if (need1 == APP_TRUE)
    {
        status = App_Bc95AtSendSimpleOkCommand(p_cmd1,
                                               APP_BC95_AT_RX_TIMEOUT_MS,
                                               p_label1,
                                               "[[BootTrack]]");
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    }
    if (need2 == APP_TRUE)
    {
        status = App_Bc95AtSendSimpleOkCommand(p_cmd2,
                                               APP_BC95_AT_RX_TIMEOUT_MS,
                                               p_label2,
                                               "[[BootTrack]]");
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    }
    return APP_STATUS_OK;
}

static AppStatus_t App_Bc95AtSendBestEffortQuery(const char *p_cmd,
                                                 uint32_t rxTimeoutMs,
                                                 const char *p_cmdLabel,
                                                 const char *p_logPrefix)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_cmd != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_cmdLabel != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_logPrefix != NULL), APP_STATUS_INVALID_PARAM);

    APP_LOGD("NBIOT", "%s TX %s", p_logPrefix, p_cmdLabel);
    status = App_Bc95AtSendCommand(p_cmd,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   rxTimeoutMs,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "%s %s skipped (status=%d)", p_logPrefix, p_cmdLabel, (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "%s %s not OK (parse=%s)",
                 p_logPrefix, p_cmdLabel, App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGD("NBIOT", "%s %s OK", p_logPrefix, p_cmdLabel);
    return APP_STATUS_OK;
}

static AppStatus_t App_Bc95AtSendBestEffortSimpleOk(const char *p_cmd,
                                                    uint32_t rxTimeoutMs,
                                                    const char *p_cmdLabel,
                                                    const char *p_logPrefix)
{
    AppStatus_t status;

    status = App_Bc95AtSendSimpleOkCommand(p_cmd, rxTimeoutMs, p_cmdLabel, p_logPrefix);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "%s %s skipped (status=%d)", p_logPrefix, p_cmdLabel, (int)status);
    }
    return status;
}

typedef enum
{
    APP_BC95_SEQ_STEP_REQUIRED_OK = 0,
    APP_BC95_SEQ_STEP_BESTEFFORT_OK,
    APP_BC95_SEQ_STEP_BESTEFFORT_QUERY,
    APP_BC95_SEQ_STEP_REBOOT,
    /* AUTOCONNECT=TRUE idempotent set (프로비저닝 원래 위치 유지) */
    APP_BC95_SEQ_STEP_NCONFIG_AUTOCONNECT_ON_IDEM
} AppBc95SeqStepType_t;

typedef struct
{
    AppBc95SeqStepType_t type;
    const char          *p_cmd;
    uint32_t             rxTimeoutMs;
    const char          *p_cmdLabel;
} AppBc95SeqStep_t;

static AppStatus_t App_Bc95AtRunSequence(const AppBc95SeqStep_t *p_steps,
                                         uint32_t stepCount,
                                         const char *p_logPrefix)
{
    uint32_t index;
    AppStatus_t status = APP_STATUS_OK;

    APP_RETURN_IF_FALSE(p_steps != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(p_logPrefix != NULL, APP_STATUS_INVALID_PARAM);

    for (index = 0u; index < stepCount; index++)
    {
        const AppBc95SeqStep_t *p_step = &p_steps[index];

        switch (p_step->type)
        {
            case APP_BC95_SEQ_STEP_REQUIRED_OK:
                status = App_Bc95AtSendSimpleOkCommand(p_step->p_cmd,
                                                       p_step->rxTimeoutMs,
                                                       p_step->p_cmdLabel,
                                                       p_logPrefix);
                APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
                break;

            case APP_BC95_SEQ_STEP_BESTEFFORT_OK:
                (void)App_Bc95AtSendBestEffortSimpleOk(p_step->p_cmd,
                                                       p_step->rxTimeoutMs,
                                                       p_step->p_cmdLabel,
                                                       p_logPrefix);
                break;

            case APP_BC95_SEQ_STEP_BESTEFFORT_QUERY:
                (void)App_Bc95AtSendBestEffortQuery(p_step->p_cmd,
                                                    p_step->rxTimeoutMs,
                                                    p_step->p_cmdLabel,
                                                    p_logPrefix);
                break;

            case APP_BC95_SEQ_STEP_REBOOT:
                status = App_Bc95AtSendRebootBestEffort();
                APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
                break;

            case APP_BC95_SEQ_STEP_NCONFIG_AUTOCONNECT_ON_IDEM:
                status = App_Bc95AtNconfigSetTrueIfNeeded();
                APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
                break;

            default:
                return APP_STATUS_INVALID_PARAM;
        }
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_Bc95AtRecoverFromServiceReadyCme(int32_t cmeErr)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int32_t funVal = -1;
    int32_t cfunCmeErr = 0;
    uint8_t isCme524 = (cmeErr == 524) ? APP_TRUE : APP_FALSE;

    APP_LOGW("NBIOT", "[[ReadyProbe]] recoverable CME=%ld streak threshold reached -> query CFUN for recovery",
             (long)cmeErr);

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CFUN_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", "[[ReadyProbe]] CFUN? query failed during recoverable-CME recovery (status=%d, cme=%ld)",
                 (int)status,
                 (long)cmeErr);
        return status;
    }

    atStatus = App_Bc95AtParseCfun((const char *)g_appBc95AtRxBuf, &funVal);
    if (atStatus == APP_BC95_AT_ERR_CME_ERROR)
    {
        (void)App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cfunCmeErr);
        APP_LOGW("NBIOT", "[[ReadyProbe]] CFUN? returned CME=%ld during recoverable-CME recovery (origin=%ld)",
                 (long)cfunCmeErr,
                 (long)cmeErr);
        return APP_STATUS_UART_TIMEOUT;
    }
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGW("NBIOT", "[[ReadyProbe]] CFUN? parse failed during recoverable-CME recovery (origin=%ld, parse=%s)",
                 (long)cmeErr,
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_UART_TIMEOUT;
    }

    APP_LOGN("NBIOT", "[[ReadyProbe]] CFUN?=%ld during recoverable-CME recovery (origin=%ld)",
             (long)funVal,
             (long)cmeErr);

    /*
     * recoverable CME를 한 번 밟은 wake 세션은 이후 probe/attach를
     * 더 보수적으로 처리하도록 플래그를 먼저 세운다.
     */
    g_appBc95ServiceReadyRecoveryActive = APP_TRUE;
    g_appBc95WakeNeedsPreAttachNetReset = APP_TRUE;
    g_appBc95LastRecoverableCmeWas524 = isCme524;
    g_appBc95LastRecoverableCmeTick = HAL_GetTick();

    if (isCme524 == APP_TRUE)
    {
        g_appBc95WakeNeedsCme524AttachBackoff = APP_TRUE;
        g_appBc95WakeSkipPreAttachNetResetOnce = APP_TRUE;
        g_appBc95Cme524AttachRecheckCount = 0u;
        APP_LOGW("NBIOT",
                 "[[ReadyProbe]] CME=524 detected -> skip pre-attach net reset once after stabilize");
    }

    if (funVal == 0)
    {
        status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CFUN_SET_FULL,
                                               APP_BC95_AT_RX_TIMEOUT_MS,
                                               "AT+CFUN=1",
                                               "[[ReadyProbe]]");
        if (status == APP_STATUS_OK)
        {
            APP_LOGW("NBIOT",
                     "[[ReadyProbe]] recoverable-CME CFUN recovery accepted -> conservative wake policy armed (cme=%ld, attachBackoff=%u)",
                     (long)cmeErr,
                     (unsigned)g_appBc95WakeNeedsCme524AttachBackoff);
        }
        else
        {
            APP_LOGW("NBIOT",
                     "[[ReadyProbe]] recoverable-CME CFUN recovery send failed (status=%d) -> attach stage keeps conservative wake policy",
                     (int)status);
        }
        return status;
    }

    APP_LOGW("NBIOT",
             "[[ReadyProbe]] CFUN already active (%ld) for recoverable CME=%ld -> keep conservative wake policy and escalate to attach recovery",
             (long)funVal,
             (long)cmeErr);

    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_Bc95AtSendRebootBestEffort(void)
{
    HAL_StatusTypeDef halStatus;
    uint16_t cmdLen;
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    cmdLen = (uint16_t)strlen(APP_BC95_AT_CMD_NRB);
    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);
    APP_LOGN("NBIOT", "[[BootTrack]] TX AT+NRB");
    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)APP_BC95_AT_CMD_NRB,
                                  cmdLen,
                                  APP_BC95_AT_TX_TIMEOUT_MS);
    if (halStatus != HAL_OK)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] AT+NRB TX failed");
        return APP_STATUS_UART_TX_FAILED;
    }

    App_Bc95AtDelayWithFeed(300u);
    status = App_Bc95AtWaitUntilReady(APP_BC95_BOOT_WAIT_BANNER_MS + APP_BC95_USIM_READY_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] AT+NRB wait-ready failed (status=%d)", (int)status);
        return status;
    }

    APP_LOGN("NBIOT", "[[BootTrack]] AT+NRB ready");
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotRunBootConfigurationSequenceCore(void)
{
    AppStatus_t status;

    /* [NCONFIG-QUERY-SKIP] BKP 캐시가 유효하면 255B NCONFIG? 쿼리를 생략한다.
     *  - state=FALSE(스티어링 완료): 부팅 sync 목적(AUTOCONNECT=FALSE)이 이미
     *    충족돼 있으므로 sync set 0회 -> 기존 boot_nrb_optimize 경로대로 NRB 스킵.
     *  - state=TRUE(provisioning NRB 직후): NVM=TRUE가 맞는 상태이므로 여기서는
     *    FALSE로 되돌리지 않고 sync 생략. CELL_RESELECTION은 provisioning에서
     *    이미 TRUE 적용 -> NRB 스킵 대상.
     * POR(갑작스런 전원) 시 BKP가 클리어되어 이 분기에 진입하지 않고
     * 기존 쿼리 경로로 폴백한다. */
    {
        uint8_t bkpIsFalse = 0u;
        if (App_NbiotNcfgBkpGetState(&bkpIsFalse) == 1u)
        {
            APP_LOGN("NBIOT", "[[BootTrack]] BKP state=%s -> skip NCONFIG? query (trusted)",
                     (bkpIsFalse != 0u) ? "FALSE" : "TRUE");
            g_appBc95NconfigSyncSetCount = 0u;
            /* 이 시퀀스의 나머지(NCONFIG sync + NRB)는 생략하고 종료:
             * QGMR/QLWFOTAIND/QREGSWT=2 등 선행 시퀀스는 이미 위에서 수행됨. */
            return APP_STATUS_OK;
        }
    }

    static const AppBc95SeqStep_t kBootCfgSeq[] =
    {
        { APP_BC95_SEQ_STEP_BESTEFFORT_QUERY, APP_BC95_AT_CMD_QGMR_QUERY,                APP_BC95_AT_RX_TIMEOUT_MS, "AT+QGMR" },
        { APP_BC95_SEQ_STEP_BESTEFFORT_OK,    APP_BC95_AT_CMD_QLWFOTAIND_OFF,            APP_BC95_AT_RX_TIMEOUT_MS, "AT+QLWFOTAIND=0" },
        { APP_BC95_SEQ_STEP_REQUIRED_OK,      APP_BC95_AT_CMD_QREGSWT_SET_2,             APP_BC95_AT_RX_TIMEOUT_MS, "AT+QREGSWT=2" },
        /* NVM debug */
        /*
        { APP_BC95_SEQ_STEP_REQUIRED_OK,      APP_BC95_AT_CMD_NCONFIG_AUTOCONNECT_ON,    APP_BC95_AT_RX_TIMEOUT_MS, "AT+NCONFIG=AUTOCONNECT,TRUE" },
        { APP_BC95_SEQ_STEP_REQUIRED_OK,      APP_BC95_AT_CMD_NCONFIG_SCRAMBLEING,       APP_BC95_AT_RX_TIMEOUT_MS, "AT+NCONFIG=CR_0354_0338_SCRAMBLING,TRUE" },
        */
    };

    status = App_Bc95AtWaitUntilReady(APP_BC95_BOOT_WAIT_BANNER_MS + APP_BC95_USIM_READY_TIMEOUT_MS);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_Bc95AtRunSequence(kBootCfgSeq,
                                   (uint32_t)(sizeof(kBootCfgSeq) / sizeof(kBootCfgSeq[0])),
                                   "[[BootTrack]]");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_Bc95AtNconfigSyncIfNeeded(
                 "AUTOCONNECT", "FALSE",
                 APP_BC95_AT_CMD_NCONFIG_AUTOCONNECT_OFF, "AT+NCONFIG=AUTOCONNECT,FALSE",
                 "CELL_RESELECTION", "TRUE",
                 APP_BC95_AT_CMD_NCONFIG_CELL_RESELECTION_ON, "AT+NCONFIG=CELL_RESELECTION,TRUE");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* [NRB 최적화 1단계] 모뎀은 세션 시작의 GPIO 전원 온에서 이미 리셋되므로
     * NCONFIG 동기화에서 NVM set이 0회였다면(직전 세션 종료 스티어링 성공)
     * 적용할 설정이 없어 NRB 없이 진행한다. set이 1회라도 있으면 기존대로
     * NRB로 설정을 확정한다(쿼리 실패 -> conservative set 2회 -> NRB 유지). */
    if (g_appBc95NconfigSyncSetCount == 0u)
    {
        APP_LOGN("NBIOT", "[[BootTrack]] NCONFIG already synced -> skip boot NRB (power-on reset applied)");
        return APP_STATUS_OK;
    }

    return App_Bc95AtSendRebootBestEffort();
}

/*
 * rxLen=0 완전 침묵(모듈 hang) 감지 시:
 *  - retry/soft recovery 생략 (재전송으로 살아날 가능성 없음)
 *  - 즉시 GPIO 전원 리파워 후 boot-config sequence 1회 재실행
 *  - 리파워 직후 지연 없음: Core 첫 단계 WaitUntilReady가 boot banner(10s+15s)를 담당
 */
static AppStatus_t App_NbiotRunBootConfigurationSequence(void)
{
    AppStatus_t status;

    g_appBc95SilenceAbortEnable  = APP_TRUE;
    g_appBc95TotalSilenceDetected = APP_FALSE;

    status = App_NbiotRunBootConfigurationSequenceCore();
    g_appBc95SilenceAbortEnable = APP_FALSE;

    if ((status != APP_STATUS_OK) && (g_appBc95TotalSilenceDetected == APP_TRUE))
    {
        APP_LOGW("NBIOT", "[[BootTrack]] modem total silence detected -> immediate power cycle, retry boot config once");

        App_HwNbiotPowerCycle();
        (void)App_NBIoTAtInit();
        g_appBc95TotalSilenceDetected = APP_FALSE;

        g_appBc95SilenceAbortEnable = APP_TRUE;
        status = App_NbiotRunBootConfigurationSequenceCore();
        g_appBc95SilenceAbortEnable = APP_FALSE;
    }

    return status;
}

static AppStatus_t App_NbiotRunBoardAttachVerificationSequence(void)
{
    AppStatus_t status;
    static const AppBc95SeqStep_t kAttachVerifyPreSeq[] =
    {
        { APP_BC95_SEQ_STEP_REQUIRED_OK,      APP_BC95_AT_CMD_NBAND_SET,       APP_BC95_AT_RX_TIMEOUT_MS, "AT+NBAND=" APP_BC95_PLATFORM_LGU_NBAND_STRING },
        { APP_BC95_SEQ_STEP_BESTEFFORT_QUERY, APP_BC95_AT_CMD_CPSMS_QUERY,     APP_BC95_AT_RX_TIMEOUT_MS, "AT+CPSMS?" },
        /*
         * NCCID/CIMI/IMEI/CGATT는 NRB 직후 service-ready 이전 타이밍에서는
         * AT error가 날 수 있다. 실제 attach는 뒤의
         * App_NBIoTCarrierAttachMandatory()가 수행하므로 여기서는 조기 CGATT를 강제하지 않는다.
         */
    };
    static const AppBc95SeqStep_t kAttachVerifyPostSeq[] =
    {
        { APP_BC95_SEQ_STEP_BESTEFFORT_QUERY, APP_BC95_AT_CMD_NCCID_QUERY,     APP_BC95_AT_RX_TIMEOUT_MS, "AT+NCCID?" },
        { APP_BC95_SEQ_STEP_BESTEFFORT_QUERY, APP_BC95_AT_CMD_IMSI,            APP_BC95_AT_RX_TIMEOUT_MS, "AT+CIMI" },
        { APP_BC95_SEQ_STEP_BESTEFFORT_QUERY, APP_BC95_AT_CMD_IMEI,            APP_BC95_AT_RX_TIMEOUT_MS, "AT+CGSN=1" },
    };

    APP_LOGN("NBIOT", "[[BootTrack]] start board attach-verification sequence");
    status = App_Bc95AtRunSequence(kAttachVerifyPreSeq,
                                   (uint32_t)(sizeof(kAttachVerifyPreSeq) / sizeof(kAttachVerifyPreSeq[0])),
                                   "[[BootTrack]]");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_Bc95AtSetFullFunction();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /*
     * 바로 직전에 CFUN=1, NCCID/CIMI/IMEI 조회, CGATT=1까지 끝난 상태이므로
     * 여기서 이어지는 attach bring-up의 두 번째 boot-banner wait는 생략한다.
     * attach mandatory 진입 직전의 conservative clear에 지워지지 않도록
     * one-shot 보호 플래그도 함께 세운다.
     */
    g_appBc95SkipNextAttachBootWait = APP_TRUE;
    g_appBc95ForceSkipAttachBootWaitOnce = APP_TRUE;

    /*
     * 실제 attach / service-ready 확인은 mandatory attach 경로에 일원화한다.
     */

    status = App_NBIoTCarrierAttachMandatory();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    (void)App_Bc95AtRunSequence(kAttachVerifyPostSeq,
                                (uint32_t)(sizeof(kAttachVerifyPostSeq) / sizeof(kAttachVerifyPostSeq[0])),
                                "[[BootTrack]]");
    (void)App_NBIoTReadQuality(APP_TRUE);
    (void)App_NBIoTSyncTime();
    return APP_STATUS_OK;
}

/* ============================================================
 *  LG U+ platform 파라미터 동기화: 읽기 -> 비교 -> 다를 때만 쓰기
 *  (Application Note: 매 부팅마다 쓰면 모듈 메모리 손상 우려)
 * ============================================================ */
#ifndef APP_BC95_PLATFORM_LGU_SERVCODE_STRING
#error "APP_BC95_PLATFORM_LGU_SERVCODE_STRING is not defined (app_build_config.h)"
#endif
#ifndef APP_BC95_PLATFORM_LGU_MODEL_STRING
#error "APP_BC95_PLATFORM_LGU_MODEL_STRING is not defined (app_build_config.h)"
#endif

#define APP_BC95_LGU_SERVERIP_BS_TEXT \
    APP_BC95_PLATFORM_LGU_BOOTSTRAP_MODE "," APP_BC95_PLATFORM_LGU_SERVER_IP_STRING "," APP_BC95_PLATFORM_LGU_SERVER_PORT_STRING
#define APP_BC95_LGU_SERVERIP_LWM2M_TEXT \
    APP_BC95_PLATFORM_LGU_LWM2M_MODE "," APP_BC95_PLATFORM_LGU_SERVER_IP_STRING "," APP_BC95_PLATFORM_LGU_SERVER_PORT_STRING
#define APP_BC95_LGU_HOLDOFF_PREFIX          "+QBOOTSTRAPHOLDOFF:"
#define APP_BC95_LGU_CMP_BUF_SIZE            (72u)
#define APP_BC95_LGU_MBSPS_PARAM_BUF_SIZE    (64u)
#define APP_BC95_LGU_MBSPS_CMD_BUF_SIZE      (80u)
#define APP_BC95_LGU_ICCID_TAIL_LEN          (6u)

/* 조회 후 응답에서 '"' 를 제거(in-place). 조회 실패 시 APP_FALSE */
static uint8_t App_NbiotQueryNormalized(const char *p_queryCmd)
{
    uint16_t rxLen = 0u;
    size_t r;
    size_t w = 0u;

    if (App_Bc95AtSendCommand(p_queryCmd,
                              g_appBc95AtRxBuf,
                              (uint16_t)sizeof(g_appBc95AtRxBuf),
                              APP_BC95_AT_RX_TIMEOUT_MS,
                              &rxLen) != APP_STATUS_OK)
    {
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
        /* query 실패 시에도 CME가 응답에 남아있으면 보고 (MBSPS query CME 4 특정) */
        App_Bc95AtLogCmeIfPresent(p_queryCmd, g_appBc95AtRxBuf, rxLen);
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
        return APP_FALSE;
    }

    /* 방어: 응답 길이 기준으로 NUL 종단을 보장 */
    if (rxLen >= (uint16_t)sizeof(g_appBc95AtRxBuf))
    {
        rxLen = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);
    }
    g_appBc95AtRxBuf[rxLen] = 0u;

    for (r = 0u; r < (size_t)rxLen; r++)
    {
        if (g_appBc95AtRxBuf[r] != (uint8_t)'"')
        {
            g_appBc95AtRxBuf[w++] = g_appBc95AtRxBuf[r];
        }
    }
    g_appBc95AtRxBuf[w] = 0u;
    return APP_TRUE;
}

/* 정규화된 응답에 p_text('"' 제거 후)가 줄 끝까지 정확히 일치해 포함되는지 확인 */
static uint8_t App_NbiotRxHas(const char *p_text)
{
    char needle[APP_BC95_LGU_CMP_BUF_SIZE];
    const char *p_hit;
    size_t w = 0u;
    char tail;

    for (; *p_text != '\0'; p_text++)
    {
        if (*p_text == '"') { continue; }
        if (w >= (sizeof(needle) - 1u)) { return APP_FALSE; }
        needle[w++] = *p_text;
    }
    if (w == 0u) { return APP_FALSE; }
    needle[w] = '\0';

    /* 대소문자 무시 탐색(strncasecmp 기반): 모뎀 응답 hex의 대소문자 변동(EPNS 등)에
     * 상관없이 매칭시키기 위한 근본 대응. 상수 문자열은 원본 유지. */
    {
        const char *p_hay = (const char *)g_appBc95AtRxBuf;
        size_t      hayLen = strlen(p_hay);

        if (hayLen < w) { return APP_FALSE; }
 
        for (; *p_hay != '\0'; p_hay++)
        {
            if (strncasecmp(p_hay, needle, w) == 0)
            {
                tail = p_hay[w];
                return ((tail == '\0') || (tail == '\r') || (tail == '\n')) ? APP_TRUE : APP_FALSE;
            }
        }
    }
    return APP_FALSE;
}

/* "+QBOOTSTRAPHOLDOFF: 0" 인지 확인 (1800 등은 FALSE) */
static uint8_t App_NbiotRxHoldoffIsZero(void)
{
    const char *p_pos = strstr((const char *)g_appBc95AtRxBuf, APP_BC95_LGU_HOLDOFF_PREFIX);

    if (p_pos == NULL) { return APP_FALSE; }
    p_pos += (sizeof(APP_BC95_LGU_HOLDOFF_PREFIX) - 1u);
    while ((*p_pos == ' ') || (*p_pos == '\t')) { p_pos++; }
    if (*p_pos != '0') { return APP_FALSE; }
    return ((p_pos[1] >= '0') && (p_pos[1] <= '9')) ? APP_FALSE : APP_TRUE;
}

/* ICCID 파싱값의 마지막 6자리. 숫자가 아니면 APP_FALSE */
static uint8_t App_NbiotIccidTail6(char *p_out, size_t outSize)
{
    char iccid[APP_NBIOT_SIM_ICCID_BUF_SIZE];
    size_t len;
    uint8_t i;
    char ch;

    if ((p_out == NULL) || (outSize < (APP_BC95_LGU_ICCID_TAIL_LEN + 1u))) { return APP_FALSE; }
    if (App_NBIoTGetSimIccid(iccid, sizeof(iccid)) != APP_TRUE) { return APP_FALSE; }

    len = strlen(iccid);
    if (len < APP_BC95_LGU_ICCID_TAIL_LEN) { return APP_FALSE; }

    for (i = 0u; i < APP_BC95_LGU_ICCID_TAIL_LEN; i++)
    {
        ch = iccid[len - APP_BC95_LGU_ICCID_TAIL_LEN + i];
        if ((ch < '0') || (ch > '9')) { return APP_FALSE; }
        p_out[i] = ch;
    }
    p_out[APP_BC95_LGU_ICCID_TAIL_LEN] = '\0';
    return APP_TRUE;
}

/* "<servcode>,<serial>,<mdn>,<iccid6>,<model>" */
static AppStatus_t App_NbiotBuildMbspsParams(char *p_out, size_t outSize)
{
    char serial[APP_NBIOT_SIM_SERIAL_BUF_SIZE];
    char mdn[APP_NBIOT_SIM_MDN_BUF_SIZE];
    char tail6[APP_BC95_LGU_ICCID_TAIL_LEN + 1u];
    int n;

    if (App_NBIoTGetSimSerial(serial, sizeof(serial)) != APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] MBSPS serial n/a (ICCID must be 19 digits)");
        return APP_STATUS_NOT_INITIALIZED;
    }
    if (App_NBIoTGetSimMdnEst(mdn, sizeof(mdn)) != APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] MBSPS mdn n/a (PLMN != 45006 or IMSI read fail)");
        return APP_STATUS_NOT_INITIALIZED;
    }
    if (App_NbiotIccidTail6(tail6, sizeof(tail6)) != APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] MBSPS iccid tail n/a");
        return APP_STATUS_NOT_INITIALIZED;
    }

    n = snprintf(p_out, outSize, "%s,%s,%s,%s,%s",
                 APP_BC95_PLATFORM_LGU_SERVCODE_STRING,
                 //APP_BC95_PLATFORM_LGU_SERIAL_STRING, //debug
                 serial,
                 mdn,
                 tail6,
                 APP_BC95_PLATFORM_LGU_MODEL_STRING);
    APP_RETURN_IF_FALSE((n > 0) && ((size_t)n < outSize), APP_STATUS_INVALID_PARAM);
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotApplyParam(uint8_t same, const char *p_setCmd, const char *p_label)
{
    AppStatus_t status;

    if (same == APP_TRUE)
    {
        APP_LOGN("NBIOT", "[[BootTrack]] %s same -> skip write", p_label);
        return APP_STATUS_OK;
    }
    status = App_Bc95AtSendSimpleOkCommand(p_setCmd, APP_BC95_AT_RX_TIMEOUT_MS, p_label, "[[BootTrack]]");
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    /* set 성공 여부와 무관하게 응답에 CME가 섞여 있으면 보고 (CME 512/4 특정) */
    App_Bc95AtLogCmeIfPresent(p_label, g_appBc95AtRxBuf, (uint16_t)strlen((const char *)g_appBc95AtRxBuf));
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
    return status;
}

static AppStatus_t App_NbiotSyncPlatformParams(void)
{
    AppStatus_t status;
    uint8_t serverQueryOk;
    uint8_t bsSame;
    uint8_t lwSame;
    uint8_t same;
    char params[APP_BC95_LGU_MBSPS_PARAM_BUF_SIZE];
    char cmd[APP_BC95_LGU_MBSPS_CMD_BUF_SIZE];
    int n;

    /* 이 시점에는 ReadIdentity가 아직 안 불렸으므로 SIM 캐시(ICCID/MDN/Serial)를 먼저 채운다. */
    (void)App_NBIoTReadIdentity(APP_FALSE);

    /* SERVERIP: 조회 1회, 쓰기 전에 두 비교를 모두 끝낸다(쓰기가 응답 버퍼를 덮어씀). */
    serverQueryOk = App_NbiotQueryNormalized(APP_BC95_AT_CMD_QLWSERVERIP_QUERY);
    bsSame = (serverQueryOk == APP_TRUE) ? App_NbiotRxHas(APP_BC95_LGU_SERVERIP_BS_TEXT)    : APP_FALSE;
    lwSame = (serverQueryOk == APP_TRUE) ? App_NbiotRxHas(APP_BC95_LGU_SERVERIP_LWM2M_TEXT) : APP_FALSE;

    status = App_NbiotApplyParam(bsSame, APP_BC95_AT_CMD_LGU_QLWSERVERIP_SET_BS, "AT+QLWSERVERIP=BS,...");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    status = App_NbiotApplyParam(lwSame, APP_BC95_AT_CMD_LGU_QLWSERVERIP_SET_LWM2M, "AT+QLWSERVERIP=LWM2M,...");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* BOOTSTRAPHOLDOFF */
    same = (App_NbiotQueryNormalized(APP_BC95_AT_CMD_QBOOTSTRAPHOLDOFF_QUERY) == APP_TRUE) ? App_NbiotRxHoldoffIsZero() : APP_FALSE;
    status = App_NbiotApplyParam(same, APP_BC95_AT_CMD_LGU_QBOOTSTRAPHOLDOFF_SET_0, "AT+QBOOTSTRAPHOLDOFF=0");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* EPNS */
    //same = (App_NbiotQueryNormalized(APP_BC95_AT_CMD_QLWEPNS_QUERY) == APP_TRUE) ? App_NbiotRxHas(APP_BC95_PLATFORM_LGU_EPNS_STRING) : APP_FALSE;
    same = APP_FALSE; //default use auto mode.
    status = App_NbiotApplyParam(same, APP_BC95_AT_CMD_LGU_QLWEPNS_AUTO_SET, "AT+QLWEPNS=0,...");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    /* MBSPS: SIM 정보로 런타임 조립. 정보가 없으면 쓰지 않고 모뎀 기존값 유지 */
    status = App_NbiotBuildMbspsParams(params, sizeof(params));
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] MBSPS skipped (status=%d), keep modem value", (int)status);
        return APP_STATUS_OK;
    }
    n = snprintf(cmd, sizeof(cmd), "AT+QLWMBSPS=%s\r\n", params);
    APP_RETURN_IF_FALSE((n > 0) && ((size_t)n < sizeof(cmd)), APP_STATUS_INVALID_PARAM);
    APP_LOGN("NBIOT", "[[BootTrack]] MBSPS params: %s", params);

    same = (App_NbiotQueryNormalized(APP_BC95_AT_CMD_QLWMBSPS_QUERY) == APP_TRUE) ? App_NbiotRxHas(params) : APP_FALSE;
    return App_NbiotApplyParam(same, cmd, "AT+QLWMBSPS=...");
}

static AppStatus_t App_NbiotRunBoardProvisioningSequence(void)
{
    AppStatus_t status;
    static const AppBc95SeqStep_t kProvisionSeq[] =
    {
        { APP_BC95_SEQ_STEP_REQUIRED_OK,      APP_BC95_AT_CMD_QREGSWT_SET_1,     APP_BC95_AT_RX_TIMEOUT_MS, "AT+QREGSWT=1" },
        { APP_BC95_SEQ_STEP_NCONFIG_AUTOCONNECT_ON_IDEM, NULL,                   APP_BC95_AT_RX_TIMEOUT_MS, "AT+NCONFIG=AUTOCONNECT,TRUE (idem)" },
        { APP_BC95_SEQ_STEP_BESTEFFORT_QUERY, APP_BC95_AT_CMD_QLWSERVERIP_QUERY, APP_BC95_AT_RX_TIMEOUT_MS, "AT+QLWSERVERIP?" },
        { APP_BC95_SEQ_STEP_REBOOT,           NULL,                              0u,                        "AT+NRB" }
    };

    /* [PROV-SKIP] BKP3R 매직 유효 && 주기 재검증 미도달이면 provisioning 전체
     * (파라미터 쿼리/set + QREGSWT=1 + AUTOCONNECT=TRUE + NRB)를 생략한다.
     * 근거: AUTOCONNECT=TRUE는 NRB로만 효력이 나지만, NVM=TRUE 상태에서는
     * 모뎀 전원온(=리부트)만으로 자동 attach가 시작된다 -> NRB 없이도 엔진
     * 기동/EVTIND 버스트가 발생한다. 후속 finalize는 기존 경로 그대로 수행. */
    {
        uint8_t provDue = 0u;
        if (App_NbiotProvSkipGet(&provDue) == 1u)
        {
            if (provDue == 0u)
            {
                s_provSkipCount++;
                APP_LOGN("NBIOT", "[[BootTrack]] provisioning skip (BKP valid, recheck %u/%u) -> power-on auto-attach",
                         (unsigned)s_provSkipCount,
                         (unsigned)APP_PROV_SKIP_RECHECK_SESSIONS);
                /* NVM=TRUE 유지: 세션 종료 FALSE 스티어링 대상 아님(플래그 미설정) */
                return APP_STATUS_OK;
            }
            APP_LOGN("NBIOT", "[[BootTrack]] provisioning recheck due (every %u sessions) -> full provisioning",
                     (unsigned)APP_PROV_SKIP_RECHECK_SESSIONS);
        }
    }

    APP_LOGN("NBIOT", "[[BootTrack]] start board provisioning sequence");

    /* [NRB 최적화 2단계] provisioning 도달 = 세션 종료 시 NVM AUTOCONNECT가
     * TRUE로 남는다 -> 전원 off 전 FALSE 스티어링 대상으로 표시 */
    s_appNbiotProvisionedThisSession = APP_TRUE;
    /* [NCONFIG-QUERY-SKIP] provisioning NRB 이후 NVM=TRUE 상태로 부팅하는
     * 후속 세션(finalize)이 쿼리 없이 판정할 수 있게 기록 */
    App_NbiotNcfgBkpSetState(0u);

    status = App_NbiotSyncPlatformParams();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_Bc95AtRunSequence(kProvisionSeq,
                                   (uint32_t)(sizeof(kProvisionSeq) / sizeof(kProvisionSeq[0])),
                                   "[[BootTrack]]");
    /* [PROV-SKIP] 풀 provisioning(NRB 포함) 성공 -> BKP 기록.
     * RunSequence의 REBOOT 단계는 SendRebootBestEffort가 WaitUntilReady(부트
     * 배너+USIM)까지 OK로 판정하므로 이 지점은 NRB 후 준비 완료 상태다. */
    if (status == APP_STATUS_OK)
    {
        App_NbiotProvSkipSet(1u);
    }
    return status;
}

static AppStatus_t App_NbiotSyncTimeSoft(void)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    AppBc95Time_t timeInfo;
    uint16_t rxLen = 0u;
    uint32_t attempt;

    APP_LOGD("NBIOT", "Synchronizing time from network (soft)...");

    for (attempt = 1u; attempt <= APP_BC95_TIME_SYNC_RETRY_MAX; attempt++)
    {
        (void)memset(&timeInfo, 0, sizeof(timeInfo));
        rxLen = 0u;
        status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CCLK_QUERY,
                                       g_appBc95AtRxBuf,
                                       (uint16_t)sizeof(g_appBc95AtRxBuf),
                                       APP_BC95_AT_RX_TIMEOUT_MS,
                                       &rxLen);
#if (APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
        App_Bc95AtLogCmeIfPresent("CCLK", g_appBc95AtRxBuf, rxLen);   /* CME 22 특정 */
#endif //(APP_NBIOT_AT_RAW_RX_DEBUG == APP_TRUE)
        if (status != APP_STATUS_OK)
        {
            APP_LOGW("NBIOT", "[[BootTrack]] soft CCLK send fail attempt=%lu/%u status=%d",
                     (unsigned long)attempt,
                     (unsigned)APP_BC95_TIME_SYNC_RETRY_MAX,
                     (int)status);
        }
        else
        {
            atStatus = App_Bc95AtParseCclk((const char *)g_appBc95AtRxBuf, &timeInfo);
            if ((atStatus == APP_BC95_AT_OK) && (timeInfo.valid != 0u))
            {
                status = App_ClockSyncFromNbiot(&timeInfo);
                if (status == APP_STATUS_OK)
                {
                    APP_LOGD("NBIOT", "Time sync done (soft)");
                    return APP_STATUS_OK;
                }

                APP_LOGW("NBIOT", "[[BootTrack]] soft RTC sync apply failed (status=%d)", (int)status);
            }
            else
            {
                APP_LOGW("NBIOT", "[[BootTrack]] soft CCLK not ready attempt=%lu/%u parse=%s raw=[%s]",
                         (unsigned long)attempt,
                         (unsigned)APP_BC95_TIME_SYNC_RETRY_MAX,
                         App_Bc95AtGetStatusString(atStatus),
                         g_appBc95AtRxBuf);
            }
        }

        if (attempt < APP_BC95_TIME_SYNC_RETRY_MAX)
        {
            App_Bc95AtDelayWithFeed(APP_BC95_TIME_SYNC_RETRY_DELAY_MS);
        }
    }

    APP_LOGW("NBIOT", "[[BootTrack]] soft time sync skipped after post-provision retries");
    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_NbiotRunPostProvisionFinalizeSequence(void)
{
    AppStatus_t status;
    AppBc95NetStatus_t netStatus;

    APP_LOGN("NBIOT", "[[BootTrack]] start post-provision settle/reattach sequence");
    App_Bc95AtDelayWithFeed(APP_BC95_POST_PROVISION_SETTLE_MS);

    status = App_Bc95AtWaitForServiceReady(APP_BC95_POST_PROVISION_SERVICE_READY_TIMEOUT_MS);
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", "[[BootTrack]] post-provision service-ready wait incomplete (status=%d)", (int)status);
    }
    else
    {
        status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CEREG_SET_3,
                                               APP_BC95_AT_RX_TIMEOUT_MS,
                                               "AT+CEREG=3",
                                               "[[BootTrack]]");
        if (status != APP_STATUS_OK)
        {
            APP_LOGW("NBIOT", "[[BootTrack]] post-provision CEREG=3 failed (status=%d)", (int)status);
        }
        else
        {
            (void)memset(&netStatus, 0, sizeof(netStatus));
            status = App_Bc95AtWaitForNetwork(APP_BC95_POST_PROVISION_REATTACH_TIMEOUT_MS, &netStatus);
            if (status == APP_STATUS_OK)
            {
                APP_LOGN("NBIOT", "[[BootTrack]] post-provision reattach ready phase=%s ip=%s",
                         App_Bc95AtGetNetPhaseString(netStatus.phase),
                         netStatus.ipAddr);
            }
            else
            {
                APP_LOGW("NBIOT", "[[BootTrack]] post-provision reattach wait incomplete (status=%d phase=%s CEREG=%s CGATT=%u)",
                         (int)status,
                         App_Bc95AtGetNetPhaseString(netStatus.phase),
                         App_Bc95AtGetCeregStatString(netStatus.ceregStat),
                         (unsigned)netStatus.cgattState);
            }
        }
    }

    (void)App_NBIoTReadIdentity(APP_TRUE);
    (void)App_NBIoTReadQuality(APP_TRUE);
    (void)App_NbiotSyncTimeSoft();
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotRunPlatformPreflight(void)
{
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_QLWSERVERIP_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+QLWSERVERIP?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_QLWEPNS_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+QLWEPNS?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_QLWMBSPS_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+QLWMBSPS?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_QBOOTSTRAPHOLDOFF_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+QBOOTSTRAPHOLDOFF?",
                                        "[[ServicePlatformWake]]");
//Debug
/*
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_NCONFIG_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+NCONFIG?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_QREGSWT_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+QREGSWT?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_CEREG_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+CEREG?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_CGATT_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+CGATT?",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_CGPADDR_QUERY,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+CGPADDR",
                                        "[[ServicePlatformWake]]");
    (void)App_Bc95AtSendBestEffortQuery(APP_BC95_AT_CMD_NUESTATS_RADIO,
                                        APP_BC95_AT_RX_TIMEOUT_MS,
                                        "AT+NUESTATS",
                                        "[[ServicePlatformWake]]");
*/

    return APP_STATUS_OK;
}

/* [mgmt-only wake] mgmt 전용 웨이크에서 bootstrap/프로비저닝/EVTIND 등록 대기를
 * 생략한다. FSM wakeup 경로에서 service 웨이크 보류 플래그가 없을 때만 세우며,
 * service 웨이크/부팅/동시 due 웨이크는 기존 풀 코스를 유지한다.
 * 실측(2026-10-30 주말 로그): 순수 mgmt 웨이크 63회 중위 47s 중 mgmt UDP 본 전송은
 * 약 3s. provisioning(QREGSWT=1 + AT+NCONFIG=AUTOCONNECT,TRUE 재설정 + QLWSERVERIP/
 * MBSPS 쓰기 + NRB)과 post-provision finalize(settle + EVTIND srv-ready 대기 +
 * reattach 대기)는 LwM2M bootstrap 전용 단계라 raw UDP mgmt 전송에 불필요하다.
 * 부팅 설정 시퀀스(AUTOCONNECT=FALSE + NRB)는 유지한다 - NRB 후 NVM의 AUTOCONNECT가
 * FALSE로 남아 이 세션(그리고 다음 전원온)에서 LwM2M 엔진이 자기 등록(bootstrap)을
 * 시작하지 않는다. 다음 service 웨이크의 provisioning이 다시 TRUE로 복원한다. */
static uint8_t s_appNbiotMgmtOnlyWake = APP_FALSE;

AppStatus_t App_NBIoTSetMgmtOnlyWake(uint8_t enable)
{
    s_appNbiotMgmtOnlyWake = enable;
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTColdBootResetTrack(void)
{
    AppStatus_t status;

    /* [EVTIND 누락 대책] 세션(부팅/wake) 시작 시 EVTIND 래치를 리셋한다.
     * 모듈 전원은 매 세션 꺼졌다 켜지므로 이전 세션의 래치(last/mask,
     * 예: mask=0x0119)는 무효하다. 리셋이 없으면 fast-path가 stale 마스크로
     * 즉시 통과해 NRB 후 엔진 재등록(EVTIND 4,0,3,8 버스트)이 끝나기 전에
     * service 전송이 나갈 수 있다. 2026-10-30 12:09 실측:
     * stale ready -> seq7~9 전송 중 policy abort, 모뎀은 4,0,0 만 출력
     * (:3/:8 미발생)인데 FW는 이를 감지하지 못했다. */
    s_bc95LastEVTIND      = -1;
    s_bc95EVTINDSeenMask  = 0u;
    s_bc95LatchIsrPending = 0u;
    s_bc95EvtindReadyVerifyPending = 0u;

    /* [NRB 최적화 2단계] provisioning 실행 플래그를 세션 시작 시 초기화 */
    s_appNbiotProvisionedThisSession = APP_FALSE;

    APP_LOGN("NBIOT", "[[BootTrack]] start cold-boot reset/provision flow");

    if (s_appNbiotMgmtOnlyWake == APP_TRUE)
    {
        /* [mgmt-only wake] 경량 경로:
         *  - 유지: 부팅 설정(AUTOCONNECT=FALSE + NRB) -> LwM2M 엔진 기동 억제,
         *          attach(App_NBIoTCarrierAttachMandatory, raw UDP에 필수)
         *  - 생략: provisioning(QREGSWT=1 + AUTOCONNECT=TRUE 재설정 + NRB)과
         *          post-provision finalize(settle + EVTIND srv-ready 대기 + reattach).
         *          bootstrap/등록 대기가 mgmt 웨이크에서 아예 발생하지 않는다. */
        APP_LOGN("NBIOT", "[[BootTrack]] mgmt-only wake: provisioning/finalize skipped (bootstrap disabled)");
        status = App_NbiotRunBootConfigurationSequence();
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

        status = App_NbiotRunBoardAttachVerificationSequence();
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

        APP_LOGN("NBIOT", "[[BootTrack]] done (mgmt-only light path)");
        return APP_STATUS_OK;
    }

    status = App_NbiotRunBootConfigurationSequence();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_NbiotRunBoardAttachVerificationSequence();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

#if (APP_POLICY_NBIOT_BOOTRACK_REG_SERVPLATFORM == APP_TRUE)
    status = App_NbiotRunBoardProvisioningSequence();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
#endif

    status = App_NbiotRunPostProvisionFinalizeSequence();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    APP_LOGN("NBIOT", "[[BootTrack]] done (board-aligned provisioning flow)");
    return APP_STATUS_OK;
}

/* 부팅 컨텍스트 piggyback 생략 플래그:
 * 부팅 wake는 fsm에서 ServicePlatformWakeTrack 직후 자체 mgmt 전송
 * (App_NBIoTTransmitMgmtLiveRecord)을 이미 수행하므로 piggyback은 순수 중복.
 * 정기 service wake는 플래그가 APP_FALSE라 기존대로 piggyback을 수행한다.
 * 시그니처 변경 없이 static 플래그 + setter로 처리(기존 호출부 영향 없음). */
static uint8_t s_appNbiotBootPiggybackSkip = APP_FALSE;

/* [BOOT-DUAL-TX 2026-11-16] 부팅 첫 전송의 live 레코드를 service 플랫폼 경로에도
 * 전달하기 위한 1회성 슬롯. FSM이 BootLiveTx 레코드를 set 하면 다음
 * App_NBIoTServicePlatformWakeTrack() 호출에서 platform TX(BuildFromRecord)에
 * 사용되고 즉시 소비(clear)된다. 미설정 시 기존대로 스토리지 기반 빌드. */
static AppMeterStorageRecord_t s_appNbiotBootServiceLiveRecord;
static uint8_t s_appNbiotBootServiceLiveValid = APP_FALSE;

AppStatus_t App_NBIoTSetBootServiceLiveRecord(const AppMeterStorageRecord_t *p_record)
{
    if (p_record == NULL)
    {
        s_appNbiotBootServiceLiveValid = APP_FALSE;
        return APP_STATUS_OK;
    }
    s_appNbiotBootServiceLiveRecord = *p_record;
    s_appNbiotBootServiceLiveValid = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTSetBootPiggybackSkip(uint8_t enable)
{
    s_appNbiotBootPiggybackSkip = enable;
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTServicePlatformWakeTrack(uint8_t deleteStorage)
{
#if (APP_TX_SERVICE_ENABLE != 1u)
    (void)deleteStorage;
    APP_LOGN("NBIOT", "[[ServicePlatformWake]] disabled by APP_TX_SERVICE_ENABLE=0");
#if (APP_TX_QREP_ENABLE == 1u)
    /* [QREP-INDEPENDENT] service가 꺼진 세션에서도 리포트는 독자 판정 */
    (void)App_NbiotQualityReportMaybeSend();
#endif
    return APP_STATUS_OK;
#else
#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE) && (APP_EPC_ACTIVE_TEST_ID == 7u)
    /* 시험 7 전용: 실제 모뎀 측정/시간동기/플랫폼 전송을 전부 건너뛰고 시나리오만 진행 */
    App_CommTest7RunCycle();
    return APP_STATUS_OK;
#else

#if (APP_POLICY_NBIOT_BOOTRACK_REG_SERVPLATFORM == APP_FALSE)
    (void)deleteStorage;
    APP_LOGW("NBIOT", "[[ServicePlatformWake]] skipped by policy");
    return APP_STATUS_OK;
#else
    AppStatus_t status;
    AppStatus_t mgmtStatus;
    AppBc95UdpResult_t mgmtSendResult;
#if (APP_COMM_PARAM_AUTOTUNE_ENABLE == APP_TRUE)
    AppCommServiceSessionMetrics_t sessionMetrics;
    const AppNbiotCarrierContext_t *pCarrier;
#endif

    APP_LOGN("NBIOT", "[[ServicePlatformWake]] start (fast-path only)");

    (void)App_NBIoTReadIdentity(APP_TRUE);
    (void)App_NBIoTReadQuality(APP_TRUE);

    /* v2: service 우선. quality는 미리 반영하되, 주기 재구성은 실제 service TX 결과까지 본 뒤 수행 */
#if (APP_COMM_PARAM_AUTOTUNE_ENABLE == APP_TRUE)
    App_CommSignalMeasureAndUpdate(App_Bc95AtGetQuality());
#endif

    (void)App_NBIoTSyncTime();
    (void)App_NbiotRunPlatformPreflight();

    status = App_Bc95AtPlatformRegister();
    if (status != APP_STATUS_OK)
    {
        /* 플랫폼 등록 실패도 mgmt UDP 전송은 계속 수행한다:
         * mgmt는 일반 소켓 경로라 등록과 무관하며, 이 wake의 전송 자체를
         * 날리는 것보다 배터리/네트워크 효율이 좋다. */
        APP_LOGW("NBIOT", "[[ServiceTx]] platform register failed (status=%d) -> continue with mgmt UDP", (int)status);
    }
    else
    {
        /* [BOOT-DUAL-TX] 부팅 live 레코드가 pending 이면 스토리지 대신 이것으로
         * platform TX를 수행하고 소비한다(스토리지 0건 부팅에서도 service
         * 플랫폼 전송 보장). 정기 service wake는 pending 없으니 기존과 동일. */
        const AppMeterStorageRecord_t *p_bootLive =
            (s_appNbiotBootServiceLiveValid == APP_TRUE) ? &s_appNbiotBootServiceLiveRecord : NULL;
        s_appNbiotBootServiceLiveValid = APP_FALSE;

        status = App_NBIoTTransmitPlatformInternal("[[ServiceTx]]",
                                                   deleteStorage,
                                                   APP_FALSE,
                                                   p_bootLive);
        if (status != APP_STATUS_OK)
        {
            APP_LOGW("NBIOT", "[[ServiceTx]] platform transmit failed (status=%d) -> continue with mgmt UDP", (int)status);
        }
        else
        {
            /* service 플랫폼 TX 성공 시에만 여기 도달 (등록+전송 모두 OK) */
        }
    }

#if (APP_COMM_PARAM_AUTOTUNE_ENABLE == APP_TRUE)
    (void)memset(&sessionMetrics, 0, sizeof(sessionMetrics));
    pCarrier = App_NBIoTCarrierGetContext();
    if ((pCarrier != NULL) && (pCarrier->attachStartTick != 0u))
    {
        sessionMetrics.attachElapsedMs = HAL_GetTick() - pCarrier->attachStartTick;
    }
    sessionMetrics.extremeWeak = App_CommIsExtremeWeak();
    App_CommApplyServiceSessionCost(&sessionMetrics);
    App_CommParamRecompose();

    if (App_CommShouldAbortRemainingServerTx() == APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[ServicePlatformWake]] policy updated, abort remaining server tx in this wake");
        return APP_STATUS_OK;
    }
#endif

        /* mgmt piggyback 활성화: service 플랫폼 TX(성공/실패 무관) 후
         * 같은 wake에서 mgmt UDP를 함께 전송한다.
         * (등록 실패 시에도 mgmt는 소켓 경로라 전송 가능 - 이번 로그 실측) */
        if (s_appNbiotBootPiggybackSkip == APP_TRUE)
        {
            /* 부팅 wake: piggyback 생략 - 직후 fsm의 부팅 mgmt 전송이 담당 */
            APP_LOGN("NBIOT", "[[ServicePlatformWake]] mgmt piggyback skipped (boot tx follows)");
        }
        else
        {
            /* [독립 스케줄] 정기 service 웨이크에서는 mgmt를 편승 전송하지 않는다.
             * mgmt 전송 경로: 부팅 live 전송 + MgmtTxSchedule due(별도/동시 due 웨이크).
             * (종래 piggyback은 service 세션의 cost abort가 mgmt 공백으로 직결됨:
             *  2026-10-30 로그 - cost=5 abort 구간에서 mgmt 80분 이상 전송 없음.
             *  abort 시 mgmt 보류는 FSM의 carryPendingAfterAbort 경로가 담당) */
            mgmtStatus = APP_STATUS_OK;
            APP_LOGN("NBIOT", "[[ServicePlatformWake]] mgmt piggyback disabled (independent schedule)");
        }
        /* [QREP-INDEPENDENT] service 세션 종료 전 독자 판정:
         * mgmt 전송 결과와 무관하게 QREP 스케줄(부팅/새벽 윈도우)이 due면 전송. */
#if (APP_TX_QREP_ENABLE == 1u)
        (void)App_NbiotQualityReportMaybeSend();
#endif
        APP_LOGN("NBIOT", "[[ServicePlatformWake]] done");
        return APP_STATUS_OK;

#endif
#endif
#endif /* APP_TX_SERVICE_ENABLE */
}

AppStatus_t App_NBIoTMgmtSocketWakeTrack(uint8_t deleteStorage)
{
#if (APP_TX_MGMT_ENABLE != 1u)
    (void)deleteStorage;
    APP_LOGN("NBIOT", "[[MgmtTx]] disabled by APP_TX_MGMT_ENABLE=0");
#if (APP_TX_QREP_ENABLE == 1u)
    /* [QREP-INDEPENDENT] mgmt가 꺼진 세션에서도 리포트는 독자 판정 */
    (void)App_NbiotQualityReportMaybeSend();
#endif
    return APP_STATUS_OK;
#endif
    APP_LOGN("NBIOT", "[[MgmtSocketWake]] start");
    (void)App_NBIoTReadIdentity(APP_TRUE);
    (void)App_NBIoTReadQuality(APP_TRUE);

    /*
     * v2: mgmt는 차선 기능. adaptive policy의 상태전환 입력으로 사용하지 않음.
     * (배터리 우선 / service-driven policy)
     */

    {
        AppStatus_t mgmtStatus = App_NBIoTTransmitMgmtUdp(deleteStorage);
        /* [QREP-INDEPENDENT] mgmt 성공/실패 무관, 세션 종료 전 독자 판정 */
#if (APP_TX_QREP_ENABLE == 1u)
        (void)App_NbiotQualityReportMaybeSend();
#endif
        return mgmtStatus;
    }
}

static AppStatus_t App_Bc95AtWaitForLinePrefix(const char *p_prefix,
                                                uint32_t timeoutMs,
                                                char *p_rxSnapshot,
                                                uint16_t snapshotSize)
{
    HAL_StatusTypeDef halStatus;
    uint32_t startTick;
    uint16_t curLen;
    uint16_t safeLen;
    AppBc95AtLineQueue_t lineQueue;

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_prefix != NULL) && (p_prefix[0] != '\0'), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((snapshotSize > 0u), APP_STATUS_INVALID_PARAM);

    App_Bc95AtScanBufferForEVTIND(g_appBc95AtRxBuf, (uint16_t)sizeof(g_appBc95AtRxBuf));
    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);
    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));
    if (p_rxSnapshot != NULL)
    {
        p_rxSnapshot[0] = '\0';
    }
    /* [v17 FIX] 기존의 두 번째 잔량 스캔 블록은 삭제: 함수 선두 스캔이
     * 이미 memset/DrainRxLine 전에 실행되어(올바른 순서) 동일 버퍼를
     * 커버하며, 이 위치의 스캔은 memset 뒤라 항상 빈 버퍼를 읽는
     * no-op였다. */

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    App_Bc95AtDisarmUrcMonitor(APP_UART_NBIOT_HANDLE);
    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE, &g_appBc95AtRxBuf[0], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    App_Bc95AtLineQueueReset(&lineQueue);
    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < timeoutMs)
    {
        APP_WWDGFeed();

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_RX_FAILED;
        }

        curLen = g_appBc95AtRxContext.receivedLength;
        if (curLen >= g_appBc95AtRxContext.bufferSize)
        {
            curLen = g_appBc95AtRxContext.bufferSize;
        }
        safeLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, curLen,
                                         (p_rxSnapshot != NULL) ? p_rxSnapshot : (char *)g_appBc95AtRxBuf,
                                         snapshotSize);
        /* EVTIND 래치 (경로 2): 대기 중 수신 라인에서 URC 캡처 */
        App_Bc95AtScanBufferForEVTIND((p_rxSnapshot != NULL) ? p_rxSnapshot : (char *)g_appBc95AtRxBuf, safeLen);
        App_Bc95AtLineQueueFeed(&lineQueue,
                                (p_rxSnapshot != NULL) ? p_rxSnapshot : (char *)g_appBc95AtRxBuf,
                                safeLen);
        if (App_Bc95AtLineQueueContainsPrefix(&lineQueue, p_prefix) == APP_TRUE)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_OK;
        }
        HAL_Delay(5u);
    }

    App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
    return APP_STATUS_UART_TIMEOUT;
}

static AppBc95AtStatus_t App_Bc95AtParseQlwevtind(const char *p_resp, int *p_eventType)
{
    const char *p_pfx;

    if ((p_resp == NULL) || (p_eventType == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    p_pfx = strstr(p_resp, APP_BC95_AT_QLWEVTIND_PREFIX);
    if (p_pfx == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_pfx += APP_BC95_AT_QLWEVTIND_PREFIX_LEN;
    while ((*p_pfx == ' ') || (*p_pfx == '	')) p_pfx++;
    if (sscanf(p_pfx, "%d", p_eventType) != 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    return APP_BC95_AT_OK;
}

static void App_NbiotServiceTxStatCount(void)
{
    s_appNbiotServiceTxAttempts++;
}

static void App_NbiotServiceSuccessStatCount(void)
{
    s_appNbiotServiceSuccessCount++;
}

/* 노트 1.3: +QLWEVTIND:0=등록 완료, :3=object 10250 observe 완료 -> :3 이후 UL/DL 가능.
 * 0 && 3 에서 준비 완료로 본다. */

/* 래치 마스크 기준 ready 판정: EVTIND 4(모뎀 초기화), 0(부트스트랩 완료),
 * 3(등록 완료) 세 이벤트가 모두 관측되었을 때만 준비완료로 인정한다.
 * 마지막 값(last) 하나만 보면 앞선 이벤트가 뒤 이벤트에 덮여 유실되므로
 * 관측 이력 비트마스크로 판정한다. (8은 준비완료 조건에 불필요) */
#define APP_BC95_AT_EVTIND_READY_MASK  ((uint16_t)0x0019u) /* bit4|bit3|bit0 */

static uint8_t App_Bc95AtEVTINDMaskReady(uint16_t seenMask)
{
    if ((seenMask & APP_BC95_AT_EVTIND_READY_MASK) ==
        APP_BC95_AT_EVTIND_READY_MASK)
    {
        return APP_TRUE;
    }
    /* [v17 ADD] 폴백 판정: :0(등록 완료) 이후 :3(observe 완료) 또는
     * :8(등록 갱신)이 하나라도 관측되면 엔진이 등록을 넘어 진행한 것이므로
     * UL/DL 준비완료로 본다. 근거(노트 1.3 + 10:21:08 세션 실측):
     * ev=4가 응답 버퍼 꼬리에 합쳐 유실되어 mask=0x0109가 되어도
     * 0/3/8 관측 이력만으로 ready 판정이 가능해 platform-ready
     * wait timeout(300초) 및 UL 스킵을 막는다. ev=4는 등록 과정의
     * 중간 이벤트라 단독으로는 UL/DL 개시 조건이 아니다. */
    return (((seenMask & 0x0001u) != 0u) &&
            (((seenMask & 0x0008u) != 0u) ||
             ((seenMask & 0x0100u) != 0u))) ? APP_TRUE : APP_FALSE;
}

/* [v17 ADD] 폴백 규칙만으로 ready가 되는지 (기본 0x0019 규칙은 미충족).
 * pre-check 로그에서 폴백 진입을 구분해 찍기 위한 보조 함수. */
static uint8_t App_Bc95AtEVTINDMaskReadyFallbackOnly(uint16_t seenMask)
{
    if ((seenMask & APP_BC95_AT_EVTIND_READY_MASK) ==
        APP_BC95_AT_EVTIND_READY_MASK)
    {
        return APP_FALSE;
    }
    return (((seenMask & 0x0001u) != 0u) &&
            (((seenMask & 0x0008u) != 0u) ||
             ((seenMask & 0x0100u) != 0u))) ? APP_TRUE : APP_FALSE;
}

uint8_t App_Bc95AtPlatformEventIsReady(int eventType)
{
    return (eventType == 3) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_Bc95AtPlatformEventNeedsRegisterRetry(int eventType)
{
    return ((eventType == 1) || (eventType == 9)) ? APP_TRUE : APP_FALSE;
}

static AppStatus_t App_Bc95AtWaitForPlatformReadyWithPolicy(uint32_t timeoutMs, uint8_t *p_needRegisterRetry)
{
    uint32_t startTick;
    char rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    uint8_t bootstrapSeen = APP_FALSE;

    uint8_t silentChunks = 0u;   /* [STALL-GUARD] 연속 무응답 청크 수 */
    uint8_t probeSent = 0u;      /* [STALL-GUARD] 프로브 AT 송신 여부 */

    if (p_needRegisterRetry != NULL)
    {
        *p_needRegisterRetry = APP_FALSE;
    }

    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < timeoutMs)
    {
        AppStatus_t status;
        int eventType = -1;
        uint32_t remain = timeoutMs - (HAL_GetTick() - startTick);
        uint32_t chunk = (remain > 5000u) ? 5000u : remain;

        /* [FIX-EVTIND-1] 대기 루프는 수신 줄 하나(eventType==3)만 보고 ready 를 판정했다.
         * 스캔 경로가 mask 를 이미 ready 로 만들어도 보지 않아 FAST_PATH 타임아웃(120s)을
         * 모두 소진한 뒤 래치가 리셋되어 이력이 버려졌다. 매 반복마다 mask 로 재판정한다. */
        if (App_Bc95AtEVTINDMaskReady(s_bc95EVTINDSeenMask) == APP_TRUE)
        {
            APP_LOGN("NBIOT", "[[BootTrack]] EVTIND-latch ready in wait loop (last=%d mask=0x%04X)",
                     s_bc95LastEVTIND, (unsigned int)s_bc95EVTINDSeenMask);
            s_bc95AtStallStreak = 0u;   /* [STALL-CLUSTER] 스톨 종료 판정 */
            return APP_STATUS_OK;
        }

        status = App_Bc95AtWaitForLinePrefix(APP_BC95_AT_QLWEVTIND_PREFIX,
                                             chunk,
                                             rxSnapshot,
                                             (uint16_t)sizeof(rxSnapshot));
        if (status == APP_STATUS_UART_TIMEOUT)
        {
            /* [STALL-GUARD] 무응답 청크 연속 추적: 2청크 침묵 시 프로브 AT 송신,
             * 3청크 침묵 지속 시 대기 창 조기 포기(caller가 NRB escalate).
             * 어떤 라인이든 수신되면 카운터 리셋(혼입/URC 포함 활성 판정). */
            silentChunks++;
            if ((silentChunks >= APP_BC95_AT_STALL_PROBE_CHUNKS) && (probeSent == 0u))
            {
                static const char stallProbe[] = "AT\r\n";
                (void)HAL_UART_Transmit(APP_UART_NBIOT_HANDLE, (const uint8_t *)stallProbe,
                                        4u, 200u);
                probeSent = 1u;
            }
            else if (silentChunks >= APP_BC95_AT_STALL_SILENT_CHUNKS)
            {
                s_bc95AtStallEpisodes++;
                s_bc95AtStallStreak++;
                APP_LOGW("NBIOT", "[[ServicePlatformWake]] AT stall detected (silent %u chunks, probe sent, episode=%lu) -> escalate now",
                         (unsigned int)silentChunks,
                         (unsigned long)s_bc95AtStallEpisodes);
                return APP_STATUS_UART_TIMEOUT;
            }
            continue;
        }
        silentChunks = 0u;
        probeSent = 0u;
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        if (App_Bc95AtParseQlwevtind(rxSnapshot, &eventType) != APP_BC95_AT_OK)
        {
            continue;
        }

#ifdef NBIOT_TRACE_DEBUG
        APP_LOGN("NBIOT", "[[ServicePlatformWake]] QLWEVTIND=%d (last=%d mask=0x%04X)",
                 eventType, s_bc95LastEVTIND, (unsigned int)s_bc95EVTINDSeenMask);
#endif /* NBIOT_TRACE_DEBUG */

        if (App_Bc95AtPlatformEventIsReady(eventType) == APP_TRUE)
        {
            s_bc95AtStallStreak = 0u;   /* [STALL-CLUSTER] 스톨 종료 판정 */
            return APP_STATUS_OK;
        }
        if (eventType == 4)
        {
            bootstrapSeen = APP_TRUE;
            continue;
        }
        if (eventType == 2)
        {
            continue;
        }
        if (App_Bc95AtPlatformEventNeedsRegisterRetry(eventType) == APP_TRUE)
        {
            if (p_needRegisterRetry != NULL)
            {
                *p_needRegisterRetry = APP_TRUE;
            }
            APP_LOGW("NBIOT", "[[ServicePlatformWake]] registration retry requested by URC=%d", eventType);
            return APP_STATUS_UART_TIMEOUT;
        }
        if (eventType == 5)
        {
            /* [DFOTA 1.6.3] 서버 DFOTA 다운로드 개시: hold 진입 후 이 주기는 TX 중단 */
            App_Bc95AtDfotaHoldEnter();
            APP_LOGW("NBIOT", "[[ServicePlatformWake]] DFOTA download started (URC=%d) -> stop platform TX for this cycle", eventType);
            return APP_STATUS_BUSY;
        }
        if ((eventType == 6) || (eventType == 7))
        {
            APP_LOGW("NBIOT", "[[ServicePlatformWake]] DFOTA-related URC=%d -> stop platform TX for this cycle", eventType);
            return APP_STATUS_FATAL;
        }
    }

    /* [FIX-EVTIND-1b] 마지막 chunk 이후에도 mask 로 한 번 더 판정하고, 실패하면 상태를 남긴다. */
    if (App_Bc95AtEVTINDMaskReady(s_bc95EVTINDSeenMask) == APP_TRUE)
    {
        APP_LOGN("NBIOT", "[[BootTrack]] EVTIND-latch ready at wait end (last=%d mask=0x%04X)",
                 s_bc95LastEVTIND, (unsigned int)s_bc95EVTINDSeenMask);
        return APP_STATUS_OK;
    }
    APP_LOGW("NBIOT", "[[ServicePlatformWake]] EVTIND state at timeout last=%d mask=0x%04X",
             s_bc95LastEVTIND, (unsigned int)s_bc95EVTINDSeenMask);
    /* [FIX-EVTIND-4] 엔진 등록 후반부 이벤트(3=registered, 8=update-done)가
     * 이미 관측됐으면 등록 완료 가능성이 높다. 마스크 완성 대기를 포기하고
     * 전송 1회로 실 등록을 검증하도록 호출부에 힌트를 남긴다. */
    if ((s_bc95LastEVTIND == 3) || (s_bc95LastEVTIND == 8))
    {
        s_bc95EvtindReadyVerifyPending = 1u;
        APP_LOGN("NBIOT", "[[ServicePlatformWake]] last=%d -> verify registration by TX instead of NRB",
                 s_bc95LastEVTIND);
    }

#ifdef NBIOT_TRACE_DEBUG
    App_Bc95EvtTraceDump("wait-timeout");
    App_Bc95RxProbeDump("wait-timeout");
#endif /* NBIOT_TRACE_DEBUG */

    APP_LOGW("NBIOT", "[[ServicePlatformWake]] platform-ready wait timeout%s",
             (bootstrapSeen == APP_TRUE) ? " after bootstrap-complete" : "");
    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_Bc95AtRunShortCampProbe(AppBc95NetStatus_t *p_status)
{
    APP_LOGI("NBIOT", APP_NBIOT_LOG_PREATTACH
        " camp probe %lums",
             (unsigned long)APP_BC95_PRE_ATTACH_CAMP_PROBE_TIMEOUT_MS);
    return App_Bc95AtWaitForNetwork(APP_BC95_PRE_ATTACH_CAMP_PROBE_TIMEOUT_MS, p_status);
}

static AppStatus_t App_Bc95AtConfirmWeakCellAfterProbe(uint8_t *p_confirmed, AppBc95Quality_t *p_quality, uint8_t *p_noCellAfterProbe)
{
    AppStatus_t status;
    AppBc95Quality_t quality;

    APP_RETURN_IF_FALSE(p_confirmed != NULL, APP_STATUS_INVALID_PARAM);
    *p_confirmed = APP_FALSE;
    if (p_quality != NULL)
    {
        (void)memset(p_quality, 0, sizeof(*p_quality));
    }
    if (p_noCellAfterProbe != NULL)
    {
        *p_noCellAfterProbe = APP_FALSE;
    }

    status = App_Bc95AtFetchQualityWithRetry(&quality,
                                             NULL,
                                             0u,
                                             APP_BC95_PRE_ATTACH_NUESTATS_RETRY_MAX);
    if (status == APP_STATUS_OK)
    {
        if (p_quality != NULL)
        {
            *p_quality = quality;
        }

        if ((quality.rssiDbm <= APP_BC95_PRE_ATTACH_ABORT_RSSI_DBM) ||
            (quality.rsrpDbm <= APP_BC95_PRE_ATTACH_ABORT_RSRP_DBM))
        {
            *p_confirmed = APP_TRUE;
            APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH
                     " weak by NUESTATS rssi=%d rsrp=%d snr=%d",
                     quality.rssiDbm, quality.rsrpDbm, quality.snrDb);
        }
        return APP_STATUS_OK;
    }

    if (status == APP_STATUS_UART_TIMEOUT)
    {
        *p_confirmed = APP_TRUE;
        if (p_noCellAfterProbe != NULL)
        {
            *p_noCellAfterProbe = APP_TRUE;
        }
        APP_LOGW("NBIOT", APP_NBIOT_LOG_PREATTACH
                 " weak: no cell after probe");
        return APP_STATUS_OK;
    }

    return status;
}

static AppBc95AtStatus_t App_Bc95AtParseQlwuldataStatus(const char *p_resp, int *p_statusOut, int *p_seqOut)
{
    const char *p_pfx;
    int status;
    int seq = -1;

    if ((p_resp == NULL) || (p_statusOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    p_pfx = strstr(p_resp, APP_BC95_AT_QLWULDATASTATUS_PREFIX);
    if (p_pfx == NULL)
    {
        return APP_BC95_AT_ERR_NO_PREFIX;
    }
    p_pfx += APP_BC95_AT_QLWULDATASTATUS_PREFIX_LEN;
    while ((*p_pfx == ' ') || (*p_pfx == '	')) p_pfx++;
    if (sscanf(p_pfx, "%d,%d", &status, &seq) < 1)
    {
        return APP_BC95_AT_ERR_FORMAT;
    }
    *p_statusOut = status;
    if (p_seqOut != NULL)
    {
        *p_seqOut = seq;
    }
    return APP_BC95_AT_OK;
}

static AppStatus_t App_Bc95AtPlatformRegister(void)
{
    AppStatus_t status;
    uint8_t needRegisterRetry = APP_FALSE;
    uint32_t attempt;

    /* EVTIND 조기 도착 래치 검사: 부팅 트랙 동안 이미 EVTIND:3(observe 완료) 또는
     * EVTIND:0(등록 완료)이 도착해 있으면 fast-path 대기 없이 즉시 준비완료 처리.
     * (READY_REQUIRE_OBSERVE 정책을 따른다) */
    if (s_bc95LatchIsrPending != 0u)
    {
        APP_LOGN("NBIOT", "[[BootTrack]] EVTIND-latch ISR monitor captured last=%d mask=0x%04X",
                 s_bc95LastEVTIND, s_bc95EVTINDSeenMask);
        s_bc95LatchIsrPending = 0u;
    }
    /* last 값 하나만 보면 앞선 이벤트(0/3)가 뒤 이벤트(8)에 덮혀 유실된다
     * (boot11/12 실측). 4,0,3 세 이벤트가 모두 관측되었으면 즉시
     * 준비완료로 판정한다. */
    if ((App_Bc95AtPlatformEventIsReady(s_bc95LastEVTIND) == APP_TRUE) ||
        (App_Bc95AtEVTINDMaskReady(s_bc95EVTINDSeenMask) == APP_TRUE))
    {
        /* [v17 ADD] 기본 규칙(4:0:3) 충족이 아니라 폴백(0+(3|8))으로
         * 진입한 경우를 로그에서 구분한다: ev=4 유실 복구 경로의
         * 실측 추적용 (마스크가 0x0119가 아닌데 ready로 진입한 세션) */
        if (App_Bc95AtEVTINDMaskReadyFallbackOnly(s_bc95EVTINDSeenMask) == APP_TRUE)
        {
            APP_LOGN("NBIOT", "[[BootTrack]] EVTIND-latch captured(0:3|8 fallback) mask=0x%04X. Ready UL/DL",
                     s_bc95EVTINDSeenMask);
        }
        else
        {
            /* [관측성] 실제 관측값을 출력해 bit0(EVTIND:0) 누락 웨이크를 즉시 식별 */
            APP_LOGN("NBIOT", "[[BootTrack]] EVTIND-latch captured(last=%d mask=0x%04X, ore=%lu fe=%lu). Ready UL/DL",
                     s_bc95LastEVTIND, s_bc95EVTINDSeenMask,
                     (unsigned long)s_bc95RxErrOreCount,
                     (unsigned long)s_bc95RxErrFeCount);
        }
#ifdef NBIOT_TRACE_DEBUG
        App_Bc95EvtTraceDump("register-immediate");
        App_Bc95RxProbeDump("register-immediate");
#endif /* NBIOT_TRACE_DEBUG */
        return APP_STATUS_OK;
    }
 
    status = App_Bc95AtWaitForPlatformReadyWithPolicy(APP_BC95_PLATFORM_FAST_PATH_TIMEOUT_MS,
                                                      &needRegisterRetry);
    if (status == APP_STATUS_OK)
    {
        APP_LOGN("NBIOT", "[[ServicePlatformWake]] already-registered fast path hit");
#ifdef NBIOT_TRACE_DEBUG
        App_Bc95EvtTraceDump("register-fastpath");
        App_Bc95RxProbeDump("register-fastpath");
#endif /* NBIOT_TRACE_DEBUG */
        return APP_STATUS_OK;
    }
    if ((status != APP_STATUS_UART_TIMEOUT) && (status != APP_STATUS_FATAL))
    {
        return status;
    }

    /* fast-path에서 못 받은 경우: 이번 wake 기준으로 래치를 리셋해
     * 등록 재시도 후의 새로운 EVTIND만 신뢰한다 */
    s_bc95LastEVTIND = -1;
    s_bc95EVTINDSeenMask = 0u;
    s_bc95LatchIsrPending = 0u;
#ifdef NBIOT_TRACE_DEBUG
    App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_MARK, APP_BC95_TRC_SRC_NONE, APP_BC95_TRC_MARK_LATCHRESET, 0u);
#endif /* NBIOT_TRACE_DEBUG */

    /* [v18 EVTIND 재시도 재설계 2026-11-04]
     * attempt 1 = 수동 대기(30s): fast-path 실패 직후의 마지막 수동 관측 기회.
     * attempt 2 = NRB 리셋 후 재등록 대기: "동일 대기 반복은 무의미" 실측
     *   (2026-10-30, 전송 단계에서 약 15분 소모로 확인)을 등록 재시도에도 일관 적용.
     *   수동 대기는 상태를 바꾸는 동작이 없으므로 attempt 2부터는 모뎀 리셋으로
     *   상태를 바꾼 뒤 엔진 재등록 EVTIND를 기다린다(전송 단계 에스컬레이션과
     *   동일한 검증된 경로 - App_Bc95AtSendRebootBestEffort는 banner+USIM ready까지 대기).
     * REGISTER_RETRY_MAX는 2 유지. attempt 2(NRB) 실패 시 여기서 확정 실패 반환하므로
     * 전송 단계의 NRB 에스컬레이션(register 3회 연속 실패 조건)과 중복되지 않는다 —
     * 이 함수가 OK를 못 돌려주면 전송이 시작되지 않아 registerFailCount가 3에
     * 도달하지 않기 때문(send 루프는 register 성공 후에만 진입). */

    for (attempt = 1u; attempt <= APP_BC95_PLATFORM_REGISTER_RETRY_MAX; attempt++)
    {
        {
            /* [FIX-EVTIND-4] 등록 후반부 이벤트(last=3/8)가 이미 관측됐으면
             * 모뎀이 버스트 일부만 발행한 것으로 실 등록 완료 가능성이 높다.
             * NRB 에스컬레이션 대신 전송 1회로 실 등록을 검증한다:
             *   - TX 성공(UDP OK) -> 등록 유효로 간주, NRB 생략(회복 ~16s 절감)
             *   - TX 실패       -> 기존대로 NRB 에스컬레이션
             * 검증 TX는 send 루프가 재시도하므로 여기서는 pending 플래그만 세운다. */
            if (s_bc95EvtindReadyVerifyPending == 1u)
            {
                s_bc95EvtindReadyVerifyPending = 0u;
                APP_LOGN("NBIOT", "[[ServicePlatformWake]] skip NRB escalation: verify registration by first TX (last=3/8 seen)");
                /* NRB 없이 재대기 없이 전송으로 진행: 루프를 통과해 send 단계로 이동.
                 * wait 실패이므로 상태는 UART_TIMEOUT 대신 OK로 간주해 진행한다. */
                return APP_STATUS_OK;
            }

            /* [STALL-CLUSTER] 연속 스톨(클러스터)이면 NRB 무의미(실측: NRB 후에도
             * 스톨 지속). NRB 생략하고 즉시 세션 포기 -> 다음 세션 회복에 맡김. */
            if (s_bc95AtStallStreak >= APP_BC95_AT_STALL_CLUSTER_NRB_SKIP_MIN)
            {
                APP_LOGW("NBIOT", "[[ServicePlatformWake]] stall cluster streak=%lu -> skip attempt 2 NRB, give up this session",
                         (unsigned long)s_bc95AtStallStreak);
                return APP_STATUS_UART_TIMEOUT;
            }

            APP_LOGW("NBIOT", "[[ServicePlatformWake]] attempt 1/2 passive wait failed -> escalate NRB reset + re-register (no repeat wait)");
            if (App_Bc95AtSendRebootBestEffort() != APP_STATUS_OK)
            {
                APP_LOGW("NBIOT", "[[ServicePlatformWake]] attempt 2/2 NRB failed -> give up registration this wake");
                return APP_STATUS_UART_TIMEOUT;
            }
            /* NRB 후 래치 리셋 — 재부팅 후의 새로운 EVTIND만 신뢰 */
            s_bc95LastEVTIND = -1;
            s_bc95EVTINDSeenMask = 0u;
            s_bc95LatchIsrPending = 0u;
#ifdef NBIOT_TRACE_DEBUG
            App_Bc95EvtTraceAdd(APP_BC95_TRC_KIND_MARK, APP_BC95_TRC_SRC_NONE, APP_BC95_TRC_MARK_LATCHRESET, 0u);
#endif /* NBIOT_TRACE_DEBUG */
        }
        APP_LOGN("NBIOT", "[[ServicePlatformWake]] auto bootstrap mode: waiting engine EVTIND (attempt %lu/%lu%s)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_PLATFORM_REGISTER_RETRY_MAX,
                 (attempt == 2u) ? ", post-NRB" : "");
        needRegisterRetry = APP_FALSE;
        status = App_Bc95AtWaitForPlatformReadyWithPolicy(APP_BC95_PLATFORM_EVENT_TIMEOUT_MS,
                                                          &needRegisterRetry);
        if (status == APP_STATUS_OK)
        {
#ifdef NBIOT_TRACE_DEBUG
            App_Bc95EvtTraceDump((attempt == 2u) ? "register-nrb-retry-ok" : "register-retry-ok");
            App_Bc95RxProbeDump((attempt == 2u) ? "register-nrb-retry-ok" : "register-retry-ok");
#endif /* NBIOT_TRACE_DEBUG */
            return APP_STATUS_OK;
        }

        APP_LOGW("NBIOT", "[[ServicePlatformWake]] register attempt %lu/%lu failed (status=%d, retry=%u)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_PLATFORM_REGISTER_RETRY_MAX,
                 (int)status,
                 (unsigned int)needRegisterRetry);

        if (attempt < APP_BC95_PLATFORM_REGISTER_RETRY_MAX)
        {
            App_Bc95AtDelayWithFeed(APP_BC95_PLATFORM_RETRY_BACKOFF_MS);
            continue;
        }
    }

    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_Bc95AtPlatformSendAndConfirm(const uint8_t *p_data, uint16_t length, uint8_t seqNum)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    uint32_t startTick;
    int qlwStatus = -1;
    int qlwSeq = -1;
    int printed;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(seqNum > 0u, APP_STATUS_INVALID_PARAM);

    /* LGU 커스텀 FW 문법(BC95-GV LG U+ Application Note v1.0.3 §1.5):
     *   AT+QLWULDATAEX=<object>,<length>,<data>,<mode>[,<seq_num>]
     * 표준 BC95-G 문법(<length> 선두)과 달리 <object>가 첫 인자다.
     * object 0 = 10250(UL/DL 데이터 컨테이너), 1 = 26241(펌웨어).
     * 표준 문법으로 보내면 +CME ERROR: 50(incorrect parameters) 발생
     * (boot12 실측 3/3 실패). */
    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       "AT+QLWULDATAEX=0,%u,",
                       (unsigned)length);
    APP_RETURN_IF_FALSE((printed > 0) && ((uint32_t)printed < sizeof(appBc95AtCmdTxBuf)), APP_STATUS_INVALID_PARAM);
    App_Bc95AtBytesToHex(p_data, length, &appBc95AtCmdTxBuf[(uint32_t)printed]);
    printed += (int)(2u * length);
    printed += snprintf(&appBc95AtCmdTxBuf[(uint32_t)printed],
                        sizeof(appBc95AtCmdTxBuf) - (uint32_t)printed,
                        ",0x0100,%u\r\n",
                        (unsigned)seqNum);
    APP_RETURN_IF_FALSE((printed > 0) && ((uint32_t)printed < sizeof(appBc95AtCmdTxBuf)), APP_STATUS_INVALID_PARAM);

    /* [v19 NNMI 유실 방지] 이번 UL 세션의 DL 응답은 이전 wake의 래치 캐시와
     * 무관해야 한다: 동일 프레임 중복 판정 방지 (부팅 직후 세션만 정상이던 원인) */
    App_Bc95AtNnmiLatchReset();
    APP_LOGN("NBIOT", "[[ServicePlatformWake]] QLWULDATAEX send obj=0 len=%u seq=%u mode=0x0100 (LGU syntax)",
             (unsigned)length,
             (unsigned)seqNum);
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    /* 전송 시도 통계 (mgmt 트레일러 tag 0xD3 보고용) */
    App_NbiotServiceTxStatCount();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    APP_RETURN_IF_FALSE(atStatus == APP_BC95_AT_OK, APP_STATUS_FATAL);

    /* [v19 NNMI 유실 방지] UL 전송 응답 버퍼에 혼입한 +NNMI 회수:
     * 서버가 STATUS 쿼리보다 먼저 DL 응답을 보내는 경우 대비 */
    App_Bc95AtScanBufferForEVTIND((const char *)g_appBc95AtRxBuf,
                                  (rxLen < (uint16_t)sizeof(g_appBc95AtRxBuf)) ? rxLen : ((uint16_t)sizeof(g_appBc95AtRxBuf) - 1u));

    startTick = HAL_GetTick();
    while ((HAL_GetTick() - startTick) < APP_BC95_PLATFORM_STATUS_TIMEOUT_MS)
    {
        APP_WWDGFeed();
        status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_QLWULDATASTATUS_QUERY,
                                       g_appBc95AtRxBuf,
                                       (uint16_t)sizeof(g_appBc95AtRxBuf),
                                       APP_BC95_AT_RX_TIMEOUT_MS,
                                       &rxLen);
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
        APP_RETURN_IF_FALSE(atStatus == APP_BC95_AT_OK, APP_STATUS_FATAL);
        /* [v19 NNMI 유실 방지 2026-11-04] STATUS 폴링 응답 버퍼에 혼입한 +NNMI를
         * 폐기 전에 회수한다. 서버 DL 응답은 STATUS 쿼리 창에 먼저 도착할 수 있고
         * 이 버퍼는 다음 폴링에서 덮어써지므로, 여기서 스캔하지 않으면 유실된다
         * (실측: platform OK seq=3 후 app ACK absent — 모뎀은 수신, f/w만 미래치).
         * ScanBufferForEVTIND가 EVTIND와 NNMI를 함께 스캔한다. */
        App_Bc95AtScanBufferForEVTIND((const char *)g_appBc95AtRxBuf,
                                      (rxLen < (uint16_t)sizeof(g_appBc95AtRxBuf)) ? rxLen : ((uint16_t)sizeof(g_appBc95AtRxBuf) - 1u));

        if (App_Bc95AtParseQlwuldataStatus((const char *)g_appBc95AtRxBuf, &qlwStatus, &qlwSeq) == APP_BC95_AT_OK)
        {
#ifdef NBIOT_TRACE_DEBUG
             APP_LOGN("NBIOT", "[[ServicePlatformWake]] QLWULDATASTATUS=%d seq=%d", qlwStatus, qlwSeq);
#endif /* NBIOT_TRACE_DEBUG */
            if (qlwStatus == 4)
            {
                /* CON 성공 URC 수신 통계 (mgmt 트레일러 tag 0xD3 보고용) */
                App_NbiotServiceSuccessStatCount();
            }

            if ((qlwStatus == 4) && ((qlwSeq < 0) || (qlwSeq == (int)seqNum)))
            {
                /* 1차 ACK(플랫폼 CoAP) 확인. 서버 애플리케이션 ACK는 +NNMI 푸시이므로
                 * 래치 폴링으로 대기한다(문서 §1.5 — NSORF 폴링은 raw UDP 전용).
                 * 타임아웃/폴은 런타임 service 전용 옵션(svcAck*)에서 조회. */
                {
                    AppMeterServerFormatOptions_t ackOpt;
                    AppStatus_t ackStatus = App_MeterServerOptionsLoad(&ackOpt);
                    uint32_t svcTimeoutMs = (uint32_t)APP_POLICY_SERVICE_ACK_TIMEOUT_MS;
                    uint32_t svcPollMs = (uint32_t)APP_POLICY_SERVICE_ACK_POLL_MS;
                    uint8_t svcEn = APP_TRUE;
                    if ((ackStatus == APP_STATUS_OK) || (ackStatus == APP_STATUS_NOT_INITIALIZED))
                    {
                        if (App_NbiotResolveWaitServiceAck(&ackOpt) == APP_TRUE)
                        {
                            svcTimeoutMs = (uint32_t)ackOpt.svcAckTimeoutSec * 1000u;
                            svcPollMs = (uint32_t)ackOpt.svcAckPoll100Ms * 100u;
                        }
                        else
                        {
                            svcEn = APP_FALSE;
                        }
                    }
                    if (svcEn != APP_TRUE)
                    {
                        APP_LOGW("NBIOT", "[[ServicePlatformWake]] policy: skip service ACK(+NNMI) wait (runtime disabled)");
                        return APP_STATUS_OK;
                    }
                    return App_Bc95AtWaitServiceAckNnmi(seqNum, svcTimeoutMs, svcPollMs);
                }
            }
            if ((qlwStatus == 2) || (qlwStatus == 3) || (qlwStatus == 5))
            {
                return APP_STATUS_UART_TIMEOUT;
            }
        }
        App_Bc95AtDelayWithFeed(APP_BC95_PLATFORM_STATUS_POLL_MS);
    }

    APP_LOGW("NBIOT", "[[ServicePlatformWake]] QLWULDATASTATUS timeout seq=%u", (unsigned)seqNum);
    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_NBIoTTransmitPlatformInternal(const char *p_logTag,
                                                     uint8_t deleteStorage,
                                                     uint8_t unsentOnly,
                                                     const AppMeterStorageRecord_t *p_liveRecord)
{
    AppStatus_t status;
    AppMeterServerFormatOptions_t opt;
    AppMeterServerFormatResult_t buildResult;
    uint8_t packet[APP_METER_SERVER_FORMAT_MAX_PACKET_SIZE];
    uint8_t seqNum;
    uint8_t effectiveDeleteStorage;
    uint32_t sendAttempt;
    uint32_t registerFailCount = 0u;

    APP_RETURN_IF_FALSE((p_logTag != NULL), APP_STATUS_INVALID_PARAM);

    status = App_NbiotBuildMeterPacket(p_logTag,
                                       deleteStorage,
                                       unsentOnly,
                                       p_liveRecord,
                                       APP_FALSE,
                                       &opt,
                                       &buildResult,
                                       packet,
                                       sizeof(packet),
                                       &effectiveDeleteStorage);
    if (status != APP_STATUS_OK)
    {
        /* [SKIP-REASON 2026-11-09] skip 사유를 로그로 구분:
         * NOT_INITIALIZED(옵션 미초기화)/레코드 부재 등 원인별 추적.
         * APP_LOGW 후속 라인 없이 한 줄로 유지(로그 게이트 정책 동일). */
        APP_LOGD("NBIOT", "%s skip send (status=%d)", p_logTag, (int)status);
        return status;
    }

    status = APP_STATUS_UART_TIMEOUT;
    for (sendAttempt = 1u; sendAttempt <= APP_BC95_PLATFORM_SEND_RETRY_MAX; sendAttempt++)
    {
        seqNum = App_Bc95AtNextUdpSeq();
        status = App_Bc95AtPlatformRegister();
        if (status != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "%s platform register failed on attempt %lu/%lu (status=%d, evtind last=%d mask=0x%04X, ore=%lu fe=%lu)",
                     p_logTag,
                     (unsigned long)sendAttempt,
                     (unsigned long)APP_BC95_PLATFORM_SEND_RETRY_MAX,
                     (int)status,
                     s_bc95LastEVTIND, s_bc95EVTINDSeenMask,
                     (unsigned long)s_bc95RxErrOreCount,
                     (unsigned long)s_bc95RxErrFeCount);
            if (status == APP_STATUS_UART_TIMEOUT)
            {
                registerFailCount++;
            }
        }
        else
        {
            status = App_Bc95AtPlatformSendAndConfirm(packet, buildResult.packetLength, seqNum);
            if (status == APP_STATUS_OK)
            {
#if (APP_COMM_PARAM_AUTOTUNE_ENABLE == APP_TRUE)
                App_CommSuccessUpdate((uint8_t)(sendAttempt - 1u), APP_FALSE); /* service 결과만 반영 */
                App_CommParamRecompose(); /* 1h <-> 4h / nightOnly 재계산 */
#endif
#if (APP_TEST_SERVER_TX_BUZZER_ENABLE == APP_TRUE)
                App_NbiotTestNotifyServerTxBuzzer();
#endif

                break;
            }
            APP_LOGD("NBIOT", "%s platform send failed on attempt %lu/%lu (status=%d)",
                     p_logTag,
                     (unsigned long)sendAttempt,
                     (unsigned long)APP_BC95_PLATFORM_SEND_RETRY_MAX,
                     (int)status);
        }

        if (sendAttempt < APP_BC95_PLATFORM_SEND_RETRY_MAX)
        {
            App_Bc95AtDelayWithFeed(APP_BC95_PLATFORM_RETRY_BACKOFF_MS);
        }
    }

    /* [에스컬레이션] register가 3회 연속 status=13으로 실패한 경우, 동일 대기를
     * 더 반복해도 결과가 같다는 실측(2026-10-30 001035~001941, 약 15분 소모)이므로
     * 모뎀을 NRB로 리셋한 뒤 register+send를 1회 재시도한다.
     * (App_Bc95AtSendRebootBestEffort가 banner+USIM ready까지 대기하며,
     *  이후 register 내부에서 엔진 재등록 EVTIND를 기다린다. 성공 시
     *  아래 autotune 판정이 성공으로 기록된다.) */
    if ((status != APP_STATUS_OK) && (registerFailCount == APP_BC95_PLATFORM_SEND_RETRY_MAX))
    {
        APP_LOGD("NBIOT", "%s register failed %lu/%lu (last status=%d) -> escalate NRB reset + 1 retry",
                 p_logTag,
                 (unsigned long)registerFailCount,
                 (unsigned long)APP_BC95_PLATFORM_SEND_RETRY_MAX,
                 (int)status);
        if (App_Bc95AtSendRebootBestEffort() != APP_STATUS_OK)
        {
            APP_LOGD("NBIOT", "%s escalate NRB failed -> keep original failure (status=%d)",
                     p_logTag, (int)status);
        }
        else
        {
            seqNum = App_Bc95AtNextUdpSeq();
            status = App_Bc95AtPlatformRegister();
            if (status == APP_STATUS_OK)
            {
                status = App_Bc95AtPlatformSendAndConfirm(packet, buildResult.packetLength, seqNum);
            }
            APP_LOGD("NBIOT", "%s escalate retry status=%d", p_logTag, (int)status);
        }
    }

#if (APP_COMM_PARAM_AUTOTUNE_ENABLE == APP_TRUE)
    if (status != APP_STATUS_OK)
    {
        App_CommSuccessUpdate(APP_BC95_PLATFORM_SEND_RETRY_MAX, APP_TRUE); /* 전체 실패 */
        App_CommParamRecompose(); /* 빠른 하향 전환 */
    }
#endif
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    if (buildResult.recordCount != 0u)
    {
        if (p_liveRecord != NULL)
        {
            APP_LOGD("NBIOT", "%s live meter record sent without storage (records=%u)",
                     p_logTag,
                     (unsigned int)buildResult.recordCount);
        }
        else if (unsentOnly == APP_TRUE)
        {
            AppStatus_t markStatus = App_MeterStorageMarkOldestUnsentSent(buildResult.recordCount);
            if (markStatus != APP_STATUS_OK)
            {
                APP_LOGD("NBIOT", "%s storage sent-mark failed (status=%ld, records=%u)",
                         p_logTag,
                         (long)markStatus,
                         (unsigned int)buildResult.recordCount);
                return markStatus;
            }
            if (effectiveDeleteStorage == APP_TRUE)
            {
                uint8_t deletedCount = 0u;
                AppStatus_t clearStatus = App_MeterStorageDeleteOldestSent(&deletedCount);
                if (clearStatus != APP_STATUS_OK)
                {
                    APP_LOGD("NBIOT", "%s storage delete failed (status=%ld, records=%u)",
                             p_logTag,
                             (long)clearStatus,
                             (unsigned int)buildResult.recordCount);
                    return clearStatus;
                }
                buildResult.cleared = (deletedCount != 0u) ? APP_TRUE : APP_FALSE;
            }
            else
            {
                APP_LOGD("NBIOT", "%s storage marked sent (records=%u, remain=%u)",
                         p_logTag,
                         (unsigned int)buildResult.recordCount,
                         (unsigned int)App_MeterStorageCount());
            }
        }
        else if (effectiveDeleteStorage == APP_TRUE)
        {
            AppStatus_t clearStatus = App_MeterStorageDeleteOldest(buildResult.recordCount);
            if (clearStatus != APP_STATUS_OK)
            {
                APP_LOGD("NBIOT", "%s storage delete failed (status=%ld, records=%u)",
                         p_logTag,
                         (long)clearStatus,
                         (unsigned int)buildResult.recordCount);
                return clearStatus;
            }
            buildResult.cleared = APP_TRUE;
        }
    }

    if (status == APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "%s PlatformSendResult: sent=%u seq=%u confirmed=1",
                 p_logTag,
                 (unsigned)buildResult.packetLength,
                 (unsigned)seqNum);
    }
    return status;
}

static AppStatus_t App_NbiotCarrierPerformSwReset(void)
{
    AppStatus_t status;
    /* [DFOTA 1.6.3] hold 중 리셋 금지 - 모듈 자체 update 흐름 보호 */
    if (App_Bc95AtDfotaHoldActive() == APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[DFOTA]] firmware update in progress -> skip SW reset (carrier reset)");
        return APP_STATUS_BUSY;
    }
 
    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET " SW reset sequence start (AT+CFUN=0 -> AT+CFUN=1)");

    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CFUN_SET_MIN,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CFUN=0",
                                           APP_NBIOT_REPORT_LOG_RESET);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    App_Bc95AtDelayWithFeed(APP_NBIOT_SW_RESET_SETTLE_MS);

    status = App_Bc95AtSetFullFunction();
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_RESET " AT+CFUN=1 failed (status=%d)", (int)status);
        return status;
    }

    App_Bc95AtDelayWithFeed(APP_NBIOT_SW_RESET_SETTLE_MS);
    (void)App_NBIoTAtInit();
    APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_RESET " SW reset sequence complete");
    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotCarrierPerformHwReset(void)
{
    /* [DFOTA 1.6.3] hold 중 전원 차단 금지 - 모듈 자체 update 흐름 보호 */
    if (App_Bc95AtDfotaHoldActive() == APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[DFOTA]] firmware update in progress -> skip HW reset (carrier reset)");
        return APP_STATUS_BUSY;
    }

    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET " HW reset sequence start (power cycle)");
    App_HwNbiotPowerCycle();
    /*
     * Do not add a post-power-on settle delay here.
     *
     * The next attach attempt enters App_Bc95AtWaitUntilReady() and starts the
     * boot-banner wait. A long delay here causes the modem's early boot banner
     * to be emitted before UART RX is armed, so banner capture fails after HW
     * reset even though the modem is actually alive.
     */
    (void)App_NBIoTAtInit();
    APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_RESET " HW reset sequence complete");
    return APP_STATUS_OK;
}

/* CGATT=0 OK 후 ready/home/CGATT=1 유지는 실측상 detach가 아예 안 먹히는 stuck 상태:
 * 4회(약5s) 폴링은 대기 낭비 - 2회로 단축하고 CFUN=0으로 조기 전환 */
#ifndef APP_NBIOT_DETACH_GRACE_POLL_MAX
#define APP_NBIOT_DETACH_GRACE_POLL_MAX   (2u)
#endif

static AppStatus_t App_NbiotCarrierWaitForCereg0(uint32_t timeoutMs)
{
    AppStatus_t status;
    AppBc95NetStatus_t snapshot;
    uint32_t startTick = HAL_GetTick();
    uint32_t readyHomeCgatt1Streak = 0u;

    while ((HAL_GetTick() - startTick) < timeoutMs)
    {
        (void)memset(&snapshot, 0, sizeof(snapshot));
        status = App_Bc95AtQueryNetStatus(&snapshot);
        if (status == APP_STATUS_OK)
        {
            g_appNbiotCarrierContext.lastNetStatus = snapshot;
            APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " poll: phase=%s CEREG=%s CGATT=%u",
                     App_Bc95AtGetNetPhaseString(snapshot.phase),
                     App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                     (unsigned)snapshot.cgattState);

            if ((snapshot.ceregStat == APP_BC95_CEREG_NOT_REGISTERED) ||
                (snapshot.cgattState == 0u) ||
                (snapshot.cfunValue != 1u))
            {
                APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " complete: CEREG=%s CGATT=%u",
                         App_Bc95AtGetCeregStatString(snapshot.ceregStat),
                         (unsigned)snapshot.cgattState);
                return APP_STATUS_OK;
            }

            if ((snapshot.phase == APP_BC95_NET_PHASE_READY) &&
                (snapshot.ceregStat == APP_BC95_CEREG_REGISTERED_HOME) &&
                (snapshot.cgattState == 1u))
            {
                readyHomeCgatt1Streak++;
                if (readyHomeCgatt1Streak >= APP_NBIOT_DETACH_GRACE_POLL_MAX)   /* grace 4->2: stuck 조기 포기 */
                {
                    APP_LOGW("NBIOT",
                             APP_NBIOT_REPORT_LOG_DETACH
                             " stuck ready/home/CGATT=1 for %lu polls -> stop wait, force CFUN0",
                             (unsigned long)readyHomeCgatt1Streak);
                    return APP_STATUS_UART_TIMEOUT;
                }
            }
            else
            {
                readyHomeCgatt1Streak = 0u;
            }
        }
        else
        {
            APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " poll query fail (status=%d)", (int)status);
        }

        App_Bc95AtDelayWithFeed(APP_NBIOT_DETACH_POLL_INTERVAL_MS);
    }

    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " timeout waiting CEREG:0");
    return APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t App_NbiotCarrierRecoverRegisteringStuck(void)
{
    g_appBc95RegisteringStuckRecoveryUsed = APP_TRUE;
    g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_NETWORK_WAIT;
    g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_NONE;

    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
             " registering-stuck soft recovery 1/%u -> no reset, re-arm CEREG and retry attach",
             (unsigned)APP_BC95_NET_REGISTERING_STUCK_RECOVERY_MAX);

    (void)App_Bc95AtSendBestEffortSimpleOk(APP_BC95_AT_CMD_CEREG_SET_3,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CEREG=3",
                                           APP_NBIOT_REPORT_LOG_ATTACH);

    (void)App_Bc95AtSendBestEffortSimpleOk(APP_BC95_AT_CMD_CGATT_DETACH,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CGATT=0",
                                           APP_NBIOT_REPORT_LOG_ATTACH);

    App_Bc95AtDelayWithFeed(1500u);

    (void)App_Bc95AtSendBestEffortSimpleOk(APP_BC95_AT_CMD_CGATT_ATTACH,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CGATT=1",
                                           APP_NBIOT_REPORT_LOG_ATTACH);

    /* 다음 attempt는 soft-retry 직후이므로 CPIN fixed delay를 짧게 */
    g_appBc95ShortRetryCpinGuardOnce = APP_TRUE;

    App_Bc95AtDelayWithFeed(APP_BC95_NET_POLL_INTERVAL_MS);

    return APP_STATUS_OK;
}

static AppStatus_t App_NbiotCarrierRecoverAfterAttachFailure(AppStatus_t lastStatus)
{
    uint32_t elapsedMs = HAL_GetTick() - g_appNbiotCarrierContext.attachStartTick;

    if (g_appBc95UsimFatal == APP_TRUE)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " no-USIM fatal -> abort without reset (elapsed=%lums)",
                 (unsigned long)elapsedMs);
        return APP_STATUS_FATAL;
    }

    if (g_appNbiotCarrierContext.lastNetStatus.phase == APP_BC95_NET_PHASE_USIM_SUSPEND)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " suspended USIM detected (rejectCause=%u) -> abort without reset/retry",
                 (unsigned)g_appNbiotCarrierContext.lastNetStatus.rejectCause);
        return APP_STATUS_FATAL;
    }

    if (g_appNbiotCarrierContext.lastNetStatus.phase == APP_BC95_NET_PHASE_USIM_TERMINATED)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " terminated USIM detected (rejectCause=%u) -> abort without reset/retry",
                 (unsigned)g_appNbiotCarrierContext.lastNetStatus.rejectCause);
        return APP_STATUS_FATAL;
    }

    if ((lastStatus == APP_STATUS_UART_TIMEOUT) &&
        (g_appNbiotCarrierContext.lastNetStatus.phase == APP_BC95_NET_PHASE_REGISTERING) &&
        (g_appNbiotCarrierContext.lastNetStatus.rejectCauseValid != APP_TRUE) &&
        (g_appBc95RegisteringStuckRecoveryUsed != APP_TRUE))
    {
        return App_NbiotCarrierRecoverRegisteringStuck();
    }

    if (elapsedMs >= APP_NBIOT_ATTACH_TOTAL_FAIL_LIMIT_MS)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " fail limit reached elapsed=%lums sw=%u hw=%u last=%d",
                 (unsigned long)elapsedMs,
                 (unsigned)g_appNbiotCarrierContext.swResetCount,
                 (unsigned)g_appNbiotCarrierContext.hwResetCount,
                 (int)lastStatus);
        return (lastStatus != APP_STATUS_OK) ? lastStatus : APP_STATUS_FATAL;
    }

    if (g_appNbiotCarrierContext.swResetCount < APP_NBIOT_ATTACH_SW_RESET_MAX)
    {
        g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_SW_RESET;
        g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_SW;
        g_appNbiotCarrierContext.swResetCount++;
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET
                 " escalate to SW reset %u/%u (elapsed=%lums)",
                 (unsigned)g_appNbiotCarrierContext.swResetCount,
                 (unsigned)APP_NBIOT_ATTACH_SW_RESET_MAX,
                 (unsigned long)elapsedMs);
        return App_NbiotCarrierPerformSwReset();
    }

    if (g_appNbiotCarrierContext.hwResetCount < APP_NBIOT_ATTACH_HW_RESET_MAX)
    {
        g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_HW_RESET;
        g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_HW;
        g_appNbiotCarrierContext.hwResetCount++;
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_RESET
                 " escalate to HW reset %u/%u (elapsed=%lums)",
                 (unsigned)g_appNbiotCarrierContext.hwResetCount,
                 (unsigned)APP_NBIOT_ATTACH_HW_RESET_MAX,
                 (unsigned long)elapsedMs);
        return App_NbiotCarrierPerformHwReset();
    }

    APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_RESET
             " reset recovery exhausted sw=%u hw=%u elapsed=%lums last=%d",
             (unsigned)g_appNbiotCarrierContext.swResetCount,
             (unsigned)g_appNbiotCarrierContext.hwResetCount,
             (unsigned long)elapsedMs,
             (int)lastStatus);
    return (lastStatus != APP_STATUS_OK) ? lastStatus : APP_STATUS_FATAL;
}

/* ============================================================
 *  UDP / DNS
 * ============================================================ */
static void App_Bc95AtBytesToHex(const uint8_t *p_in, uint32_t inLen, char *p_out)
{
    static const char hexTab[] = "0123456789ABCDEF";
    uint32_t i;
    for (i = 0u; i < inLen; i++)
    {
        p_out[(2u * i)]      = hexTab[(p_in[i] >> 4u) & 0x0Fu];
        p_out[(2u * i) + 1u] = hexTab[p_in[i] & 0x0Fu];
    }
    p_out[2u * inLen] = '\0';
}

static uint8_t App_Bc95AtNextUdpSeq(void)
{
    g_appBc95UdpSeqCounter++;
    if (g_appBc95UdpSeqCounter == 0u) g_appBc95UdpSeqCounter = 1u;
    return g_appBc95UdpSeqCounter;
}

static AppBc95AtStatus_t App_Bc95AtParseQdnsResult(const char *p_resp,
                                                   char *p_ipOut,
                                                   uint32_t ipBufSize,
                                                   uint8_t *p_found,
                                                   uint8_t *p_isFail)
{
    const char *p_scan;
    const char *p_pfx;
    const char *p_line;
    const char *p_end;
    uint32_t    lineLen;
    uint32_t    i;

    if ((p_resp == NULL) || (p_ipOut == NULL) || (ipBufSize < APP_BC95_IP_STR_SIZE) ||
        (p_found == NULL) || (p_isFail == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    *p_found  = APP_FALSE;
    *p_isFail = APP_FALSE;
    p_scan = p_resp;

    while ((p_pfx = strstr(p_scan, APP_BC95_AT_QDNS_PREFIX)) != NULL)
    {
        p_line = p_pfx + APP_BC95_AT_QDNS_PREFIX_LEN;
        while ((*p_line == ' ') || (*p_line == '\t')) p_line++;

        p_end = p_line;
        while ((*p_end != '\0') && (*p_end != '\r') && (*p_end != '\n')) p_end++;
        if (*p_end == '\0')
        {
            return APP_BC95_AT_ERR_NO_DATA;
        }

        lineLen = (uint32_t)(p_end - p_line);
        if ((lineLen == 4u) && (strncmp(p_line, "FAIL", 4u) == 0))
        {
            *p_found  = APP_TRUE;
            *p_isFail = APP_TRUE;
            return APP_BC95_AT_OK;
        }

        if ((lineLen < 7u) || (lineLen >= ipBufSize))
        {
            return APP_BC95_AT_ERR_FORMAT;
        }

        for (i = 0u; i < lineLen; i++)
        {
            char c = p_line[i];
            if (((c < '0') || (c > '9')) && (c != '.'))
            {
                return APP_BC95_AT_ERR_FORMAT;
            }
        }

        (void)memcpy(p_ipOut, p_line, lineLen);
        p_ipOut[lineLen] = '\0';
        *p_found = APP_TRUE;
        return APP_BC95_AT_OK;
    }

    return APP_BC95_AT_ERR_NO_PREFIX;
}

AppStatus_t App_Bc95AtResolveHost(const char *p_hostname, char *p_ipOut, uint32_t ipBufSize)
{
    AppStatus_t       status;
    HAL_StatusTypeDef halStatus;
    AppBc95AtStatus_t atStatus;
    char              atCmd[96];
    char              rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];
    int               cmdLen;
    int32_t           cmeErr;
    uint32_t          rxLen;
    uint32_t          snapLen;
    uint32_t          startTick;
    uint32_t          elapsed;
    uint32_t          hostLen;
    uint8_t           qdnsFound;
    uint8_t           qdnsFail;

    if ((p_hostname == NULL) || (p_ipOut == NULL) || (ipBufSize < APP_BC95_IP_STR_SIZE))
    {
        APP_LOGD("NBIOT", "ResolveHost: invalid param");
        return APP_STATUS_FATAL;
    }
    hostLen = (uint32_t)strlen(p_hostname);
    if ((hostLen == 0u) || (hostLen > APP_BC95_HOSTNAME_MAX_LEN))
    {
        APP_LOGD("NBIOT", "ResolveHost: bad hostname len=%lu", (unsigned long)hostLen);
        return APP_STATUS_FATAL;
    }
    (void)memset(p_ipOut, 0, ipBufSize);

    App_Bc95AtDrainRxLine(APP_UART_NBIOT_HANDLE, 30u);
    App_Bc95AtDelayWithFeed(APP_BC95_DNS_INTERCMD_DELAY_MS);

    cmdLen = snprintf(atCmd, sizeof(atCmd), "AT+QDNS=0,%s\r\n", p_hostname);
    if ((cmdLen <= 0) || (cmdLen >= (int)sizeof(atCmd)))
    {
        APP_LOGD("NBIOT", "ResolveHost: cmd build fail");
        return APP_STATUS_FATAL;
    }
    APP_LOGD("NBIOT", "DNS query: %s", p_hostname);

    (void)memset(g_appBc95AtRxBuf, 0, APP_BC95_AT_RX_BUF_SIZE);
    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = (uint16_t)(APP_BC95_AT_RX_BUF_SIZE - 1u);
    g_appBc95AtRxContext.receivedLength = 0u;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    App_Bc95AtDisarmUrcMonitor(APP_UART_NBIOT_HANDLE);
    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE, &g_appBc95AtRxBuf[0], 1u);
    if (halStatus != HAL_OK)
    {
        APP_LOGD("NBIOT", "ResolveHost: RX_IT fail (%d)", halStatus);
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    halStatus = HAL_UART_Transmit(APP_UART_NBIOT_HANDLE,
                                  (uint8_t *)atCmd, (uint16_t)cmdLen,
                                  APP_BC95_DNS_CMD_TIMEOUT_MS);
    if (halStatus != HAL_OK)
    {
        APP_LOGD("NBIOT", "ResolveHost: TX fail (%d)", halStatus);
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen >= (APP_BC95_AT_RX_BUF_SIZE - 1u))
        {
            rxLen = (APP_BC95_AT_RX_BUF_SIZE - 1u);
        }
        snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, (uint16_t)rxLen,
                                         rxSnapshot, (uint16_t)sizeof(rxSnapshot));

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            APP_LOGD("NBIOT", "ResolveHost: UART err (rxLen=%lu)", (unsigned long)rxLen);
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_RX_FAILED;
        }

        cmeErr = 0;
        atStatus = App_Bc95AtCheckResponse(rxSnapshot, &cmeErr);
        if (atStatus == APP_BC95_AT_OK)
        {
            break;
        }
        if ((atStatus == APP_BC95_AT_ERR_AT_ERROR) || (atStatus == APP_BC95_AT_ERR_CME_ERROR))
        {
            APP_LOGD("NBIOT", "ResolveHost: response error parse=%s cme=%ld raw=[%s]",
                     App_Bc95AtGetStatusString(atStatus), (long)cmeErr, rxSnapshot);
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_TIMEOUT;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= APP_BC95_DNS_CMD_TIMEOUT_MS)
        {
            cmeErr = 0;
            atStatus = App_Bc95AtCheckResponse(rxSnapshot, &cmeErr);
            if (atStatus == APP_BC95_AT_OK)
            {
                APP_LOGD("NBIOT", "ResolveHost: late OK detected (rxLen=%lu)", (unsigned long)snapLen);
                break;
            }
            APP_LOGD("NBIOT", "ResolveHost: OK timeout (rxLen=%lu, raw=[%s])",
                     (unsigned long)rxLen, rxSnapshot);
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_WWDGFeed();
        HAL_Delay(5u);
    }

    startTick = HAL_GetTick();
    status    = APP_STATUS_UART_TIMEOUT;

    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen >= (APP_BC95_AT_RX_BUF_SIZE - 1u))
        {
            rxLen = (APP_BC95_AT_RX_BUF_SIZE - 1u);
        }
        snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, (uint16_t)rxLen,
                                         rxSnapshot, (uint16_t)sizeof(rxSnapshot));

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            APP_LOGD("NBIOT", "ResolveHost: UART err in URC wait raw=[%s]", rxSnapshot);
            status = APP_STATUS_UART_RX_FAILED;
            break;
        }

        qdnsFound = APP_FALSE;
        qdnsFail  = APP_FALSE;
        atStatus = App_Bc95AtParseQdnsResult(rxSnapshot, p_ipOut, ipBufSize, &qdnsFound, &qdnsFail);
        if ((atStatus == APP_BC95_AT_OK) && (qdnsFound == APP_TRUE))
        {
            if (qdnsFail == APP_TRUE)
            {
                APP_LOGD("NBIOT", "DNS FAIL for %s (transient)", p_hostname);
                status = APP_STATUS_UART_TIMEOUT;
            }
            else
            {
                APP_LOGD("NBIOT", "DNS OK: %s -> %s", p_hostname, p_ipOut);
                status = APP_STATUS_OK;
            }
            break;
        }
        if ((atStatus != APP_BC95_AT_ERR_NO_PREFIX) && (atStatus != APP_BC95_AT_ERR_NO_DATA))
        {
            APP_LOGD("NBIOT", "ResolveHost: invalid QDNS URC raw=[%s]", rxSnapshot);
            (void)memset(p_ipOut, 0, ipBufSize);
            status = APP_STATUS_UART_TIMEOUT;
            break;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= APP_BC95_DNS_URC_TIMEOUT_MS)
        {
            qdnsFound = APP_FALSE;
            qdnsFail  = APP_FALSE;
            atStatus = App_Bc95AtParseQdnsResult(rxSnapshot, p_ipOut, ipBufSize, &qdnsFound, &qdnsFail);
            if ((atStatus == APP_BC95_AT_OK) && (qdnsFound == APP_TRUE) && (qdnsFail == APP_FALSE))
            {
                APP_LOGD("NBIOT", "ResolveHost: late QDNS detected %s -> %s", p_hostname, p_ipOut);
                status = APP_STATUS_OK;
            }
            else
            {
                APP_LOGD("NBIOT", "ResolveHost: URC timeout (rxLen=%lu, raw=[%s])",
                         (unsigned long)rxLen, rxSnapshot);
                status = APP_STATUS_UART_TIMEOUT;
            }
            break;
        }
        APP_WWDGFeed();
        HAL_Delay(10u);
    }

    App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
    return status;
}

AppStatus_t App_Bc95AtResolveHostRobust(const char *p_hostname, char *p_ipOut, uint32_t ipBufSize)
{
    return App_Bc95AtResolveHostRobustInternal(p_hostname, p_ipOut, ipBufSize, NULL, NULL);
}

#ifdef NBIOT_SUPPORT_DNS
typedef struct
{
    uint8_t  valid;
    uint32_t tickMs;
    char     host[APP_BC95_HOSTNAME_MAX_LEN];
    char     ip[APP_BC95_IP_STR_SIZE];
} AppBc95DnsWarmupCacheEntry_t;

#define APP_BC95_DNS_WARMUP_CACHE_TTL_MS    (3600000u)  /* 1 hour */

static AppBc95DnsWarmupCacheEntry_t g_appBc95DnsWarmupCache;

static uint8_t App_Bc95AtWarmupDnsCacheLookup(const char *p_hostname,
                                              char *p_ipOut,
                                              uint32_t ipBufSize)
{
    uint32_t now;

    APP_RETURN_IF_FALSE((p_hostname != NULL), APP_FALSE);
    APP_RETURN_IF_FALSE((p_ipOut != NULL), APP_FALSE);
    APP_RETURN_IF_FALSE((ipBufSize >= APP_BC95_IP_STR_SIZE), APP_FALSE);

    if (g_appBc95DnsWarmupCache.valid != APP_TRUE)
    {
        return APP_FALSE;
    }

    if (strncmp(g_appBc95DnsWarmupCache.host,
                p_hostname,
                sizeof(g_appBc95DnsWarmupCache.host)) != 0)
    {
        return APP_FALSE;
    }

    now = HAL_GetTick();
    if ((now - g_appBc95DnsWarmupCache.tickMs) >= APP_BC95_DNS_WARMUP_CACHE_TTL_MS)
    {
        g_appBc95DnsWarmupCache.valid = APP_FALSE;
        return APP_FALSE;
    }

    (void)strncpy(p_ipOut, g_appBc95DnsWarmupCache.ip, ipBufSize - 1u);
    p_ipOut[ipBufSize - 1u] = '\0';
    return APP_TRUE;
}

static void App_Bc95AtWarmupDnsCacheStore(const char *p_hostname, const char *p_ip)
{
    if ((p_hostname == NULL) || (p_ip == NULL))
    {
        return;
    }

    (void)memset(&g_appBc95DnsWarmupCache, 0, sizeof(g_appBc95DnsWarmupCache));
    g_appBc95DnsWarmupCache.valid = APP_TRUE;
    g_appBc95DnsWarmupCache.tickMs = HAL_GetTick();
    (void)strncpy(g_appBc95DnsWarmupCache.host,
                  p_hostname,
                  sizeof(g_appBc95DnsWarmupCache.host) - 1u);
    (void)strncpy(g_appBc95DnsWarmupCache.ip,
                  p_ip,
                  sizeof(g_appBc95DnsWarmupCache.ip) - 1u);
}

static AppStatus_t App_Bc95AtWarmupDns(const char *p_hostname)
{
    AppStatus_t status;
    char ipBuf[APP_BC95_IP_STR_SIZE];
    uint32_t attempt;

    APP_RETURN_IF_FALSE((p_hostname != NULL), APP_STATUS_INVALID_PARAM);
    APP_LOGI("NBIOT", APP_NBIOT_LOG_DNS 
        " warm %s", p_hostname);

    if (App_Bc95AtWarmupDnsCacheLookup(p_hostname, ipBuf, sizeof(ipBuf)) == APP_TRUE)
    {
        APP_LOGI("NBIOT", APP_NBIOT_LOG_DNS 
            " hit %s ip=%s", p_hostname, ipBuf);
        return APP_STATUS_OK;
    }

    for (attempt = 1u; attempt <= APP_BC95_DNS_FAIL_RETRY_MAX; attempt++)
    {
        status = App_Bc95AtResolveHostRobust(p_hostname, ipBuf, sizeof(ipBuf));
        if (status == APP_STATUS_OK)
        {
            App_Bc95AtWarmupDnsCacheStore(p_hostname, ipBuf);
            APP_LOGI("NBIOT", APP_NBIOT_LOG_DNS
                 " ok %lu %s ip=%s",
                     (unsigned long)attempt, p_hostname, ipBuf);
            return APP_STATUS_OK;
        }
        APP_LOGI("NBIOT", APP_NBIOT_LOG_DNS 
            " fail %lu/%lu",
                 (unsigned long)attempt, (unsigned long)APP_BC95_DNS_FAIL_RETRY_MAX);
        if (attempt < APP_BC95_DNS_FAIL_RETRY_MAX)
            App_Bc95AtDelayWithFeed(APP_BC95_DNS_FAIL_RETRY_DELAY_MS);
    }
    APP_LOGI("NBIOT", APP_NBIOT_LOG_DNS
         " skip");

    return APP_STATUS_UART_TIMEOUT;
}
#endif // NBIOT_SUPPORT_DNS

/* ============================================================
 *  UDP socket / send
 * ============================================================ */
AppStatus_t App_Bc95AtCreateUdpSocket(uint16_t localPort, int32_t *p_socketOut)
{
    AppStatus_t  status;
    AppBc95AtStatus_t atStatus;
    uint16_t  rxLen;
    int  printed;
    int  parsedSock;
    const char *p_line;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_socketOut != NULL), APP_STATUS_INVALID_PARAM);
    *p_socketOut = -1;

    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       APP_BC95_AT_CMD_NSOCR_UDP_FMT, (unsigned)localPort);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
        return APP_STATUS_INVALID_PARAM;

    rxLen = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "NSOCR send fail (status=%d)", (int)status);
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "NSOCR resp err (parse=%s, CME=%ld)",
                 App_Bc95AtGetStatusString(atStatus), (long)cmeErr);
        return APP_STATUS_FATAL;
    }

    parsedSock = -1;
    p_line = (const char *)g_appBc95AtRxBuf;
    while (*p_line != '\0')
    {
        const char *p_lineStart = p_line;
        while ((*p_lineStart == '\r') || (*p_lineStart == '\n')) p_lineStart++;
        if ((*p_lineStart >= '0') && (*p_lineStart <= '9'))
        {
            if (sscanf(p_lineStart, "%d", &parsedSock) == 1) break;
        }
        while ((*p_line != '\0') && (*p_line != '\n')) p_line++;
        if (*p_line == '\n') p_line++;
    }

    if (parsedSock < 0)
    {
        APP_LOGD("NBIOT", "NSOCR socket id parse fail. raw=[%s]", g_appBc95AtRxBuf);
        /* 패치: 알 수 없는 소켓 누수 방지 */
        App_Bc95AtCloseAllSockets();
        return APP_STATUS_FATAL;
    }

    *p_socketOut = (int32_t)parsedSock;
    /* [ADD] 개설 성공 소켓 추적 (범위 밖 id는 미추적) */
    if (parsedSock <= APP_BC95_UDP_SOCKET_ID_MAX)
        s_nbiotUdpSocketOpenMask |= ((uint32_t)1u << parsedSock);
    APP_LOGD("NBIOT", "UDP socket created: %ld", (long)parsedSock);
    return APP_STATUS_OK;
}

#if 0 //optimize for del
static AppBc95AtStatus_t App_Bc95AtParseNsostr(const char *p_resp,
                                               int *p_sock, int *p_seq, int *p_status)
{
    const char *p_pfx;
    if ((p_resp == NULL) || (p_sock == NULL) || (p_seq == NULL) || (p_status == NULL))
        return APP_BC95_AT_ERR_PARAM;

    p_pfx = strstr(p_resp, APP_BC95_AT_NSOSTR_PREFIX);
    if (p_pfx == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_pfx += APP_BC95_AT_NSOSTR_PREFIX_LEN;
    while ((*p_pfx == ' ') || (*p_pfx == '\t')) p_pfx++;
    if (sscanf(p_pfx, "%d,%d,%d", p_sock, p_seq, p_status) != 3) return APP_BC95_AT_ERR_FORMAT;
    return APP_BC95_AT_OK;
}
#endif

static AppBc95AtStatus_t App_Bc95AtFindNsostrForSocketSeq(const char *p_resp,
                                                          int targetSock,
                                                          int targetSeq,
                                                          int *p_statusOut)
{
    const char *p_scan;
    const char *p_pfx;
    const char *p_line;
    const char *p_end;
    int parsedSock;
    int parsedSeq;
    int parsedStatus;

    if ((p_resp == NULL) || (p_statusOut == NULL))
    {
        return APP_BC95_AT_ERR_PARAM;
    }

    p_scan = p_resp;
    while ((p_pfx = strstr(p_scan, APP_BC95_AT_NSOSTR_PREFIX)) != NULL)
    {
        p_line = p_pfx + APP_BC95_AT_NSOSTR_PREFIX_LEN;
        while ((*p_line == ' ') || (*p_line == '\t')) p_line++;

        p_end = p_line;
        while ((*p_end != '\0') && (*p_end != '\r') && (*p_end != '\n')) p_end++;
        if (*p_end == '\0')
        {
            return APP_BC95_AT_ERR_NO_DATA;
        }

        if (sscanf(p_line, "%d,%d,%d", &parsedSock, &parsedSeq, &parsedStatus) == 3)
        {
            if ((parsedSock == targetSock) && (parsedSeq == targetSeq))
            {
                *p_statusOut = parsedStatus;
                return APP_BC95_AT_OK;
            }
        }

        p_scan = (*p_end == '\0') ? p_end : (p_end + 1);
    }

    return APP_BC95_AT_ERR_NO_PREFIX;
}

static AppBc95AtStatus_t App_Bc95AtParseNsorfForSocket(const char *p_resp,
                                                       int targetSock,
                                                       uint16_t *p_payloadLen)
{
    const char *p_scan;
    const char *p_pfx;
    int parsedSock;
    char parsedIp[APP_BC95_IP_STR_SIZE];
    unsigned parsedPort;
    unsigned parsedLen;
    char parsedHex[2u * APP_BC95_UDP_MAX_PAYLOAD + 1u];
    unsigned parsedRemain;

    APP_RETURN_IF_FALSE((p_resp != NULL) && (p_payloadLen != NULL), APP_BC95_AT_ERR_PARAM);

    /* [NSORF 무접두 폴백 2026-11-05] 실측(10:28): 본 펌웨어는 +NSORF 접두 없이
     * "+NSONMI:1,8" URC 직후 데이터 라인 "<sock>,<ip>,<port>,<len>,<hex>,<remain>"를
     * 내보낸다(체감: 폴링 응답 버퍼에 OK만 보이던 현상의 원인). 접두 검색 실패 시
     * 라인 단위 6필드 패턴 매칭 폴백을 먼저 시도한다. */
    if (strstr(p_resp, APP_BC95_AT_NSORF_PREFIX) == NULL)
    {
        const char *p_line = p_resp;
        while (p_line != NULL)
        {
            const char *p_eol = strchr(p_line, '\n');
            size_t lineLen = (p_eol != NULL) ? (size_t)(p_eol - p_line) : strlen(p_line);
            char lineBuf[128];

            if (lineLen > 0u && lineLen < sizeof(lineBuf))
            {
                (void)memcpy(lineBuf, p_line, lineLen);
                lineBuf[lineLen] = '\0';

                /* 라인 정리: 앞뒤 CR/공백 제거 */
                char *p_trim = lineBuf;
                while ((*p_trim == '\r') || (*p_trim == ' ')) { p_trim++; }
                size_t tl = strlen(p_trim);
                while ((tl > 0u) && ((p_trim[tl - 1u] == '\r') || (p_trim[tl - 1u] == ' ')))
                {
                    p_trim[--tl] = '\0';
                }

                parsedSock = -1;
                parsedIp[0] = '\0';
                parsedPort = 0u;
                parsedLen = 0u;
                parsedHex[0] = '\0';
                parsedRemain = 0u;

                /* 6필드 패턴: sock,ip,port,len,hexdata,remain (ip는 dotted-quad 강제) */
                if (sscanf(p_trim,
                           "%d,%63[0-9.],%u,%u,%1024[^,],%u",
                           &parsedSock,
                           parsedIp,
                           &parsedPort,
                           &parsedLen,
                           parsedHex,
                           &parsedRemain) >= 5)
                {
                    /* NSOST 에코("1,83") 등 오검출 방지: dotted IP + len>0 + hex 필수 */
                    if ((parsedSock == targetSock) && (parsedLen > 0u) &&
                        (parsedPort > 0u) && (parsedPort <= 65535u) &&
                        (strchr(parsedIp, '.') != NULL) && (parsedHex[0] != '\0'))
                    {
#ifdef NBIOT_TRACE_DEBUG
                        APP_LOGN("NBIOT", "[[AckDbg]] NSORF prefixless matched sock=%d len=%u",
                                 parsedSock,
                                 (unsigned)parsedLen);
#endif /* NBIOT_TRACE_DEBUG */
                        *p_payloadLen = (uint16_t)parsedLen;
                        return APP_BC95_AT_OK;
                    }
                }
            }

            p_line = (p_eol != NULL) ? (p_eol + 1) : NULL;
        }
    }

    p_scan = p_resp;
    while ((p_pfx = strstr(p_scan, APP_BC95_AT_NSORF_PREFIX)) != NULL)
    {
        parsedSock = -1;
        parsedIp[0] = '\0';
        parsedPort = 0u;
        parsedLen = 0u;
        parsedHex[0] = '\0';
        parsedRemain = 0u;

        if (sscanf(p_pfx + APP_BC95_AT_NSORF_PREFIX_LEN,
                   "%d,%63[^,],%u,%u,%1024[^,],%u",
                   &parsedSock,
                   parsedIp,
                   &parsedPort,
                   &parsedLen,
                   parsedHex,
                   &parsedRemain) >= 5)
        {
            if ((parsedSock == targetSock) && (parsedLen > 0u))
            {
                *p_payloadLen = (uint16_t)parsedLen;
                /* [NSORF 파싱 디버그] 파싱 성공 상세 - sock/port/len + 본문 선두 hex */
#ifdef NBIOT_TRACE_DEBUG
                {
                    char dbgBody[97];   /* 선두 48바 */
                    unsigned n = parsedLen;
                    unsigned k;
                    if (n > 48u) { n = 48u; }
                    for (k = 0u; k < n; k++)
                    {
                        unsigned hi = (unsigned)(unsigned char)parsedHex[2u * k];
                        unsigned lo = (unsigned)(unsigned char)parsedHex[(2u * k) + 1u];
                        /* hex 문자를 값으로 복원 */
                        int hv = (hi <= '9') ? (hi - '0') : ((hi | 0x20u) - 'a' + 10);
                        int lv = (lo <= '9') ? (lo - '0') : ((lo | 0x20u) - 'a' + 10);
                        (void)snprintf(&dbgBody[k * 2u], 3u, "%02X", (unsigned)((hv * 16) + lv));
                    }
                    dbgBody[n * 2u] = '\0';
                    APP_LOGN("NBIOT", "[[AckDbg]] NSORF parsed sock=%d ip=%s port=%u len=%u body(48B)=%s%s",
                             parsedSock, parsedIp, parsedPort, parsedLen, dbgBody,
                             (parsedLen > 48u) ? "..." : "");
                }
#endif /* NBIOT_TRACE_DEBUG */
                return APP_BC95_AT_OK;
            }
        }

        p_scan = p_pfx + APP_BC95_AT_NSORF_PREFIX_LEN;
    }

    return APP_BC95_AT_ERR_NO_DATA;
}

static AppStatus_t App_Bc95AtWaitUdpAck(int32_t socketId, uint32_t timeoutMs, uint32_t pollMs)
{
#if ((APP_POLICY_WAIT_MGMT_ACK_ENABLE != APP_TRUE) && (APP_POLICY_WAIT_SERVICE_ACK_ENABLE != APP_TRUE))
    (void)socketId;
    (void)timeoutMs;
    (void)pollMs;
    return APP_STATUS_OK;
#else
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint32_t startTick;
    uint16_t rxLen;
    uint16_t ackPayloadLen;
    int printed;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE(socketId >= 0, APP_STATUS_INVALID_PARAM);

    /* [ACK 경로별 기능 2026-11-04]
     * mgmt    : NSORF 폴링(소켓 수신 버퍼 조회) — raw UDP 서버의 직접 응답을 확인.
     * service : NSORF 폴링 + NNMI 래치 카운터 폴링 병행 — raw 폴백 경로라도 서버
     *           응답이 LwM2M DL(+NNMI)로 푸시될 수 있으므로 래치 변화로도 ACK를
     *           판정한다. service 성공 시 DL-ACK(cmd별) 카운트도 함께 증가.
     * 두 경로 모두 수신 라인 스캔에서 NNMI/EVTIND를 회수해 유실을 방지한다. */
    {
        uint32_t beforeNnmiCount = s_bc95NnmiCount;
        uint8_t  servicePath = (APP_POLICY_WAIT_MGMT_ACK_ENABLE == APP_TRUE) ? APP_FALSE : APP_TRUE;
        /* mgmt/service 스위치가 둘 다 TRUE인 경우: 컴파일타임 정보로는 경로를
         * 알 수 없으므로 호출부의 isMgmtPath 분기에서 이미 걸러져 들어온다.
         * (TransmitUdpInternal의 ackPathEnabled 검사) 여기서는 스위치 조합으로
         * service 모드 여부만 추정한다. */
        if ((APP_POLICY_WAIT_SERVICE_ACK_ENABLE == APP_TRUE) &&
            (APP_POLICY_WAIT_MGMT_ACK_ENABLE != APP_TRUE))
        {
            servicePath = APP_TRUE;    /* service 전용: NNMI 병행 판정 활성 */
        }

        startTick = HAL_GetTick();
        while ((HAL_GetTick() - startTick) < timeoutMs)
        {
            APP_WWDGFeed();

            printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                               APP_BC95_AT_CMD_NSORF_FMT,
                               (long)socketId,
                               (unsigned)APP_POLICY_SERVER_ACK_READ_BYTES);
            APP_RETURN_IF_FALSE((printed > 0) && ((uint32_t)printed < sizeof(appBc95AtCmdTxBuf)), APP_STATUS_INVALID_PARAM);

            rxLen = 0u;
            status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                           g_appBc95AtRxBuf,
                                           (uint16_t)sizeof(g_appBc95AtRxBuf),
                                           APP_BC95_SOCKET_TIMEOUT_MS,
                                           &rxLen);
            if (status == APP_STATUS_OK)
            {
                cmeErr = 0;
                atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
                if (atStatus == APP_BC95_AT_OK)
                {
                    ackPayloadLen = 0u;
                    /* [NSORF 파싱 디버그 2026-11-04] 수신 응답 원본을 그대로 기록 -
                     * 서버가 보낸 83바 본문(NSORF 포함)의 실제 내용을 확인해
                     * 파싱 실패/본문 해석 문제를 화면에서 바로 판정한다 */
                    {
                        uint16_t dbgLen = (rxLen < (uint16_t)sizeof(g_appBc95AtRxBuf)) ? rxLen : ((uint16_t)sizeof(g_appBc95AtRxBuf) - 1u);
                        char dbgHex[(2u * 256u) + 1u];
                        uint16_t di;
                        for (di = 0u; (di < dbgLen) && (((di * 2u) + 2u) < sizeof(dbgHex)); di++)
                        {
                            (void)snprintf(&dbgHex[di * 2u], 3u, "%02X", (unsigned)g_appBc95AtRxBuf[di]);
                        }
                        dbgHex[di * 2u] = '\0';
#ifdef NBIOT_TRACE_DEBUG
                        APP_LOGN("NBIOT", "[[AckDbg]] NSORF raw rxLen=%u hex=%s",
                                 (unsigned)dbgLen, dbgHex);
#endif /* NBIOT_TRACE_DEBUG */
                    }
                    if (App_Bc95AtParseNsorfForSocket((const char *)g_appBc95AtRxBuf,
                                                      (int)socketId,
                                                      &ackPayloadLen) == APP_BC95_AT_OK)
                    {
                        /* [포맷 통일 2026-11-04] NSORF 응답 본문의 A3 프레임은
                         * mgmt/service 공용으로 동일 동작(명령 실행)을 수행한다.
                         * 카운트는 service 경로만 반영(mgmt는 통계 불필요).
                         * mgmt 전용 cmd(0x60~0x7F reserved)는 no-op 로그. */
                        {
                            /* [NSORF hex 디코드 v3 2026-11-05] 본 모뎀은 NSORF 데이터를
                             * hex 문자열("A3015050")로 준다 → 버퍼에 0xA3 바이트가 없어
                             * 기존 스캔은 항상 실패. 데이터 필드를 찾아 hex 디코드한
                             * 프레임으로 실행한다.
                             * [v3 수정 2026-11-05 실측(11:51)] 콤마 카운트 방식은 "+NSONMI:1,4"
                             * URC 접두의 콤마까지 섽나 4번째 콤마가 데이터 라인의 3번째
                             * 필드(포트 뒤)에 걸려 디코드가 항상 실패(n=0). 라인 단위로
                             * 바꿔 "+NSONMI"/"OK"가 아닌 유효 라인을 6필드 분해하고
                             * 5번째 필드(인덱스 4, 데이터)를 디코드한다. */
                            uint8_t hexFrame[APP_BC95_NNMI_LATCH_MAX];
                            uint16_t hexFrameLen = 0u;
                            int hexDecoded = 0;
                            {
                                const char *p_buf = (const char *)g_appBc95AtRxBuf;
                                const char *p_line = p_buf;
                                while ((p_line != NULL) && (hexDecoded == 0))
                                {
                                    const char *p_eol = strchr(p_line, '\n');
                                    size_t lineLen = (p_eol != NULL) ? (size_t)(p_eol - p_line) : strlen(p_line);
                                    char lineBuf[128];

                                    if ((lineLen > 0u) && (lineLen < sizeof(lineBuf)))
                                    {
                                        (void)memcpy(lineBuf, p_line, lineLen);
                                        lineBuf[lineLen] = '\0';
                                        char *p_trim = lineBuf;
                                        while ((*p_trim == '\r') || (*p_trim == ' ')) { p_trim++; }
                                        size_t tl = strlen(p_trim);
                                        while ((tl > 0u) && ((p_trim[tl - 1u] == '\r') || (p_trim[tl - 1u] == ' ')))
                                        {
                                            p_trim[--tl] = '\0';
                                        }

                                        /* URC 접두/OK/빈 라인 제외 — 데이터 라인만 6필드 분해 */
                                        if ((tl >= 6u) && (p_trim[0] != '+') && (strncmp(p_trim, "OK", 2u) != 0))
                                        {
                                            int f = 0;
                                            char *save = NULL;
                                            char *tok = NULL;
                                            char *fields[8];
                                            for (f = 0; f < 8; f++) { fields[f] = NULL; }
                                            f = 0;
                                            tok = p_trim;
                                            while ((tok != NULL) && (f < 8))
                                            {
                                                fields[f++] = tok;
                                                char *c = strchr(tok, ',');
                                                if (c == NULL) { break; }
                                                *c = '\0';
                                                tok = c + 1;
                                            }
                                            /* sock,ip,port,len,hexdata[,remain] — 5필드 이상 */
                                            if ((f >= 5) && (fields[4] != NULL) && (fields[4][0] != '\0') &&
                                                (strchr(fields[1], '.') != NULL))
                                            {
                                                const char *p_hex = fields[4];
                                                uint16_t n = 0u;
                                                while (((n * 2u) + 1u) < APP_BC95_NNMI_LATCH_MAX)
                                                {
                                                    char c1 = p_hex[n * 2u];
                                                    char c2 = p_hex[(n * 2u) + 1u];
                                                    int v1, v2;
                                                    if ((c1 >= '0') && (c1 <= '9')) { v1 = c1 - '0'; }
                                                    else if ((c1 >= 'A') && (c1 <= 'F')) { v1 = (c1 - 'A') + 10; }
                                                    else if ((c1 >= 'a') && (c1 <= 'f')) { v1 = (c1 - 'a') + 10; }
                                                    else { break; }
                                                    if ((c2 >= '0') && (c2 <= '9')) { v2 = c2 - '0'; }
                                                    else if ((c2 >= 'A') && (c2 <= 'F')) { v2 = (c2 - 'A') + 10; }
                                                    else if ((c2 >= 'a') && (c2 <= 'f')) { v2 = (c2 - 'a') + 10; }
                                                    else { break; }
                                                    hexFrame[n] = (uint8_t)((v1 * 16) + v2);
                                                    n++;
                                                }
                                                if (n > 0u)
                                                {
                                                    hexFrameLen = n;
                                                    hexDecoded = 1;
                                                }
                                            }
                                        }
                                    }
                                    p_line = (p_eol != NULL) ? (p_eol + 1) : NULL;
                                }
                            }

                            uint8_t frameCmd = 0u;
                            uint16_t scanStart = 0u;
                            uint16_t scanLen = hexDecoded ? hexFrameLen : rxLen;
                            const uint8_t *p_scan = hexDecoded ? hexFrame : g_appBc95AtRxBuf;
                            for (scanStart = 0u; (scanStart + 4u) <= scanLen; scanStart++)
                            {
                                if (p_scan[scanStart] == 0xA3u)
                                {
                                    /* [수정 2026-11-05] frameLen/범위는 스캔 대상(디코드 프레임)
                                     * 기준 — 원본 AT 버퍼 참조 시 ASCII 문자값으로 해석되어
                                     * 분류가 항상 차단됐다. */
                                    uint8_t frameLen = p_scan[scanStart + 1u];
                                    if ((frameLen + 3u) <= (scanLen - scanStart))
                                    {
                                        if (App_DlAckClassifyFrame(&p_scan[scanStart],
                                                                   (uint16_t)(frameLen + 3u),
                                                                   &frameCmd) == APP_STATUS_OK)
                                        {
                                            if (servicePath == APP_TRUE)
                                            {
                                                App_DlAckCountFrame(&p_scan[scanStart],
                                                                    (uint16_t)(frameLen + 3u),
                                                                    APP_TRUE);
                                            }
                                            App_DlCmdExecute(&p_scan[scanStart],
                                                             (uint16_t)(frameLen + 3u),
                                                             servicePath);
                                            APP_LOGD("NBIOT", "Server ACK frame cmd=0x%02X(%s) bytes=%u path=%s",

                                                     (unsigned)frameCmd, App_DlAckNameOf(frameCmd),
                                                     (unsigned)ackPayloadLen,
                                                     (servicePath == APP_TRUE) ? "svc" : "mgmt");
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        APP_LOGD("NBIOT", "Server ACK received (sock=%ld, bytes=%u%s)",
                                 (long)socketId,
                                 (unsigned)ackPayloadLen,
                                 (servicePath == APP_TRUE) ? ", svc-path" : "");
                        return APP_STATUS_OK;
                    }
                }
            }


            /* [ACK 경로별 기능] service 경로: NSORF 대기 중에도 DL 응답이
             * +NNMI로 푸시되면 래치 카운터 변화로 ACK 판정(DL-ACK 카운트는
             * 래치 수신 시점에 이미 App_DlAckCountFrame에서 증가함) */
            if (servicePath == APP_TRUE)
            {
                App_Bc95AtNnmiScanDmaBuf();
                if (s_bc95NnmiCount != beforeNnmiCount)
                {
                    APP_LOGD("NBIOT", "Server ACK via +NNMI latch (svc-path) count=%lu->%lu",
                             (unsigned long)beforeNnmiCount,
                             (unsigned long)s_bc95NnmiCount);
                    return APP_STATUS_OK;
                }
            }

            /* [NSORF 파싱 디버그] NSORF 없는 응답이면 요약 1줄 기록(스팸 방지 1/10) */
            {
                static uint8_t s_ackDbgSkip = 0u;
                s_ackDbgSkip++;
                if (s_ackDbgSkip >= 10u)
                {
                    s_ackDbgSkip = 0u;
#ifdef NBIOT_TRACE_DEBUG
                    APP_LOGD("NBIOT", "[[AckDbg]] NSORF absent poll rxLen=%u", (unsigned)rxLen);
#endif /* NBIOT_TRACE_DEBUG */
                }
            }
            App_Bc95AtDelayWithFeed(pollMs);
        }

        APP_LOGD("NBIOT", "Server ACK timeout (sock=%ld, timeout=%lums%s)",
                 (long)socketId,
                 (unsigned long)timeoutMs,
                 (servicePath == APP_TRUE) ? ", svc-path" : "");
        return APP_STATUS_UART_TIMEOUT;
    }
#endif
}

AppStatus_t App_Bc95AtUdpSendAndConfirm(int32_t        socketId,
                                        const char    *p_ip,
                                        uint16_t       port,
                                        const uint8_t *p_data,
                                        uint16_t       length,
                                        uint8_t        seqNum,
                                        uint32_t       confirmTimeoutMs)
{
    HAL_StatusTypeDef halStatus;
    AppStatus_t       status;
    AppBc95AtStatus_t atStatus;
    uint32_t          headerLen;
    int               printed;
    uint16_t          rxLen;
    uint16_t          bufCap;
    uint16_t          snapLen;
    uint32_t          startTick;
    uint32_t          elapsed;
    int               parsedStatus = -1;
    int32_t           cmeErr;
    char              appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];
    char              rxSnapshot[APP_BC95_AT_RX_BUF_SIZE];

    APP_RETURN_IF_FALSE(seqNum > 0u, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_ip != NULL) && (p_ip[0] != '\0'), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(socketId >= 0, APP_STATUS_INVALID_PARAM);

    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       "AT+NSOST=%ld,%s,%u,%u,",
                       (long)socketId, p_ip, (unsigned)port, (unsigned)length);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
    {
        APP_LOGD("NBIOT", "NSOST: header build fail");
        return APP_STATUS_INVALID_PARAM;
    }
    headerLen = (uint32_t)printed;

    if ((headerLen + (2u * (uint32_t)length) + 16u) >= sizeof(appBc95AtCmdTxBuf))
    {
        APP_LOGD("NBIOT", "NSOST: buf too small");
        return APP_STATUS_INVALID_PARAM;
    }
    App_Bc95AtBytesToHex(p_data, length, &appBc95AtCmdTxBuf[headerLen]);
    headerLen += (2u * (uint32_t)length);

    printed = snprintf(&appBc95AtCmdTxBuf[headerLen],
                       sizeof(appBc95AtCmdTxBuf) - headerLen,
                       ",%u\r\n", (unsigned)seqNum);
    if ((printed <= 0) || ((headerLen + (uint32_t)printed) >= sizeof(appBc95AtCmdTxBuf)))
    {
        APP_LOGD("NBIOT", "NSOST: tail build fail");
        return APP_STATUS_INVALID_PARAM;
    }

    APP_LOGD("NBIOT", "NSOST send: sock=%ld, ip=%s, port=%u, len=%u, seq=%u",
             (long)socketId, p_ip, (unsigned)port, (unsigned)length, (unsigned)seqNum);

    rxLen  = 0u;
    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "NSOST send fail (status=%d, rxLen=%u)", (int)status, (unsigned)rxLen);
        if (status == APP_STATUS_UART_RX_FAILED) return APP_STATUS_UART_RX_FAILED;
        return APP_STATUS_UART_TIMEOUT;
    }

    snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, rxLen,
                                     rxSnapshot, (uint16_t)sizeof(rxSnapshot));
    cmeErr   = 0;
    atStatus = App_Bc95AtCheckResponse(rxSnapshot, &cmeErr);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "NSOST no OK (parse=%s, cme=%ld, rxLen=%lu, raw=[%s])",
                 App_Bc95AtGetStatusString(atStatus), (long)cmeErr, (unsigned long)snapLen, rxSnapshot);
        return APP_STATUS_UART_TIMEOUT;
    }

    if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                         (int)socketId,
                                         (int)seqNum,
                                         &parsedStatus) == APP_BC95_AT_OK)
    {
        if (parsedStatus == 1)
        {
            APP_LOGD("NBIOT", "NSOSTR confirmed (fast): sock=%ld, seq=%u",
                     (long)socketId, (unsigned)seqNum);
            return APP_STATUS_OK;
        }
        APP_LOGD("NBIOT", "NSOSTR fail (fast): sock=%ld, seq=%u, status=%d (transient)",
                 (long)socketId, (unsigned)seqNum, parsedStatus);
        return APP_STATUS_UART_TIMEOUT;
    }

    bufCap = (uint16_t)(sizeof(g_appBc95AtRxBuf) - 1u);

    if (rxLen >= bufCap)
    {
        APP_LOGD("NBIOT", "NSOSTR wait: buf full after OK (rxLen=%u)", (unsigned)rxLen);
        if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                             (int)socketId,
                                             (int)seqNum,
                                             &parsedStatus) == APP_BC95_AT_OK)
        {
            return (parsedStatus == 1) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
        }
        return APP_STATUS_UART_TIMEOUT;
    }

    g_appBc95AtRxContext.p_huart        = APP_UART_NBIOT_HANDLE;
    g_appBc95AtRxContext.p_buffer       = g_appBc95AtRxBuf;
    g_appBc95AtRxContext.bufferSize     = bufCap;
    g_appBc95AtRxContext.receivedLength = rxLen;
    g_appBc95AtRxContext.completed      = APP_FALSE;
    g_appBc95AtRxContext.error          = APP_FALSE;
    g_appBc95AtRxContext.active         = APP_TRUE;

    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF |
                          UART_CLEAR_NEF  | UART_CLEAR_PEF);

    /* URC 모니터가 RX IT 를 쥐고 있으면 HAL_UART_Receive_IT 가 HAL_BUSY 로
     * 실패한다(boot12 실측: "NSOSTR wait: RX_IT start fail (2)"). NSOSTR 응답
     * 대기를 시작하기 전에 모니터를 해제해 IT 를 이 컨텍스트에 넘긴다. */
    App_Bc95AtDisarmUrcMonitor(APP_UART_NBIOT_HANDLE);
    halStatus = HAL_UART_Receive_IT(APP_UART_NBIOT_HANDLE,
                                    &g_appBc95AtRxBuf[rxLen], 1u);
    if (halStatus != HAL_OK)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        APP_LOGD("NBIOT", "NSOSTR wait: RX_IT start fail (%d)", (int)halStatus);
        return APP_STATUS_UART_RX_FAILED;
    }

    if (g_appBc95AtRxContext.error == APP_TRUE)
    {
        App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
        APP_LOGD("NBIOT", "NSOSTR wait: error right after start");
        return APP_STATUS_UART_RX_FAILED;
    }

    startTick = HAL_GetTick();
    while (1)
    {
        rxLen = g_appBc95AtRxContext.receivedLength;
        if (rxLen >= bufCap)
        {
            rxLen = bufCap;
        }
        snapLen = App_Bc95AtCopySnapshot(g_appBc95AtRxBuf, rxLen,
                                         rxSnapshot, (uint16_t)sizeof(rxSnapshot));

        if (g_appBc95AtRxContext.error == APP_TRUE)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF  | UART_CLEAR_PEF);
            APP_LOGD("NBIOT", "NSOSTR wait: UART err (rxLen=%u, raw=[%s])",
                     (unsigned)g_appBc95AtRxContext.receivedLength, rxSnapshot);
            return APP_STATUS_UART_RX_FAILED;
        }

        atStatus = App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                                    (int)socketId,
                                                    (int)seqNum,
                                                    &parsedStatus);
        if (atStatus == APP_BC95_AT_OK)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            if (parsedStatus == 1)
            {
                APP_LOGD("NBIOT", "NSOSTR confirmed: sock=%ld, seq=%u (elapsed=%lums)",
                         (long)socketId, (unsigned)seqNum,
                         (unsigned long)(HAL_GetTick() - startTick));
                return APP_STATUS_OK;
            }
            APP_LOGD("NBIOT", "NSOSTR fail: sock=%ld, seq=%u, status=%d (transient)",
                     (long)socketId, (unsigned)seqNum, parsedStatus);
            return APP_STATUS_UART_TIMEOUT;
        }

        if (g_appBc95AtRxContext.receivedLength >= bufCap)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            APP_LOGD("NBIOT", "NSOSTR wait: buffer full (rxLen=%u, raw=[%s])",
                     (unsigned)g_appBc95AtRxContext.receivedLength, rxSnapshot);
            if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                                 (int)socketId,
                                                 (int)seqNum,
                                                 &parsedStatus) == APP_BC95_AT_OK)
            {
                return (parsedStatus == 1) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
            }
            return APP_STATUS_UART_TIMEOUT;
        }

        elapsed = HAL_GetTick() - startTick;
        if (elapsed >= confirmTimeoutMs)
        {
            App_Bc95AtFinishRxIt(APP_UART_NBIOT_HANDLE);
            __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE,
                                  UART_CLEAR_OREF | UART_CLEAR_FEF |
                                  UART_CLEAR_NEF  | UART_CLEAR_PEF);
            if (App_Bc95AtFindNsostrForSocketSeq(rxSnapshot,
                                                 (int)socketId,
                                                 (int)seqNum,
                                                 &parsedStatus) == APP_BC95_AT_OK)
            {
                APP_LOGD("NBIOT", "NSOSTR late-detected: sock=%ld, seq=%u, status=%d",
                         (long)socketId, (unsigned)seqNum, parsedStatus);
                return (parsedStatus == 1) ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
            }
            APP_LOGD("NBIOT", "NSOSTR timeout (sock=%ld, seq=%u, rxLen=%u, raw=[%s])",
                     (long)socketId, (unsigned)seqNum, (unsigned)rxLen, rxSnapshot);
            return APP_STATUS_UART_TIMEOUT;
        }
        APP_WWDGFeed();
        HAL_Delay(10u);
    }
}

static AppStatus_t App_Bc95AtCloseSocketInternal(int32_t socketId,
                                                    uint8_t quietNoSocket,
                                                    AppBc95AtStatus_t *p_atStatusOut)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;
    int printed;
    int32_t cmeErr;
    char appBc95AtCmdTxBuf[APP_BC95_AT_CMD_TX_BUF_SIZE];

    APP_RETURN_IF_FALSE((g_appBc95AtInitialized == APP_TRUE), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((socketId >= 0), APP_STATUS_INVALID_PARAM);

    if (p_atStatusOut != NULL)
    {
        *p_atStatusOut = APP_BC95_AT_OK;
    }

    printed = snprintf(appBc95AtCmdTxBuf, sizeof(appBc95AtCmdTxBuf),
                       APP_BC95_AT_CMD_NSOCL_FMT, (long)socketId);
    if ((printed <= 0) || ((uint32_t)printed >= sizeof(appBc95AtCmdTxBuf)))
        return APP_STATUS_INVALID_PARAM;

    status = App_Bc95AtSendCommand(appBc95AtCmdTxBuf,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_SOCKET_TIMEOUT_MS, &rxLen);

#ifdef NBIoT_SIMULATION_CODE
    APP_LOGD("NBIOT", "Forcely err NSOCL send fail test..."); // NBIoT simulation test
    status = APP_STATUS_INVALID_PARAM;
    quietNoSocket = APP_FALSE;
#endif // NBIoT_SIMULATION_CODE
    if (status != APP_STATUS_OK)
    {
        if (quietNoSocket != APP_TRUE)
        {
            APP_LOGD("NBIOT", "NSOCL send fail (sock=%ld, status=%d)", (long)socketId, (int)status);
        }
        return status;
    }

    cmeErr = 0;
    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, &cmeErr);
    if (p_atStatusOut != NULL)
    {
        *p_atStatusOut = atStatus;
    }
#ifdef NBIoT_SIMULATION_CODE
    APP_LOGD("NBIOT", "Forcely err NSOCL not OK test..."); // NBIoT simulation test
    atStatus = APP_BC95_AT_ERR_AT_ERROR;
#endif // NBIoT_SIMULATION_CODE
    if (atStatus != APP_BC95_AT_OK)
    {
        if (quietNoSocket != APP_TRUE)
        {
            APP_LOGD("NBIOT", "NSOCL not OK (sock=%ld, parse=%s)",
                     (long)socketId, App_Bc95AtGetStatusString(atStatus));
        }
        return APP_STATUS_FATAL;
    }
    APP_LOGD("NBIOT", "UDP socket closed: %ld", (long)socketId);
    /* [ADD] 닫기 성공 -> 추적 해제 */
    if ((socketId >= 0) && (socketId <= APP_BC95_UDP_SOCKET_ID_MAX))
        s_nbiotUdpSocketOpenMask &= ~((uint32_t)1u << socketId);
    return APP_STATUS_OK;
}

AppStatus_t App_Bc95AtCloseSocket(int32_t socketId)
{
    return App_Bc95AtCloseSocketInternal(socketId, APP_FALSE, NULL);
}

void App_Bc95AtCloseAllSockets(void)
{
    int sid;
    uint32_t closedCount = 0u;
    uint32_t alreadyClosedCount = 0u;
    uint32_t sendFailCount = 0u;
    uint32_t otherFailCount = 0u;

    APP_LOGD("NBIOT", "close sockets 0..%d (open mask=0x%02lx)",
             APP_BC95_UDP_SOCKET_ID_MAX, (unsigned long)s_nbiotUdpSocketOpenMask);
    for (sid = 0; sid <= APP_BC95_UDP_SOCKET_ID_MAX; sid++)
    {
        AppStatus_t closeStatus;
        AppBc95AtStatus_t atStatus = APP_BC95_AT_OK;

        /* [ADD] 미개설 소켓은 NSOCL 생략 (LGU FW 530 예방) */
        if ((s_nbiotUdpSocketOpenMask & ((uint32_t)1u << sid)) == 0u)
            continue;

        closeStatus = App_Bc95AtCloseSocketInternal((int32_t)sid, APP_TRUE, &atStatus);
        if (closeStatus == APP_STATUS_OK)
        {
            closedCount++;
        }
        else if ((closeStatus == APP_STATUS_FATAL) &&
                 ((atStatus == APP_BC95_AT_ERR_AT_ERROR) ||
                  (atStatus == APP_BC95_AT_ERR_NO_OK) ||
                  (atStatus == APP_BC95_AT_ERR_CME_ERROR)))  /* [ADD] CME 530=이미 닫힘(LGU FW) */
        {
            alreadyClosedCount++;
            /* [ADD] 마스크가 실제보다 낙관적이었던 경우 정합성 복구 */
            s_nbiotUdpSocketOpenMask &= ~((uint32_t)1u << sid);
        }
        else if ((closeStatus == APP_STATUS_UART_RX_FAILED) ||
                 (closeStatus == APP_STATUS_UART_TX_FAILED) ||
                 (closeStatus == APP_STATUS_UART_TIMEOUT))
        {
            sendFailCount++;
        }
        else
        {
            otherFailCount++;
        }

        App_NBIoTDetachNoteOutcome(closeStatus);
        if (App_NBIoTDetachModuleDead() == APP_TRUE)
        {
            APP_LOGD("NBIOT", "sock clean dead streak=%u abort %d..%d",
                     s_nbiotDetachNoRespStreak, sid + 1, APP_BC95_UDP_SOCKET_ID_MAX);
            otherFailCount += (uint32_t)(APP_BC95_UDP_SOCKET_ID_MAX - sid);
            break;
        }

        APP_WWDGFeed();
    }

    APP_LOGD("NBIOT", "sock clean c=%lu ac=%lu sf=%lu of=%lu",
             (unsigned long)closedCount,
             (unsigned long)alreadyClosedCount,
             (unsigned long)sendFailCount,
             (unsigned long)otherFailCount);
}

AppStatus_t App_Bc95AtUdpSendOnce(const char *p_logTag,
                                  const char *p_host, uint16_t port,
                                  const uint8_t *p_data, uint16_t length,
                                  AppBc95UdpResult_t *p_result,
                                  uint8_t isMgmtPath)
{
    /* [DFOTA 1.6.3] hold 중에는 모듈 제어(AT command)를 금지한다 */
    if (App_Bc95AtDfotaHoldActive() == APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[DFOTA]] firmware update in progress -> skip UDP send (%s)", p_logTag);
        return APP_STATUS_BUSY;
    }

    AppStatus_t status;
    AppMeterServerFormatOptions_t opt;
    uint8_t waitServerAck;
    uint32_t ackTimeoutMs;
    uint32_t ackPollMs;
    int32_t socketId = -1;
    char ipBuf[APP_BC95_IP_STR_SIZE];
    uint32_t attempt;
    uint8_t  seq = 0u;
    AppBc95UdpResult_t result;
    uint8_t dnsAttempts = 0u;
    uint32_t dnsElapsedMs = 0u;
    uint8_t usedCachedIp = APP_FALSE;

    (void)memset(&result, 0, sizeof(result));
    result.socketId  = -1;
    result.lastStage = APP_BC95_UDP_STAGE_NONE;
    ipBuf[0] = '\0';

    APP_RETURN_IF_FALSE((p_logTag != NULL) && (p_host != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_data != NULL) && (length > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((length <= APP_BC95_UDP_MAX_PAYLOAD), APP_STATUS_INVALID_PARAM);

    status = App_MeterServerOptionsLoad(&opt);
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_NOT_INITIALIZED))
    {
        return status;
    }
    waitServerAck = ((isMgmtPath != APP_FALSE) ? App_NbiotResolveWaitServerAck(&opt)
                                               : App_NbiotResolveWaitServiceAck(&opt));
    if (g_appBc95UdpSkipAckWait != APP_FALSE)
    {
        waitServerAck = 0u;    /* [QUALITY-REPORT] 품질서버는 ACK 프로토콜 없음 */
    }
    ackTimeoutMs = ((isMgmtPath != APP_FALSE) ? App_MeterServerOptionsGetAckTimeoutMs(&opt)
                                              : ((uint32_t)opt.svcAckTimeoutSec * 1000u));
    ackPollMs = ((isMgmtPath != APP_FALSE) ? App_MeterServerOptionsGetAckPollMs(&opt)
                                           : ((uint32_t)opt.svcAckPoll100Ms * 100u));

    /* 1) DNS */
    result.lastStage = APP_BC95_UDP_STAGE_RESOLVE;
#ifdef NBIOT_SUPPORT_DNS
    if (App_NbiotIsIpv4Literal(p_host) == APP_TRUE)
    {
        (void)strncpy(ipBuf, p_host, sizeof(ipBuf) - 1u);
    }
    else if (App_NbiotDnsCacheLookup(p_host, ipBuf, sizeof(ipBuf)) == APP_TRUE)
    {
        usedCachedIp = APP_TRUE;
        result.usedCachedIp = APP_TRUE;
        APP_LOGD("NBIOT", "DNS cache hit: %s -> %s", p_host, ipBuf);
    }
    else
    {
        status = App_Bc95AtResolveHostRobustInternal(p_host, ipBuf, sizeof(ipBuf),
                                                     &dnsAttempts, &dnsElapsedMs);
        result.dnsAttempts = dnsAttempts;
        result.dnsElapsedMs = dnsElapsedMs;
        if ((status != APP_STATUS_OK) || (ipBuf[0] == '\\0'))
        {
            APP_LOGD("NBIOT", "DNS resolve failed for %s", p_host);
            if (p_result != NULL) (void)memcpy(p_result, &result, sizeof(*p_result));
            return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
        }
        App_NbiotDnsCacheStore(p_host, ipBuf);
    }

    (void)strncpy(result.resolvedIp, ipBuf, sizeof(result.resolvedIp) - 1u);
#else
    (void)strcpy(ipBuf, p_host);
    (void)strcpy(result.resolvedIp, ipBuf);
#endif

    /* 2) Create socket (재시도) */
    result.lastStage = APP_BC95_UDP_STAGE_CREATE;
    for (attempt = 1u; attempt <= APP_BC95_UDP_SEND_RETRY_MAX; attempt++)
    {
        APP_WWDGFeed();
        status = App_Bc95AtCreateUdpSocket(APP_BC95_UDP_LOCAL_PORT, &socketId);
        if (status == APP_STATUS_OK) break;

        APP_LOGD("NBIOT", "NSOCR retry %lu/%lu (status=%d)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_UDP_SEND_RETRY_MAX, (int)status);
        if (attempt < APP_BC95_UDP_SEND_RETRY_MAX)
            App_Bc95AtDelayWithFeed(APP_BC95_UDP_SEND_RETRY_DELAY_MS);
    }
    if ((status != APP_STATUS_OK) || (socketId < 0))
    {
        APP_LOGD("NBIOT", "UDP socket create failed");
        if (p_result != NULL) (void)memcpy(p_result, &result, sizeof(*p_result));
        return (status != APP_STATUS_OK) ? status : APP_STATUS_FATAL;
    }
    result.socketId = socketId;

    /* 3) Send (재시도, 매 시도마다 새 seq) */
    result.lastStage = APP_BC95_UDP_STAGE_SEND;
    status = APP_STATUS_FATAL;
    for (attempt = 1u; attempt <= APP_BC95_UDP_SEND_RETRY_MAX; attempt++)
    {
        seq = App_Bc95AtNextUdpSeq();   /* 패치: 전역 단조 증가 seq */
        result.seqNumber = seq;
        result.udpSendAttempts = (uint8_t)attempt;

        APP_WWDGFeed();
        status = App_Bc95AtUdpSendAndConfirm(socketId, ipBuf, port,
                                             p_data, length, seq,
                                             APP_BC95_NSOST_CONFIRM_TIMEOUT_MS);
        if (status == APP_STATUS_OK)
        {
            result.lastStage     = APP_BC95_UDP_STAGE_CONFIRM;
            result.sentBytes     = length;
            result.sendConfirmed = APP_TRUE;
            break;
        }
        if (status == APP_STATUS_INVALID_PARAM)
        {
            APP_LOGD("NBIOT", "UDP send invalid param");
            break;
        }

        APP_LOGD("NBIOT", "UDP send retry %lu/%lu (status=%d)",
                 (unsigned long)attempt,
                 (unsigned long)APP_BC95_UDP_SEND_RETRY_MAX, (int)status);
        if (attempt < APP_BC95_UDP_SEND_RETRY_MAX)
            App_Bc95AtDelayWithFeed(APP_BC95_UDP_SEND_RETRY_DELAY_MS);
    }

#ifdef NBIOT_SUPPORT_DNS
    if ((status != APP_STATUS_OK) && (usedCachedIp == APP_TRUE))
    {
        App_NbiotDnsCacheInvalidate(p_host);
    }
#endif

#if ((APP_POLICY_WAIT_MGMT_ACK_ENABLE == APP_TRUE) || (APP_POLICY_WAIT_SERVICE_ACK_ENABLE == APP_TRUE))
    if ((status == APP_STATUS_OK) && (waitServerAck != APP_TRUE) &&
        ((isMgmtPath != APP_FALSE) ? (APP_POLICY_WAIT_MGMT_ACK_ENABLE != APP_TRUE)
                                   : (APP_POLICY_WAIT_SERVICE_ACK_ENABLE != APP_TRUE)))
    {
        /* [ACK 독립 제어 2026-11-04] 컴파일 타임 스위치로 이 경로의 ACK 대기가
         * 꺼진 경우를 명시적으로 스킵한다. MGMT=FALSE/SERVICE=TRUE 설정에서
         * mgmt raw 경로는 ackWaitEnabled 기본값(FALSE)이라 여기로 진입해
         * "wait mgmt ACK" 대기 없이 즉시 통과된다(로그 한 줄로 증거 남김). */
        APP_LOGD("NBIOT", "%s policy: skip %s ACK wait (compile-time path disabled)",
                 p_logTag, (isMgmtPath != APP_FALSE) ? "mgmt" : "service");
    }
    else if ((status == APP_STATUS_OK) && (waitServerAck == APP_TRUE))
    {
        const char *p_ackPath = (isMgmtPath != APP_FALSE) ? "mgmt" : "service";
        uint8_t ackPathEnabled = (isMgmtPath != APP_FALSE)
                                 ? (uint8_t)APP_POLICY_WAIT_MGMT_ACK_ENABLE
                                 : (uint8_t)APP_POLICY_WAIT_SERVICE_ACK_ENABLE;
        if (ackPathEnabled != APP_TRUE)
        {
            APP_LOGD("NBIOT", "%s policy: skip %s ACK wait (path disabled)", p_logTag, p_ackPath);
        }
        else
        {
            APP_LOGD("NBIOT", "%s wait %s ACK timeout=%lu ms poll=%lu ms", p_logTag, p_ackPath, (unsigned long)ackTimeoutMs, (unsigned long)ackPollMs);
            status = App_Bc95AtWaitUdpAck(socketId, ackTimeoutMs, ackPollMs);
        }
    }
    else if ((status == APP_STATUS_OK) && (waitServerAck != APP_TRUE))
    {
        APP_LOGD("NBIOT", "%s policy: skip %s ACK wait (runtime disabled)",
                 p_logTag, (isMgmtPath != APP_FALSE) ? "mgmt" : "service");
    }
#endif

    /* 4) Close (성공/실패 무관) */
    result.lastStage = APP_BC95_UDP_STAGE_CLOSE;
    (void)App_Bc95AtCloseSocket(socketId);

    if (status == APP_STATUS_OK)
    {
        result.lastStage = APP_BC95_UDP_STAGE_DONE;
        APP_LOGD("NBIOT", "UDP send done: %s:%u, %u bytes, seq=%u",
                 ipBuf, (unsigned)port, (unsigned)length, (unsigned)seq);
    }
    else
    {
        APP_LOGD("NBIOT", "UDP send/ack failed (stage=%d, status=%d)",
                 (int)result.lastStage, (int)status);
    }

    if (p_result != NULL) (void)memcpy(p_result, &result, sizeof(*p_result));
    return status;
}

/* 전역 - 마지막 동기화된 시간 */
static AppBc95Time_t g_appBc95AtLastTime;

/* ============================================================
 *  AT+CCLK? 응답 파싱
 *
 *  응답 형식:
 *    +CCLK:25/05/22,14:30:45+36
 *    OK
 *
 *  필드:
 *    YY/MM/DD : 연/월/일 (실제 연도 = 2000 + YY)
 *    HH:MM:SS : 시:분:초
 *    ±TZ      : 타임존, 15분 단위 (선택, 예: +36 = UTC+9h)
 *
 *  BC95-GV 는 네트워크 시간을 받기 전에는
 *  "+CCLK:70/01/01,00:00:00+00" 같은 값을 반환할 수 있다.
 *  연도가 APP_BC95_TIME_MIN_VALID_YEAR 미만이면 NO_DATA 로 처리.
 * ============================================================ */
AppBc95AtStatus_t App_Bc95AtParseCclk(const char *p_resp, AppBc95Time_t *p_time)
{
    AppBc95AtStatus_t status;
    int32_t cmeErr;
    const char *p_payload;
    int parsedYy = 0, parsedMo = 0, parsedDd = 0;
    int parsedHh = 0, parsedMi = 0, parsedSs = 0;
    int parsedTz = 0;
    char tzSign  = '+';
    int parsedFields;

    if ((p_resp == NULL) || (p_time == NULL)) return APP_BC95_AT_ERR_PARAM;
    (void)memset(p_time, 0, sizeof(*p_time));

    cmeErr = 0;
    status = App_Bc95AtCheckResponse(p_resp, &cmeErr);
    if (status != APP_BC95_AT_OK) return status;

    p_payload = strstr(p_resp, APP_BC95_AT_CCLK_PREFIX);
    if (p_payload == NULL) return APP_BC95_AT_ERR_NO_PREFIX;
    p_payload += APP_BC95_AT_CCLK_PREFIX_LEN;
    while ((*p_payload == ' ') || (*p_payload == '\t') || (*p_payload == '"'))
        p_payload++;

    /* "YY/MM/DD,HH:MM:SS±TZ" (TZ 있는 경우) */
    parsedFields = sscanf(p_payload, "%d/%d/%d,%d:%d:%d%c%d",
                          &parsedYy, &parsedMo, &parsedDd,
                          &parsedHh, &parsedMi, &parsedSs,
                          &tzSign, &parsedTz);

    if (parsedFields < 6)
    {
        APP_LOGD("NBIOT", "CCLK format error: [%s]", p_payload);
        return APP_BC95_AT_ERR_FORMAT;
    }

    /* TZ 부분이 부족하면 0 으로 */
    if (parsedFields < 8)
    {
        parsedTz = 0;
        tzSign   = '+';
    }

    /* 범위 검증 */
    if ((parsedYy < 0)  || (parsedYy > 99)  ||
        (parsedMo < 1)  || (parsedMo > 12)  ||
        (parsedDd < 1)  || (parsedDd > 31)  ||
        (parsedHh < 0)  || (parsedHh > 23)  ||
        (parsedMi < 0)  || (parsedMi > 59)  ||
        (parsedSs < 0)  || (parsedSs > 59)  ||
        (parsedTz < 0)  || (parsedTz > 96))
    {
        return APP_BC95_AT_ERR_RANGE;
    }

    /* 네트워크 시간을 못 받은 상태 거르기 */
    if ((uint16_t)(2000 + parsedYy) < APP_BC95_TIME_MIN_VALID_YEAR)
    {
        APP_LOGD("NBIOT", "CCLK: network time not synced yet (year=20%02d)", parsedYy);
        return APP_BC95_AT_ERR_NO_DATA;
    }

    p_time->dateTime.year   = (uint16_t)(2000 + parsedYy);
    p_time->dateTime.month  = (uint8_t)parsedMo;
    p_time->dateTime.day    = (uint8_t)parsedDd;
    p_time->dateTime.hour   = (uint8_t)parsedHh;
    p_time->dateTime.minute = (uint8_t)parsedMi;
    p_time->dateTime.second = (uint8_t)parsedSs;

    if (tzSign == '-')
        p_time->tzQuarterHour = (int8_t)(-parsedTz);
    else
        p_time->tzQuarterHour = (int8_t)parsedTz;

    p_time->valid = APP_TRUE;
    return APP_BC95_AT_OK;
}

/* ============================================================
 *  AT+CCLK? 송신 + 파싱
 * ============================================================ */
AppStatus_t App_Bc95AtFetchTime(AppBc95Time_t *p_time)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    APP_RETURN_IF_FALSE((p_time != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(p_time, 0, sizeof(*p_time));

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CCLK_QUERY,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "CCLK send fail (status=%d, rxLen=%u)",
                 (int)status, (unsigned)rxLen);
        return status;
    }

    atStatus = App_Bc95AtParseCclk((const char *)g_appBc95AtRxBuf, p_time);
    if (atStatus == APP_BC95_AT_OK)
    {
        (void)memcpy(&g_appBc95AtLastTime, p_time, sizeof(g_appBc95AtLastTime));
        return APP_STATUS_OK;
    }

    if (atStatus == APP_BC95_AT_ERR_NO_DATA)
    {
        /* 아직 네트워크 시간 미수신 - 일시 실패 */
        return APP_STATUS_UART_TIMEOUT;
    }

    APP_LOGD("NBIOT", "CCLK parse fail (atStatus=%s, raw=[%s])",
             App_Bc95AtGetStatusString(atStatus), g_appBc95AtRxBuf);
    return APP_STATUS_FATAL;
}

AppStatus_t App_Bc95AtFetchTimeWithRetry(AppBc95Time_t *p_time, uint32_t maxRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint32_t i;

    if (maxRetry == 0u) maxRetry = 1u;

    for (i = 0u; i < maxRetry; i++)
    {
        st = App_Bc95AtFetchTime(p_time);
        if (st == APP_STATUS_OK) return APP_STATUS_OK;
        if (st == APP_STATUS_FATAL) return st;

        APP_LOGD("NBIOT", "CCLK retry %lu/%lu (status=%d)",
                 (unsigned long)(i + 1u), (unsigned long)maxRetry, (int)st);
        if (i < (maxRetry - 1u))
            App_Bc95AtDelayWithFeed(APP_BC95_TIME_SYNC_RETRY_DELAY_MS);
    }
    return st;
}

/* ============================================================
 *  AT+CTZU=1 (자동 타임존 동기화 활성화)
 *    부팅 시 1회 호출하면 이후 네트워크 시간이 자동 갱신된다.
 *    실패해도 치명적이지 않음 (수동 CCLK 로 fallback 가능).
 * ============================================================ */
AppStatus_t App_Bc95AtEnableAutoTimezone(void)
{
    AppStatus_t status;
    AppBc95AtStatus_t atStatus;
    uint16_t rxLen = 0u;

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CTZU_ENABLE,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS, &rxLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "CTZU=1 send fail (status=%d) - ignored",
                 (int)status);
        return status;
    }

    atStatus = App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL);
    if (atStatus != APP_BC95_AT_OK)
    {
        APP_LOGD("NBIOT", "CTZU=1 not OK (parse=%s) - ignored(optional feature not supported)",
                 App_Bc95AtGetStatusString(atStatus));
        return APP_STATUS_FATAL;
    }

    APP_LOGD("NBIOT", "Auto-timezone (CTZU=1) enabled");
    return APP_STATUS_OK;
}

/* ============================================================
 *  통합: 모듈에서 시간 가져와 RTC 에 적용 (기존 RTC_SetTime 사용)
 *
 *  주의: RTC_SetTime() 의 year 인자가 2자리 ('25' 같은 BCD)인지
 *        4자리 ('2025')인지에 따라 호출 방식이 달라진다.
 *        STM32 HAL 의 RTC_DateTypeDef.Year 는 0~99 (2000 기준) 이므로
 *        2자리(YY) 전달이 표준이다. 여기서는 YY 만 전달.
 * ============================================================ */
AppStatus_t App_Bc95AtSyncTimeToRtc(void)
{
    AppBc95Time_t timeInfo;

    if ((App_Bc95AtFetchTimeWithRetry(&timeInfo, APP_BC95_TIME_SYNC_RETRY_MAX) == APP_STATUS_OK) &&
        (timeInfo.valid != 0u))
    {
        return App_ClockSyncFromNbiot(&timeInfo); /* INITS/오차 판단 + 타임존 반영 */
    }
    return APP_STATUS_FATAL;
}

#ifndef APP_RTC_SYNC_THRESHOLD_SEC
#define APP_RTC_SYNC_THRESHOLD_SEC   (30)   /* 이 값(초) 초과 시 RTC 재설정 */
#endif

/* --- 시각 <-> epoch 변환  */
/* AppDateTime_t → 2000-01-01 기준 경과 초. 시각 비교 전용(윤년 반영) */
static int64_t App_DateTimeToEpoch(const AppDateTime_t *dt)
{
    static const uint16_t daysBeforeMonth[12] =
        { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int64_t days = 0;
    uint16_t y;

    for (y = 2000u; y < dt->year; y++)
    {
        uint8_t leap = (((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u))) ? 1u : 0u;
        days += (365 + leap);
    }
    if ((dt->month >= 1u) && (dt->month <= 12u))
    {
        days += daysBeforeMonth[dt->month - 1u];
        if (dt->month > 2u)
        {
            uint8_t leap = (((dt->year % 4u) == 0u) &&
                            (((dt->year % 100u) != 0u) || ((dt->year % 400u) == 0u))) ? 1u : 0u;
            days += leap;
        }
    }
    days += (dt->day - 1);

    return (days * 86400LL) + ((int64_t)dt->hour * 3600LL)
         + ((int64_t)dt->minute * 60LL) + (int64_t)dt->second;
}

/* epoch 초 → AppDateTime_t (역변환). 타임존 보정 결과를 다시 년/월/일로 */
#if 0 //optimize for del
static void App_EpochToDateTime(int64_t epoch, AppDateTime_t *dt)
{
    static const uint8_t mdaysNorm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int64_t days = epoch / 86400LL;
    int64_t rem  = epoch % 86400LL;
    uint16_t y = 2000u;

    if (rem < 0) { rem += 86400LL; days -= 1; }

    dt->hour   = (uint8_t)(rem / 3600LL);
    dt->minute = (uint8_t)((rem % 3600LL) / 60LL);
    dt->second = (uint8_t)(rem % 60LL);

    for (;;)
    {
        uint8_t leap = (((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u))) ? 1u : 0u;
        int64_t yearDays = 365 + leap;
        if (days < yearDays) break;
        days -= yearDays;
        y++;
    }
    dt->year = y;

    {
        uint8_t m;
        uint8_t leap = (((y % 4u) == 0u) && (((y % 100u) != 0u) || ((y % 400u) == 0u))) ? 1u : 0u;
        for (m = 0u; m < 12u; m++)
        {
            uint8_t dmax = mdaysNorm[m] + (((m == 1u) && leap) ? 1u : 0u);
            if (days < dmax) break;
            days -= dmax;
        }
        dt->month = (uint8_t)(m + 1u);
        dt->day   = (uint8_t)(days + 1);
    }
}
#endif

/**
 * @brief NB-IoT CCLK 시각을 타임존 보정 후 RTC와 비교하여,
 *        RTC가 미설정(INITS=0)이거나 오차가 임계값을 넘으면 RTC를 설정한다.
 */
AppStatus_t App_ClockSyncFromNbiot(const AppBc95Time_t *nbTime)
{
    AppDateTime_t rtcTime;
    int64_t nbEpoch;
    int64_t diff;
    uint8_t needSet = 0u;

    APP_RETURN_IF_FALSE((nbTime != NULL) && (nbTime->valid != 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(nbTime->dateTime.year >= APP_BC95_TIME_MIN_VALID_YEAR, APP_STATUS_INVALID_PARAM);

    APP_LOGD("NBIOT", "Get Module time: %04u-%02u-%02u %02u:%02u:%02u (TZ=%+d/15min)",
             (unsigned)nbTime->dateTime.year, (unsigned)nbTime->dateTime.month, (unsigned)nbTime->dateTime.day,
             (unsigned)nbTime->dateTime.hour, (unsigned)nbTime->dateTime.minute, (unsigned)nbTime->dateTime.second,
             (int)nbTime->tzQuarterHour);

    nbEpoch = App_DateTimeToEpoch(&nbTime->dateTime);

    if(!IsUpdatedRTC())
    {
        /* 백업 도메인이 초기 상태 → 아직 유효 시각 없음 */
        APP_LOGD("RTC", "INITS=0 (uninitialized) -> set from NBIoT");
        needSet = 1u;
    }
    else if (RTC_GetTime(&rtcTime) != APP_STATUS_OK)
    {
        APP_LOGD("RTC", "RTC read failed -> set from NBIoT");
        needSet = 1u;
    }
    else
    {
        diff = nbEpoch - App_DateTimeToEpoch(&rtcTime);
        if (diff < 0) { diff = -diff; }

        APP_LOGD("RTC", "NBIoT vs RTC diff=%ld sec", (long)diff);

        if (diff > (int64_t)APP_RTC_SYNC_THRESHOLD_SEC)
        {
            APP_LOGD("RTC", "diff exceeds %d sec -> resync", APP_RTC_SYNC_THRESHOLD_SEC);
            needSet = 1u;
        }
        else
        {
            APP_LOGN("RTC", "[[RTC]] within tolerance -> keep RTC");
        }
    }

    if (needSet)
    {
        RTC_SetTime(nbTime->dateTime.year, nbTime->dateTime.month, nbTime->dateTime.day,
                    nbTime->dateTime.hour, nbTime->dateTime.minute, nbTime->dateTime.second);
        /* RTC_SetTime → HAL_RTC_SetTime/SetDate 가 INIT 진입/해제하며 INITS=1로 만듦 */

        APP_LOGN("RTC", "[[RTC]] set to %04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)nbTime->dateTime.year,
                 (unsigned)nbTime->dateTime.month, (unsigned)nbTime->dateTime.day,
                 (unsigned)nbTime->dateTime.hour, (unsigned)nbTime->dateTime.minute, (unsigned)nbTime->dateTime.second);

        App_FsmInvalidateRtcSchedules();
    }

    return APP_STATUS_OK;
}

const AppBc95Time_t *App_Bc95AtGetLastTime(void)
{
    return &g_appBc95AtLastTime;
}

/* ============================================================
 *  Application level wrapper
 * ============================================================ */
AppStatus_t App_NBIoTSyncTime(void)
{
    AppStatus_t status;

    APP_LOGD("NBIOT", "Synchronizing time from network...");

    status = App_Bc95AtSyncTimeToRtc();
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "Time sync failed (status=%d)", (int)status);
        return status;
    }

    APP_LOGD("NBIOT", "Time sync done");
    return APP_STATUS_OK;
}

/* ============================================================
 *  Application level
 * ============================================================ */
AppStatus_t App_NBIoTAtInit(void)
{
    /* [DFOTA 1.6.3] 모듈 리부트/재초기화 = update 완료(REBOOT_CAUSE_SECURITY_
     * FOTA_UPGRADE) 후 재진입. hold를 해제해 정상 제어를 회복하며, 기존
     * BootTrack의 AT+QGMR 로 업데이트된 버전을 확인한다. */
    if (s_bc95DfotaHoldActive != 0u)
    {
        App_Bc95AtDfotaHoldClear("module re-init");
    }

    APP_RETURN_IF_FALSE((APP_UART_NBIOT_HANDLE != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(&g_appBc95AtRxContext, 0, sizeof(g_appBc95AtRxContext));
    (void)memset(g_appBc95AtRxBuf, 0, sizeof(g_appBc95AtRxBuf));
    (void)memset(g_appBc95AtImeiBcd, 0, sizeof(g_appBc95AtImeiBcd));
    (void)memset(g_appBc95AtImsiBcd, 0, sizeof(g_appBc95AtImsiBcd));
    (void)memset(&g_appBc95AtQuality, 0, sizeof(g_appBc95AtQuality));
    (void)memset(g_appBc95AtQualityBcd, 0, sizeof(g_appBc95AtQualityBcd));
    (void)memset(&g_appBc95AtNetStatus, 0, sizeof(g_appBc95AtNetStatus));
    /*
     * ATT-05/06/07: do NOT clear g_appNbiotCarrierContext here.
     *
     * App_NBIoTAtInit() is called after SW/HW reset recovery inside one mandatory
     * attach session. Clearing the carrier context here resets attachAttemptCount,
     * swResetCount, hwResetCount, and lastResetType back to zero, which makes the
     * logs repeat as attempt=1 and SW reset 1/3 forever even though elapsed time is
     * still cumulative.
     *
     * The carrier context is intentionally initialized once at the start of
     * App_NBIoTCarrierAttachMandatory().
     */

    g_appBc95UdpSeqCounter = 0u;
    s_nbiotUdpSocketOpenMask = 0u;  /* [ADD] 모뎀 리셋 후 소켓 전무 -> 추적 리셋 */
    g_appBc95UsimFatal = APP_FALSE;
    g_appBc95AtInitialized = APP_TRUE;
    /* [DL-ACK 테이블 부팅 초기화 2026-11-05] 기존은 CountFrame/RegisterCmd의 lazy-init
     * 의존(첫 프레임 수신 시 ISR 컨텍스트에서 memset/strncpy 실행 가능). 부팅 시점에
     * 명시적으로 초기화해 태스크 컨텍스트에서 테이블을 구성한다. */
    App_DlAckInit();
#ifdef NBIoT_SIMULATION_CODE
    APP_LOGD("NBIOT", "Forcely err not initializedBC95 test..."); // NBIoT simulation test
    g_appBc95AtInitialized = APP_FALSE;
#endif // NBIoT_SIMULATION_CODE

    APP_LOGD("NBIOT", "NBIoT Init");

    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTBringUp(void)
{
    AppStatus_t status;
    uint8_t wakeCpinGuardTimedOut = APP_FALSE;

    status = App_Bc95AtWaitUntilReady(APP_BC95_BOOT_WAIT_BANNER_MS + 6000u);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "Module boot timeout");
        return status;
    }

    /*
     * wake fast-path에서만 CPIN guard를 수행한다.
     * timeout이면 fast-path를 강제로 끄고 보수 경로로만 진행한다.
     */
    if (g_appBc95SkipNextAttachBootWait == APP_TRUE)
    {
        status = App_Bc95AtWakeDelayAndWaitForCpinReady();
        if (status == APP_STATUS_FATAL)
        {
            APP_LOGE("NBIOT", "[[ReadyProbe]] wake CPIN guard fatal");
            return status;
        }
        if (status != APP_STATUS_OK)
        {
            wakeCpinGuardTimedOut = APP_TRUE;
            g_appBc95SkipNextAttachBootWait = APP_FALSE;
            APP_LOGW("NBIOT",
                     "[[ReadyProbe]] wake CPIN guard timeout -> fastPath OFF, conservative service-ready");

            /*
             * 더 엄격한 차단안이 필요하면 아래 한 줄로 교체:
             * return APP_STATUS_UART_TIMEOUT;
             */
        }
    }

    status = App_Bc95AtWaitForServiceReady(APP_BC95_USIM_READY_TIMEOUT_MS +
                                           APP_BC95_SERVICE_READY_SETTLE_MS +
                                           APP_BC95_AT_RX_TIMEOUT_MS +
                                           APP_BC95_SERVICE_READY_POST_CFUN_SETTLE_MS);
    if (status != APP_STATUS_OK)
    {
        if (g_appBc95UsimFatal == APP_TRUE)
        {
            APP_LOGD("NBIOT", "Module service ready fatal");
        }
        else
        {
            APP_LOGD("NBIOT", "Module service ready fail (status=%d)", (int)status);
        }
        return status;
    }

    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CEREG_SET_3,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+CEREG=3",
                                           APP_NBIOT_REPORT_LOG_ATTACH);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " failed to set CEREG=3 (status=%d)", (int)status);
        return status;
    }

    APP_LOGD("NBIOT", "Module ready");
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTBringUpWithReset(uint8_t maxResetRetry)
{
    AppStatus_t st = APP_STATUS_FATAL;
    uint8_t i;

    if (maxResetRetry == 0u) maxResetRetry = 1u;

    if (g_appBc95AtInitialized != APP_TRUE)
    {
        (void)App_NBIoTAtInit();
    }

    for (i = 0u; i < maxResetRetry; i++)
    {
        st = App_NBIoTBringUp();
        if (st == APP_STATUS_OK) return APP_STATUS_OK;

        APP_LOGD("NBIOT", "BringUp failed (%d), HW reset %u/%u",
                 (int)st, (unsigned)(i + 1u), (unsigned)maxResetRetry);

        App_HwNbiotPowerCycle();   /* weak default: no-op */
        App_Bc95AtDelayWithFeed(2000u);
        (void)App_NBIoTAtInit();   /* 리셋 후 컨텍스트 재초기화 */
    }
    return st;
}

AppStatus_t App_NBIoTNetworkBringUp(void)
{
    AppStatus_t status;
    AppBc95NetStatus_t netStatus;

    status = App_Bc95AtWaitForNetwork(APP_BC95_NET_DEFAULT_TIMEOUT_MS, &netStatus);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "Network not ready (phase=%s)",
                 App_Bc95AtGetNetPhaseString(netStatus.phase));
        return status;
    }

    /* 자동 타임존 활성화 (실패해도 무시) */
    (void)App_Bc95AtEnableAutoTimezone();

    /* 네트워크 시간 → RTC 동기화 */
    (void)App_NBIoTSyncTime();

#ifdef NBIOT_SUPPORT_DNS
    (void)App_Bc95AtWarmupDns(WARMUPDNS_SERVER_DOMAIN);
#endif // NBIOT_SUPPORT_DNS

    APP_LOGD("NBIOT", "Network ready");
    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTCarrierAttachMandatory(void)
{
#if (APP_NBIOT_MANDATORY_LAYER_ENABLE != APP_TRUE)
    return App_NBIoTNetworkBringUp();
#else
    AppStatus_t status;
    AppStatus_t recoverStatus;
    AppBc95NetStatus_t netStatus;
    AppBc95NetStatus_t probeNetStatus;
    uint8_t specialWakePolicy;
    uint32_t attachWindowMs;
    int16_t preAttachCsqRssiDbm;
    uint8_t preAttachWeakConfirmed;
    AppBc95Quality_t preAttachQuality;
    uint8_t preAttachNoCellAfterProbe;
    uint8_t preAttachNeedShortProbe;
    uint8_t preAttachEarlyAbort;
    int16_t preAttachSignalDbm;
    AppNbiotPreAttachAbortPending_t preAttachAbort = { 0 };

    if (g_appBc95AtInitialized != APP_TRUE)
    {
        (void)App_NBIoTAtInit();
    }

    (void)memset(&g_appNbiotCarrierContext, 0, sizeof(g_appNbiotCarrierContext));
    g_appBc95RegisteringStuckRecoveryUsed = APP_FALSE;
    g_appNbiotCarrierContext.attachStartTick = HAL_GetTick();
    g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_BOOT_WAIT;
    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_IDLE;
    g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_NONE;

    APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
             " mandatory start (window=%lums total=%lums swMax=%u hwMax=%u)",
             (unsigned long)APP_NBIOT_ATTACH_TIMEOUT_MS,
             (unsigned long)APP_NBIOT_ATTACH_TOTAL_FAIL_LIMIT_MS,
             (unsigned)APP_NBIOT_ATTACH_SW_RESET_MAX,
             (unsigned)APP_NBIOT_ATTACH_HW_RESET_MAX);

    while (1)
    {
        specialWakePolicy = APP_FALSE;
        attachWindowMs = APP_NBIOT_ATTACH_TIMEOUT_MS;

        (void)memset(&netStatus, 0, sizeof(netStatus));
        g_appNbiotCarrierContext.attachAttemptCount++;
        g_appNbiotCarrierContext.lastAttemptTick = HAL_GetTick();
        g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_BOOT_WAIT;

        APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " attempt=%u state=%s",
                 (unsigned)g_appNbiotCarrierContext.attachAttemptCount,
                 App_NbiotCarrierAttachStateString(g_appNbiotCarrierContext.attachState));

        if (g_appBc95ForceSkipAttachBootWaitOnce == APP_TRUE)
        {
            APP_LOGN("NBIOT", "[[Attach]] keep recent-NRB fast-path hint once for boottrack attach-verification");
            g_appBc95ForceSkipAttachBootWaitOnce = APP_FALSE;
        }
        else
        {
            APP_LOGW("NBIOT", "[[Attach]] conservative wake policy: clear recent-NRB fast-path hint before bring-up");
            g_appBc95SkipNextAttachBootWait = APP_FALSE;
        }

        status = App_NBIoTBringUp();
        if (status == APP_STATUS_OK)
        {
            preAttachCsqRssiDbm = 0;
            preAttachWeakConfirmed = APP_FALSE;
            preAttachNoCellAfterProbe = APP_FALSE;
            preAttachNeedShortProbe = APP_FALSE;
            preAttachEarlyAbort = APP_FALSE;
            (void)memset(&preAttachQuality, 0, sizeof(preAttachQuality));
            (void)memset(&probeNetStatus, 0, sizeof(probeNetStatus));

            preAttachSignalDbm = 0;
            status = App_Bc95AtReadPreAttachSignalDbm(&preAttachSignalDbm, &preAttachCsqRssiDbm);
            if ((status == APP_STATUS_OK) &&
                (preAttachSignalDbm <= APP_BC95_PRE_ATTACH_SIGNAL_WEAK_DBM))
            {
                preAttachNeedShortProbe = APP_TRUE;
                APP_LOGD("NBIOT", "[PreAttach] weak signal power detected (%d dBm) -> short camp probe",
                         (int)preAttachSignalDbm);
            }
            else if (status != APP_STATUS_OK)
            {
                preAttachNeedShortProbe = APP_TRUE;
                preAttachCsqRssiDbm = 0;
                APP_LOGD("NBIOT", "[PreAttach] radio power unavailable -> short camp probe first");
                status = APP_STATUS_OK;
            }

            if (preAttachNeedShortProbe == APP_TRUE)
            {
                status = App_Bc95AtRunShortCampProbe(&probeNetStatus);
                g_appNbiotCarrierContext.lastNetStatus = probeNetStatus;

                if (status == APP_STATUS_OK)
                {
                    APP_LOGD("NBIOT", "[PreAttach] short camp probe reached READY -> reuse result");
                    netStatus = probeNetStatus;
                    goto attach_finalize;
                }

                (void)App_Bc95AtConfirmWeakCellAfterProbe(&preAttachWeakConfirmed,
                                                          &preAttachQuality,
                                                          &preAttachNoCellAfterProbe);

                if ((preAttachWeakConfirmed == APP_TRUE) ||
                    (preAttachNoCellAfterProbe == APP_TRUE))
                {
                    preAttachEarlyAbort = APP_TRUE;
                }
                else if ((probeNetStatus.phase != APP_BC95_NET_PHASE_READY) &&
                         (probeNetStatus.phase != APP_BC95_NET_PHASE_ATTACHING) &&
                         (probeNetStatus.phase != APP_BC95_NET_PHASE_WAITING_IP))
                {
                    preAttachEarlyAbort = APP_TRUE;
                }

                if (preAttachEarlyAbort == APP_TRUE)
                {
                    App_NbiotPreAttachAbortDefer(
                        &preAttachAbort,
                        (preAttachQuality.valid == APP_TRUE) ? &preAttachQuality : NULL,
                        preAttachCsqRssiDbm,
                        preAttachNoCellAfterProbe,
                        g_appNbiotCarrierContext.attachAttemptCount);

                    g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_ABORT;
                    g_appNbiotCarrierContext.lastNetStatus = probeNetStatus;
                    status = APP_STATUS_UART_TIMEOUT;

                    APP_LOGD("NBIOT", "[PreAttach] early-abort this attempt -> skip long network wait and go recovery");
                    goto attach_finalize;
                }

                APP_LOGD("NBIOT", "[PreAttach] short probe inconclusive -> continue normal attach");
                status = APP_STATUS_OK;
            }

            /*
             * 2차 보수화:
             * CME=524 wake shortcut(추가 stabilize/backoff, skip-once)를 사용하지 않는다.
             * -> bring-up 성공 후에는 boot와 같은 일반 attach 정책으로 바로 진행.
             */
            if (g_appBc95WakeNeedsCme524AttachBackoff == APP_TRUE)
            {
                APP_LOGW("NBIOT",
                         "[[Attach]] conservative wake policy: ignore CME=524 wake shortcut/backoff");
                g_appBc95WakeNeedsCme524AttachBackoff = APP_FALSE;
                g_appBc95WakeSkipPreAttachNetResetOnce = APP_FALSE;
            }

            if (g_appBc95WakeNeedsPreAttachNetReset == APP_TRUE)
            {
                APP_LOGW("NBIOT",
                         "[[Attach]] conservative wake policy: ignore wake pre-attach net-reset shortcut and keep default attach window");
                g_appBc95WakeNeedsPreAttachNetReset = APP_FALSE;
                g_appBc95WakeSkipPreAttachNetResetOnce = APP_FALSE;
            }
            g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_NETWORK_WAIT;
            status = App_Bc95AtWaitForNetwork(attachWindowMs, &netStatus);
            g_appNbiotCarrierContext.lastNetStatus = netStatus;
        }

attach_finalize:

        g_appNbiotCarrierContext.lastStatus = status;

        if (status == APP_STATUS_OK)
        {
            g_appBc95ServiceReadyRecoveryActive = APP_FALSE;
            g_appBc95WakeSkipPreAttachNetResetOnce = APP_FALSE;
            g_appBc95LastRecoverableCmeWas524 = APP_FALSE;
            g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_READY;

            (void)App_Bc95AtEnableAutoTimezone();
            (void)App_NBIoTSyncTime();
#ifdef NBIOT_SUPPORT_DNS
            (void)App_Bc95AtWarmupDns(WARMUPDNS_SERVER_DOMAIN);
#endif // NBIOT_SUPPORT_DNS
            APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                     " success attempt=%u elapsed=%lums phase=%s ip=%s specialWake=%u",
                     (unsigned)g_appNbiotCarrierContext.attachAttemptCount,
                     (unsigned long)(HAL_GetTick() - g_appNbiotCarrierContext.attachStartTick),
                     App_Bc95AtGetNetPhaseString(netStatus.phase),
                     netStatus.ipAddr,
                     (unsigned)specialWakePolicy);
            App_NbiotPreAttachAbortDiscard(&preAttachAbort,
                                           g_appNbiotCarrierContext.attachAttemptCount);
            return APP_STATUS_OK;
        }

        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                 " fail attempt=%u status=%d phase=%s CEREG=%s CGATT=%u ip=%s specialWake=%u",
                 (unsigned)g_appNbiotCarrierContext.attachAttemptCount,
                 (int)status,
                 App_Bc95AtGetNetPhaseString(netStatus.phase),
                 App_Bc95AtGetCeregStatString(netStatus.ceregStat),
                 (unsigned)netStatus.cgattState,
                 netStatus.ipAddr,
                 (unsigned)specialWakePolicy);

        recoverStatus = App_NbiotCarrierRecoverAfterAttachFailure(status);
        if (recoverStatus != APP_STATUS_OK)
        {
            g_appNbiotCarrierContext.attachState = APP_NBIOT_ATTACH_STATE_ABORT;
            g_appNbiotCarrierContext.lastStatus = recoverStatus;
            APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_ATTACH
                     " abort state=%s reset=%s status=%d",
                     App_NbiotCarrierAttachStateString(g_appNbiotCarrierContext.attachState),
                     App_NbiotCarrierResetTypeString(g_appNbiotCarrierContext.lastResetType),
                     (int)recoverStatus);
            App_NbiotPreAttachAbortFlush(&preAttachAbort);
            return recoverStatus;
        }

        if (g_appNbiotCarrierContext.lastResetType == APP_NBIOT_CARRIER_RESET_NONE)
        {
            g_appBc95ShortRetryBootBannerWaitOnce = APP_TRUE;
        }
    }
#endif
}

AppStatus_t App_NBIoTReadIdentity(uint8_t bSaveInfo)
{
    AppStatus_t status;
    uint8_t appImeiBcd[APP_BC95_IMEI_BCD_BYTES];
    uint8_t appImsiBcd[APP_BC95_IMSI_BCD_BYTES];

    status = App_Bc95AtFetchImeiWithRetry(appImeiBcd, sizeof(appImeiBcd),
                                          APP_BC95_IMEI_FETCH_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "IMEI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMEI", appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);
    App_Bc95PrintDecoded("IMEI", appImeiBcd, (uint32_t)APP_BC95_IMEI_BCD_BYTES);

    status = App_Bc95AtFetchImsiWithRetry(appImsiBcd, sizeof(appImsiBcd),
                                          APP_BC95_IMSI_FETCH_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "IMSI fetch failed (status=%d)", (int)status);
        return status;
    }
    App_Bc95PrintBcd("IMSI", appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);
    App_Bc95PrintDecoded("IMSI", appImsiBcd, (uint32_t)APP_BC95_IMSI_BCD_BYTES);

    App_Bc95LogSimInfo(appImsiBcd);

    /* ----- Options에 반영 (IMEI 8B + IMSI 8B = 16B) ----- */
    if (bSaveInfo == APP_TRUE)
    {
        AppMeterServerFormatOptions_t opt;

        (void)App_MeterServerOptionsLoad(&opt); /* 기존 값 유지하면서 일부만 갱신 */
        App_MeterServerOptionsSetMobileId(&opt, appImeiBcd, appImsiBcd);
        (void)App_MeterServerOptionsUpdate(&opt); /* 변경 시에만 EEPROM write */
    }

    return APP_STATUS_OK;
}

AppStatus_t App_NBIoTReadQuality(uint8_t bSaveInfo)
{
    AppStatus_t status;
    AppBc95Quality_t quality;

    status = App_Bc95AtFetchQualityWithRetry(&quality, g_appBc95AtQualityBcd,
                                             sizeof(g_appBc95AtQualityBcd),
                                             APP_BC95_QUALITY_FETCH_RETRY_MAX);
    if (status != APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "Quality fetch failed (status=%d)", (int)status);
        return status;
    }
    App_NBIoTPrintQuality(&quality);
    App_Bc95PrintBcd("QUALITY", g_appBc95AtQualityBcd, (uint32_t)APP_BC95_QUALITY_BCD_BYTES);

    /* ----- Options에 반영 (NB-IoT 10B) ----- */
    if (bSaveInfo == APP_TRUE)
    {
        AppMeterServerFormatOptions_t opt;

        (void)App_MeterServerOptionsLoad(&opt);
        App_MeterServerOptionsSetWirelessQuality(&opt,
                                                 g_appBc95AtQualityBcd,
                                                 (uint8_t)APP_BC95_QUALITY_BCD_BYTES);
        (void)App_MeterServerOptionsUpdate(&opt);
    }

    return APP_STATUS_OK;
}

/* ============================================================
 * [QUALITY-REPORT 2026-11-09] NB-IoT 단말 품질리포트 (규격 v1.8 / MSG Ver.04)
 *  - 목적지 : APP_QUALITY_REPORT_HOST:PORT (10.120.182.10:50000, 고정)
 *  - 시점   : mgmt 전송 성공 세션에서 판정(별도 wake 없음)
 *             만료일 도래 && 새벽 윈도우(02~06시) -> 전송
 *             윈도우 놓침(mgmt 주기 최대 72h 등) -> 만료+1일 경과 시 즉시 전송(catch-up)
 *  - 주기   : APP_QUALITY_REPORT_INTERVAL_DAYS (고정값, 1=하루/2=이틀)
 *  - 측정   : AT+NUESTATS(RADIO) 1회 - Signal power/ SNR/ TX power/
 *             Cell ID/ CURRENT BAND (raw 단위 0.1dB, BC95-GV 매뉴얼 106~107p)
 *  - 패킷   : Ver(1) CTN(6) 전원(3) CGI(4) RSRP(2) SINR(2) 모델명(1+n)
 *             FW(1+n) TXPWR(2) 위치(1) 이웃셀(1) UEINFO(1) PORT(3) RSV(3)
 * ============================================================ */
#define APP_QUALITY_REPORT_CMD_CGMR             "AT+CGMR\r\n"
#define APP_QUALITY_REPORT_NUESTATS_SIGNAL_TAG  "Signal power:"
#define APP_QUALITY_REPORT_NUESTATS_SNR_TAG     "SNR:"
#define APP_QUALITY_REPORT_NUESTATS_TXPWR_TAG   "TX power:"
#define APP_QUALITY_REPORT_NUESTATS_CELLID_TAG  "Cell ID:"
#define APP_QUALITY_REPORT_NUESTATS_BAND_TAG    "CURRENT BAND:"
#define APP_QUALITY_REPORT_PACKET_MAX           (64u)

typedef struct
{
    int32_t  rsrpCdB;      /* Signal power (0.1dB 단위 raw) */
    int32_t  snrCdB;       /* SNR (0.1dB 단위 raw) */
    int32_t  txPowerCdB;   /* TX power (0.1dB 단위 raw) */
    uint32_t cellId;       /* CGI(4B, big-endian으로 전송) */
    uint8_t  band;         /* CURRENT BAND */
    uint8_t  txValid;
    uint8_t  valid;
} AppNbiotQualityRadioStats_t;

/* 태그 뒤의 부호 있는 10진 정수 추출. 못 찾으면 *p_found=APP_FALSE */
static int32_t App_NbiotQualityTagValue(const char *p_resp, const char *p_tag,
                                        uint8_t *p_found)
{
    const char *p_pos;
    long value = 0L;

    *p_found = APP_FALSE;
    if ((p_resp == NULL) || (p_tag == NULL))
    {
        return 0L;
    }
    p_pos = strstr(p_resp, p_tag);
    if (p_pos == NULL)
    {
        return 0L;
    }
    p_pos += strlen(p_tag);
    while ((*p_pos == ' ') || (*p_pos == '\t')) { p_pos++; }
    if (sscanf(p_pos, "%ld", &value) == 1)
    {
        *p_found = APP_TRUE;
    }
    return (int32_t)value;
}

/* AT+NUESTATS(RADIO) 1회 조회 -> 리포트 필요 항목 파싱 */
static AppStatus_t App_NbiotQualityFetchRadioStats(AppNbiotQualityRadioStats_t *p_stats)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    uint8_t rsrpFound;
    uint8_t snrFound;
    uint8_t txFound;
    uint8_t cellFound;
    uint8_t bandFound;
    int32_t cellRaw;
    int32_t bandRaw;

    APP_RETURN_IF_FALSE(p_stats != NULL, APP_STATUS_INVALID_PARAM);
    (void)memset(p_stats, 0, sizeof(*p_stats));

    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NUESTATS_RADIO,
                                   g_appBc95AtRxBuf,
                                   (uint16_t)sizeof(g_appBc95AtRxBuf),
                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                   &rxLen);
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_UART_TIMEOUT))
    {
        APP_LOGW("NBIOT", "[[QREP]] NUESTATS send fail (status=%d)", (int)status);
        return status;
    }

    p_stats->rsrpCdB = App_NbiotQualityTagValue((const char *)g_appBc95AtRxBuf,
                                                APP_QUALITY_REPORT_NUESTATS_SIGNAL_TAG,
                                                &rsrpFound);
    p_stats->snrCdB  = App_NbiotQualityTagValue((const char *)g_appBc95AtRxBuf,
                                                APP_QUALITY_REPORT_NUESTATS_SNR_TAG,
                                                &snrFound);
    p_stats->txPowerCdB = App_NbiotQualityTagValue((const char *)g_appBc95AtRxBuf,
                                                   APP_QUALITY_REPORT_NUESTATS_TXPWR_TAG,
                                                   &txFound);
    cellRaw = App_NbiotQualityTagValue((const char *)g_appBc95AtRxBuf,
                                       APP_QUALITY_REPORT_NUESTATS_CELLID_TAG,
                                       &cellFound);
    bandRaw = App_NbiotQualityTagValue((const char *)g_appBc95AtRxBuf,
                                       APP_QUALITY_REPORT_NUESTATS_BAND_TAG,
                                       &bandFound);
    p_stats->cellId = (cellFound != APP_FALSE) ? (uint32_t)cellRaw : 0u;
    p_stats->band   = (bandFound != APP_FALSE) ? (uint8_t)bandRaw : 0u;
    p_stats->txValid = ((txFound != APP_FALSE) &&
                        (p_stats->txPowerCdB != APP_BC95_AT_NUESTATS_INVALID_RAW) &&
                        (labs(p_stats->txPowerCdB) <= (int32_t)APP_BC95_AT_NUESTATS_RAW_ABS_MAX))
                       ? APP_TRUE : APP_FALSE;

    if ((rsrpFound == APP_FALSE) || (snrFound == APP_FALSE))
    {
        APP_LOGW("NBIOT", "[[QREP]] NUESTATS tags missing (rsrp=%u snr=%u)",
                 (unsigned)rsrpFound, (unsigned)snrFound);
        return APP_STATUS_FATAL;   /* 이번 세션 스킵, 다음 세션 재시도 */
    }
    p_stats->valid = APP_TRUE;
    return APP_STATUS_OK;
}

static uint8_t App_NbiotQualityBcdByte(uint16_t value)
{
    uint16_t v = value;
    if (v > 99u) { v = 99u; }
    return (uint8_t)(((v / 10u) * 16u) + (v % 10u));
}

/* 규격 필드 순서대로 패킷 조립 (최대 ~48B) */
static AppStatus_t App_NbiotQualityBuildPacket(const AppNbiotQualityRadioStats_t *p_stats,
                                               uint8_t bootReport,
                                               uint8_t *p_pkt, uint16_t cap,
                                               uint16_t *p_len)
{
    uint16_t cursor = 0u;
    char imsiStr[APP_BC95_SIM_INFO_IMSI_DIGITS + 1u];
    char mdnStr[APP_BC95_SIM_INFO_MDN_BUF_SIZE];
    char fwStr[24];
    char modFw[16];
    uint16_t rsrpAbs;
    int32_t  snrVal;
    uint16_t snrAbs;
    size_t fwLen;

    APP_RETURN_IF_FALSE((p_pkt != NULL) && (p_len != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(cap >= APP_QUALITY_REPORT_PACKET_MAX, APP_STATUS_BUFFER_OVERFLOW);

    (void)memset(p_pkt, 0, cap);

    /* 1. MSG Ver */
    p_pkt[cursor++] = APP_QUALITY_REPORT_MSG_VER;

    /* 2. CTN 6B BCD: MDN(est)='0'+IMSI[5..14] 11자리 + pad nibble
     *    012-2299-1234 -> 00 12 22 99 12 34 */
    if ((App_Bc95SimImsiBcdToDigits(g_appBc95AtImsiBcd, imsiStr, sizeof(imsiStr)) == APP_TRUE) &&
        (App_Bc95SimEstimateMdn(imsiStr, mdnStr, sizeof(mdnStr)) == APP_TRUE))
    {
        uint8_t i;
        for (i = 0u; i < 6u; i++)
        {
            uint8_t hi = (i == 0u) ? 0u : (uint8_t)(mdnStr[(i * 2u) - 1u] - '0');
            uint8_t lo = (uint8_t)(mdnStr[(i * 2u)] - '0');
            p_pkt[cursor++] = (uint8_t)((hi * 16u) + lo);
        }
    }
    else
    {
        cursor += 6u;   /* IMSI 미취득: 0x00 채움 */
    }

    /* 3. 전원 3B (UNIT=01 전압, LEVEL=정수부+소수부 BCD, 3.62V -> 03 62) */
    {
        uint32_t adcVref;
        uint32_t adcVbat;
        uint32_t vddaMv;
        uint32_t vbatMv;
        if (Battery_ReadVoltage_Averaged_mV(&adcVref, &adcVbat, &vddaMv, &vbatMv) == HAL_OK)
        {
            p_pkt[cursor++] = 0x01u;
            p_pkt[cursor++] = App_NbiotQualityBcdByte((uint16_t)(vbatMv / 1000u));
            p_pkt[cursor++] = App_NbiotQualityBcdByte((uint16_t)((vbatMv % 1000u) / 10u));
        }
        else
        {
            p_pkt[cursor++] = 0x99u;   /* 디바이스 미지원 */
            p_pkt[cursor++] = 0x99u;
            p_pkt[cursor++] = 0x99u;
        }
    }

    /* 4. CGI 4B HEX (Serving Cell ID, big-endian) */
    p_pkt[cursor++] = (uint8_t)((p_stats->cellId >> 24) & 0xFFu);
    p_pkt[cursor++] = (uint8_t)((p_stats->cellId >> 16) & 0xFFu);
    p_pkt[cursor++] = (uint8_t)((p_stats->cellId >> 8) & 0xFFu);
    p_pkt[cursor++] = (uint8_t)(p_stats->cellId & 0xFFu);

    /* 5. RF: RSRP 2B(백단위+2자리 BCD, 부호 미표기) + SINR 2B(부호+2자리 BCD)
     *    -875(=−87.5dBm) -> 00 87 / 1120 -> 01 12, SNR 250 -> 00 25, -50 -> 01 05 */
    rsrpAbs = (uint16_t)((p_stats->rsrpCdB < 0) ? -p_stats->rsrpCdB : p_stats->rsrpCdB);
    rsrpAbs = (uint16_t)(rsrpAbs / 10u);
    p_pkt[cursor++] = (uint8_t)((rsrpAbs / 100u) & 0xFFu);
    p_pkt[cursor++] = App_NbiotQualityBcdByte((uint16_t)(rsrpAbs % 100u));
    snrVal = p_stats->snrCdB;
    snrAbs = (uint16_t)((snrVal < 0) ? -snrVal : snrVal);
    snrAbs = (uint16_t)(snrAbs / 10u);
    if (snrAbs > 99u) { snrAbs = 99u; }
    p_pkt[cursor++] = (uint8_t)((snrVal < 0) ? 0x01u : 0x00u);
    p_pkt[cursor++] = App_NbiotQualityBcdByte(snrAbs);

    /* 6. 모델명 1B(len)+ASCII (규격: 서비스디바이스 모델명, 최대 20B) */
    {
        const char *p_model = APP_QUALITY_REPORT_MODEL_STRING;
        size_t modelLen = strlen(p_model);
        if (modelLen > 19u) { modelLen = 19u; }
        p_pkt[cursor++] = (uint8_t)modelLen;
        (void)memcpy(&p_pkt[cursor], p_model, modelLen);
        cursor += (uint16_t)modelLen;
    }

    /* 7. FW 버전 1B(len)+ASCII: 단말FW "/" 모듈FW (모듈 조회 실패 시 단말FW만) */
    (void)snprintf(fwStr, sizeof(fwStr), "V%u.%02u",
                   (unsigned)APP_FW_VERSION_MAJOR, (unsigned)APP_FW_VERSION_MINOR);
    modFw[0] = '\0';
    {
        AppStatus_t gmrStatus;
        uint16_t gmrLen = 0u;
        const char *p_pos;
        const char *p_run;
        size_t k = 0u;
        gmrStatus = App_Bc95AtSendCommand(APP_QUALITY_REPORT_CMD_CGMR,
                                          g_appBc95AtRxBuf,
                                          (uint16_t)sizeof(g_appBc95AtRxBuf),
                                          APP_BC95_AT_RX_TIMEOUT_MS,
                                          &gmrLen);
        if ((gmrStatus == APP_STATUS_OK) || (gmrStatus == APP_STATUS_UART_TIMEOUT))
        {
            p_pos = strstr((const char *)g_appBc95AtRxBuf, "GMR");
            p_run = (p_pos != NULL) ? (p_pos + 3) : (const char *)g_appBc95AtRxBuf;
            while ((*p_run != '\0') && ((*p_run < 0x20) || (*p_run > 0x7E))) { p_run++; }
            while ((p_run[k] >= 0x20) && (p_run[k] <= 0x7E) && (k < (sizeof(modFw) - 1u)))
            {
                modFw[k] = p_run[k];
                k++;
            }
            modFw[k] = '\0';
            if ((k < 3u) || (strncmp(modFw, "OK", 2u) == 0)) { modFw[0] = '\0'; }
        }
        if (modFw[0] != '\0')
        {
            (void)strncat(fwStr, "/", sizeof(fwStr) - strlen(fwStr) - 1u);
            (void)strncat(fwStr, modFw, sizeof(fwStr) - strlen(fwStr) - 1u);
        }
    }
    fwLen = strlen(fwStr);
    if (fwLen > 20u) { fwLen = 20u; }   /* 규격: size 최대 20B(초과 시 축약) */
    p_pkt[cursor++] = (uint8_t)fwLen;
    (void)memcpy(&p_pkt[cursor], fwStr, fwLen);
    cursor += (uint16_t)fwLen;

    /* 8. TX POWER 2B BCD (부호 00=양수/01=음수, 모듈 미지원 시 02 00) */
    if (p_stats->txValid != APP_FALSE)
    {
        uint16_t txAbs = (uint16_t)((p_stats->txPowerCdB < 0) ? -p_stats->txPowerCdB
                                                              : p_stats->txPowerCdB);
        txAbs = (uint16_t)(txAbs / 10u);
        p_pkt[cursor++] = (uint8_t)((p_stats->txPowerCdB < 0) ? 0x01u : 0x00u);
        p_pkt[cursor++] = App_NbiotQualityBcdByte(txAbs);
    }
    else
    {
        p_pkt[cursor++] = 0x02u;
        p_pkt[cursor++] = 0x00u;
    }

    /* 9. 위치정보 1B(미사용 0x00), 10. Neighbor Cell ID 1B(미사용 0x00) */
    p_pkt[cursor++] = 0x00u;
    p_pkt[cursor++] = 0x00u;

    /* 11. UE INFO 1B 비트패턴
     *     bit7=B1 bit6=B5(LTE850) bit5=B7 bit4=n78 bit3=n257 bit2=예비
     *     bit1=운영타입 고정형(=1) bit0=Report구분(부팅후=1/주기=0). 샘플 0x43=LTE850+고정+부팅후 */
    {
        uint8_t ueInfo = 0x02u;
        if (p_stats->band == 5u) { ueInfo |= 0x40u; }
        if (bootReport != APP_FALSE) { ueInfo |= 0x01u; }
        p_pkt[cursor++] = ueInfo;
    }

    /* 12. PORT INFO 3B(미사용 0x00), 13. Reserved F1F1F1 */
    cursor += 3u;
    p_pkt[cursor++] = 0xF1u;
    p_pkt[cursor++] = 0xF1u;
    p_pkt[cursor++] = 0xF1u;

    *p_len = cursor;
    return APP_STATUS_OK;
}

/* 세션 종료 전 호출(호출부는 service/mgmt/boot 다수): 만료/새벽 윈도우 판정 후 리포트 1발 전송.
 * [QREP-INDEPENDENT] mgmt 전송 결과와 무관하게 호출되며, 중복 호출 시 due 판정이
 * 걸러주므로 안전하다. */
static AppStatus_t App_NbiotQualityReportMaybeSend(void)
{
#if (APP_TX_QREP_ENABLE != 1u)
    /* [TX-PATH-GATE] 품질리포트 비활성: 스케줄 갱신 없이 조용히 스킵 */
    return APP_STATUS_OK;
#endif
    AppDateTime_t now;
    AppNbiotQualityRadioStats_t stats;
    AppBc95UdpResult_t sendResult;
    AppStatus_t status;
    uint32_t nowDays;
    uint32_t elapsedDays;
    uint8_t pkt[APP_QUALITY_REPORT_PACKET_MAX];
    uint16_t pktLen = 0u;
    uint8_t bootReport;
    uint8_t due;
    uint8_t hourInWindow;

    (void)memset(&sendResult, 0, sizeof(sendResult));

    if (RTC_GetTime(&now) != APP_STATUS_OK)
    {
        return APP_STATUS_NOT_INITIALIZED;   /* RTC 미확정 -> 판정 보류 */
    }

    nowDays = ((uint32_t)now.year * 366u) + ((uint32_t)now.month * 31u) +
              (uint32_t)now.day;
    bootReport = (g_appQualityReportLastValid == APP_FALSE) ? APP_TRUE : APP_FALSE;

    due = bootReport;
    if (due == APP_FALSE)
    {
        elapsedDays = nowDays - g_appQualityReportLastDays;
        if (elapsedDays < (uint32_t)APP_QUALITY_REPORT_INTERVAL_DAYS)
        {
            return APP_STATUS_OK;   /* 미만료 */
        }
        hourInWindow = ((now.hour >= APP_QUALITY_REPORT_WINDOW_START_HOUR) &&
                        (now.hour < APP_QUALITY_REPORT_WINDOW_END_HOUR)) ? APP_TRUE : APP_FALSE;
        /* 새벽 윈도우 우선. 윈도우를 놓친 경우(mgmt 주기 72h 등) 만료+1일 경과 시 즉시 전송 */
        due = ((hourInWindow != APP_FALSE) ||
               (elapsedDays >= ((uint32_t)APP_QUALITY_REPORT_INTERVAL_DAYS +
                                (uint32_t)APP_QUALITY_REPORT_CATCHUP_EXTRA_DAYS)))
              ? APP_TRUE : APP_FALSE;
    }
    if (due == APP_FALSE)
    {
        return APP_STATUS_OK;
    }

    status = App_NbiotQualityFetchRadioStats(&stats);
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", "[[QREP]] radio stats fail (status=%d) - retry next session",
                 (int)status);
        return status;
    }

    status = App_NbiotQualityBuildPacket(&stats, bootReport, pkt, sizeof(pkt), &pktLen);
    if (status != APP_STATUS_OK)
    {
        APP_LOGW("NBIOT", "[[QREP]] packet build fail (status=%d)", (int)status);
        return status;
    }

    g_appBc95UdpSkipAckWait = APP_TRUE;
    status = App_Bc95AtUdpSendOnce("QREP",
                                   APP_QUALITY_REPORT_HOST,
                                   APP_QUALITY_REPORT_PORT,
                                   pkt,
                                   pktLen,
                                   &sendResult,
                                   APP_FALSE);
    g_appBc95UdpSkipAckWait = APP_FALSE;

    if (status == APP_STATUS_OK)
    {
        g_appQualityReportLastValid = APP_TRUE;
        g_appQualityReportLastDays  = nowDays;
        APP_LOGN("NBIOT", "[[QREP]] sent (len=%u, boot=%u, %u.%02u %02u:%02u)",
                 (unsigned)pktLen, (unsigned)bootReport,
                 (unsigned)now.year, (unsigned)now.month,
                 (unsigned)now.hour, (unsigned)now.minute);
    }
    else
    {
        APP_LOGW("NBIOT", "[[QREP]] send fail (status=%d, stage=%d)",
                 (int)status, (int)sendResult.lastStage);
    }
    return status;
}

static AppStatus_t App_NBIoTTransmitUdpInternal(const char *p_logTag,
                                                const char *p_host,
                                                uint16_t port,
                                                uint8_t deleteStorage,
                                                uint8_t unsentOnly,
                                                const AppMeterStorageRecord_t *p_liveRecord,
                                                uint8_t appendSelfTestToMgmt,
                                                AppBc95UdpResult_t *p_sendResultOut,
                                                uint8_t isMgmtPath)
{
    AppStatus_t status;
    AppBc95UdpResult_t sendResult;
    AppMeterServerFormatOptions_t opt;
    AppMeterServerFormatResult_t buildResult;
    uint8_t packet[APP_METER_SERVER_FORMAT_MAX_PACKET_SIZE];
    uint8_t effectiveDeleteStorage;

    APP_RETURN_IF_FALSE((p_logTag != NULL) && (p_host != NULL), APP_STATUS_INVALID_PARAM);

    status = App_NbiotBuildMeterPacket(p_logTag,
                                       deleteStorage,
                                       unsentOnly,
                                       p_liveRecord,
                                       appendSelfTestToMgmt,
                                       &opt,
                                       &buildResult,
                                       packet,
                                       sizeof(packet),
                                       &effectiveDeleteStorage);
    if (status == APP_STATUS_OK)
    {
        status = App_Bc95AtUdpSendOnce(p_logTag,
                                       p_host,
                                       port,
                                       packet,
                                       buildResult.packetLength,
                                       &sendResult,
                                      (isMgmtPath != APP_FALSE) ? APP_TRUE : APP_FALSE);
        if (p_sendResultOut != NULL)
        {
            *p_sendResultOut = sendResult;
        }

        if ((status == APP_STATUS_OK) && (isMgmtPath != APP_FALSE))
        {
            /* [QUALITY-REPORT] mgmt 전송 성공 세션에서 판정(별도 wake 없음) */
            (void)App_NbiotQualityReportMaybeSend();
        }

        if ((status == APP_STATUS_OK) && (buildResult.recordCount != 0u))
        {
            if (p_liveRecord != NULL)
            {
                APP_LOGD("NBIOT", "%s live meter record sent without storage (records=%u)",
                         p_logTag,
                         (unsigned int)buildResult.recordCount);
            }
            else if (unsentOnly == APP_TRUE)
            {
                AppStatus_t markStatus = App_MeterStorageMarkOldestUnsentSent(buildResult.recordCount);
                if (markStatus != APP_STATUS_OK)
                {
                    APP_LOGD("NBIOT", "%s storage sent-mark failed (status=%ld, records=%u)",
                             p_logTag,
                             (long)markStatus,
                             (unsigned int)buildResult.recordCount);
                    return markStatus;
                }

                if (effectiveDeleteStorage == APP_TRUE)
                {
                    uint8_t deletedCount = 0u;
                    AppStatus_t clearStatus = App_MeterStorageDeleteOldestSent(&deletedCount);
                    if (clearStatus != APP_STATUS_OK)
                    {
                        APP_LOGE("NBIOT", "%s storage delete failed (status=%ld, records=%u)",
                                 p_logTag,
                                 (long)clearStatus,
                                 (unsigned int)buildResult.recordCount);
                        return clearStatus;
                    }
                    buildResult.cleared = (deletedCount != 0u) ? APP_TRUE : APP_FALSE;
                    if (deletedCount != 0u)
                    {
                        APP_LOGD("NBIOT", "%s storage deleted (records=%u, remain=%u)",
                                 p_logTag,
                                 (unsigned int)deletedCount,
                                 (unsigned int)App_MeterStorageCount());
                    }
                }
                else
                {
                    APP_LOGD("NBIOT", "%s storage marked sent (records=%u, remain=%u)",
                             p_logTag,
                             (unsigned int)buildResult.recordCount,
                             (unsigned int)App_MeterStorageCount());
                }
            }
            else if (effectiveDeleteStorage == APP_TRUE)
            {
                AppStatus_t clearStatus = App_MeterStorageDeleteOldest(buildResult.recordCount);
                if (clearStatus != APP_STATUS_OK)
                {
                    APP_LOGD("NBIOT", "%s storage delete failed (status=%ld, records=%u)",
                             p_logTag,
                             (long)clearStatus,
                             (unsigned int)buildResult.recordCount);
                    return clearStatus;
                }
                buildResult.cleared = APP_TRUE;
                APP_LOGD("NBIOT", "%s storage deleted (records=%u, remain=%u)",
                         p_logTag,
                         (unsigned int)buildResult.recordCount,
                         (unsigned int)App_MeterStorageCount());
            }
        }
    }
    else
    {
        /* [SKIP-REASON 2026-11-09] build 실패(레코드 0건/옵션 미초기화) 시의 스킵.
         * 이 스킵은 전송 대상이 없는 정상 상태이므로 WARN 대신 status 표기로 통일. */
        APP_LOGD("NBIOT", "%s skip send (status=%d)", p_logTag, (int)status);
    }

    APP_LOGD("NBIOT", "%s build len=%u payload=%u rec=%u checksum=0x%02X cleared=%u delete=%u live=%u",
             p_logTag,
             (unsigned int)buildResult.packetLength,
             (unsigned int)buildResult.payloadLength,
             (unsigned int)buildResult.recordCount,
             (unsigned int)buildResult.checksum,
             (unsigned int)buildResult.cleared,
             (unsigned int)deleteStorage,
             (unsigned int)((p_liveRecord != NULL) ? APP_TRUE : APP_FALSE));

    if (status == APP_STATUS_OK)
    {
        APP_LOGD("NBIOT", "%s SendResult: stage=%d, ip=%s, port=%u, sent=%u, seq=%u, confirmed=%u",
                 p_logTag,
                 (int)sendResult.lastStage,
                 sendResult.resolvedIp,
                 port,
                 (unsigned)sendResult.sentBytes,
                 (unsigned)sendResult.seqNumber,
                 (unsigned)sendResult.sendConfirmed);
#if (APP_TEST_SERVER_TX_BUZZER_ENABLE == APP_TRUE)
                App_NbiotTestNotifyServerTxBuzzer();
#endif
    }

    return status;
}

AppStatus_t App_NBIoTTransmitServiceUdp(uint8_t deleteStorage)
{
#if (APP_TX_MGMT_ENABLE != 1u)
    (void)deleteStorage;
    APP_LOGN("NBIOT", "[[MgmtTx]] disabled by APP_TX_MGMT_ENABLE=0");
    return APP_STATUS_OK;
#endif

#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_DOMAIN, APP_SERVICE_SERVER_PORT, deleteStorage, APP_TRUE, NULL, APP_FALSE, NULL, APP_FALSE);
#else
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_IP, APP_SERVICE_SERVER_PORT, deleteStorage, APP_TRUE, NULL, APP_FALSE, NULL, APP_FALSE);
#endif
}

AppStatus_t App_NBIoTTransmitMgmtUdp(uint8_t deleteStorage)
{
#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_DOMAIN, APP_MGMT_SERVER_PORT, deleteStorage, APP_FALSE, NULL, APP_TRUE, NULL, APP_TRUE);
#else
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_IP, APP_MGMT_SERVER_PORT, deleteStorage, APP_FALSE, NULL, APP_TRUE, NULL, APP_TRUE);
#endif
}

AppStatus_t App_NBIoTTransmitServiceLiveRecord(const AppMeterStorageRecord_t *p_record)
{
#if (APP_TX_MGMT_ENABLE != 1u)
    (void)p_record;
    APP_LOGN("NBIOT", "[[MgmtTx]] live disabled by APP_TX_MGMT_ENABLE=0");
#if (APP_TX_QREP_ENABLE == 1u)
    /* [QREP-INDEPENDENT] mgmt가 꺼진 부팅 세션에서도 리포트는 독자 판정 */
    (void)App_NbiotQualityReportMaybeSend();
#endif
    return APP_STATUS_OK;
#endif

#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_DOMAIN, APP_SERVICE_SERVER_PORT, APP_FALSE, APP_FALSE, p_record, APP_FALSE, NULL, APP_FALSE);
#else
    return App_NBIoTTransmitUdpInternal("[[ServiceTx]]", MY_SERVER_IP, APP_SERVICE_SERVER_PORT, APP_FALSE, APP_FALSE, p_record, APP_FALSE, NULL, APP_FALSE);
#endif

    /* [QREP-INDEPENDENT] 부팅 mgmt 전송 성공/실패 무관 세션 종료 전 독자 판정 */
#if (APP_TX_QREP_ENABLE == 1u)
    (void)App_NbiotQualityReportMaybeSend();
#endif
}

AppStatus_t App_NBIoTTransmitMgmtLiveRecord(const AppMeterStorageRecord_t *p_record)
{
#ifdef NBIOT_SUPPORT_DNS
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_DOMAIN, APP_MGMT_SERVER_PORT, APP_FALSE, APP_FALSE, p_record, APP_TRUE, NULL, APP_TRUE);
#else
    return App_NBIoTTransmitUdpInternal("[[MgmtTx]]", APP_MGMT_SERVER_IP, APP_MGMT_SERVER_PORT, APP_FALSE, APP_FALSE, p_record, APP_TRUE, NULL, APP_TRUE);
#endif
}

AppStatus_t App_NBIoTTransmitUdp(void)
{
    return App_NBIoTTransmitServiceUdp(APP_TRUE);
}

/* [NCONFIG-QUERY-SKIP] BKP4R 상태 캐시 (RTC BKP는 POR에서 클리어,
 * 그 외 리셋에서 유지. HAL_PWR_EnableBkUpAccess는 main.c에서 이미 수행). */
static void App_NbiotNcfgBkpSetState(uint8_t isFalse)
{
    RTC->BKP4R = (isFalse != 0u) ? APP_NBIOT_NCFG_BKP_MAGIC_STATE_FALSE
                                 : APP_NBIOT_NCFG_BKP_MAGIC_STATE_TRUE;
}

static uint8_t App_NbiotProvSkipGet(uint8_t *p_due)
{
    if (RTC->BKP3R != APP_NBIOT_PROV_BKP_MAGIC)
    {
        return 0u;   /* 소실/POR -> 풀 provisioning 필요 */
    }
    if (p_due != NULL)
    {
        *p_due = (s_provSkipCount >= APP_PROV_SKIP_RECHECK_SESSIONS) ? 1u : 0u;
    }
    return 1u;
}

static void App_NbiotProvSkipSet(uint8_t done)
{
    if (done != 0u)
    {
        RTC->BKP3R = APP_NBIOT_PROV_BKP_MAGIC;
        s_provSkipCount = 0u;
    }
    else
    {
        RTC->BKP3R = 0u;
        s_provSkipCount = 0u;
    }
}

static uint8_t App_NbiotNcfgBkpGetState(uint8_t *p_isFalse)
{
    uint32_t v = RTC->BKP4R;
    if (v == APP_NBIOT_NCFG_BKP_MAGIC_STATE_FALSE)
    {
        if (p_isFalse != NULL) { *p_isFalse = 1u; }
        return 1u;
    }
    if (v == APP_NBIOT_NCFG_BKP_MAGIC_STATE_TRUE)
    {
        if (p_isFalse != NULL) { *p_isFalse = 0u; }
        return 1u;
    }
    return 0u;  /* POR/소실: 폴백 필요 */
}

/*
 * [NRB 최적화 2단계] 세션 종료 스티어링:
 * provisioning이 남긴 NVM AUTOCONNECT=TRUE를 전원 off 전에 FALSE로 되돌려
 * 다음 세션 부팅 설정의 NRB를 생략 가능하게 한다(best-effort).
 *  - 스티어링 실패/미실행 시 다음 세션 부팅 sync가 기존 경로(FALSE set + NRB)로 회복
 *  - CFUN=0 이후 AT는 응답하므로 쿼리/쓰기 가능(RF 명령만 불가)
 */
static void App_NbiotSteerAutoconnectFalseAtPowerOff(void)
{
    AppStatus_t status;
    uint16_t rxLen = 0u;
    uint16_t safeLen;

    if (g_appBc95AtInitialized != APP_TRUE)
    {
        APP_LOGW("NBIOT", "[[PowerOff]] no AT ctx -> AUTOCONNECT steering skipped");
        return;
    }

    APP_LOGN("NBIOT", "[[PowerOff]] TX AT+NCONFIG? (AUTOCONNECT steering)");
    status = App_Bc95AtSendCommand(APP_BC95_AT_CMD_NCONFIG_QUERY,
                                   g_appBc95NcfgQueryBuf,
                                   (uint16_t)sizeof(g_appBc95NcfgQueryBuf),
                                   APP_NBIOT_NCONFIG_QUERY_RX_TIMEOUT_MS,
                                   &rxLen);
    if ((status != APP_STATUS_OK) &&
        !((status == APP_STATUS_UART_TIMEOUT) && (rxLen > 0u)))
    {
        APP_LOGW("NBIOT", "[[PowerOff]] NCONFIG? query failed (status=%d rxLen=%u) -> skip steering",
                 (int)status, (unsigned)rxLen);
        return;
    }

    safeLen = (rxLen < (uint16_t)(sizeof(g_appBc95NcfgQueryBuf) - 1u))
                  ? rxLen : (uint16_t)(sizeof(g_appBc95NcfgQueryBuf) - 1u);
    g_appBc95NcfgQueryBuf[safeLen] = (uint8_t)'\0';

    if (App_Bc95AtNconfigTokenHasValue((const char *)g_appBc95NcfgQueryBuf,
                                       "AUTOCONNECT", "TRUE") != APP_TRUE)
    {
        APP_LOGN("NBIOT", "[[PowerOff]] AUTOCONNECT not TRUE -> steering skipped");
        return;
    }

    /* 응답 꼬리가 모두 흐른 뒤 set(부팅 sync와 동일한 TX-during-TX 드랍 방지) */
    App_Bc95AtDelayWithFeed(APP_NBIOT_NCONFIG_QUERY_SETTLE_MS);
    status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_NCONFIG_AUTOCONNECT_OFF,
                                           APP_BC95_AT_RX_TIMEOUT_MS,
                                           "AT+NCONFIG=AUTOCONNECT,FALSE",
                                           "[[PowerOff]]");
    if (status == APP_STATUS_OK)
    {
        /* NVM 커밋 여유 후 전원 차단 */
        App_Bc95AtDelayWithFeed(APP_NBIOT_NVM_WRITE_GAP_MS);
        /* [NCONFIG-QUERY-SKIP] 다음 세션 부팅이 쿼리 없이 판정할 수 있게 기록 */
        App_NbiotNcfgBkpSetState(1u);
        APP_LOGN("NBIOT", "[[PowerOff]] AUTOCONNECT steering done -> next session boot NRB skippable");
    }
    else
    {
        APP_LOGW("NBIOT", "[[PowerOff]] AUTOCONNECT steering failed (status=%d) -> next boot NRB runs", (int)status);
    }
}

AppStatus_t App_NBIoTCarrierPowerOffMandatory(void)
{
#if (APP_NBIOT_MANDATORY_LAYER_ENABLE != APP_TRUE)
    return App_GpioLpSetNbiotPowered(APP_FALSE);
#else
    AppStatus_t status = APP_STATUS_OK;
    AppStatus_t detachWaitStatus = APP_STATUS_OK;
    AppStatus_t cfunMinStatus = APP_STATUS_OK;

    s_nbiotDetachNoRespStreak = 0u;   /* 이번 PowerOff 시도 시작 시 스트릭 초기화 */

    g_appNbiotCarrierContext.lastPowerOffCfun0Status = APP_STATUS_OK;
    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_DETACH_REQ;
    g_appNbiotCarrierContext.lastDetachTick = HAL_GetTick();

    APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF " start(sock+CGATT0+CEREG0+CFUN0)");

    App_Bc95AtCloseAllSockets();   /* 소켓 정리 루프 내부에서 스트릭을 이미 갱신함 */

    /* 소켓 정리 도중 모듈이 죽은 것으로 판정되면 CGATT/CFUN 을 건너뛰고 바로 강제 종료 */
    if (App_NBIoTDetachModuleDead() == APP_TRUE)
    {
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                 " dead streak=%u @sockclean -> skip CGATT/CFUN", s_nbiotDetachNoRespStreak);
        detachWaitStatus = APP_STATUS_UART_TIMEOUT;
        cfunMinStatus     = APP_STATUS_UART_TIMEOUT;
        g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_HW;   /* 다음 부팅에 HW리셋 경로 유도 */
        goto force_power_off;
    }

    if (g_appBc95AtInitialized == APP_TRUE)
    {
        if (g_appNbiotCarrierContext.attachState == APP_NBIOT_ATTACH_STATE_READY)
        {
            status = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CGATT_DETACH,
                                                   APP_BC95_AT_RX_TIMEOUT_MS,
                                                   "AT+CGATT=0",
                                                   APP_NBIOT_REPORT_LOG_DETACH);
            App_NBIoTDetachNoteOutcome(status);

            /* CGATT=0 자체에서 무응답이 임계값에 도달하면 즉시 이탈 */
            if (App_NBIoTDetachModuleDead() == APP_TRUE)
            {
                APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                         " dead streak=%u @CGATT0 -> skip wait/CFUN", s_nbiotDetachNoRespStreak);
                detachWaitStatus = status;
                cfunMinStatus     = status;
                g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_HW;
                goto force_power_off;
            }

            if (status == APP_STATUS_OK)
            {
                g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_WAIT_CEREG0;
                detachWaitStatus = App_NbiotCarrierWaitForCereg0(APP_NBIOT_DETACH_WAIT_CEREG0_MS);
                App_NBIoTDetachNoteOutcome(detachWaitStatus);
                if (detachWaitStatus != APP_STATUS_OK)
                {
                    APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " incomplete -> force power off (status=%d)", (int)detachWaitStatus);
                }
            }
            else
            {
                detachWaitStatus = status;
                APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " command failed -> force power off (status=%d)", (int)status);
            }
        }
        else
        {
            detachWaitStatus = APP_STATUS_OK;
            APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_DETACH " skipped (attach not ready, state=%s)",
                     App_NbiotCarrierAttachStateString(g_appNbiotCarrierContext.attachState));
        }

        /* CGATT/CEREG 단계에서 이미 무응답 판정났으면 CFUN=0 자체도 건너뛴다 */
        if (App_NBIoTDetachModuleDead() == APP_TRUE)
        {
            APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                     " dead streak=%u before CFUN0 -> skip CFUN0", s_nbiotDetachNoRespStreak);
            cfunMinStatus = detachWaitStatus;
            g_appNbiotCarrierContext.lastResetType = APP_NBIOT_CARRIER_RESET_HW;
            goto force_power_off;
        }

        /* CFUN=0 재전송 간격 3s->1s: detach 처리 중 바쁜 모듈에 3s 대기는 무의미(실측:
         * 1회째 3s 무응답, 2회째 즉답 CME 51). 1초 후 1회 재전송, 총 2회 시도 */
        {
            uint16_t cfun0RxLen = 0u;

            cfunMinStatus = App_Bc95AtSendCommand(APP_BC95_AT_CMD_CFUN_SET_MIN,
                                                  g_appBc95AtRxBuf,
                                                  (uint16_t)sizeof(g_appBc95AtRxBuf),
                                                  APP_BC95_AT_RX_TIMEOUT_MS,
                                                  &cfun0RxLen);
            if ((cfunMinStatus != APP_STATUS_OK) ||
                (App_Bc95AtCheckResponse((const char *)g_appBc95AtRxBuf, NULL) != APP_BC95_AT_OK))
            {
                App_Bc95AtDelayWithFeed(APP_NBIOT_POWEROFF_CFUN0_RETRY_DELAY_MS);
                cfunMinStatus = App_Bc95AtSendSimpleOkCommand(APP_BC95_AT_CMD_CFUN_SET_MIN,
                                                              APP_BC95_AT_RX_TIMEOUT_MS,
                                                              "AT+CFUN=0",
                                                              APP_NBIOT_REPORT_LOG_POWEROFF);
            }
        }

        App_NBIoTDetachNoteOutcome(cfunMinStatus);
        if (cfunMinStatus != APP_STATUS_OK)
        {
            APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                     " CFUN0 fail st=%d", (int)cfunMinStatus);

        }
    }
    else
    {
        APP_LOGW("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
                 " no AT ctx -> direct off");

    }

    /* [NRB 최적화 2단계] 정상 종료 경로에서만 스티어링.
     * dead 모듈 경로(goto force_power_off)는 AT 불가 -> 스킵되며
     * 다음 세션 부팅 sync가 기존 경로로 회복한다. */
    if (s_appNbiotProvisionedThisSession == APP_TRUE)
    {
        App_NbiotSteerAutoconnectFalseAtPowerOff();
        s_appNbiotProvisionedThisSession = APP_FALSE;
    }

force_power_off:   /* 라벨 */
    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_FORCE_OFF;
    status = App_GpioLpSetNbiotPowered(APP_FALSE);
    if (status != APP_STATUS_OK)
    {
        APP_LOGE("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF " pwr pin off fail st=%d", (int)status);
        g_appNbiotCarrierContext.lastStatus = status;
        return status;
    }

    g_appNbiotCarrierContext.powerOffState = APP_NBIOT_POWEROFF_STATE_DONE;
    g_appNbiotCarrierContext.lastPowerOffDoneTick = HAL_GetTick();
    g_appNbiotCarrierContext.lastPowerOffCfun0Status = cfunMinStatus;
    g_appNbiotCarrierContext.lastStatus = APP_STATUS_OK;
    APP_LOGN("NBIOT", APP_NBIOT_REPORT_LOG_POWEROFF
             " done state=%s detachStatus=%d cfun0Status=%d",
             App_NbiotCarrierPowerOffStateString(g_appNbiotCarrierContext.powerOffState),
             (int)detachWaitStatus,
             (int)cfunMinStatus);
    return APP_STATUS_OK;
#endif
}

const AppNbiotCarrierContext_t *App_NBIoTCarrierGetContext(void)
{
    return &g_appNbiotCarrierContext;
}

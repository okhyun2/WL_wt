#ifndef APP_BUILD_CONFIG_H
#define APP_BUILD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @file    app_build_config.h
 * @brief   Application-wide build configuration and constant definitions.
 */

#define APP_FALSE                                   (0u)
#define APP_TRUE                                    (1u)

#define APP_NAME_STRING                             "WaterLink WaterTerminal"

///////////////////////////////////////////////////////////////////////////////////////////////////////
/* ================================================================
 *  EPC Test Mode (시험용 빌드 옵션)
 * ================================================================ */
#define APP_EPC_TEST_MODE_ENABLE                    (APP_FALSE)   /* 1:시험 코드 포함, 0:완전 제거 */

#define APP_EPC_LOG_TAG                             "EPC"
#define APP_SELFDIAG_LOG_TAG                        "SELFDIAG"   /* 신규: TEST3 자가진단 전용 로그 태그 */

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
#define APP_EPC_ACTIVE_TEST_ID                      (7)   /* 1:시험1, 2:시험2, 3:시험3(자가진단), 7:시험7(통신파라미터), 12:시험12(전원이상/복구) */

#if (APP_EPC_ACTIVE_TEST_ID == 1u)
#define APP_EPC_TEST_ID_STRING                      "TEST1"
#define APP_EPC_TEST1_METERING_PERIOD_SEC           (10u)

#elif (APP_EPC_ACTIVE_TEST_ID == 2u)
#define APP_EPC_TEST_ID_STRING                      "TEST2"
#define APP_EPC_TEST2_METERING_PERIOD_SEC           (10u)

#elif (APP_EPC_ACTIVE_TEST_ID == 3u)                              
#define APP_EPC_TEST_ID_STRING                      "TEST3"
#define APP_EPC_TEST3_SELFDIAG_PERIOD_SEC           (10u)  /* 자가진단 반복 주기(초) */
#define APP_EPC_TEST3_DUT_LABEL                     "SAMPLE-03"  /* SAMPLE-01:DUT-C, SAMPLE-02:DUT-B, SAMPLE-03:DUT-A 시료 식별 라벨: 보드마다 값 변경 */
#define APP_EPC_TEST3_WATCHDOG_DISABLE_EXTERNAL_FEED (APP_FALSE) /* DUT-C 외부 watchdog test . APP_TRUE:watchdog no feed*/

#elif (APP_EPC_ACTIVE_TEST_ID == 7u)
#define APP_EPC_TEST_ID_STRING                      "TEST7"

/* ----------------------------------------------------------------
 *  TEST7: 시험 조건 전송 주기 단축 (서비스/관리 전송: 1시간 -> 10분)
 *  App_FsmTxScheduleCheckDue / EnsureInitialized / ConsumeDueNow 에서
 *  #ifdef 로 참조하는 디버그 오버라이드 매크로. periodHours(EEPROM)보다
 *  우선 적용되어 다음 due 시각 계산까지 그대로 반영된다.
 * ---------------------------------------------------------------- */
#define APP_EPC_TEST7_TX_PERIOD_MIN                 (2u)   /* 시험 조건 전송 주기(분) */
#define APP_DEBUG_TX_PERIOD_MS                       (APP_EPC_TEST7_TX_PERIOD_MIN * 60000u)   /* service TX */
#define APP_DEBUG_MGMT_TX_PERIOD_MS                  (APP_EPC_TEST7_TX_PERIOD_MIN * 60000u)   /* management TX */

#define APP_EPC_TEST7_METERING_PERIOD_MIN (1u)  /* 검침 주기(분) */
#define APP_DEBUG_METER_PERIOD_MS   (APP_EPC_TEST7_METERING_PERIOD_MIN * 60000u)

#elif (APP_EPC_ACTIVE_TEST_ID == 12u)
#define APP_EPC_TEST_ID_STRING                      "TEST12"

/* ----------------------------------------------------------------
 *  TEST12: 전원 이상 및 복구 신뢰성
 *  부팅 카운터(EEPROM) 읽기 -> +1 증가 -> 저장 -> 판정 로그 출력까지만
 *  수행하고, MX_WWDG_Init() 호출 이전에 정지한다(워치독 미기동 상태).
 *  -> 이후 전원 재인가(리셋)만이 다음 사이클을 유발하는 유일한 트리거.
 * ---------------------------------------------------------------- */
#define APP_EPC_TEST12_UART_FLUSH_DELAY_MS           (50u)

#endif
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////
/* ============================================================
 *  [FW-UPD] NB-IoT OTA 펌웨어 업데이트 (6.9) 빌드 스위치
 *  - 기본: #undef (비활성) -- 관련 코드/AT 명령 등록/리셋 동작이 모두
 *    컴파일에서 제거되어 바이너리에 남지 않는다.
 *  - 활성화: 아래 주석을 해제한다 (#define). 부트로더 사이드
 *    (SUPPORT_BOOTLOADER_UPDATE)와 별개 스위치다.
 * ============================================================ */
/* #define APP_NBIOT_FW_UPD_ENABLE */

/* flash layout (bootloader + single application) */
/*
#define APP_FLASH_TARGET_SIZE_BYTES                 (128u * 1024u) //128KB:128u*1024u, 192KB:192u*1024u
#define APP_BOOTLOADER_BASE_ADDR                    (0x08000000u)
#define APP_BOOTLOADER_SIZE_BYTES                   (8u * 1024u)
#define APP_IMAGE_BASE_ADDR                         (0x08002000u)
#define APP_IMAGE_NAME                              "app"
*/

/* f/w 버전 체계: "0x71.5.1" (major=0x71, minor, patch).
 * 바이너리 2바이트 필드(NFC 서울/검침 서버 포맷 firmwareVersion)는 major, minor만
 * 전송(0x71, 5) — patch는 문자열(로그/QREP)에서만 표기된다. */
#define APP_FW_VERSION_MAJOR                        (0x71u)
#define APP_FW_VERSION_MINOR                        (5u)
#define APP_FW_VERSION_PATCH                        (1u)
#define APP_FW_VERSION_STRING                       "0x71.5.1"
 
#define NBIOT_SUPPORT_DNS
#ifdef NBIOT_SUPPORT_DNS
//#define MY_SERVER_DOMAIN              "acorp2.iptime.org" //TODO test server
#define MY_SERVER_DOMAIN              "dev-aro.com" //TODO test server

#define WARMUPDNS_SERVER_DOMAIN       MY_SERVER_DOMAIN
#define APP_MGMT_SERVER_DOMAIN        MY_SERVER_DOMAIN
#else
#define MY_SERVER_IP                    "59.27.15.61" //public ip
#define APP_MGMT_SERVER_IP             MY_SERVER_IP
#endif // NBIOT_SUPPORT_DNS
#define APP_SERVICE_SERVER_PORT                 (6001u)
#define APP_MGMT_SERVER_PORT                    (6001u)

/* BC95-GV board-aligned platform provisioning defaults */
#define APP_BC95_PLATFORM_LGU_SERVER_IP_STRING      "lbk.onem2mstp.uplus.co.kr"
#define APP_BC95_PLATFORM_LGU_SERVER_PORT_STRING    "5783"
//#define APP_BC95_PLATFORM_LGU_SERVER_IP_STRING      "mef.onem2mstp.uplus.co.kr"
//#define APP_BC95_PLATFORM_LGU_SERVER_PORT_STRING    "443"
#define APP_BC95_PLATFORM_LGU_BOOTSTRAP_MODE        "BS"            //fix
#define APP_BC95_PLATFORM_LGU_LWM2M_MODE            "LWM2M"         //fix
#define APP_BC95_PLATFORM_LGU_EPNS_STRING           "ASN_CSE-D-8931f40c9a-ADJ1"
//#define APP_BC95_PLATFORM_LGU_SERIAL_STRING         "02132120"  //debug
#define APP_BC95_PLATFORM_LGU_SERVCODE_STRING       "ADJ1"          //fix. service-code
#define APP_BC95_PLATFORM_LGU_MODEL_STRING          "ACO-WT-NB26"     //fix. device name
//realtime 생성.
//#define APP_BC95_PLATFORM_LGU_MBSPS_STRING          "ADJ1,4323800010,01226680136,919592,ACO-WT-NB26" //QLWMBSPS:<serv_code>,<ser_no>,<ctn>,<iccid>,<model>
#define APP_BC95_PLATFORM_LGU_NBAND_STRING          "5"
/* boot info banner ([[BootInfo]]) : 단말 정적 정보. 모듈 동적 정보는 AT 조회, 실패 시 아래 폴백 상수 사용 */
#define APP_BOOT_INFO_DEVICE_MODEL_STRING           "ACO-WT-NB26"           /* 단말 모델명 */
#define APP_BOOT_INFO_MODULE_NAME_STRING            "BC95-GV"               /* 폴백: AT+CGMM 실패 시 */
#define APP_BOOT_INFO_MODULE_FW_STRING              "BC95GVBAR02A01_LGU"    /* 폴백: AT+QGMR 실패 시 */
#define APP_BOOT_INFO_MODULE_MAKER_STRING           "Quectel"               /* 폴백: AT+CGMI 실패 시 */

#define APP_SUPERLOOP_IDLE_DELAY_MS                 (1u)

#define APP_DEFAULT_RTC_YEAR                        (2026)
#define APP_DEFAULT_RTC_MONTH                       (1)
#define APP_DEFAULT_RTC_DAY                         (1)
#define APP_DEFAULT_RTC_HOUR                        (0)
#define APP_DEFAULT_RTC_MIN                         (0)
#define APP_DEFAULT_RTC_SEC                         (0)

#define APP_CLOCK_MSI_RANGE_BOOT                    RCC_MSIRANGE_5
#define APP_CLOCK_SYSCLK_BOOT_HZ                    (2097000u)
#define APP_CLOCK_HCLK_BOOT_HZ                      (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_PCLK1_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_PCLK2_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_FLASH_LATENCY_BOOT                FLASH_LATENCY_0
#define APP_CLOCK_LSE_NOMINAL_HZ                    (32768u)
#define APP_CLOCK_LSE_DRIVE                         RCC_LSEDRIVE_MEDIUMHIGH
/* RV3032 CLKOUT(32.768kHz)을 LSE 소스로 사용: APP_TRUE=LSE bypass(외부 클럭 입력),
   APP_FALSE=보드 크리스털 발진(기존 동작). bypass에서는 LSEDRV가 무의미함 */
#define APP_CLOCK_LSE_BYPASS_ENABLE                 (APP_TRUE) /* V0.4 board: RV-3032 CLKOUT supplies 32.768kHz to LSE input */
/* [V0.4 eRTC] RV-3032 CLKOUT(32.768kHz) 확인을 I2C1로 수행. V0.3 보드(RV-3032 없음)에서는
 * I2C NACK -> 자동 스킵(LSI 유지)하므로 단일 빌드로 혼재 운용 가능. */
#define APP_ERTC_ENABLE                             (APP_TRUE)
#define APP_CLOCK_LSI_NOMINAL_HZ                    (APP_CLOCK_LSE_NOMINAL_HZ)
#define APP_CLOCK_SYSTICK_HZ                        (1000u)
#define APP_CLOCK_LSE_READY_TIMEOUT_MS              (8000u)
#define APP_CLOCK_LSE_RETRY_COUNT                   (3u)
#define APP_CLOCK_LSE_RETRY_OFF_DELAY_MS            (100u)
#define APP_CLOCK_LSE_RETRY_ON_DELAY_MS             (500u)
#define APP_CLOCK_LSE_POST_READY_SETTLE_MS          (300u)
/* 부팅 시 LSE를 블로킹 대기하지 않기 위한 백그라운드 정책 (신규) */
#define APP_CLOCK_LSE_BG_POLL_PERIOD_MS              (200u)
#define APP_CLOCK_LSE_BG_TOTAL_TIMEOUT_MS            (10000u)
#define APP_CLOCK_LSE_CSS_ARM_DELAY_MS              (2000u)  /* 전환 후 CSS 무장 전 추가 안정화 대기 */
#define APP_CLOCK_LSE_CSS_ARM_RECHECK_MS            (50u)    /* 안정화 대기 중 LSERDY 재확인 주기 */

#define APP_DEBUG_UART_TIMEOUT_MS                   (100u)
#define APP_DEBUG_CONSOLE_RX_LINE_SIZE              (64u)
#define APP_DEBUG_CONSOLE_EOL                       "\r\n"
#define APP_LOG_BUFFER_SIZE                         (192u)
#define APP_LOG_HEXDUMP_BYTES_PER_LINE              (16u)

#define APP_BUILD_IS_PRODUCTION                     (APP_FALSE) //if true. change swd&debug pin -> analog input.(=disable swd&debug).
//#define APP_BUILD_IS_PRODUCTION                     (APP_TRUE) //if true. change swd&debug pin -> analog input.(=disable swd&debug).

#if (APP_BUILD_IS_PRODUCTION == APP_TRUE)
/* APP_TRUE: APP_LOGT/D/I 호출과 포맷 문자열을 빌드에서 제거(flash 절감). SWD 핀 설정과 무관. 검증용 빌드는 APP_FALSE. */
#define APP_LOG_STRIP_VERBOSE                       (APP_TRUE) //debug:APP_FALSE, PRODUCTION:APP_TRUE
#define APP_BUILD_DEBUG_ENABLED                     (APP_FALSE) //del timestamp
/* [2026-11-05] 디버깅/추적 코드 게이트 — AckDbg/EvtTrace/RxProbe/probe 카운터/트레이스 상수.
 * 제품 빌드에서 끄면 관련 전역·함수·로그가 모두 컴파일에서 제외된다. */
#undef NBIOT_TRACE_DEBUG
/* 가속 테스트(검침,주기보고,MGMT보고)시: define */
#undef APP_TEST_PERIOD_ACCEL //PRODUCT-> #undef
#define APP_TEST_SERVER_TX_BUZZER_ENABLE            (APP_FALSE)
#else
/* APP_TRUE: APP_LOGT/D/I 호출과 포맷 문자열을 빌드에서 제거(flash 절감). SWD 핀 설정과 무관. 검증용 빌드는 APP_FALSE. */
#define APP_LOG_STRIP_VERBOSE                       (APP_FALSE) //debug:APP_FALSE, PRODUCTION:APP_TRUE
#define APP_BUILD_DEBUG_ENABLED                     (APP_TRUE)
/* [2026-11-05] 디버깅/추적 코드 게이트 — AckDbg/EvtTrace/RxProbe/probe 카운터/트레이스 상수.
 * 제품 빌드에서 끄면 관련 전역·함수·로그가 모두 컴파일에서 제외된다. */
#undef NBIOT_TRACE_DEBUG
/* 가속 테스트(검침,주기보고,MGMT보고)시: define */
#undef APP_TEST_PERIOD_ACCEL //PRODUCT-> #undef
#define APP_TEST_SERVER_TX_BUZZER_ENABLE            (APP_TRUE)
#endif //(APP_BUILD_IS_PRODUCTION == APP_TRUE)

//////////////////////////////////////////////////////////////////////////////////////

#define SUPPORT_EEPROM
#undef SUPPORT_FALASH

//select support meter
#define SUPPORT_METER_NORMAL
#undef SUPPORT_METER_SC1xxx

/* [TRACE-COMP-GATE] 주기 반복 컴포넌트의 trace comp 로그 게이트:
 * meter/rtc/housekeeping/watchdog 등 주기 세션마다 동일 전이 시퀀스를
 * 반복 출력해 13.4h 실측에서 로그의 ~9%(5,044/55,707)를 차지했다.
 * 이벤트성 컴포넌트(nfc/nbiot) 전이는 이상 세션 식별 가치가 있어 유지.
 * 1로 세팅하면 기존대로 전량 출력(회귀 시 사용). 기본 OFF. */
#define APP_FSM_TRACE_PERIODIC_COMP (0u)   /* 1 = 주기 컴포넌트 trace comp 재활성화 */

//NBIoT simulation code
#undef NBIoT_SIMULATION_CODE

#undef SUPPORT_SELFTEST
#define APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE       (APP_TRUE)
#if defined(SUPPORT_SELFTEST) || (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE)
#define APP_SELFTEST_FAIL_STOPS_BOOT                (APP_FALSE)
#define APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT         (2u)
#define APP_SELFTEST_BUZZER_ERROR_BEEP_COUNT        (3u)
#define APP_SELFTEST_BUZZER_BEEP_ON_MS              (80u)
#define APP_SELFTEST_BUZZER_BEEP_OFF_MS             (50u)
#define APP_SELFTEST_BUZZER_ERROR_ON_MS             (60u)
#define APP_SELFTEST_BUZZER_ERROR_OFF_MS            (60u)
#define APP_SELFTEST_I2C_READY_TRIALS               (2u)
#define APP_SELFTEST_I2C_READY_TIMEOUT_MS           (20u)
#define APP_SELFTEST_UART_TIMEOUT_MS                (100u)
#define APP_SELFTEST_UART_REPLY_METER_NORMAL_TIMEOUT_MS          (1200u)
#define APP_SELFTEST_UART_REPLY_METER_SC1xxx_TIMEOUT_MS          (800u)
#define APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN (21u)
#define APP_SELFTEST_UART_METER_LOOP_EXPECTED_RX_MIN_LEN (5u)
#define APP_SELFTEST_UART_METER_REINIT_PREP_DELAY_MS             (20u)
#define APP_SELFTEST_UART_METER_REINIT_SETTLE_DELAY_MS           (100u)
#define APP_SELFTEST_UART_METER_POST_NBIOT_SETTLE_DELAY_MS       (300u)
#define APP_SELFTEST_UART_METER_POST_CFUN_FAIL_SETTLE_DELAY_MS   (2500u)
#define APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN (15u)
#define APP_SELFTEST_UART_RX_BUFFER_SIZE            (32u)

#define APP_SELFTEST_NFC_I2C_ADDRESS_7BIT           (0x54u)
#define APP_SELFTEST_AUX_I2C_ADDRESS_7BIT           (0x44u)   /* [SHT40] V0.4 센서 교체: SHTC3(0x70) -> SHT40(0x44) */

#endif /* SUPPORT_SELFTEST || APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE */

/* ----------------------------------------------------------------
 * Test option: buzzer notification on successful server transmit
 *  - APP_TRUE : service/mgmt server 전송 성공 시 1회 부저
 *  - APP_FALSE: 비활성
 * ---------------------------------------------------------------- */
#define APP_TEST_SERVER_TX_BUZZER_ON_MS             (80u)
#define APP_TEST_SERVER_TX_BUZZER_OFF_MS            (40u)

/* ----------------------------------------------------------------
 * Management TX only: append temp_humi, self-test summary trailer
 * ---------------------------------------------------------------- */
#define APP_MGMT_TX_APPEND_ENV_ENABLE               (APP_TRUE)
#define APP_MGMT_TX_ENV_TAG                         (0xD1u)
#define APP_MGMT_TX_ENV_PAYLOAD_LEN                 (5u)
#define APP_MGMT_TX_APPEND_SELFTEST_ENABLE          (APP_TRUE)
#define APP_MGMT_TX_SELFTEST_TAG                    (0xD2u)
#define APP_MGMT_TX_SELFTEST_PAYLOAD_LEN            (7u)
#define APP_MGMT_TX_APPEND_SERVICE_STAT_ENABLE      (APP_TRUE)
#define APP_MGMT_TX_SERVICE_STAT_TAG                (0xD3u)
#define APP_MGMT_TX_SERVICE_STAT_PAYLOAD_LEN        (3u)  

/* [LSE-STUCK] device-health 트레일러(tag 0xD4): LSE 확정 실패 시에만 부가.
 * 정상 단말 프레임은 1바이트도 변하지 않는다(조건부 append).
 * payload(1바이트): bit0=LSE_FAIL. 영구 sleep 게이트는 기본 FALSE -
 * 서버 판정 플로우 실측 후 TRUE 전환한다. */
#define APP_MGMT_TX_APPEND_HEALTH_ENABLE            (APP_TRUE)
#define APP_MGMT_TX_HEALTH_TAG                      (0xD4u)
#define APP_MGMT_TX_HEALTH_PAYLOAD_LEN              (1u)
#define APP_CLOCK_LSE_STUCK_PERMA_SLEEP_ENABLE      (APP_FALSE)

/* NFC production hardening */
#define APP_NFC_MASTER_KEY_BYTES                      { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define APP_NFC_TEMP_THRESHOLD_MIN_X10               (-400)
#define APP_NFC_TEMP_THRESHOLD_MAX_X10               (1250)
#define APP_NFC_REPORT_INTERVAL_MIN_SEC              (10u)
#define APP_NFC_REPORT_INTERVAL_MAX_SEC              (86400u)
#define APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE      (0u) //0:disable(default), 1:enable. refresh ms tx nfc. for kc identify
#define APP_NFC_TEST_MODE_REFRESH_MS                (100u)


/* [MGMT-SCHEDULE] mgmt 전송도 QREP과 동일한 일 단위 독립 스케줄로 운용한다.
 * - 주기: INTERVAL_DAYS(1=매일, 2=이틀 간격), 2일 주기 시 DAY_PARITY로
 *   전송일을 홀수날/짝수날로 강제한다(달력 기준 - 설치 시점 무관).
 * - 서버 옵션(managementReportingPeriodHours)은 스케줄에 반영되지 않는다.
 * - wake(AlarmB)은 service/mgmt/qrep due의 최솟값으로 결정된다. */
#define APP_MGMT_TX_INTERVAL_DAYS                   (1u)   /* 1=하루 1회, 2=2일 1회 */
#define APP_MGMT_TX_DAY_PARITY                      (0u)   /* 0=짝수날, 1=홀수날 */
/* [MGMT-WINDOW] 새벽 전송 윈도우(02~06시) + catch-up. QREP과 동일 정책:
 * due일이라도 윈도우 밖이면 대기하고, 만료+1일 경과 시 윈도우 무시 즉시 전송. */
#define APP_MGMT_TX_WINDOW_START_HOUR               (2u)   /* 새벽 윈도우 02시 */
#define APP_MGMT_TX_WINDOW_END_HOUR                 (4u)   /* ~ 06시 (04시 포함) */
#define APP_MGMT_TX_CATCHUP_EXTRA_DAYS              (1u)   /* 만료 후 1일 경과 시 즉시 전송 */

/* [QUALITY-REPORT 2026-11-09] NB-IoT 단말 품질리포트 (규격 v1.8, MSG Ver.04)
 * 목적지/주기/새벽 윈도우는 고정값(컴파일타임 확정, NFC/서버 설정 아님). */
//#define APP_QUALITY_REPORT_HOST                     "10.120.182.10" //TODO default server
#define APP_QUALITY_REPORT_HOST                     "acorp2.iptime.org" //TODO test server
#define APP_QUALITY_REPORT_PORT                     (50000u)
#define APP_QUALITY_REPORT_MSG_VER                  (0x04u)
#define APP_QUALITY_REPORT_INTERVAL_DAYS            (1u)   /* 1=하루 1회, 2=2일 1회 */
#define APP_QUALITY_REPORT_DAY_PARITY               (0u)   /* 0=짝수날, 1=홀수날 */
#define APP_QUALITY_REPORT_WINDOW_START_HOUR        (4u)   /* 새벽 윈도우 02시 */
#define APP_QUALITY_REPORT_WINDOW_END_HOUR          (6u)   /* ~ 06시 (04시 포함) */
#define APP_QUALITY_REPORT_CATCHUP_EXTRA_DAYS       (1u)   /* 만료 후 1일 경과 시 윈도우 무시 즉시 전송 */
#define APP_QUALITY_REPORT_MODEL_STRING             APP_BC95_PLATFORM_LGU_MODEL_STRING

#define APP_FSM_USE_WFI_IDLE                        (APP_TRUE)
#define APP_FSM_IDLE_DELAY_MS                       (1u)
#define APP_FSM_BOOT_RESET_HOLD_MS                  (1000u)
#define APP_FSM_DEBUG_POLL_PERIOD_MS                (1u)
#define APP_FSM_HOUSEKEEPING_PERIOD_MS              (500u)
#define APP_FSM_METER_PERIOD_MS                     (500u)
#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
#define APP_FSM_NFC_PERIOD_MS                       (APP_NFC_TEST_MODE_REFRESH_MS)
#else
#define APP_FSM_NFC_PERIOD_MS                       (500u)
#endif
#define APP_FSM_AUX_PERIOD_MS                       (1000u)
#define APP_FSM_NBIOT_PERIOD_MS                     (500u)
#define APP_FSM_SERVER_PERIOD_MS                    (1000u)
#define APP_FSM_RTC_PERIOD_MS                       (1000u)
#define APP_FSM_LPTIM_PERIOD_MS                     (1000u)
#define APP_FSM_WATCHDOG_PERIOD_MS                  (1000u)
#define APP_FSM_STORAGE_PERIOD_MS                   (500u)

/* ================================================================
 *  Periodic Self-Diagnostics on Meter Wake (운영 모드 상시 자가진단)
 * ================================================================ */
#define APP_SELFDIAG_PERIODIC_ON_METER_WAKE_ENABLE   (APP_TRUE)   /* 1:검침 웨이크업 시 주기적 자가진단 활성화, 0:완전 비활성화 */
#define APP_SELFDIAG_PERIODIC_WAKE_INTERVAL_HOURS    (24u)        /* 최소 실행 간격(시간). 24=하루 1회 (문서화 용도) */

#define APP_RTC_LSE_ASYNC_PREDIV                    (127u)
#define APP_RTC_LSE_SYNC_PREDIV                     (255u)
#define APP_RTC_LSI_ASYNC_PREDIV                    (127u)
#define APP_RTC_LSI_SYNC_PREDIV                     (288u)
#define APP_RTC_WAKEUP_PERIOD_MS                    (60*60*1000u) //1h
//#define APP_RTC_WAKEUP_PERIOD_MS                    (0u) // test/debug/fallback only
#define APP_RTC_WAKEUP_PERIOD_TEST_FALLBACK_ONLY    (APP_TRUE)
#define APP_RTC_ALARM_COLLISION_TX_DELAY_SEC        (60u)

/* NBIoT 부팅 시 최초 연결 지연용 고정 오프셋 범위(초) */
#define APP_NBIOT_BOOT_OFFSET_MAX_SEC               (5u)
/* NBIoT 통신 충돌 방지용 장치별 오프셋/지터 설정 */
#define APP_NBIOT_XMIT_OFFSET_MAX_SEC               (60u)   /* 장치별 고정 오프셋 범위(0~60초) */
#define APP_NBIOT_XMIT_JITTER_MAX_SEC               (10u)    /* 매 주기 추가되는 랜덤 지터(0~10초) */

#define APP_WATCHDOG_EXTERNAL_FEED_PULSE_MS         (50u)
#define APP_WATCHDOG_EXTERNAL_FEED_DUTY_PERCENT     (50u)
#define APP_WATCHDOG_EXTERNAL_FEED_BOOT_PRIME_CNT   (1u)

#define APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT          (2u)
#define APP_LP_STOP_DEBUG_DRY_RUN                   (APP_FALSE)
#define APP_LP_TEST_FORCE_ADC_FULL_OFF_BEFORE_STOP  (APP_TRUE)
#define APP_NFC_ENTER_STANDBY_BEFORE_MCU_STOP      (APP_TRUE)

/* LGU+ Step1 Carrier Mandatory Layer */
#define APP_NBIOT_MANDATORY_LAYER_ENABLE           (APP_TRUE)
#define APP_NBIOT_ATTACH_TIMEOUT_MS                (60000u)
#define APP_NBIOT_ATTACH_TOTAL_FAIL_LIMIT_MS       (600000u)
#define APP_NBIOT_ATTACH_SW_RESET_MAX              (3u)
#define APP_NBIOT_ATTACH_HW_RESET_MAX              (1u)
#define APP_NBIOT_DETACH_WAIT_CEREG0_MS            (15000u)
#define APP_NBIOT_DETACH_POLL_INTERVAL_MS          (1000u)
/* detach 중 ready/home/CGATT=1 고착이면 장시간 대기하지 않고 CFUN=0으로 이행 */
#define APP_NBIOT_DETACH_READY_STUCK_POLL_MAX      (4u)
#define APP_NBIOT_AT_NORESP_RETRY_MAX              (3u)
#define APP_NBIOT_SW_RESET_SETTLE_MS               (1500u)
#define APP_NBIOT_HW_RESET_SETTLE_MS               (2000u)

/* OPS-01/02 period policy defaults (allowed values: 1/2/3/4/6/8/12/24h. default:meter-1h, report-4h) */
#define APP_POLICY_DEFAULT_METERING_PERIOD_HOURS   (1u)
#define APP_POLICY_DEFAULT_REPORTING_PERIOD_HOURS  (1u)
#define APP_POLICY_DEFAULT_REPORT_SPREAD_HOURS     (0u) /* 0: legacy fixed spread fallback */
#define APP_POLICY_DELETE_AFTER_UDP_SEND_SUCCESS   (APP_FALSE)
/* [ACK 정책 분리 2026-11-04] 기존 APP_POLICY_WAIT_SERVER_ACK_ENABLE는
   ServiceTx/MgmtTx 공용 UDP ACK 대기 플래그였음(단일 App_Bc95AtUdpSendOnce 공유).
   경로별 제어를 위해 둘로 분리 — 기본값은 기존 동작 보존(둘 다 TRUE). */
#define APP_POLICY_WAIT_MGMT_ACK_ENABLE            (APP_TRUE)  /* [[MgmtTx]] mgmt 서버 ACK 대기 */
#define APP_POLICY_WAIT_SERVICE_ACK_ENABLE         (APP_TRUE)  /* [[ServiceTx]] service 서버 ACK 대기 */
/* [ACK 정책 상수 정리 2026-11-05] 경로별 2종 체계(MGMT / SERVICE)로 통합.
 * 실측(2026-11-05): mgmt NSORF 수신은 폴링 1회차(220~250ms)에 완료, 정상 세션
 * EVTIND 4→3 평균 2.0s → 타임아웃은 여유 있는 30s 유지(이상 세션은 NRB 에스컬레이션).
 * 파생 DEFAULT_* 매크로는 사용처(옵션 기본값 초기화)에서 인라인 계산으로 대체하고 삭제.
 * 이전 공용 마스터(옵션 기본용)도 삭제 — 옵션 기본값은 경로별 플래그를 직접 사용. */
/* [MGMT 경로] raw UDP(NSOST→NSORF) ACK 대기 — WAIT_MGMT_ACK_ENABLE과 짝 */
#define APP_POLICY_MGMT_ACK_TIMEOUT_MS             (3000u)
#define APP_POLICY_MGMT_ACK_POLL_MS                (100u)
/* [SERVICE 경로] 플랫폼 UL(QLWULDATAEX) 후 앱 ACK(+NNMI) 대기 — QLWULDATASTATUS=4(1차 ACK) 이후.
 * 서버가 DL 응답을 생성해 단말 +NNMI로 도달하기까지의 상한. 0이 아닌 값 유지 필요. */
#define APP_POLICY_SERVICE_ACK_TIMEOUT_MS          (3000u)
#define APP_POLICY_SERVICE_ACK_POLL_MS             (100u)
/* 공용: NSORF 읽기 요청 바이트(경로 무관) */
#define APP_POLICY_SERVER_ACK_READ_BYTES           (64u)
#define APP_POLICY_DEFAULT_REPORT_SPREAD_HOURS     (0u) /* 0: legacy fixed spread fallback */
#define APP_POLICY_REPORT_SPREAD_FALLBACK_MS       (APP_NBIOT_XMIT_OFFSET_MAX_SEC * 1000u)
//TODO change serial number -> serial_bcd 5
//#define APP_POLICY_DEFAULT_DEVICE_SERIAL_BCD       "\x12\x34\x56\x78\x90" 
#define APP_POLICY_DEFAULT_DEVICE_SERIAL_BCD       "\x46\x10\x00\x00\x05" 
//#define APP_POLICY_DEFAULT_DEVICE_SERIAL_BCD       "\x45\x67\x00\x00\x02" 
#define APP_POLICY_NBIOT_BOOTRACK_REG_SERVPLATFORM (APP_TRUE) //default: TRUE


/* ================================================================
 *  Comm Parameter Auto-Tuning (무선환경 기반 통신 파라미터 자동설정)
 * ================================================================ */
#define APP_COMM_PARAM_AUTOTUNE_ENABLE          (APP_TRUE) //default:TRUE

#define APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE    (APP_FALSE) 

#define APP_COMM_RSSI_WEAK_DBM            (-100)
#define APP_COMM_RSRP_WEAK_DBM            (-110)

/*
 * Pre-attach weak-signal fast abort policy
 * - bring-up 직후 AT+CSQ로 1차 선별
 * - 약전계면 짧은 camp probe 후 NUESTATS=CELL로 최종 확인
 * - confirmed weak/not-camped 이면 긴 attach wait로 들어가지 않고 이번 wake attach를 조기 종료
 */
#define APP_BC95_PRE_ATTACH_RSSI_WEAK_DBM          (-106)
#define APP_BC95_PRE_ATTACH_CAMP_PROBE_TIMEOUT_MS (6000u)
#define APP_BC95_PRE_ATTACH_ABORT_RSSI_DBM        (-108)
#define APP_BC95_PRE_ATTACH_ABORT_RSRP_DBM        (-118)
#define APP_BC95_PRE_ATTACH_NUESTATS_RETRY_MAX    (1u)

#define APP_COMM_SUCCESS_STREAK_NEEDED          (2u)
#define APP_COMM_GOOD_MAX_ATTEMPT_IDX           (2u)   /* 0,1,2 = 1~3차 성공 -> HIGH */

/*
 * Adaptive policy v2
 * - service 우선, mgmt는 정책 입력에서 제외
 * - 배터리 우선: 악화는 빠르게(1h -> 4h), 회복은 느리게(4h -> 1h)
 * - 추가 wake/probe 없이 service TX 1회에서 얻는 quality + retry/fail 비용만 사용
 */
#define APP_COMM_SIGNAL_RECOVER_MARGIN_DB       (5)    /* weak -> strong 복귀 여유폭 */
#define APP_COMM_COST_SCORE_MAX                 (7u)
#define APP_COMM_COST_WEAK_ENTER_THRESHOLD      (2u)   /* cost score >= 2 => weak(4h) */
#define APP_COMM_COST_STRONG_EXIT_THRESHOLD     (0u)   /* cost score == 0 => strong(1h) */
#define APP_COMM_COST_NIGHT_ONLY_THRESHOLD      (5u)   /* extreme poor + LOW 일 때만 nightOnly */
#define APP_COMM_COST_FAIL_PENALTY              (3u)   /* 전체 실패 */
#define APP_COMM_COST_RETRY_PENALTY             (2u)   /* 2차 이상 성공 */
#define APP_COMM_COST_WEAK_SUCCESS_PENALTY      (1u)   /* weak but 1차 성공 */
#define APP_COMM_COST_GOOD_RECOVERY_REWARD      (1u)   /* strong + 1차 성공 */
#define APP_COMM_SERVICE_EXPENSIVE_ATTEMPT_IDX  (1u)   /* 0=1차 성공, 1=2차 성공 */

/* v2.1: attach / DNS / UDP retry + same-wake abort */
#define APP_COMM_ATTACH_SLOW_MS                      (20000u)
#define APP_COMM_DNS_SLOW_MS                         (8000u)
#define APP_COMM_COST_ATTACH_SLOW_PENALTY           (1u)
#define APP_COMM_COST_DNS_SLOW_PENALTY              (1u)
#define APP_COMM_COST_UDP_RETRY_PENALTY             (1u)
#define APP_COMM_COST_ABORT_REMAINING_TX_THRESHOLD  (4u)

/* extreme weak -> nightOnly */
#define APP_COMM_EXTREME_WEAK_RSRP_DBM              (-120)
#define APP_COMM_EXTREME_WEAK_SNR_DB                (0)

/* DNS cache */
#define APP_BC95_DNS_CACHE_TTL_MS                   (86400000u) /* 24h */

#define APP_COMM_ADAPTIVE_STRONG_REPORT_HOURS       (APP_POLICY_DEFAULT_REPORTING_PERIOD_HOURS)
#define APP_COMM_ADAPTIVE_WEAK_REPORT_HOURS         (4u)   /* 약전계 시 4시간. 사용자 설정. */
#define APP_COMM_ADAPTIVE_BYPASS_NIGHT_ONLY_DEFAULT (APP_FALSE)  /* APP_FALSE-> nightOnly 정상 동작: 통신 성공 상태가 LOW일 때 야간 전송 모드(01:00~04:00) 적용 */

#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
#define APP_COMM_ADAPTIVE_TEST_OVERRIDE_ENABLE      (APP_TRUE)
#define APP_COMM_ADAPTIVE_STRONG_TX_PERIOD_MIN      (0u)   /* 0 = hour 기반 정상 스케줄 사용 */
#define APP_COMM_ADAPTIVE_WEAK_TX_PERIOD_MIN        (0u)
#define APP_COMM_ADAPTIVE_STRONG_METER_PERIOD_MIN   (0u)
#define APP_COMM_ADAPTIVE_WEAK_METER_PERIOD_MIN     (0u)
#else
#define APP_COMM_ADAPTIVE_TEST_OVERRIDE_ENABLE      (APP_FALSE)
#endif

/* legacy alias */
#define APP_COMM_PERIOD_WEAK_HOURS              (APP_COMM_ADAPTIVE_WEAK_REPORT_HOURS)

#define APP_COMM_NIGHT_START_HOUR               (1u)   /* 01:00 */
#define APP_COMM_NIGHT_END_HOUR                 (4u)   /* 04:00 */

#define APP_MSGQ_DEPTH                              (16u)
#define APP_MSGQ_CAPACITY                           (APP_MSGQ_DEPTH)

#define APP_STORAGE_LAYOUT_REV                      (1u)
#define APP_STORAGE_DATA_EEPROM_SLOT_COUNT          (2u)
#define APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES     (64u)
#define APP_STORAGE_FLASH_PARTITION_PAGE_COUNT      (8u)
#define APP_STORAGE_FLASH_RECORD_STRIDE_BYTES       (FLASH_PAGE_SIZE)
#define APP_STORAGE_PARAM_FLAGS_VALID               (0x00000001u)

#define APP_STORAGE_EEPROM_TOTAL_SIZE_BYTES             (6u * 1024u)
//BANK1 size 3Kbyte(0xC00)
#define APP_STORAGE_CONFIG_EEPROM_OFFSET_BYTES          (0)
#define APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES            (1u * 1024u)
#define APP_STORAGE_METER_OPTION_EEPROM_OFFSET_BYTES    (APP_STORAGE_CONFIG_EEPROM_OFFSET_BYTES + APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES)
#define APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES      (1u * 1024u)
#define APP_STORAGE_RESERVED_EEPROM_OFFSET_BYTES        (APP_STORAGE_METER_OPTION_EEPROM_OFFSET_BYTES + APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES)
#define APP_STORAGE_RESERVED_EEPROM_SIZE_BYTES          (1u * 1024u)
//BANK2 size 3Kbyte(0xC00) - meter data save by circular buffer
#define APP_STORAGE_METER_DATA_EEPROM_OFFSET_BYTES      (3u * 1024u)
#define APP_STORAGE_METER_DATA_EEPROM_SIZE_BYTES        (3u * 1024u)

#define APP_STORAGE_CONFIG_SLOT_COUNT                           (4u)
#define APP_STORAGE_OPTION_SLOT_COUNT                           (4u)

/* ===== [TX-PATH-GATE 2026-11-16] 전송 경로별 컴파일 타임 스위치 =====
 * APP_TX_SERVICE_ENABLE : 서비스 플랫폼(LwM2M/CoAP) 전송 경로 on/off.
 *   0이면 service wake(정기/부팅)의 platform TX를 스킵. mgmt/QREP는 영향 없음.
 * APP_TX_MGMT_ENABLE    : 관제 mgmt UDP 전송 경로 on/off.
 *   0이면 정기 mgmt due/부팅 mgmt live 전송을 모두 스킵.
 * APP_TX_QREP_ENABLE    : 품질리포트(quality report) 전송 on/off.
 *   0이면 리포트를 만들지/보내지 않는다. mgmt와 독립적으로 판정·전송된다
 *   (mgmt 전송 결과/활성 여부에 의존하지 않음).
 * 셋 모두 1(기본)이면 현행 동작과 100% 동일. */
#define APP_TX_SERVICE_ENABLE                       (1u)
#define APP_TX_MGMT_ENABLE                          (1u)
#define APP_TX_QREP_ENABLE                          (1u)

#if !defined(STM32L073xx)
#error "This project requires STM32L073xx device support."
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_BUILD_CONFIG_H */

#ifndef APP_SYSTEM_H
#define APP_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"
#include "app_gpio_lp.h"

/**
 * @file    app_system.h
 * @brief   Application bootstrap, low-power control, and top-level superloop interface.
 */

////////////////////////////////////////////////////////////////////////////
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Wakeup 플래그 비트 정의 */
typedef enum {
    WAKEUP_FLAG_NONE        = 0x00000000,

    /* LPTIM1 관련 */
    WAKEUP_FLAG_LPTIM1_ARR  = 0x00000001,  // Auto-Reload Match
    WAKEUP_FLAG_LPTIM1_CMP  = 0x00000002,  // Compare Match

    /* RTC 관련 */
    WAKEUP_FLAG_RTC_ALARM_A = 0x00000004,  // RTC Alarm A
    WAKEUP_FLAG_RTC_ALARM_B = 0x00000008,  // RTC Alarm B
    WAKEUP_FLAG_RTC_WUT     = 0x00000010,  // RTC Wakeup Timer

    /* EXTI GPIO 핀별 */
    WAKEUP_FLAG_EXTI_PIN0   = 0x00000020,
    WAKEUP_FLAG_EXTI_PIN1   = 0x00000040,
    WAKEUP_FLAG_EXTI_PIN2   = 0x00000080,
    WAKEUP_FLAG_EXTI_PIN3   = 0x00000100,
    WAKEUP_FLAG_EXTI_PIN4   = 0x00000200,
    WAKEUP_FLAG_EXTI_PIN5   = 0x00000400,
    WAKEUP_FLAG_EXTI_PIN6   = 0x00000800,
    WAKEUP_FLAG_EXTI_PIN7   = 0x00001000,
    WAKEUP_FLAG_EXTI_PIN8   = 0x00002000,
    WAKEUP_FLAG_EXTI_PIN9   = 0x00004000,
    WAKEUP_FLAG_EXTI_PIN10  = 0x00008000,
    WAKEUP_FLAG_EXTI_PIN11  = 0x00010000,
    WAKEUP_FLAG_EXTI_PIN12  = 0x00020000,
    WAKEUP_FLAG_EXTI_PIN13  = 0x00040000,
    WAKEUP_FLAG_EXTI_PIN14  = 0x00080000,
    WAKEUP_FLAG_EXTI_PIN15  = 0x00100000,
} wakeup_flag_t;

/* 그룹 마스크 정의 (편의용) */
#define WAKEUP_MASK_LPTIM1      (WAKEUP_FLAG_LPTIM1_ARR | WAKEUP_FLAG_LPTIM1_CMP)
#define WAKEUP_MASK_RTC_ALARM   (WAKEUP_FLAG_RTC_ALARM_A | WAKEUP_FLAG_RTC_ALARM_B)
#define WAKEUP_MASK_RTC_ALL     (WAKEUP_MASK_RTC_ALARM | WAKEUP_FLAG_RTC_WUT)
#define WAKEUP_MASK_EXTI0_1     (WAKEUP_FLAG_EXTI_PIN0 | WAKEUP_FLAG_EXTI_PIN1)
#define WAKEUP_MASK_EXTI2_3     (WAKEUP_FLAG_EXTI_PIN2 | WAKEUP_FLAG_EXTI_PIN3)
#define WAKEUP_MASK_EXTI4_15    (WAKEUP_FLAG_EXTI_PIN4  | WAKEUP_FLAG_EXTI_PIN5  | \
                                 WAKEUP_FLAG_EXTI_PIN6  | WAKEUP_FLAG_EXTI_PIN7  | \
                                 WAKEUP_FLAG_EXTI_PIN8  | WAKEUP_FLAG_EXTI_PIN9  | \
                                 WAKEUP_FLAG_EXTI_PIN10 | WAKEUP_FLAG_EXTI_PIN11 | \
                                 WAKEUP_FLAG_EXTI_PIN12 | WAKEUP_FLAG_EXTI_PIN13 | \
                                 WAKEUP_FLAG_EXTI_PIN14 | WAKEUP_FLAG_EXTI_PIN15)

/* 처리 결과 구조체 */
typedef struct {
    volatile uint32_t pending_flags;    // IRQHandler에서 누적된 미처리 플래그
    uint32_t          processed_flags;  // 메인 루프에서 처리 완료된 플래그
    uint8_t           source_count;     // 감지된 소스 개수
    uint32_t          raw_lptim_isr;    // 디버그용 원본 레지스터 값
    uint32_t          raw_rtc_isr;      // 디버그용 원본 레지스터 값
    uint32_t          raw_exti_pr;      // 디버그용 원본 레지스터 값
} wakeup_context_t;

/* 전역 컨텍스트 */
extern wakeup_context_t g_wakeup_ctx;
////////////////////////////////////////////////////////////////////////////

/* 리셋 원인 식별용 매직 넘버 */
#define WWDG_RESET_MAGIC    0xDEADBEEF
#define NORMAL_BOOT_MAGIC   0x12345678
/* EWI 최대 허용 횟수: X × 65.54ms */
#define EWI_MAX_COUNT   10    // 약 9.8초
/* 리셋 원인 문자열 변환용 */
typedef struct {
    uint32_t reset_flags;
    uint32_t bkp0;
    uint32_t bkp1;
    uint32_t bkp2;
    uint32_t bkp3;
    uint32_t bkp4;
} BootInfo_t;

typedef enum
{
    APP_BOOT_STAGE_RESET = 0,
    APP_BOOT_STAGE_HAL_READY,
    APP_BOOT_STAGE_CLOCK_READY,
    APP_BOOT_STAGE_PERIPH_READY,
    APP_BOOT_STAGE_GPIO_LP_READY,
    APP_BOOT_STAGE_DEBUG_READY,
    APP_BOOT_STAGE_LOG_READY,
    APP_BOOT_STAGE_SELFTEST_DONE,
    APP_BOOT_STAGE_FSM_READY,
    APP_BOOT_STAGE_APP_READY
} AppBootStage_t;

typedef enum
{
    APP_SYSTEM_LP_MODE_RUN = 0,
    APP_SYSTEM_LP_MODE_SLEEP,
    APP_SYSTEM_LP_MODE_STOP
} AppSystemLowPowerMode_t;

typedef enum
{
    APP_SYSTEM_WAKE_SRC_NONE         = 0x00000000u,
    APP_SYSTEM_WAKE_SRC_NBIOT_RI     = 0x00000001u,
    APP_SYSTEM_WAKE_SRC_NFC_ED       = 0x00000002u,
    APP_SYSTEM_WAKE_SRC_REED         = 0x00000004u,
    APP_SYSTEM_WAKE_SRC_RTC          = 0x00000008u,
    APP_SYSTEM_WAKE_SRC_LPTIM        = 0x00000010u,
    APP_SYSTEM_WAKE_SRC_DEBUG_DRYRUN = 0x40000000u,
    APP_SYSTEM_WAKE_SRC_UNKNOWN      = 0x80000000u
} AppSystemWakeSource_t;

typedef struct
{
    uint8_t initialized;
    uint8_t debugReady;
    uint8_t logReady;
    uint8_t selfTestCompleted;
    uint8_t selfTestFailed;
    uint8_t fsmReady;
    uint8_t stopRequested;
    uint8_t stopNoWakeRequested;
    uint8_t stopQualificationCount;
    AppBootStage_t bootStage;
    AppStatus_t selfTestStatus;
    AppStatus_t fsmStatus;
    uint32_t bootSysClockHz;
    uint32_t loopCounter;
    uint32_t idleCounter;
    uint32_t sleepEntryCount;
    uint32_t stopEntryCount;
    uint32_t stopCandidateCount;
    uint32_t stopDryRunCount;
    uint32_t rtcWakeEventCount;
    uint32_t rtcAlarmAWakeEventCount;
    uint32_t rtcAlarmBWakeEventCount;
    uint32_t pendingRtcAlarmFlags;
    uint32_t lastRtcAlarmFlags;
    uint32_t lptimWakeEventCount;
    uint32_t oldWakeSourceMask;
    uint32_t wakeSourceMask;
    uint32_t lastWakeTickMs;
    uint32_t lastStopRequestTickMs;
    uint32_t lastSleepEntryTickMs;
    uint32_t lastStopEntryTickMs;
    AppSystemLowPowerMode_t lastLowPowerMode;
} AppSystemContext_t;

static AppSystemContext_t g_appSystemContext;
static const char g_appVersionString[] = "71.5.1";
static AppGpioLpConfig_t g_appGpioLpConfig;
static char g_appSystemWakeString[64];

#define APP_SYSTEM_RTC_TIMEOUT_LOOPS              (200000u)
#define APP_SYSTEM_RTC_WPR_KEY1                   (0xCAu)
#define APP_SYSTEM_RTC_WPR_KEY2                   (0x53u)
#define APP_SYSTEM_LPTIM1_ARR                     (1023U)   // 32.768kHz / 32 / 1024 = 1Hz
/*
LPTIM_PRESCALER_DIV32                   
LPTIM_PRESCALER_DIV64                   
LPTIM_PRESCALER_DIV128 
*/
#define APP_SYSTEM_LPTIM1_PRESCALER               LPTIM_PRESCALER_DIV32                   

extern BootInfo_t g_boot_info;

AppStatus_t App_SystemInit(void);
void App_SystemProcess(void);
AppStatus_t App_SystemOnBeforeStopEnter(void);
AppStatus_t App_SystemOnAfterStopExit(void);
AppStatus_t App_SystemPrepareForStop(void);
AppStatus_t App_SystemRecoverFromStop(void);
AppStatus_t App_SystemSetNbiotPowered(uint8_t powered);
AppStatus_t App_SystemRequestLowPower(uint8_t allowStop);
AppStatus_t App_SystemRequestLowPowerNoWake(uint8_t allowStopNoWake);
void App_SystemNotifyWakeSource(uint32_t sourceMask);
void App_SystemHandleLptim1AutoReloadMatchCallback(void);
void App_SystemHandleRtcCallBack(void);
void App_SystemHandleExtiCallBack(uint16_t GPIO_Pin);
uint32_t App_SystemGetWakeSourceMask(void);
uint32_t App_SystemGetPendingRtcAlarmFlags(void);
uint32_t App_SystemConsumeRtcAlarmFlags(void);
const AppSystemContext_t *App_SystemGetContext(void);
const char *App_SystemGetVersionString(void);
const char *App_SystemGetWakeSourceString(void);
const char *App_SystemGetLowPowerModeString(void);
AppStatus_t App_SystemRunBootSelfTest(void);
AppStatus_t App_SystemRunWakeDataCollection(void);
void Get_BootInfo(BootInfo_t *pBootInfo);
uint8_t App_SystemPrepareNfcStandbyForStop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_H */

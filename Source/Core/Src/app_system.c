#include "app_system.h"
#include "main.h"

#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_debug.h"
#include "app_dualboot.h"
#include "app_gpio_lp.h"
#include "app_hw.h"
#include "app_log.h"
#include "app_fsm.h"
#include "app_msgq.h"
#include "app_selftest.h"
#include "nfc_ntag5_ntp53321.h"
#include "app_meter_storage.h"
#include "app_meter_server_format.h"
#include "app_nbiot.h"

wakeup_context_t g_wakeup_ctx = {0};
extern NFC_NTP53321_Handle_t g_nfcTagHandle;
extern RTC_HandleTypeDef hrtc;
BootInfo_t g_boot_info;

static void Print_BootInfo(BootInfo_t *pBootInfo);
static void App_SystemForceAdcFullOffBeforeStop(void);
static AppStatus_t App_SystemRestoreAdcAfterWakeFromStop(void);
static void App_SystemRollbackNfcStandbyIfNeeded(uint8_t standbyPrepareState);
static void App_SystemRecoverNfcAfterWake(uint8_t standbyPrepareState);
static AppStatus_t App_SystemRtcConfigureAlarmAForMetering(uint8_t *p_configured);
static AppStatus_t App_SystemRtcConfigureAlarmBForReporting(uint8_t *p_configured);

static uint8_t App_SystemCanDebugLog(void)
{
    const AppLogContext_t *p_logContext;

    p_logContext = App_LogGetContext();
    return ((g_appSystemContext.logReady == APP_TRUE) &&
            (p_logContext != NULL) &&
            (p_logContext->initialized == APP_TRUE)) ? APP_TRUE : APP_FALSE;
}

static const char *App_SystemBuildWakeupFlagString(uint32_t wakeFlags)
{
    static char wakeFlagString[96];
    uint8_t first;

    if (wakeFlags == WAKEUP_FLAG_NONE)
    {
        return "NONE";
    }

    wakeFlagString[0] = '\0';
    first = APP_TRUE;

#define APP_SYSTEM_APPEND_WAKE_FLAG(name_literal)                \
    do                                                           \
    {                                                            \
        if (first == APP_FALSE)                                  \
        {                                                        \
            (void)strncat(wakeFlagString, "|", sizeof(wakeFlagString) - strlen(wakeFlagString) - 1u); \
        }                                                        \
        (void)strncat(wakeFlagString, (name_literal), sizeof(wakeFlagString) - strlen(wakeFlagString) - 1u); \
        first = APP_FALSE;                                       \
    } while (0)

    if ((wakeFlags & WAKEUP_FLAG_LPTIM1_ARR) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("LPTIM1_ARR"); }
    if ((wakeFlags & WAKEUP_FLAG_LPTIM1_CMP) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("LPTIM1_CMP"); }
    if ((wakeFlags & WAKEUP_FLAG_RTC_ALARM_A) != 0u) { APP_SYSTEM_APPEND_WAKE_FLAG("RTC_ALARM_A"); }
    if ((wakeFlags & WAKEUP_FLAG_RTC_ALARM_B) != 0u) { APP_SYSTEM_APPEND_WAKE_FLAG("RTC_ALARM_B"); }
    if ((wakeFlags & WAKEUP_FLAG_RTC_WUT) != 0u)     { APP_SYSTEM_APPEND_WAKE_FLAG("RTC_WUT"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN0) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI0"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN1) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI1"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN2) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI2"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN3) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI3"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN4) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI4"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN5) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI5"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN6) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI6"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN7) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI7"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN8) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI8"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN9) != 0u)   { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI9"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN10) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI10"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN11) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI11"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN12) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI12"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN13) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI13"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN14) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI14"); }
    if ((wakeFlags & WAKEUP_FLAG_EXTI_PIN15) != 0u)  { APP_SYSTEM_APPEND_WAKE_FLAG("EXTI15"); }

#undef APP_SYSTEM_APPEND_WAKE_FLAG

    return wakeFlagString;
}


static void handle_lptim1_wakeup(uint32_t flags)
{
    if (flags & WAKEUP_FLAG_LPTIM1_ARR) {
        /* Auto-Reload Match: 주기적 타이머 처리 */
        App_SystemHandleLptim1AutoReloadMatchCallback();
    }
    
    if (flags & WAKEUP_FLAG_LPTIM1_CMP) {
        /* Compare Match: 스케줄된 이벤트 처리 */
        // 예: scheduled_event_execute();
    }
}

static void handle_rtc_alarm_wakeup(uint32_t flags)
{
    uint32_t rtcAlarmFlags;
    uint8_t handled = APP_FALSE;

    rtcAlarmFlags = flags & WAKEUP_MASK_RTC_ALARM;
    if (rtcAlarmFlags == 0u)
    {
        return;
    }

    g_appSystemContext.pendingRtcAlarmFlags |= rtcAlarmFlags;
    g_appSystemContext.lastRtcAlarmFlags = rtcAlarmFlags;

    if ((rtcAlarmFlags & WAKEUP_FLAG_RTC_ALARM_A) != 0u)
    {
        g_appSystemContext.rtcAlarmAWakeEventCount++;
        handled = APP_TRUE;
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            APP_LOGI("LP",
                     "RTC Alarm A wake count=%lu",
                     (unsigned long)g_appSystemContext.rtcAlarmAWakeEventCount);
        }
    }

    if ((rtcAlarmFlags & WAKEUP_FLAG_RTC_ALARM_B) != 0u)
    {
        g_appSystemContext.rtcAlarmBWakeEventCount++;
        handled = APP_TRUE;
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            APP_LOGI("LP",
                     "RTC Alarm B wake count=%lu",
                     (unsigned long)g_appSystemContext.rtcAlarmBWakeEventCount);
        }
    }

    if (handled == APP_TRUE)
    {
        App_SystemHandleRtcCallBack();
    }
}

static void handle_rtc_wut_wakeup(void)
{
    g_appSystemContext.lastRtcAlarmFlags = WAKEUP_FLAG_RTC_WUT;
    App_SystemHandleRtcCallBack();
}

static void handle_exti_wakeup(uint32_t flags)
{
    /* 각 핀을 개별적으로 처리 */
    for (uint8_t pin = 0; pin <= 15; pin++) {
        uint32_t pin_flag = (WAKEUP_FLAG_EXTI_PIN0 << pin);
        if (flags & pin_flag) {
            /* 해당 핀 인터럽트 처리 */
            App_SystemHandleExtiCallBack(1u<<pin);
        }
    }
}

void debug_print_wakeup_info(void)
{
    APP_LOGD("SYS", "[DEBUG] source cnt: %d", g_wakeup_ctx.source_count);
    if (g_wakeup_ctx.source_count > 0)
    {
        APP_LOGD("SYS", "[DEBUG] Multiple wakeup sources detected: %d", g_wakeup_ctx.source_count);
        APP_LOGD("SYS", "  Processed flags: 0x%08lX", g_wakeup_ctx.processed_flags);
        APP_LOGD("SYS", "  Raw registers - LPTIM: 0x%08lX, RTC: 0x%08lX, EXTI: 0x%08lX",
                 g_wakeup_ctx.raw_lptim_isr, g_wakeup_ctx.raw_rtc_isr, g_wakeup_ctx.raw_exti_pr);
    }
}

void wakeup_process_all_pending(void)
{
    __DSB();    // Data Synchronization Barrier
    __ISB();    // Instruction Synchronization Barrier

    /* Race Condition 방지: 플래그를 로컬 변수로 안전하게 복사 */
    __disable_irq();
    uint32_t pending_flags = g_wakeup_ctx.pending_flags;
    g_wakeup_ctx.pending_flags = WAKEUP_FLAG_NONE;
    __enable_irq();

    /* 처리할 플래그가 없으면 즉시 반환 */
    if (pending_flags == WAKEUP_FLAG_NONE) {
        return;
    }

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP",
                 "Wake pending flags=0x%08lX(%s) raw_lptim=0x%08lX raw_rtc=0x%08lX raw_exti=0x%08lX",
                 (unsigned long)pending_flags,
                 App_SystemBuildWakeupFlagString(pending_flags),
                 (unsigned long)g_wakeup_ctx.raw_lptim_isr,
                 (unsigned long)g_wakeup_ctx.raw_rtc_isr,
                 (unsigned long)g_wakeup_ctx.raw_exti_pr);
    }

    /* 소스 개수 계산 */
    uint32_t temp = pending_flags;
    g_wakeup_ctx.source_count = 0;
    while (temp) {
        g_wakeup_ctx.source_count += (temp & 1U);
        temp >>= 1;
    }

    /* 모든 소스를 순회하며 처리 (중간에 return 없이 전체 검사) */

    /* 1. LPTIM1 처리 */
    if (pending_flags & WAKEUP_MASK_LPTIM1) {
        handle_lptim1_wakeup(pending_flags & WAKEUP_MASK_LPTIM1);
        g_wakeup_ctx.processed_flags |= (pending_flags & WAKEUP_MASK_LPTIM1);
    }

    /* 2. RTC Alarm 처리 */
    if (pending_flags & WAKEUP_MASK_RTC_ALARM) {
        handle_rtc_alarm_wakeup(pending_flags & WAKEUP_MASK_RTC_ALARM);
        g_wakeup_ctx.processed_flags |= (pending_flags & WAKEUP_MASK_RTC_ALARM);
    }

    /* 3. RTC Wakeup Timer 처리 */
    if (pending_flags & WAKEUP_FLAG_RTC_WUT) {
        handle_rtc_wut_wakeup();
        g_wakeup_ctx.processed_flags |= WAKEUP_FLAG_RTC_WUT;
    }

    /* 4. 모든 EXTI 핀 처리 */
    uint32_t exti_flags = pending_flags & (WAKEUP_MASK_EXTI0_1 | WAKEUP_MASK_EXTI2_3 | WAKEUP_MASK_EXTI4_15);
    if (exti_flags) {
        handle_exti_wakeup(exti_flags);
        g_wakeup_ctx.processed_flags |= exti_flags;
    }

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP",
                 "Wake processed flags=0x%08lX(%s) source_count=%u",
                 (unsigned long)g_wakeup_ctx.processed_flags,
                 App_SystemBuildWakeupFlagString(g_wakeup_ctx.processed_flags),
                 (unsigned int)g_wakeup_ctx.source_count);
    }

    /* 미처리 플래그 확인 */
    uint32_t unhandled = pending_flags & ~g_wakeup_ctx.processed_flags;
    if (unhandled) {
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            APP_LOGE("LP",
                     "Wake unhandled flags=0x%08lX(%s)",
                     (unsigned long)unhandled,
                     App_SystemBuildWakeupFlagString(unhandled));
        }
        /* 예상치 못한 wakeup 소스 처리 또는 오류 처리 */
        Error_Handler();
    }
}

///////////////////////////////////////////////////////

static const char *App_SystemBuildWakeSourceString(uint32_t wakeMask)
{
    uint8_t first;

    if (wakeMask == APP_SYSTEM_WAKE_SRC_NONE)
    {
        return "NONE";
    }

    g_appSystemWakeString[0] = '\0';
    first = APP_TRUE;

#define APP_SYSTEM_APPEND_WAKE(name_literal)                     \
    do                                                           \
    {                                                            \
        if (first == APP_FALSE)                                  \
        {                                                        \
            (void)strncat(g_appSystemWakeString, "|", sizeof(g_appSystemWakeString) - strlen(g_appSystemWakeString) - 1u); \
        }                                                        \
        (void)strncat(g_appSystemWakeString, (name_literal), sizeof(g_appSystemWakeString) - strlen(g_appSystemWakeString) - 1u); \
        first = APP_FALSE;                                       \
    } while (0)

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_NBIOT_RI) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("NBIOT_RI");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_NFC_ED) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("NFC_ED");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_REED) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("REED");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_RTC) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("RTC");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_LPTIM) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("LPTIM");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_DEBUG_DRYRUN) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("DEBUG_DRYRUN");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_UNKNOWN) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("UNKNOWN");
    }

#undef APP_SYSTEM_APPEND_WAKE

    return g_appSystemWakeString;
}

static void App_SystemResetStopQualification(void)
{
    g_appSystemContext.stopQualificationCount = 0u;
}

static AppStatus_t App_SystemRtcOpenBackupDomain(void)
{
    uint32_t timeoutLoops;

    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR, PWR_CR_DBP);

    timeoutLoops = APP_SYSTEM_RTC_TIMEOUT_LOOPS;
    while (((PWR->CR & PWR_CR_DBP) == 0u) && (timeoutLoops > 0u))
    {
        timeoutLoops--;
    }

    APP_RETURN_IF_FALSE((PWR->CR & PWR_CR_DBP) != 0u, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static void App_SystemRtcWriteProtectionDisable(void)
{
    RTC->WPR = APP_SYSTEM_RTC_WPR_KEY1;
    RTC->WPR = APP_SYSTEM_RTC_WPR_KEY2;
}

static void App_SystemRtcWriteProtectionEnable(void)
{
    RTC->WPR = 0xFFu;
}

static AppStatus_t App_SystemRtcWaitFlagSet(uint32_t flagMask)
{
    uint32_t timeoutLoops;

    timeoutLoops = APP_SYSTEM_RTC_TIMEOUT_LOOPS;
    while (((RTC->ISR & flagMask) == 0u) && (timeoutLoops > 0u))
    {
        timeoutLoops--;
    }

    APP_RETURN_IF_FALSE((RTC->ISR & flagMask) != 0u, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRtcInitBase(void)
{
    AppStatus_t status;

    status = App_SystemRtcOpenBackupDomain();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    SET_BIT(RCC->CSR, RCC_CSR_LSEON);
    APP_RETURN_IF_FALSE((__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET), APP_STATUS_INIT_FAILED);

    if (((RCC->CSR & RCC_CSR_RTCSEL) != RCC_CSR_RTCSEL_LSE) || ((RCC->CSR & RCC_CSR_RTCEN) == 0u))
    {
        SET_BIT(RCC->CSR, RCC_CSR_RTCRST);
        CLEAR_BIT(RCC->CSR, RCC_CSR_RTCRST);
        MODIFY_REG(RCC->CSR, RCC_CSR_RTCSEL, RCC_CSR_RTCSEL_LSE);
        SET_BIT(RCC->CSR, RCC_CSR_RTCEN);
    }

    if ((RTC->ISR & RTC_ISR_INITS) == 0u)
    {
        App_SystemRtcWriteProtectionDisable();
        SET_BIT(RTC->ISR, RTC_ISR_INIT);
        status = App_SystemRtcWaitFlagSet(RTC_ISR_INITF);
        if (status != APP_STATUS_OK)
        {
            App_SystemRtcWriteProtectionEnable();
            return status;
        }

        CLEAR_BIT(RTC->CR, RTC_CR_FMT | RTC_CR_WUTE | RTC_CR_WUTIE);
        RTC->PRER = ((APP_RTC_LSE_ASYNC_PREDIV << RTC_PRER_PREDIV_A_Pos) & RTC_PRER_PREDIV_A) |
                    ((APP_RTC_LSE_SYNC_PREDIV << RTC_PRER_PREDIV_S_Pos) & RTC_PRER_PREDIV_S);
        RTC->TR = 0u;
        RTC->DR = (1u << RTC_DR_WDU_Pos) | (1u << RTC_DR_MU_Pos) | (1u << RTC_DR_DU_Pos);
        CLEAR_BIT(RTC->ISR, RTC_ISR_INIT);
        App_SystemRtcWriteProtectionEnable();
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRtcConfigureWakeupTimerSeconds(uint32_t wakeupSeconds,
                                                            uint32_t *pWakeupSeconds)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE(wakeupSeconds != 0u, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(wakeupSeconds <= 0x10000u, APP_STATUS_INVALID_PARAM);

    status = App_SystemRtcInitBase();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    App_SystemRtcWriteProtectionDisable();
    CLEAR_BIT(RTC->CR, RTC_CR_WUTE | RTC_CR_WUTIE);
    status = App_SystemRtcWaitFlagSet(RTC_ISR_WUTWF);
    if (status != APP_STATUS_OK)
    {
        App_SystemRtcWriteProtectionEnable();
        return status;
    }

    CLEAR_BIT(RTC->ISR, RTC_ISR_WUTF);
    EXTI->PR = EXTI_PR_PIF20;
    RTC->WUTR = (wakeupSeconds - 1u) & RTC_WUTR_WUT;
    MODIFY_REG(RTC->CR, RTC_CR_WUCKSEL, RTC_CR_WUCKSEL_2);
    SET_BIT(RTC->CR, RTC_CR_WUTIE | RTC_CR_WUTE);
    App_SystemRtcWriteProtectionEnable();

    if (pWakeupSeconds != NULL)
    {
        *pWakeupSeconds = wakeupSeconds;
    }
    return APP_STATUS_OK;
}

static uint8_t s_deviceOffsetApplied = APP_FALSE;

static AppStatus_t App_SystemRtcConfigureWakeupTimer(uint32_t *pWakeupSeconds)
{
    uint32_t wakeupSeconds;
    uint32_t deviceHash;
    uint32_t jitterSeed;
    uint32_t jitterSeconds;

    wakeupSeconds = (APP_RTC_WAKEUP_PERIOD_MS + 999u) / 1000u;
    if (wakeupSeconds == 0u)
    {
        wakeupSeconds = 1u;
    }

    deviceHash = App_ClockGetDeviceUidHash();

    /* 매 사이클 값이 바뀌도록 RTC subsecond를 섞어 지터 시드 생성 */
    jitterSeed = deviceHash ^ HAL_GetTick() ^ (RTC->SSR);
    jitterSeconds = jitterSeed % (APP_NBIOT_XMIT_JITTER_MAX_SEC + 1u);

    if (s_deviceOffsetApplied == APP_FALSE)
    {
        uint32_t fixedOffset = deviceHash % (APP_NBIOT_XMIT_OFFSET_MAX_SEC + 1u);
        wakeupSeconds += fixedOffset;
        s_deviceOffsetApplied = APP_TRUE;

        APP_LOGI("LP", "RTC WUT initial device offset applied: %lu sec (uidHash=0x%08lX)",
                 (unsigned long)fixedOffset, (unsigned long)deviceHash);
    }

    wakeupSeconds += jitterSeconds;

    return App_SystemRtcConfigureWakeupTimerSeconds(wakeupSeconds, pWakeupSeconds);
}

static AppStatus_t App_SystemRtcConfigureAlarmInternal(const AppDateTime_t *p_dueTime,
                                                       uint32_t alarmId,
                                                       const char *p_label)
{
    RTC_AlarmTypeDef alarm;

    APP_RETURN_IF_FALSE((p_dueTime != NULL) && (p_label != NULL), APP_STATUS_INVALID_PARAM);

    if (App_SystemRtcInitBase() != APP_STATUS_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    (void)memset(&alarm, 0, sizeof(alarm));
    alarm.AlarmTime.Hours = p_dueTime->hour;
    alarm.AlarmTime.Minutes = p_dueTime->minute;
    alarm.AlarmTime.Seconds = p_dueTime->second;
    alarm.AlarmTime.TimeFormat = RTC_HOURFORMAT12_AM;
    alarm.AlarmTime.SubSeconds = 0u;
    alarm.AlarmTime.SecondFraction = 0u;
    alarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    alarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
    alarm.AlarmMask = RTC_ALARMMASK_NONE;
    alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    alarm.AlarmDateWeekDay = p_dueTime->day;
    alarm.Alarm = alarmId;

    if (HAL_RTC_DeactivateAlarm(&hrtc, alarmId) != HAL_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    __HAL_RTC_ALARM_EXTI_CLEAR_FLAG();
    if (alarmId == RTC_ALARM_A)
    {
        __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRAF);
    }
    else
    {
        __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, RTC_FLAG_ALRBF);
    }

    APP_RETURN_IF_FALSE(HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN) == HAL_OK,
                        APP_STATUS_INIT_FAILED);

    APP_LOGI("RTC", "%s set %04u-%02u-%02u %02u:%02u:%02u",
             p_label,
             (unsigned int)p_dueTime->year,
             (unsigned int)p_dueTime->month,
             (unsigned int)p_dueTime->day,
             (unsigned int)p_dueTime->hour,
             (unsigned int)p_dueTime->minute,
             (unsigned int)p_dueTime->second);
    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRtcConfigureAlarmAForMetering(uint8_t *p_configured)
{
    AppDateTime_t dueTime;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_configured != NULL, APP_STATUS_INVALID_PARAM);
    *p_configured = APP_FALSE;

    status = App_FsmGetNextMeterDueTime(&dueTime);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemRtcConfigureAlarmInternal(&dueTime, RTC_ALARM_A, "AlarmA(meter)");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    *p_configured = APP_TRUE;
    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRtcConfigureAlarmBForReporting(uint8_t *p_configured)
{
    AppDateTime_t dueTime;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_configured != NULL, APP_STATUS_INVALID_PARAM);
    *p_configured = APP_FALSE;

    status = App_FsmGetNextTxDueTime(&dueTime);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemRtcConfigureAlarmInternal(&dueTime, RTC_ALARM_B, "AlarmB(tx)");
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    *p_configured = APP_TRUE;
    return APP_STATUS_OK;
}


static void App_SystemRtcDisableWakeupTimer(void)
{
    App_SystemRtcWriteProtectionDisable();
    CLEAR_BIT(RTC->CR, RTC_CR_WUTE | RTC_CR_WUTIE);
    (void)App_SystemRtcWaitFlagSet(RTC_ISR_WUTWF);
    CLEAR_BIT(RTC->ISR, RTC_ISR_WUTF);
    EXTI->PR = EXTI_PR_PIF20;
    App_SystemRtcWriteProtectionEnable();
}

static AppStatus_t App_SystemInitRtcWakeup(void)
{
    AppStatus_t status;

    status = App_SystemRtcInitBase();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    EXTI->IMR |= EXTI_IMR_IM20;
    EXTI->RTSR |= EXTI_RTSR_RT20;
    EXTI->FTSR &= ~EXTI_FTSR_FT20;
    EXTI->PR = EXTI_PR_PIF20;

    HAL_NVIC_SetPriority(RTC_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(RTC_IRQn);

    return APP_STATUS_OK;
}

const char *App_SystemGetLowPowerModeString(void)
{
    switch (g_appSystemContext.lastLowPowerMode)
    {
        case APP_SYSTEM_LP_MODE_SLEEP: return "SLEEP";
        case APP_SYSTEM_LP_MODE_STOP:  return "STOP";
        case APP_SYSTEM_LP_MODE_RUN:
        default:                       return "RUN";
    }
}

const char *App_SystemGetWakeSourceString(void)
{
    return App_SystemBuildWakeSourceString(g_appSystemContext.wakeSourceMask);
}

uint32_t App_SystemGetWakeSourceMask(void)
{
    return g_appSystemContext.wakeSourceMask;
}

uint32_t App_SystemGetPendingRtcAlarmFlags(void)
{
    return g_appSystemContext.pendingRtcAlarmFlags;
}

uint32_t App_SystemConsumeRtcAlarmFlags(void)
{
    uint32_t flags;

    flags = g_appSystemContext.pendingRtcAlarmFlags;
    g_appSystemContext.pendingRtcAlarmFlags = 0u;
    return flags;
}

AppStatus_t App_SystemRequestShortRtcWakeup(uint32_t wakeupSeconds)
{
    APP_RETURN_IF_FALSE((wakeupSeconds > 0u) && (wakeupSeconds <= 0x10000u), APP_STATUS_INVALID_PARAM);

    g_appSystemContext.forcedRtcWakeupSeconds = wakeupSeconds;
    APP_LOGI("RTC", "[[CollisionPolicy]] request deferred tx short wake=%lus", (unsigned long)wakeupSeconds);
    return APP_STATUS_OK;
}

void App_SystemNotifyWakeSource(uint32_t sourceMask)
{
    if (sourceMask == 0u)
    {
        return;
    }

    g_appSystemContext.wakeSourceMask |= sourceMask;
    g_appSystemContext.lastWakeTickMs = HAL_GetTick();
    g_appSystemContext.stopRequested = APP_FALSE;
    App_SystemResetStopQualification();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGD("WAKE",
                       "source=%s mask=0x%08lX tick=%lu",
                       App_SystemBuildWakeSourceString(g_appSystemContext.wakeSourceMask),
                       (unsigned long)g_appSystemContext.wakeSourceMask,
                       (unsigned long)g_appSystemContext.lastWakeTickMs);
    }
#endif
}

static void App_SystemQueueStateCommand(uint8_t nextState)
{
    (void)App_FsmQueueStateFront(nextState, 1u, 0u);
}

static void App_SystemConfigureWakeupInterrupts(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_LPTIM1_CLK_ENABLE();

    HAL_NVIC_SetPriority(LPTIM1_IRQn, 1u, 0);
    HAL_NVIC_EnableIRQ(LPTIM1_IRQn);
    HAL_NVIC_SetPriority(RTC_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(RTC_IRQn);
    HAL_NVIC_SetPriority(EXTI0_1_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
    HAL_NVIC_SetPriority(EXTI2_3_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static AppStatus_t App_SystemValidateHandles(void)
{
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_UART_NBIOT_HANDLE->Instance == LPUART1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_I2C_NFC_HANDLE->Instance == I2C2, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_I2C_AUX_HANDLE->Instance == I2C3, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_ADC_BATTERY_HANDLE->Instance == ADC1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_CRC_HANDLE->Instance == CRC, APP_STATUS_HW_HANDLE_INVALID);
    return APP_STATUS_OK;
}

static void App_SystemApplySafeOutputs(void)
{
    App_HwSetNbiotEnable(GPIO_PIN_RESET);
    App_HwSetNbiotReset(GPIO_PIN_SET);
    App_HwSetChargeBoot0(GPIO_PIN_RESET);
}

static AppStatus_t App_SystemInitLowPowerGpio(void)
{
    AppStatus_t status;

    App_GpioLpGetDefaultConfig(&g_appGpioLpConfig);

    if(APP_BUILD_IS_PRODUCTION == APP_TRUE)
    {
        g_appGpioLpConfig.swdPolicy = APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION;
    }
    else
    {
        g_appGpioLpConfig.swdPolicy = APP_GPIO_LP_SWD_KEEP;
    }

    g_appGpioLpConfig.stopClockDisableMask =
        APP_GPIO_LP_CLK_ADC1 |
        APP_GPIO_LP_CLK_CRC |
        APP_GPIO_LP_CLK_USART1 |
        APP_GPIO_LP_CLK_USART2 |
        APP_GPIO_LP_CLK_LPUART1 |
        APP_GPIO_LP_CLK_I2C2 |
        APP_GPIO_LP_CLK_I2C3 |
        APP_GPIO_LP_CLK_LPTIM1 |
        APP_GPIO_LP_CLK_SYSCFG;

    status = App_GpioLpInit(&g_appGpioLpConfig);
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_GPIO_LP_INIT_FAILED;
    }

    status = App_GpioLpSetNbiotPowered(APP_FALSE);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_GpioLpApplyRunBaseState();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemPrintBootLogs(void)
{
    const AppClockContext_t *p_clockContext;

    p_clockContext = App_ClockGetContext();

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_BANNER APP_DEBUG_CONSOLE_EOL) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
#endif
    APP_RETURN_IF_FALSE(App_LogInit() == APP_STATUS_OK, APP_STATUS_LOG_INIT_FAILED);

    g_appSystemContext.logReady = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_LOG_READY;

    APP_LOGI("SYS", "Boot complete: %s v%s", APP_NAME_STRING, App_SystemGetVersionString());
    APP_LOGI("CLK", "SYS=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu MSI=%lu LSE=%u",
                                 (unsigned long)p_clockContext->sysclkHz,
                                 (unsigned long)p_clockContext->hclkHz,
                                 (unsigned long)p_clockContext->pclk1Hz,
                                 (unsigned long)p_clockContext->pclk2Hz,
                                 (unsigned long)p_clockContext->msiRange,
                                 (unsigned int)p_clockContext->lseReady);
    APP_LOGI("SYS", "Device UID hash=0x%08lX", (unsigned long)App_ClockGetDeviceUidHash());
    APP_LOGI("GPIO", "LP policy ready: SWD=%lu",
                                 (unsigned long)g_appGpioLpConfig.swdPolicy);
    APP_LOGI("RTC", "STOP wake period=%lu ms (%s)",
                                 (unsigned long)APP_RTC_WAKEUP_PERIOD_MS,
                                 (APP_RTC_WAKEUP_PERIOD_MS == 0) ? "Don't work stop":"LSE rtc wakeup");
    APP_LOGI("DBG", "USART1 debug console ready at %lu baud",
                                 (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate);
    APP_LOGI("METER", "USART2 meter ready at %lu baud",
                                 (unsigned long)APP_UART_METER_HANDLE->Init.BaudRate);
    APP_LOGI("NBIoT", "LPUART1 NBIoT ready at %lu baud",
                                 (unsigned long)APP_UART_NBIOT_HANDLE->Init.BaudRate);
    APP_LOGI("NFC", "I2C2 NFC ready at %sKhz",
                                 (((unsigned long)APP_I2C_NFC_HANDLE->Init.Timing == 0x00000708)?"100":"unknown"));
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    APP_RETURN_IF_FALSE(App_DebugConsolePrintPrompt() == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
#endif
    APP_LOGI("SYS", "Boot path complete: clock/log/debug ready");

    return APP_STATUS_OK;
}

#ifdef SUPPORT_SELFTEST
AppStatus_t App_SystemRunBootSelfTest(void)
{
    AppStatus_t status;

    APP_LOGI("SELF", "######################## run selftest");

    status = App_SelfTestInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_SELFTEST_INIT_FAILED;
    }

    App_SelfTestSetNbiotExecutedHint(APP_TRUE);
    status = App_SelfTestRunBootSequence();
    g_appSystemContext.selfTestCompleted = APP_TRUE;
    g_appSystemContext.selfTestStatus = status;
    g_appSystemContext.selfTestFailed = (status == APP_STATUS_OK) ? APP_FALSE : APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_SELFTEST_DONE;

    if (status == APP_STATUS_OK)
    {
        APP_LOGI("SELF", "------ Boot self-test finished without failures");
        return APP_STATUS_OK;
    }

    APP_LOGW("SELF", "!!!!!! Boot self-test completed with one or more failures");

    if (APP_SELFTEST_FAIL_STOPS_BOOT == APP_TRUE)
    {
        return status;
    }

    return APP_STATUS_OK;
}
#endif // SUPPORT_SELFTEST

AppStatus_t App_SystemRunWakeDataCollection(void)
{
#if (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE)
    AppStatus_t status;

    APP_LOGI("SELF", "######################## run wake data collection");

    status = App_SelfTestInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_SELFTEST_INIT_FAILED;
    }

    App_SelfTestSetNbiotExecutedHint(APP_FALSE);
    status = App_SelfTestRunDataCollectionSequence();

    if (status == APP_STATUS_OK)
    {
        APP_LOGI("SELF", "------ Wake data collection finished without failures");
        return APP_STATUS_OK;
    }

    APP_LOGW("SELF", "!!!!!! Wake data collection completed with one or more failures");

    if (APP_SELFTEST_FAIL_STOPS_BOOT == APP_TRUE)
    {
        return status;
    }
#endif

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemInitFsm(void)
{
    AppStatus_t status;

    status = App_MsgqInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_FsmInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.fsmReady = APP_TRUE;
    g_appSystemContext.fsmStatus = APP_STATUS_OK;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_FSM_READY;

    APP_LOGI("FSM", "FSM ready: queue=%u state=%s",
                                 (unsigned int)APP_MSGQ_CAPACITY,
                                 App_FsmGetCurrentStateString());

    return APP_STATUS_OK;
}

static uint64_t rtc_time_before_stop = 0;

static void App_SystemForceAdcFullOffBeforeStop(void)
{
#if (APP_LP_TEST_FORCE_ADC_FULL_OFF_BEFORE_STOP == APP_TRUE)
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();

    sConfig.Rank = ADC_RANK_NONE;
    sConfig.Channel = ADC_CHANNEL_1;
    (void)HAL_ADC_ConfigChannel(APP_ADC_BATTERY_HANDLE, &sConfig);
    sConfig.Channel = ADC_CHANNEL_VREFINT;
    (void)HAL_ADC_ConfigChannel(APP_ADC_BATTERY_HANDLE, &sConfig);
    sConfig.Channel = ADC_CHANNEL_TEMPSENSOR;
    (void)HAL_ADC_ConfigChannel(APP_ADC_BATTERY_HANDLE, &sConfig);

    (void)HAL_ADC_Stop(APP_ADC_BATTERY_HANDLE);
    (void)HAL_ADC_DeInit(APP_ADC_BATTERY_HANDLE);

    CLEAR_BIT(ADC->CCR, ADC_CCR_VREFEN | ADC_CCR_TSEN);

    if ((ADC1->CR & ADC_CR_ADEN) != 0u)
    {
        SET_BIT(ADC1->CR, ADC_CR_ADDIS);
        for (volatile uint32_t wait = 0u; wait < 10000u; wait++)
        {
            if ((ADC1->CR & ADC_CR_ADEN) == 0u)
            {
                break;
            }
        }
    }

    CLEAR_BIT(ADC1->CR, ADC_CR_ADVREGEN);
    __HAL_RCC_ADC1_FORCE_RESET();
    __HAL_RCC_ADC1_RELEASE_RESET();
    __HAL_RCC_ADC1_CLK_DISABLE();
#endif
}

static AppStatus_t App_SystemRestoreAdcAfterWakeFromStop(void)
{
#if (APP_LP_TEST_FORCE_ADC_FULL_OFF_BEFORE_STOP == APP_TRUE)
    ADC_ChannelConfTypeDef sConfig = {0};

    __HAL_RCC_ADC1_CLK_ENABLE();

    APP_ADC_BATTERY_HANDLE->Instance = ADC1;
    APP_ADC_BATTERY_HANDLE->Init.OversamplingMode = DISABLE;
    APP_ADC_BATTERY_HANDLE->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV1;
    APP_ADC_BATTERY_HANDLE->Init.Resolution = ADC_RESOLUTION_12B;
    APP_ADC_BATTERY_HANDLE->Init.SamplingTime = ADC_SAMPLETIME_160CYCLES_5;
    APP_ADC_BATTERY_HANDLE->Init.ScanConvMode = ADC_SCAN_DIRECTION_FORWARD;
    APP_ADC_BATTERY_HANDLE->Init.DataAlign = ADC_DATAALIGN_RIGHT;
    APP_ADC_BATTERY_HANDLE->Init.ContinuousConvMode = DISABLE;
    APP_ADC_BATTERY_HANDLE->Init.DiscontinuousConvMode = DISABLE;
    APP_ADC_BATTERY_HANDLE->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    APP_ADC_BATTERY_HANDLE->Init.ExternalTrigConv = ADC_SOFTWARE_START;
    APP_ADC_BATTERY_HANDLE->Init.DMAContinuousRequests = DISABLE;
    APP_ADC_BATTERY_HANDLE->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    APP_ADC_BATTERY_HANDLE->Init.Overrun = ADC_OVR_DATA_PRESERVED;
    APP_ADC_BATTERY_HANDLE->Init.LowPowerAutoWait = DISABLE;
    APP_ADC_BATTERY_HANDLE->Init.LowPowerFrequencyMode = ENABLE;
    APP_ADC_BATTERY_HANDLE->Init.LowPowerAutoPowerOff = DISABLE;

    if (HAL_ADC_Init(APP_ADC_BATTERY_HANDLE) != HAL_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    sConfig.Channel = ADC_CHANNEL_1;
    sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
    if (HAL_ADC_ConfigChannel(APP_ADC_BATTERY_HANDLE, &sConfig) != HAL_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }
#endif
    return APP_STATUS_OK;
}

#define APP_SYSTEM_NFC_STANDBY_PREP_NONE      (0u)
#define APP_SYSTEM_NFC_STANDBY_PREP_ALREADY   (1u)
#define APP_SYSTEM_NFC_STANDBY_PREP_ENTERED   (2u)

uint8_t App_SystemPrepareNfcStandbyForStop(void)
{
#if (APP_NFC_ENTER_STANDBY_BEFORE_MCU_STOP == APP_TRUE)
    NFC_Result_t nfcRet;

    if (g_nfcTagHandle.state == NFC_STATE_UNINITIALIZED ||
        g_nfcTagHandle.state == NFC_STATE_ERROR)
    {
        APP_LOGW("NFC", "Skip standby prepare: state=%u", (unsigned int)g_nfcTagHandle.state);
        return APP_SYSTEM_NFC_STANDBY_PREP_NONE;
    }

    if (g_nfcTagHandle.state == NFC_STATE_STOP)
    {
        APP_LOGI("NFC", "NTP53321 already in standby state before MCU STOP");
        return APP_SYSTEM_NFC_STANDBY_PREP_ALREADY;
    }

    nfcRet = NFC_NTP53321_EnterStandby(&g_nfcTagHandle);
    if (nfcRet == NFC_RESULT_OK)
    {
        APP_LOGI("NFC", "NTP53321 standby entered before MCU STOP");
        return APP_SYSTEM_NFC_STANDBY_PREP_ENTERED;
    }

    APP_LOGW("NFC", "NTP53321 standby enter failed before MCU STOP ret=%d", (int)nfcRet);
#endif
    return APP_SYSTEM_NFC_STANDBY_PREP_NONE;
}

static void App_SystemRollbackNfcStandbyIfNeeded(uint8_t standbyPrepareState)
{
#if (APP_NFC_ENTER_STANDBY_BEFORE_MCU_STOP == APP_TRUE)
    if (standbyPrepareState == APP_SYSTEM_NFC_STANDBY_PREP_ENTERED)
    {
        (void)NFC_NTP53321_ExitStandby(&g_nfcTagHandle);
    }
#else
    (void)standbyPrepareState;
#endif
}

static void App_SystemRecoverNfcAfterWake(uint8_t standbyPrepareState)
{
#if (APP_NFC_ENTER_STANDBY_BEFORE_MCU_STOP == APP_TRUE)
    if ((standbyPrepareState == APP_SYSTEM_NFC_STANDBY_PREP_ALREADY) ||
        (standbyPrepareState == APP_SYSTEM_NFC_STANDBY_PREP_ENTERED) ||
        (g_nfcTagHandle.state == NFC_STATE_STOP))
    {
        NFC_Result_t nfcRet = NFC_NTP53321_ExitStandby(&g_nfcTagHandle);
        if (nfcRet != NFC_RESULT_OK)
        {
            APP_LOGW("NFC", "ExitStandby after wake failed ret=%d", (int)nfcRet);
        }
        else
        {
            APP_LOGI("NFC", "NTP53321 exited standby after wake");
        }
        return;
    }
#endif

    g_nfcTagHandle.stats.total_sleep_ticks += (HAL_GetTick() - g_nfcTagHandle.sleep_enter_tick);
    g_nfcTagHandle.state = NFC_STATE_ACTIVE;
    (void)standbyPrepareState;
}

static AppStatus_t App_SystemEnterStopMode(void)
{
    AppStatus_t status;
    uint8_t standbyPrepareState = APP_SYSTEM_NFC_STANDBY_PREP_NONE;
    AppStatus_t adcRestoreStatus;
	
    APP_LOGI("LP", "Enter STOP mode%s",
             (g_appSystemContext.stopNoWakeRequested == APP_TRUE) ? " (fatal/no-wake)" : "");

    g_appSystemContext.stopCandidateCount++;

    if (g_appSystemContext.stopNoWakeRequested == APP_TRUE)
    {
        App_SystemRtcDisableWakeupTimer();
        g_appSystemContext.oldWakeSourceMask &= ~(APP_SYSTEM_WAKE_SRC_RTC | APP_SYSTEM_WAKE_SRC_LPTIM);
        APP_LOGW("LP", "fatal no-wake STOP: periodic RTC/LPTIM wake disabled");
    }
    else if( (g_appSystemContext.oldWakeSourceMask == APP_SYSTEM_WAKE_SRC_NONE) ||
             (g_appSystemContext.oldWakeSourceMask & APP_SYSTEM_WAKE_SRC_RTC) )
    {
        uint8_t alarmAConfigured = APP_FALSE;
        uint8_t alarmBConfigured = APP_FALSE;
        uint8_t forcedShortConfigured = APP_FALSE;
        uint32_t wakeupSeconds = 0;
        uint32_t forcedWakeupSeconds = g_appSystemContext.forcedRtcWakeupSeconds;

        g_appSystemContext.oldWakeSourceMask &= ~APP_SYSTEM_WAKE_SRC_RTC;

        status = App_SystemRtcConfigureAlarmAForMetering(&alarmAConfigured);
        if ((status != APP_STATUS_OK) && (status != APP_STATUS_NOT_INITIALIZED))
        {
            APP_LOGW("LP", "AlarmA configure failed -> fallback to WUT (status=%ld)", (long)status);
            alarmAConfigured = APP_FALSE;
        }

        status = App_SystemRtcConfigureAlarmBForReporting(&alarmBConfigured);
        if ((status != APP_STATUS_OK) && (status != APP_STATUS_NOT_INITIALIZED))
        {
            APP_LOGW("LP", "AlarmB configure failed -> fallback to WUT (status=%ld)", (long)status);
            alarmBConfigured = APP_FALSE;
        }

        if (forcedWakeupSeconds > 0u)
        {
            status = App_SystemRtcConfigureWakeupTimerSeconds(forcedWakeupSeconds, &wakeupSeconds);
            if (status == APP_STATUS_OK)
            {
                forcedShortConfigured = APP_TRUE;
                g_appSystemContext.forcedRtcWakeupSeconds = 0u;
                APP_LOGI("RTC", "STOP deferred short WUT:%lus kept with alarms A=%u B=%u",
                         (unsigned long)wakeupSeconds,
                         (unsigned int)alarmAConfigured,
                         (unsigned int)alarmBConfigured);
            }
            else
            {
                APP_LOGW("RTC", "Deferred short WUT configure failed (status=%ld)", (long)status);
            }
        }

        if (((alarmAConfigured == APP_TRUE) || (alarmBConfigured == APP_TRUE)) &&
            (forcedShortConfigured != APP_TRUE))
        {
            App_SystemRtcDisableWakeupTimer();
            APP_LOGI("RTC", "STOP scheduled alarms: A=%u B=%u (WUT fallback disabled)",
                     (unsigned int)alarmAConfigured,
                     (unsigned int)alarmBConfigured);
        }
        else if ((alarmAConfigured == APP_TRUE) || (alarmBConfigured == APP_TRUE))
        {
            APP_LOGI("RTC", "STOP scheduled alarms: A=%u B=%u with deferred short WUT=%lus",
                     (unsigned int)alarmAConfigured,
                     (unsigned int)alarmBConfigured,
                     (unsigned long)wakeupSeconds);
        }
        else if (forcedShortConfigured == APP_TRUE)
        {
            APP_LOGI("RTC", "STOP deferred short WUT only:%lus", (unsigned long)wakeupSeconds);
        }
        else
        {
            APP_LOGD("LP", "RTCConfigureWakeupTimer fallback");
            status = App_SystemRtcConfigureWakeupTimer(&wakeupSeconds);
            if (status != APP_STATUS_OK)
            {
                APP_LOGE("LP", "RtcConfigureWakeupTimer");
                return status;
            }

            APP_LOGI("RTC", "STOP periodic fallback:%ds", wakeupSeconds);
        }
    }
    else if(g_appSystemContext.oldWakeSourceMask & APP_SYSTEM_WAKE_SRC_LPTIM)
    {
        float fTemp = 0.0;
        uint32_t wakeupSeconds = 0;
        uint32_t lptim1_prescaler = (APP_SYSTEM_LPTIM1_PRESCALER == LPTIM_PRESCALER_DIV32) ? 32:
                                    ((APP_SYSTEM_LPTIM1_PRESCALER == LPTIM_PRESCALER_DIV64) ? 64: 128);
        fTemp = (((APP_SYSTEM_LPTIM1_ARR+1) * lptim1_prescaler)/32768) + 0.5;
        wakeupSeconds = (uint32_t)fTemp;
        g_appSystemContext.oldWakeSourceMask &= ~APP_SYSTEM_WAKE_SRC_LPTIM;
        APP_LOGI("LPTIM", "STOP periodic:%ds", wakeupSeconds);
    }

    standbyPrepareState = App_SystemPrepareNfcStandbyForStop();

    status = App_SystemPrepareForStop();
    if (status != APP_STATUS_OK)
    {
        App_SystemRollbackNfcStandbyIfNeeded(standbyPrepareState);
        APP_LOGE("LP", "systemPrepareForStop");
        return status;
    }

    App_SystemForceAdcFullOffBeforeStop();

    g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_STOP;
    g_appSystemContext.lastStopEntryTickMs = HAL_GetTick();
    g_appSystemContext.wakeSourceMask = APP_SYSTEM_WAKE_SRC_NONE;

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        // this don't print because stopped external interface.
        APP_LOGI("LP", "STOP candidate=%lu qual=%u idle=%lu sleep=%lu stop=%lu dryrun=%u",
                       (unsigned long)g_appSystemContext.stopCandidateCount,
                       (unsigned int)g_appSystemContext.stopQualificationCount,
                       (unsigned long)g_appSystemContext.idleCounter,
                       (unsigned long)g_appSystemContext.sleepEntryCount,
                       (unsigned long)g_appSystemContext.stopEntryCount,
                       (unsigned int)APP_LP_STOP_DEBUG_DRY_RUN);
    }
#endif

#if (APP_LP_STOP_DEBUG_DRY_RUN == APP_TRUE)
    g_appSystemContext.stopDryRunCount++;
    App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_DEBUG_DRYRUN);
    status = App_SystemRecoverFromStop();
    if (status != APP_STATUS_OK)
    {
        App_SystemRollbackNfcStandbyIfNeeded(standbyPrepareState);
        return status;
    }
    App_SystemRecoverNfcAfterWake(standbyPrepareState);
	
	adcRestoreStatus = App_SystemRestoreAdcAfterWakeFromStop();
    if (adcRestoreStatus != APP_STATUS_OK)
    {
        APP_LOGE("LP", "ADC restore after STOP dry-run failed: status=%lu", (unsigned long)adcRestoreStatus);
        return adcRestoreStatus;
    }
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        // this don't print because stopped external interface.
        APP_LOGW("LP", "STOP dry-run only: candidate=%lu dryrun=%lu wake=%s",
                       (unsigned long)g_appSystemContext.stopCandidateCount,
                       (unsigned long)g_appSystemContext.stopDryRunCount,
                       App_SystemGetWakeSourceString());
    }
#endif
    return APP_STATUS_OK;
#else

    /* Stop 진입 직전 WWDG 최대 충전 */
    HAL_WWDG_Refresh(APP_WWDG_HANDLE);

    // nfc prepare
    g_nfcTagHandle.sleep_enter_tick = HAL_GetTick();
#if (APP_NFC_ENTER_STANDBY_BEFORE_MCU_STOP != APP_TRUE)
    g_nfcTagHandle.state            = NFC_STATE_STOP;
#endif

    // Stop 진입 전 RTC 시간 저장
    rtc_time_before_stop = RTC_GetTimeMs();

    __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc, RTC_FLAG_WUTF);
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP",
                 "STOP enter prep: rtc_before=%llu old_wake_mask=0x%08lX pending_flags=0x%08lX",
                 (unsigned long long)rtc_time_before_stop,
                 (unsigned long)g_appSystemContext.oldWakeSourceMask,
                 (unsigned long)g_wakeup_ctx.pending_flags);
    }

    HAL_PWREx_EnableUltraLowPower();   /* VREFINT off in Stop */
    HAL_PWREx_EnableFastWakeUp();      /* VREFINT 안정 대기 skip */
    __HAL_FLASH_SLEEP_POWERDOWN_ENABLE();

    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /*
    * ---- Entry STOP mode
    */

    status = App_ClockRecoverAfterStop();
    HAL_ResumeTick();
    if (status != APP_STATUS_OK)
    {
        return status;
    }
    status = App_SystemRecoverFromStop();
    if (status != APP_STATUS_OK)
    {
        App_SystemRollbackNfcStandbyIfNeeded(standbyPrepareState);
        return status;
    }

    App_SystemRecoverNfcAfterWake(standbyPrepareState);

    adcRestoreStatus = App_SystemRestoreAdcAfterWakeFromStop();
    if (adcRestoreStatus != APP_STATUS_OK)
    {
        APP_LOGE("LP", "ADC restore after STOP wake failed: status=%lu", (unsigned long)adcRestoreStatus);
        return adcRestoreStatus;
    }

    if (g_appSystemContext.stopNoWakeRequested == APP_TRUE)
    {
        APP_LOGW("LP", "fatal no-wake STOP woke unexpectedly -> re-enter STOP");
        g_appSystemContext.stopRequested = APP_TRUE;
        g_appSystemContext.stopQualificationCount = APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT;
        return APP_STATUS_OK;
    }

    // Wake-up 후 경과 시간 계산 및 보정
    uint64_t rtc_time_after = RTC_GetTimeMs() + 100; //100msec tunnning
    uint32_t elapsed_ms = CalcElapsedMs(rtc_time_before_stop, rtc_time_after);
    
    // 전역 offset에 누락된 시간 누적
    g_tick_offset += elapsed_ms;

    wakeup_process_all_pending();

    /* Wakeup 직후 WWDG 즉시 Refresh (안전 마진 확보) */
    HAL_WWDG_Refresh(APP_WWDG_HANDLE);

#ifdef DEBUG
    debug_print_wakeup_info();
#endif
    g_appSystemContext.oldWakeSourceMask |= g_appSystemContext.wakeSourceMask;

    if (g_appSystemContext.wakeSourceMask == APP_SYSTEM_WAKE_SRC_NONE)
    {
        App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_UNKNOWN);
    }

    g_appSystemContext.stopEntryCount++;

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP", "STOP exit wake=%s, mask=%x count=%lu tick=%lu",
                       App_SystemGetWakeSourceString(),
                       g_appSystemContext.wakeSourceMask,
                       (unsigned long)g_appSystemContext.stopEntryCount,
                       (unsigned long)g_appSystemContext.lastWakeTickMs);
    }

    return APP_STATUS_OK;
#endif
}

static void App_SystemHandleIdle(void)
{
    AppStatus_t status;
    const AppFsmSummary_t *p_fsmSummary;

    p_fsmSummary = App_FsmGetSummary();
    g_appSystemContext.idleCounter++;

    if (g_appSystemContext.stopRequested == APP_TRUE)
    {
        if (g_appSystemContext.stopQualificationCount < 0xFFu)
        {
            if (APP_RTC_WAKEUP_PERIOD_MS > 0)
            {
                g_appSystemContext.stopQualificationCount++;
            }
        }

#ifdef DEBUG
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            APP_LOGD("LP",
                           "STOP qualify: step=%u/%u decision=%s idle=%lu dispatch=%lu",
                           (unsigned int)g_appSystemContext.stopQualificationCount,
                           (unsigned int)APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT,
                           App_FsmGetDecisionString(),
                           (unsigned long)g_appSystemContext.idleCounter,
                           (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u));
        }
#endif

        if (g_appSystemContext.stopQualificationCount >= APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT)
        {
            APP_LOGW("LP",
                     "STOP handoff: stop=%u no_wake=%u qualify=%u decision=%s idle=%lu dispatch=%lu",
                     (unsigned int)g_appSystemContext.stopRequested,
                     (unsigned int)g_appSystemContext.stopNoWakeRequested,
                     (unsigned int)g_appSystemContext.stopQualificationCount,
                     App_FsmGetDecisionString(),
                     (unsigned long)g_appSystemContext.idleCounter,
                     (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u));

            g_appSystemContext.stopRequested = APP_FALSE;

            App_SystemResetStopQualification();
            status = App_SystemEnterStopMode();
            if (status != APP_STATUS_OK)
            {
                g_appSystemContext.fsmStatus = status;
                App_ErrorRecord(status, __FILE__, __LINE__);
                APP_LOGE("LP", "STOP entry/exit failed: status=%lu", (unsigned long)status);
            }
            return;
        }
    }
    else
    {
        App_SystemResetStopQualification();
    }

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGD("SYS",
                       "Entering idle path: mode=%s stop_req=%u idle=%lu dispatch=%lu",
                       (APP_FSM_USE_WFI_IDLE == APP_TRUE) ? "WFI" : "delay",
                       (unsigned int)g_appSystemContext.stopRequested,
                       (unsigned long)g_appSystemContext.idleCounter,
                       (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u));
    }
#endif

    if (APP_FSM_USE_WFI_IDLE == APP_TRUE)
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_SLEEP;
        g_appSystemContext.lastSleepEntryTickMs = HAL_GetTick();
        g_appSystemContext.sleepEntryCount++;
        __WFI();
    }
    else
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;
        HAL_Delay(APP_FSM_IDLE_DELAY_MS);
    }
}

void App_SystemHandleLptim1AutoReloadMatchCallback(void)
{
    if (g_appSystemContext.stopNoWakeRequested == APP_TRUE)
    {
        APP_LOGW("LP", "Ignore LPTIM wake in fatal no-wake STOP");
        return;
    }

    g_appSystemContext.lptimWakeEventCount++;
    App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_LPTIM);
    App_SystemQueueStateCommand(APP_FSM_STATE_LPTIM_WAKE_SERVICE);

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP",
                 "Wake source callback: LPTIM count=%lu raw_isr=0x%08lX pending=0x%08lX",
                 (unsigned long)g_appSystemContext.lptimWakeEventCount,
                 (unsigned long)g_wakeup_ctx.raw_lptim_isr,
                 (unsigned long)g_wakeup_ctx.pending_flags);
    }
}

void App_SystemHandleRtcCallBack(void)
{
    if (g_appSystemContext.stopNoWakeRequested == APP_TRUE)
    {
        APP_LOGW("LP", "Ignore RTC wake in fatal no-wake STOP");
        return;
    }

    g_appSystemContext.rtcWakeEventCount++;
    App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_RTC);
    App_SystemQueueStateCommand(APP_FSM_STATE_RTC_WAKE_SERVICE);
    App_SystemQueueStateCommand(APP_FSM_STATE_WATCHDOG_FEED);

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP",
                 "Wake source callback: RTC count=%lu raw_rtc=0x%08lX pending=0x%08lX alarm_flags=0x%08lX",
                 (unsigned long)g_appSystemContext.rtcWakeEventCount,
                 (unsigned long)g_wakeup_ctx.raw_rtc_isr,
                 (unsigned long)g_wakeup_ctx.pending_flags,
                 (unsigned long)g_appSystemContext.pendingRtcAlarmFlags);
    }
}

void App_SystemHandleExtiCallBack(uint16_t GPIO_Pin)
{
    if (g_appSystemContext.stopNoWakeRequested == APP_TRUE)
    {
        APP_LOGW("LP", "Ignore EXTI wake pinmask=0x%04X in fatal no-wake STOP", (unsigned int)GPIO_Pin);
        return;
    }

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGI("LP",
                 "Wake source callback: EXTI pinmask=0x%04X raw_exti=0x%08lX pending=0x%08lX",
                 (unsigned int)GPIO_Pin,
                 (unsigned long)g_wakeup_ctx.raw_exti_pr,
                 (unsigned long)g_wakeup_ctx.pending_flags);
    }

    switch (GPIO_Pin)
    {
        case NFC_ED_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_NFC_ED);
            App_SystemQueueStateCommand(APP_FSM_STATE_NFC_WAIT_EVENT);
            break;

        default:
            if (App_SystemCanDebugLog() == APP_TRUE)
            {
                APP_LOGW("LP", "Unhandled EXTI wake pinmask=0x%04X", (unsigned int)GPIO_Pin);
            }
            break;
    }
}

AppStatus_t App_SystemInit(void)
{
    AppStatus_t status;
    const AppClockContext_t *clockContext;

    (void)memset(&g_appSystemContext, 0, sizeof(g_appSystemContext));
    App_ErrorInit();

    g_appSystemContext.bootStage = APP_BOOT_STAGE_HAL_READY;
    g_appSystemContext.selfTestStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSystemContext.fsmStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;

    APP_RETURN_IF_FALSE(App_ClockIsInitialized() == APP_TRUE, APP_STATUS_CLOCK_NOT_INITIALIZED);

    clockContext = App_ClockGetContext();
    APP_RETURN_IF_FALSE(clockContext->initialized == APP_TRUE, APP_STATUS_CLOCK_NOT_INITIALIZED);

    g_appSystemContext.bootSysClockHz = clockContext->sysclkHz;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_CLOCK_READY;

    status = App_SystemValidateHandles();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.bootStage = APP_BOOT_STAGE_PERIPH_READY;
    App_SystemApplySafeOutputs();
    App_SystemConfigureWakeupInterrupts();

    status = App_SystemInitRtcWakeup();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

  #if 0 //kiki TODO del
    //Set default RTC Set
    RTC_SetTime(APP_DEFAULT_RTC_YEAR, APP_DEFAULT_RTC_MONTH, APP_DEFAULT_RTC_DAY, APP_DEFAULT_RTC_HOUR, APP_DEFAULT_RTC_MIN, APP_DEFAULT_RTC_SEC);
#endif

    status = App_SystemInitLowPowerGpio();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_MeterStorageInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_DeviceConfigInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_MeterServerOptionsInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.bootStage = APP_BOOT_STAGE_GPIO_LP_READY;

    status = App_DualBootInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_DebugConsoleInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_DEBUG_INIT_FAILED;
    }

    g_appSystemContext.debugReady = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_DEBUG_READY;

    status = App_SystemPrintBootLogs();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    (void)App_DualBootConfirmSlot2();
    APP_LOGI("BOOT",
               "running image=%s(%lu) state=%s active=%lu pending=%lu",
               App_DualBootGetCurrentSlotName(),
               (unsigned long)App_DualBootGetCurrentSlotId(),
               App_DualBootGetBootStateString(),
               (unsigned long)App_DualBootGetInfo()->activeSlot,
               (unsigned long)App_DualBootGetInfo()->pendingSlot);

    // booting info
    Print_BootInfo(&g_boot_info);

    App_DeviceConfigInfo();
    App_MeterServerOptionsInfo();
    App_ReservedEepromInfo();
    App_MeterStorageInfo();

    App_NBIoTAtInit();
    App_MeterInit();
    App_NfcInit();

    status = App_SystemInitFsm();
    if (status != APP_STATUS_OK)
    {
        g_appSystemContext.fsmStatus = status;
        return status;
    }

    g_appSystemContext.initialized = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_APP_READY;

    APP_LOGI("SYS", "Initial attach/selftest/report and wake report routines delegated to FSM");

#ifdef DEBUG
    APP_LOGD("SYS", "Application ready: boot=%lu/%lu stop_req=%u",
                                 (unsigned long)g_appSystemContext.bootStage,
                                 (unsigned long)APP_BOOT_STAGE_APP_READY,
                                 (unsigned int)g_appSystemContext.stopRequested);
#endif


    return APP_STATUS_OK;
}

void App_SystemProcess(void)
{
    AppStatus_t status;
    const AppFsmSummary_t *p_fsmSummary;

    if (g_appSystemContext.initialized != APP_TRUE)
    {
        App_ErrorRecord(APP_STATUS_NOT_INITIALIZED, __FILE__, __LINE__);
        App_ErrorTrap();
    }

    status = App_FsmRun();
    g_appSystemContext.fsmStatus = status;
    if (status != APP_STATUS_OK)
    {
        App_ErrorRecord(status, __FILE__, __LINE__);
#ifdef DEBUG
        APP_LOGE("SYS", "FSM run failed: status=%lu", (unsigned long)status);
#endif
    }

    p_fsmSummary = App_FsmGetSummary();
    if (((p_fsmSummary != NULL) && (p_fsmSummary->lastLoopDispatchCount == 0u)) ||
        (g_appSystemContext.stopRequested == APP_TRUE))
    {
#ifdef DEBUG
        if ((p_fsmSummary != NULL) &&
            (p_fsmSummary->lastLoopDispatchCount != 0u) &&
            (g_appSystemContext.stopRequested == APP_TRUE) &&
            (App_SystemCanDebugLog() == APP_TRUE))
        {
            APP_LOGD("LP",
                           "idle gate forced: dispatch=%lu decision=%s stop_req=%u",
                           (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                           App_FsmGetDecisionString(),
                           (unsigned int)g_appSystemContext.stopRequested);
        }
#endif
        App_SystemHandleIdle();
    }
    else
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;
        App_SystemResetStopQualification();
    }

    App_DualBootService();
    g_appSystemContext.loopCounter++;
}

AppStatus_t App_SystemOnBeforeStopEnter(void)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    status = App_GpioLpOnBeforeStopEnter();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGD("LP", "before STOP status=%lu", (unsigned long)status);
    }
#endif
    return status;
}

AppStatus_t App_SystemOnAfterStopExit(void)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    status = App_GpioLpOnAfterStopExit();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        APP_LOGD("LP", "after STOP status=%lu wake=%s", (unsigned long)status, App_SystemGetWakeSourceString());
    }
#endif
    return status;
}

AppStatus_t App_SystemPrepareForStop(void)
{
    return App_SystemOnBeforeStopEnter();
}

AppStatus_t App_SystemRecoverFromStop(void)
{
    return App_SystemOnAfterStopExit();
}

AppStatus_t App_SystemSetNbiotPowered(uint8_t powered)
{
    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    return App_GpioLpSetNbiotPowered(powered);
}

AppStatus_t App_SystemRequestLowPower(uint8_t allowStop)
{
    uint8_t previousRequest;
#ifdef DEBUG
    const AppFsmSummary_t *p_fsmSummary;
#endif

    APP_RETURN_IF_FALSE((allowStop == APP_FALSE) || (allowStop == APP_TRUE), APP_STATUS_INVALID_PARAM);

    previousRequest = g_appSystemContext.stopRequested;
    g_appSystemContext.stopRequested = allowStop;

#ifdef DEBUG
    APP_LOGD("LP",
             "STOP request update: prev=%u new=%u no_wake=%u caller_tick=%lu",
             (unsigned int)previousRequest,
             (unsigned int)allowStop,
             (unsigned int)g_appSystemContext.stopNoWakeRequested,
             (unsigned long)HAL_GetTick());
#endif

    if (allowStop == APP_TRUE)
    {
        g_appSystemContext.lastStopRequestTickMs = HAL_GetTick();
    }
    else
    {
        /* 중요:
         * 일반 STOP 취소는 stopRequested만 해제한다.
         * stopNoWakeRequested는 여기서 건드리지 않는다.
         * no-wake 정책은 App_SystemRequestLowPowerNoWake()만 관리한다.
         */
        App_SystemResetStopQualification();
    }

#ifdef DEBUG
    p_fsmSummary = App_FsmGetSummary();
    if ((previousRequest != allowStop) && (App_SystemCanDebugLog() == APP_TRUE))
    {
        APP_LOGD("LP",
                 "stop_request=%u decision=%s dispatch=%lu qualify=%u no_wake=%u",
                 (unsigned int)allowStop,
                 App_FsmGetDecisionString(),
                 (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u),
                 (unsigned int)g_appSystemContext.stopQualificationCount,
                 (unsigned int)g_appSystemContext.stopNoWakeRequested);
    }
#endif
    return APP_STATUS_OK;
}

AppStatus_t App_SystemRequestLowPowerNoWake(uint8_t allowStopNoWake)
{
    APP_RETURN_IF_FALSE((allowStopNoWake == APP_FALSE) || (allowStopNoWake == APP_TRUE), APP_STATUS_INVALID_PARAM);

    APP_LOGW("LP",
             "STOP no-wake request: new=%u before stop=%u",
             (unsigned int)allowStopNoWake,
             (unsigned int)g_appSystemContext.stopRequested);

    if (allowStopNoWake == APP_TRUE)
    {
        g_appSystemContext.stopNoWakeRequested = APP_TRUE;
        return App_SystemRequestLowPower(APP_TRUE);
    }

    /* no-wake 해제는 전용 경로에서만 명시적으로 수행 */
    g_appSystemContext.stopNoWakeRequested = APP_FALSE;
    return App_SystemRequestLowPower(APP_FALSE);
}

const AppSystemContext_t *App_SystemGetContext(void)
{
    return &g_appSystemContext;
}

const char *App_SystemGetVersionString(void)
{
    return g_appVersionString;
}

void Get_BootInfo(BootInfo_t *pBootInfo)
{
    /* 리셋 플래그 저장 (클리어 전에) */
    pBootInfo->reset_flags = RCC->CSR;

    /* 백업 레지스터 읽기 */
    pBootInfo->bkp0 = RTC->BKP0R;
    pBootInfo->bkp1 = RTC->BKP1R;
    pBootInfo->bkp2 = RTC->BKP2R;
    pBootInfo->bkp3 = RTC->BKP3R;
    pBootInfo->bkp4 = RTC->BKP4R;

    /* === 3) 리셋 플래그 클리어 === */
    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* === 4) BKP 레지스터를 정상 부팅 상태로 마킹 === */
    RTC->BKP0R = NORMAL_BOOT_MAGIC;
    RTC->BKP1R = 0x00000000;
    RTC->BKP2R = 0x00000000;
    RTC->BKP3R = 0x00000000;
    RTC->BKP4R = 0x00000000;
}

static void Print_BootInfo(BootInfo_t *pBootInfo)
{
    APP_LOGI("BOOT", "========================================");
    APP_LOGI("BOOT", "  STM32L073 Boot Information");
    APP_LOGI("BOOT", "========================================");

    /* --- 리셋 원인 표시 --- */
    APP_LOGI("BOOT", "[Reset Flags] RCC_CSR = 0x%08lX",
           (unsigned long)pBootInfo->reset_flags);

    if (pBootInfo->reset_flags & RCC_CSR_LPWRRSTF)
        APP_LOGI("BOOT", "  - Low Power Reset");
    if (pBootInfo->reset_flags & RCC_CSR_WWDGRSTF)
        APP_LOGI("BOOT", "  - *** WWDG Reset ***");
    if (pBootInfo->reset_flags & RCC_CSR_IWDGRSTF)
        APP_LOGI("BOOT", "  - IWDG Reset");
    if (pBootInfo->reset_flags & RCC_CSR_SFTRSTF)
        APP_LOGI("BOOT", "  - Software Reset");
    if (pBootInfo->reset_flags & RCC_CSR_PORRSTF)
        APP_LOGI("BOOT", "  - Power-On Reset (POR/PDR)");
    if (pBootInfo->reset_flags & RCC_CSR_PINRSTF)
        APP_LOGI("BOOT", "  - NRST Pin Reset");
    if (pBootInfo->reset_flags & RCC_CSR_OBLRSTF)
        APP_LOGI("BOOT", "  - Option Byte Loader Reset");

    /* --- 백업 레지스터 내용 표시 --- */
    APP_LOGI("BOOT", "[Backup Registers]");
    APP_LOGI("BOOT", "  BKP0R = 0x%08lX", (unsigned long)pBootInfo->bkp0);
    APP_LOGI("BOOT", "  BKP1R = 0x%08lX", (unsigned long)pBootInfo->bkp1);
    APP_LOGI("BOOT", "  BKP2R = 0x%08lX", (unsigned long)pBootInfo->bkp2);
    APP_LOGI("BOOT", "  BKP3R = 0x%08lX", (unsigned long)pBootInfo->bkp3);
    APP_LOGI("BOOT", "  BKP4R = 0x%08lX", (unsigned long)pBootInfo->bkp4);

    /* BKP0R 의미 해석 */
    switch (pBootInfo->bkp0)
    {
        case WWDG_RESET_MAGIC:
            APP_LOGI("BOOT", "  --> Abnormal termination by WWDG!");
            break;
        case NORMAL_BOOT_MAGIC:
            APP_LOGI("BOOT", "  --> Reset from previous normal operation");
            break;
        case 0x00000000:
            APP_LOGI("BOOT", "  --> Backup domain cleared (POR or VBAT lost)");
            break;
        default:
            APP_LOGI("BOOT", "  --> Unknown value");
            break;
    }

    APP_LOGI("BOOT", "  BKP1R = 0x%08lX  --> Emergency saved data",
           (unsigned long)pBootInfo->bkp1);

    /* --- Additional WWDG diagnosis --- */
    if ((pBootInfo->reset_flags & RCC_CSR_WWDGRSTF) &&
        (pBootInfo->bkp0 == WWDG_RESET_MAGIC))
    {
        APP_LOGI("BOOT", "[WWDG Diagnosis]");
        APP_LOGI("BOOT", "  EWI emergency handler executed successfully.");
        APP_LOGI("BOOT", "  Last status code: 0x%08lX",
               (unsigned long)pBootInfo->bkp1);
    }
    else if (pBootInfo->reset_flags & RCC_CSR_WWDGRSTF)
    {
        APP_LOGI("BOOT", "[WWDG Diagnosis]");
        APP_LOGI("BOOT", "  WWDG reset occurred, but no EWI handler trace found.");
        APP_LOGI("BOOT", "  --> EWI was disabled or reset occurred before callback.");
    }

    APP_LOGI("BOOT", "========================================");
}


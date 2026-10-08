#include <stdio.h>
#include <string.h>
#include "app_log.h"
#include "app_comm_param.h"
#include "app_build_config.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"

static AppCommSignalState_t  g_appCommSignalState  = APP_COMM_SIGNAL_STRONG;
static AppCommSuccessState_t g_appCommSuccessState = APP_COMM_SUCCESS_HIGH;
static uint8_t g_appCommGoodStreak = 0u;
static uint8_t g_appCommBadStreak  = 0u;
static uint8_t g_appCommAdaptiveLoaded = APP_FALSE;
static AppCommAdaptivePolicy_t g_appCommAdaptivePolicy;

#define APP_COMM_DEVICECFG_MARKER_IDX              (5u)
#define APP_COMM_DEVICECFG_STRONG_REPORT_IDX       (6u)
#define APP_COMM_DEVICECFG_WEAK_REPORT_IDX         (7u)
#define APP_COMM_DEVICECFG_STRONG_TX_MIN_IDX       (8u)
#define APP_COMM_DEVICECFG_WEAK_TX_MIN_IDX         (9u)
#define APP_COMM_DEVICECFG_STRONG_METER_MIN_IDX    (10u)
#define APP_COMM_DEVICECFG_WEAK_METER_MIN_IDX      (11u)
#define APP_COMM_DEVICECFG_FLAGS_IDX               (12u)
#define APP_COMM_DEVICECFG_MARKER_VALUE            (0xC7u)

static uint8_t app_comm_is_supported_reporting_period(uint8_t hours)
{
    return (uint8_t)((hours > 0u) && (App_MeterServerOptionsIsPeriodSupported(hours) == APP_TRUE));
}

static uint32_t app_comm_minutes_to_ms(uint8_t minutes)
{
    return (minutes == 0u) ? 0u : ((uint32_t)minutes * 60000u);
}

void App_CommAdaptivePolicySetDefaults(AppCommAdaptivePolicy_t *p_policy)
{
    if (p_policy == NULL) { return; }

    (void)memset(p_policy, 0, sizeof(*p_policy));
    p_policy->strongReportingHours = (uint8_t)APP_COMM_ADAPTIVE_STRONG_REPORT_HOURS;
    p_policy->weakReportingHours   = (uint8_t)APP_COMM_ADAPTIVE_WEAK_REPORT_HOURS;
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    p_policy->strongTxPeriodMin    = (uint8_t)APP_COMM_ADAPTIVE_STRONG_TX_PERIOD_MIN;
    p_policy->weakTxPeriodMin      = (uint8_t)APP_COMM_ADAPTIVE_WEAK_TX_PERIOD_MIN;
    p_policy->strongMeterPeriodMin = (uint8_t)APP_COMM_ADAPTIVE_STRONG_METER_PERIOD_MIN;
    p_policy->weakMeterPeriodMin   = (uint8_t)APP_COMM_ADAPTIVE_WEAK_METER_PERIOD_MIN;
    p_policy->flags                = (uint8_t)(APP_COMM_ADAPTIVE_BYPASS_NIGHT_ONLY_DEFAULT
                                               ? APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY : 0u);
#endif
}

static void app_comm_adaptive_policy_sanitize(AppCommAdaptivePolicy_t *p_policy)
{
    if (p_policy == NULL) { return; }

    if (app_comm_is_supported_reporting_period(p_policy->strongReportingHours) == APP_FALSE)
    {
        p_policy->strongReportingHours = (uint8_t)APP_COMM_ADAPTIVE_STRONG_REPORT_HOURS;
    }
    if (app_comm_is_supported_reporting_period(p_policy->weakReportingHours) == APP_FALSE)
    {
        p_policy->weakReportingHours = (uint8_t)APP_COMM_ADAPTIVE_WEAK_REPORT_HOURS;
    }
}

static void app_comm_adaptive_policy_decode_from_device(const AppDeviceConfig_t *p_device,
                                                        AppCommAdaptivePolicy_t *p_policy)
{
    App_CommAdaptivePolicySetDefaults(p_policy);
    if ((p_device == NULL) || (p_policy == NULL)) { return; }

    if (p_device->reserved[APP_COMM_DEVICECFG_MARKER_IDX] != APP_COMM_DEVICECFG_MARKER_VALUE)
    {
        return;
    }

    p_policy->strongReportingHours = p_device->reserved[APP_COMM_DEVICECFG_STRONG_REPORT_IDX];
    p_policy->weakReportingHours   = p_device->reserved[APP_COMM_DEVICECFG_WEAK_REPORT_IDX];
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    p_policy->strongTxPeriodMin    = p_device->reserved[APP_COMM_DEVICECFG_STRONG_TX_MIN_IDX];
    p_policy->weakTxPeriodMin      = p_device->reserved[APP_COMM_DEVICECFG_WEAK_TX_MIN_IDX];
    p_policy->strongMeterPeriodMin = p_device->reserved[APP_COMM_DEVICECFG_STRONG_METER_MIN_IDX];
    p_policy->weakMeterPeriodMin   = p_device->reserved[APP_COMM_DEVICECFG_WEAK_METER_MIN_IDX];
    p_policy->flags                = p_device->reserved[APP_COMM_DEVICECFG_FLAGS_IDX];
#endif

    app_comm_adaptive_policy_sanitize(p_policy);
}

static void app_comm_adaptive_policy_ensure_loaded(void)
{
    AppDeviceConfig_t cfg;
    AppStatus_t status;

    if (g_appCommAdaptiveLoaded == APP_TRUE) { return; }

    App_CommAdaptivePolicySetDefaults(&g_appCommAdaptivePolicy);
    status = App_DeviceConfigLoad(&cfg);
    if ((status == APP_STATUS_OK) || (status == APP_STATUS_NOT_INITIALIZED))
    {
        app_comm_adaptive_policy_decode_from_device(&cfg, &g_appCommAdaptivePolicy);
    }
    g_appCommAdaptiveLoaded = APP_TRUE;
}

void App_CommAdaptivePolicyGet(AppCommAdaptivePolicy_t *p_policy)
{
    app_comm_adaptive_policy_ensure_loaded();
    if (p_policy != NULL)
    {
        *p_policy = g_appCommAdaptivePolicy;
    }
}

void App_CommAdaptivePolicySet(const AppCommAdaptivePolicy_t *p_policy)
{
    app_comm_adaptive_policy_ensure_loaded();
    if (p_policy == NULL) { return; }

    g_appCommAdaptivePolicy = *p_policy;
    app_comm_adaptive_policy_sanitize(&g_appCommAdaptivePolicy);
    g_appCommAdaptiveLoaded = APP_TRUE;
}

uint32_t App_CommGetTxPeriodOverrideMs(void)
{
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    uint8_t minutes;

    app_comm_adaptive_policy_ensure_loaded();
    minutes = (g_appCommSignalState == APP_COMM_SIGNAL_WEAK)
              ? g_appCommAdaptivePolicy.weakTxPeriodMin
              : g_appCommAdaptivePolicy.strongTxPeriodMin;
    return app_comm_minutes_to_ms(minutes);
#else
    return 0u;
#endif
}

uint32_t App_CommGetMgmtTxPeriodOverrideMs(void)
{
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    return App_CommGetTxPeriodOverrideMs();
#else
    return 0u;
#endif
}

uint32_t App_CommGetMeterPeriodOverrideMs(void)
{
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    uint8_t minutes;

    app_comm_adaptive_policy_ensure_loaded();
    minutes = (g_appCommSignalState == APP_COMM_SIGNAL_WEAK)
              ? g_appCommAdaptivePolicy.weakMeterPeriodMin
              : g_appCommAdaptivePolicy.strongMeterPeriodMin;
    return app_comm_minutes_to_ms(minutes);
#else
    return 0u;
#endif
}

uint8_t App_CommShouldBypassNightOnlyGate(void)
{
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    app_comm_adaptive_policy_ensure_loaded();
    return (uint8_t)((g_appCommAdaptivePolicy.flags & APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY) != 0u);
#else
    return APP_FALSE;
#endif
}

void App_CommSignalMeasureAndUpdate(const AppBc95Quality_t *p_quality)
{
    AppCommSignalState_t prevState;

    if ((p_quality == NULL) || (p_quality->valid != APP_TRUE)) { return; }

    prevState = g_appCommSignalState;

    if ((p_quality->rssiDbm <= APP_COMM_RSSI_WEAK_DBM) ||
        (p_quality->rsrpDbm <= APP_COMM_RSRP_WEAK_DBM))
    {
        g_appCommSignalState = APP_COMM_SIGNAL_WEAK;
    }
    else
    {
        g_appCommSignalState = APP_COMM_SIGNAL_STRONG;
    }

    APP_LOGN("COMM", "signal state %s -> %s (RSSI=%d RSRP=%d)",
         (prevState == APP_COMM_SIGNAL_STRONG) ? "STRONG" : "WEAK",
         (g_appCommSignalState == APP_COMM_SIGNAL_STRONG) ? "STRONG" : "WEAK",
         p_quality->rssiDbm, p_quality->rsrpDbm);
}

void App_CommSuccessUpdate(uint8_t attemptUsedIdx, uint8_t allFailed)
{
    uint8_t isGood = ((allFailed != APP_TRUE) && (attemptUsedIdx <= APP_COMM_GOOD_MAX_ATTEMPT_IDX))
                     ? APP_TRUE : APP_FALSE;

    if (isGood == APP_TRUE)
    {
        g_appCommGoodStreak = (g_appCommGoodStreak < 0xFFu) ? (g_appCommGoodStreak + 1u) : 0xFFu;
        g_appCommBadStreak = 0u;
        if (g_appCommGoodStreak >= APP_COMM_SUCCESS_STREAK_NEEDED)
        {
            g_appCommSuccessState = APP_COMM_SUCCESS_HIGH;
        }
    }
    else
    {
        g_appCommBadStreak = (g_appCommBadStreak < 0xFFu) ? (g_appCommBadStreak + 1u) : 0xFFu;
        g_appCommGoodStreak = 0u;
        if (g_appCommBadStreak >= APP_COMM_SUCCESS_STREAK_NEEDED)
        {
            g_appCommSuccessState = APP_COMM_SUCCESS_LOW;
        }
    }

    APP_LOGN("COMM", "success update: attemptIdx=%u allFailed=%u goodStreak=%u/%u badStreak=%u/%u",
         attemptUsedIdx, allFailed, g_appCommGoodStreak, APP_COMM_SUCCESS_STREAK_NEEDED,
         g_appCommBadStreak, APP_COMM_SUCCESS_STREAK_NEEDED);
}

AppCommSignalState_t  App_CommGetSignalState(void)  { return g_appCommSignalState; }
AppCommSuccessState_t App_CommGetSuccessState(void) { return g_appCommSuccessState; }

void App_CommParamRecompose(void)
{
    AppMeterServerFormatOptions_t opt;
    AppCommAdaptivePolicy_t policy;
    uint8_t targetReportingHours;
    uint8_t targetNightOnly;

    app_comm_adaptive_policy_ensure_loaded();
    policy = g_appCommAdaptivePolicy;

    if (App_MeterServerOptionsLoad(&opt) == APP_STATUS_NOT_INITIALIZED)
    {
        /* defaults already applied by Load(); fall through to recompute below */
    }

    targetReportingHours = (g_appCommSignalState == APP_COMM_SIGNAL_WEAK)
                           ? policy.weakReportingHours
                           : policy.strongReportingHours;

#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    if ((policy.flags & APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY) != 0u)
    {
        targetNightOnly = APP_FALSE;
    }
    else
#endif
    {
        targetNightOnly = (g_appCommSuccessState == APP_COMM_SUCCESS_LOW) ? APP_TRUE : APP_FALSE;
    }

    if ((opt.reportingPeriodHours != targetReportingHours) ||
        (opt.nightOnly != targetNightOnly))
    {
        App_MeterServerOptionsSetTxPeriods(&opt,
                                           opt.meteringPeriodHours,
                                           targetReportingHours,
                                           opt.managementReportingPeriodHours);
        App_MeterServerOptionsSetNightOnly(&opt,
                                           targetNightOnly,
                                           (uint8_t)APP_COMM_NIGHT_START_HOUR,
                                           (uint8_t)APP_COMM_NIGHT_END_HOUR);
        (void)App_MeterServerOptionsUpdate(&opt);
    }

    APP_LOGN("COMM", "param applied: reportPeriod=%uh nightOnly=%u txOv=%lu meterOv=%lu",
         opt.reportingPeriodHours,
         opt.nightOnly,
         (unsigned long)App_CommGetTxPeriodOverrideMs(),
         (unsigned long)App_CommGetMeterPeriodOverrideMs());
}

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE) && (APP_EPC_ACTIVE_TEST_ID == 7u)
typedef struct
{
    const char *name;
    int16_t     rssiDbm;
    int16_t     rsrpDbm;
    uint8_t     attemptIdx;
    uint8_t     allFailed;
    uint8_t     repeatCount;
    uint8_t     expWeak;         /* 1=WEAK 기대, 0=STRONG 기대 */
    uint8_t     expLow;          /* 1=LOW 기대,  0=HIGH 기대 */
    uint8_t     expPeriodHours;
    uint8_t     expNightOnly;
} AppTest7Case_t;

#define APP_TEST7_CASE_COUNT (5u)

static const AppTest7Case_t g_appTest7CaseTable[APP_TEST7_CASE_COUNT] =
{
    /* name              rssi  rsrp  attemptIdx allFailed repeat expWeak expLow expPeriodH expNightOnly */
    { "A_normal",         -70,  -75,  0,         APP_FALSE, 2,    APP_FALSE, APP_FALSE, APP_POLICY_DEFAULT_REPORTING_PERIOD_HOURS, APP_FALSE },
    { "B_weakSignal",    -102, -112,  0,         APP_FALSE, 2,    APP_TRUE,  APP_FALSE, APP_COMM_PERIOD_WEAK_HOURS,                APP_FALSE },
    { "C_lowSuccess",     -70,  -75,  3,         APP_TRUE,  2,    APP_FALSE, APP_TRUE,  APP_POLICY_DEFAULT_REPORTING_PERIOD_HOURS, APP_TRUE  },
    { "D_combo_weak_low",-102, -112,  3,         APP_TRUE,  2,    APP_TRUE,  APP_TRUE,  APP_COMM_PERIOD_WEAK_HOURS,                APP_TRUE  },
    { "E_restoreToA",     -70,  -75,  0,         APP_FALSE, 2,    APP_FALSE, APP_FALSE, APP_POLICY_DEFAULT_REPORTING_PERIOD_HOURS, APP_FALSE },
};

static uint8_t  s_appTest7CaseIndex   = 0u;
static uint8_t  s_appTest7RepeatIndex = 0u;
static uint32_t s_appTest7Seq         = 0u;
static uint16_t s_appTest7PassCount   = 0u;
static uint16_t s_appTest7FailCount   = 0u;
static uint32_t s_appTest7LapCount    = 1u; 

/* 케이스 진입 시 success streak를 목표 패턴으로 선반영(워밍업)하여
   HIGH<->LOW 전환 디바운스(APP_COMM_SUCCESS_STREAK_NEEDED)로 인한
   1회차 오탐(FAIL)을 방지한다. 실제 카운트/로그에는 포함되지 않는다. */
static void App_Test7WarmupSuccessStreak(const AppTest7Case_t *pCase)
{
    uint8_t warmupCount = (APP_COMM_SUCCESS_STREAK_NEEDED > 0u)
                          ? (uint8_t)(APP_COMM_SUCCESS_STREAK_NEEDED - 1u)
                          : 0u;
    uint8_t i;

    for (i = 0u; i < warmupCount; i++)
    {
        App_CommSuccessUpdate(pCase->attemptIdx, pCase->allFailed);
    }
}

void App_CommTest7RunCycle(void)
{
    const AppTest7Case_t   *pCase;
    AppBc95Quality_t        fakeQuality;
    AppMeterServerFormatOptions_t opt;
    uint8_t  actualWeak, actualLow;
    uint8_t  pass;
    char     mismatchBuf[96];
    uint32_t mismatchOffset = 0u;

    if (s_appTest7CaseIndex >= APP_TEST7_CASE_COUNT)
    {
        APP_LOGN("TEST7", "test=TEST7,summary=ALL_COMPLETE,total=%u,pass=%u,fail=%u",
                 (unsigned)s_appTest7Seq,
                 (unsigned)s_appTest7PassCount,
                 (unsigned)s_appTest7FailCount);
        return;
    }

    pCase = &g_appTest7CaseTable[s_appTest7CaseIndex];
    s_appTest7Seq++;

    /* 1) 가짜 품질값 주입 */
    (void)memset(&fakeQuality, 0, sizeof(fakeQuality));
    fakeQuality.valid   = APP_TRUE;
    fakeQuality.rssiDbm = pCase->rssiDbm;
    fakeQuality.rsrpDbm = pCase->rsrpDbm;

    App_CommSignalMeasureAndUpdate(&fakeQuality);

    /* 1.5) 새 케이스로 진입하는 시점(첫 반복)에만 streak 워밍업 수행 */
    if (s_appTest7RepeatIndex == 0u)
    {
        App_Test7WarmupSuccessStreak(pCase);
    }

    /* 2) 가짜 전송 결과 주입 (실제 판정용 카운트 호출) */
    App_CommSuccessUpdate(pCase->attemptIdx, pCase->allFailed);

    /* 3) 파라미터 재계산 및 적용값 로드 */
    App_CommParamRecompose();
    (void)memset(&opt, 0, sizeof(opt));
    (void)App_MeterServerOptionsLoad(&opt);

    /* 4) 판정 */
    actualWeak = (g_appCommSignalState  == APP_COMM_SIGNAL_WEAK)  ? APP_TRUE : APP_FALSE;
    actualLow  = (g_appCommSuccessState == APP_COMM_SUCCESS_LOW)  ? APP_TRUE : APP_FALSE;

    mismatchBuf[0] = '\0';
    pass = APP_TRUE;

    if (actualWeak != pCase->expWeak)
    {
        pass = APP_FALSE;
        mismatchOffset += (uint32_t)snprintf(&mismatchBuf[mismatchOffset],
                                              sizeof(mismatchBuf) - mismatchOffset,
                                              "signal(exp=%s,act=%s);",
                                              pCase->expWeak ? "WEAK" : "STRONG",
                                              actualWeak     ? "WEAK" : "STRONG");
    }
    if (actualLow != pCase->expLow)
    {
        pass = APP_FALSE;
        mismatchOffset += (uint32_t)snprintf(&mismatchBuf[mismatchOffset],
                                              sizeof(mismatchBuf) - mismatchOffset,
                                              "success(exp=%s,act=%s);",
                                              pCase->expLow ? "LOW" : "HIGH",
                                              actualLow     ? "LOW" : "HIGH");
    }
    if (opt.reportingPeriodHours != pCase->expPeriodHours)
    {
        pass = APP_FALSE;
        mismatchOffset += (uint32_t)snprintf(&mismatchBuf[mismatchOffset],
                                              sizeof(mismatchBuf) - mismatchOffset,
                                              "periodH(exp=%u,act=%u);",
                                              (unsigned)pCase->expPeriodHours,
                                              (unsigned)opt.reportingPeriodHours);
    }
    if (opt.nightOnly != pCase->expNightOnly)
    {
        pass = APP_FALSE;
        mismatchOffset += (uint32_t)snprintf(&mismatchBuf[mismatchOffset],
                                              sizeof(mismatchBuf) - mismatchOffset,
                                              "nightOnly(exp=%u,act=%u);",
                                              (unsigned)pCase->expNightOnly,
                                              (unsigned)opt.nightOnly);
    }

    if (pass == APP_TRUE) { s_appTest7PassCount++; } else { s_appTest7FailCount++; }

    /* 5) key=value 로그를 2줄로 분할 출력 (버퍼 오버런 방지) */
    APP_LOGN("TEST7",
        "test=TEST7,seq=%lu,case=%s,rssi=%d,rsrp=%d,attemptIdx=%u,allFailed=%u,"
        "signal=%s,success=%s,periodH=%u,nightOnly=%u",
        (unsigned long)s_appTest7Seq,
        pCase->name,
        (int)pCase->rssiDbm,
        (int)pCase->rsrpDbm,
        (unsigned)pCase->attemptIdx,
        (unsigned)pCase->allFailed,
        actualWeak ? "WEAK" : "STRONG",
        actualLow  ? "LOW"  : "HIGH",
        (unsigned)opt.reportingPeriodHours,
        (unsigned)opt.nightOnly);

    APP_LOGN("TEST7",
        "test=TEST7,seq=%lu,expSignal=%s,expSuccess=%s,expPeriodH=%u,expNightOnly=%u,"
        "result=%s%s%s",
        (unsigned long)s_appTest7Seq,
        pCase->expWeak ? "WEAK" : "STRONG",
        pCase->expLow  ? "LOW"  : "HIGH",
        (unsigned)pCase->expPeriodHours,
        (unsigned)pCase->expNightOnly,
        pass ? "PASS" : "FAIL",
        pass ? "" : ",mismatch=",
        pass ? "" : mismatchBuf);

    /* 6) 다음 회차/케이스 인덱스 진행 */
    s_appTest7RepeatIndex++;
    if (s_appTest7RepeatIndex >= pCase->repeatCount)
    {
        s_appTest7RepeatIndex = 0u;
        s_appTest7CaseIndex++;

        if (s_appTest7CaseIndex >= APP_TEST7_CASE_COUNT)
        {
            APP_LOGN("TEST7",
                     "test=TEST7,summary=LAP_COMPLETE,lap=%lu,total=%lu,pass=%u,fail=%u",
                     (unsigned long)s_appTest7LapCount,
                     (unsigned long)s_appTest7Seq,
                     (unsigned)s_appTest7PassCount,
                     (unsigned)s_appTest7FailCount);

            /* E_restoreToA 이후에도 계속 순환: A_normal부터 다시 시작 */
            s_appTest7CaseIndex = 0u;
            s_appTest7LapCount++;
        }
    }
}

#endif /* APP_EPC_TEST_MODE_ENABLE && APP_EPC_ACTIVE_TEST_ID == 7 */

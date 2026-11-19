#include <stdio.h>
#include <string.h>
#include "app_log.h"
#include "app_comm_param.h"
#include "stm32l0xx_hal.h"
#include "app_build_config.h"
#include "app_meter_server_format.h"
#include "app_meter_storage.h"

static AppCommSignalState_t  g_appCommSignalState  = APP_COMM_SIGNAL_STRONG;
static AppCommSuccessState_t g_appCommSuccessState = APP_COMM_SUCCESS_HIGH;
static uint8_t g_appCommGoodStreak = 0u;
static uint8_t g_appCommBadStreak  = 0u;
static uint8_t g_appCommAdaptiveLoaded = APP_FALSE;
static uint8_t g_appCommServiceCostScore = 0u;   /* RAM-only: EEPROM layout 변경 없음 */
static uint8_t g_appCommExtremeWeakLatched = APP_FALSE;
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

static void app_comm_cost_score_add(uint8_t delta)
{
    uint16_t next = (uint16_t)g_appCommServiceCostScore + (uint16_t)delta;
    g_appCommServiceCostScore = (next > APP_COMM_COST_SCORE_MAX)
                              ? (uint8_t)APP_COMM_COST_SCORE_MAX
                              : (uint8_t)next;
}

static void app_comm_cost_score_sub(uint8_t delta)
{
    g_appCommServiceCostScore = (g_appCommServiceCostScore > delta)
                              ? (uint8_t)(g_appCommServiceCostScore - delta)
                              : 0u;
}

static uint8_t app_comm_is_effective_weak(void)
{
    return (uint8_t)((g_appCommSignalState == APP_COMM_SIGNAL_WEAK) || (g_appCommServiceCostScore >= APP_COMM_COST_WEAK_ENTER_THRESHOLD));
}

/* ---- [REPORT-LADDER] 약전계 리포트 주기 사다리 ----
 * 기준 주기(정책 weak 슬롯 -- 서버/NFC 설정 가능, 기본 4h)에서 시작해 지원
 * 주기 집합의 다음 값을 따라 상승한다: 4h -> 6h -> 8h -> 12h -> 24h
 * (기준이 1h이면 2h부터, 3h/72h는 제외).
 * 단계별 진행 규칙(세션 결과 기준):
 *   - 성공 1회      -> 즉시 기준 주기로 복귀(사다리/nightOnly 해제)
 *   - 실패 + 약전계 -> 1회 시도 후 다음 단계
 *   - 실패 + 강전계 -> 3회 시도 후 다음 단계(망/서버 일시 장애 여유)
 * 최상단(24h) 도달 시 nightOnly 윈도우 적용(야간 1회로 수렴), 24h 주기 세션
 * 7회(=7일) 유지 후 사다리를 리셋해 기준 주기부터 재동작한다.
 * 상태는 RAM 런타임만 사용(MCU 리셋 시 기준 주기로 초기화). mgmt/QREP 주기는
 * 변경하지 않는다. */
#define APP_COMM_LADDER_TOP_HOURS        (24u)
#define APP_COMM_LADDER_TOP_HOLD_RESET   (7u)   /* 최상단 유지 세션 수 = 7일 */
#define APP_COMM_LADDER_WEAK_TRIES       (1u)   /* 약전계: 단계당 시도 1회 */
#define APP_COMM_LADDER_STRONG_TRIES     (3u)   /* 강전계: 단계당 시도 3회 */

static const uint8_t s_commLadderHours[] = { 2u, 4u, 6u, 8u, 12u, 24u };
static uint8_t s_commLadderActive  = 0u;  /* 1: 기준 주기에서 사다리 상승 중 */
static uint8_t s_commLadderCur     = 0u;  /* 현재 약전계 목표 주기(활성 시) */
static uint8_t s_commLadderTries   = 0u;  /* 현재 단계에서 완료된 세션 수 */
static uint8_t s_commLadderTopHold = 0u;  /* 최상단 유지 세션 수 */

static uint8_t app_comm_ladder_base_hours(void)
{
    uint8_t base = g_appCommAdaptivePolicy.weakReportingHours;
    if ((base == 0u) ||
        (App_MeterServerOptionsIsPeriodSupported(base) == APP_FALSE))
    {
        base = (uint8_t)APP_COMM_ADAPTIVE_WEAK_REPORT_HOURS;
    }
    return base;
}

static uint8_t app_comm_ladder_next_after(uint8_t hours)
{
    uint8_t i;
    for (i = 0u; i < (uint8_t)(sizeof(s_commLadderHours) / sizeof(s_commLadderHours[0])); i++)
    {
        if (s_commLadderHours[i] > hours)
        {
            return s_commLadderHours[i];
        }
    }
    return 0u;  /* 다음 단계 없음(최상단) */
}

static uint8_t app_comm_ladder_weak_hours(void)
{
    uint8_t base = app_comm_ladder_base_hours();
    if (s_commLadderActive == 0u) { return base; }
    if (s_commLadderCur < base)   { return base; }  /* 서버가 기준을 올리면 따라감 */
    return s_commLadderCur;
}

void App_CommLadderReset(void)
{
    s_commLadderActive  = 0u;
    s_commLadderCur     = 0u;
    s_commLadderTries   = 0u;
    s_commLadderTopHold = 0u;
}

void App_CommLadderOnServiceResult2(uint8_t allFailed)
{
    App_CommLadderOnServiceResult(allFailed);
}

void App_CommLadderOnServiceResult(uint8_t allFailed)
{
    uint8_t base = app_comm_ladder_base_hours();
    uint8_t weak;
    uint8_t maxTries;

    if (allFailed != APP_TRUE)
    {
        /* 성공 1회 -> 기준 주기로 즉시 복귀 */
        if (s_commLadderActive != 0u)
        {
            APP_LOGN("COMM", "[[CommLadder]] success -> ladder reset (base %uh)",
                     (unsigned)base);
        }
        App_CommLadderReset();
        return;
    }

    weak = app_comm_is_effective_weak();
    maxTries = (weak == APP_TRUE) ? APP_COMM_LADDER_WEAK_TRIES : APP_COMM_LADDER_STRONG_TRIES;

    if (s_commLadderTries < (uint8_t)(maxTries - 1u))
    {
        s_commLadderTries++;
        APP_LOGN("COMM", "[[CommLadder]] session failed, retry at %uh (%u/%u, %s)",
                 (unsigned)app_comm_ladder_weak_hours(),
                 (unsigned)s_commLadderTries, (unsigned)maxTries,
                 (weak == APP_TRUE) ? "weak" : "strong");
        return;
    }
    s_commLadderTries = 0u;

    if (s_commLadderActive == 0u)
    {
        uint8_t next = app_comm_ladder_next_after(base);
        if (next == 0u)
        {
            /* 기준이 이미 최상단: 24h 고정 + nightOnly */
            s_commLadderActive = APP_TRUE;
            s_commLadderCur    = APP_COMM_LADDER_TOP_HOURS;
            APP_LOGN("COMM", "[[CommLadder]] base is top -> 24h + night-only");
            return;
        }
        s_commLadderActive = APP_TRUE;
        s_commLadderCur    = next;
        APP_LOGN("COMM", "[[CommLadder]] weak session done -> report period %uh",
                 (unsigned)next);
        if (next == APP_COMM_LADDER_TOP_HOURS)
        {
            APP_LOGN("COMM", "[[CommLadder]] top period 24h -> night-only window on");
        }
        return;
    }

    if (s_commLadderCur >= APP_COMM_LADDER_TOP_HOURS)
    {
        /* 최상단 유지 중: 실패 세션 7회(7일) 경과 시 리셋 */
        s_commLadderTopHold++;
        if (s_commLadderTopHold >= APP_COMM_LADDER_TOP_HOLD_RESET)
        {
            APP_LOGN("COMM", "[[CommLadder]] 24h held 7 days -> MCU reboot (ladder reset, base %uh, night-only off)",
                     (unsigned)base);
            /* [REPORT-LADDER v4] 7일 만료 시 MCU 리부트로 리셋한다.
             * 사다리/nightOnly는 RAM 상태라 리부트 시 자동 초기화(기준 주기),
             * 모뎀도 전원 인가 상태에서 재부팅되어 깨끗한 상태로 재시작한다.
             * 위 로그는 플러시 보장을 위해 리부트 직전 마지막 출력이다. */
            (void)base;
            HAL_NVIC_SystemReset();   /* 반환 없음 */
        }
        else
        {
            APP_LOGN("COMM", "[[CommLadder]] 24h hold %u/%u",
                     (unsigned)s_commLadderTopHold, (unsigned)APP_COMM_LADDER_TOP_HOLD_RESET);
        }
        return;
    }

    {
        uint8_t next = app_comm_ladder_next_after(s_commLadderCur);
        if (next == 0u)
        {
            s_commLadderCur = APP_COMM_LADDER_TOP_HOURS;
        }
        else
        {
            s_commLadderCur = next;
        }
        APP_LOGN("COMM", "[[CommLadder]] weak session done -> report period %uh",
                 (unsigned)s_commLadderCur);
        if (s_commLadderCur == APP_COMM_LADDER_TOP_HOURS)
        {
            APP_LOGN("COMM", "[[CommLadder]] top period 24h -> night-only window on");
        }
    }
}

void App_CommOnPreAttachAbort(const AppBc95Quality_t *p_quality,
                              int16_t csqRssiDbm,
                              uint8_t noCellAfterProbe)
{
    AppBc95Quality_t pseudoQuality;
    uint8_t haveQuality = APP_FALSE;

    if ((p_quality != NULL) && (p_quality->valid == APP_TRUE))
    {
        pseudoQuality = *p_quality;
        haveQuality = APP_TRUE;
    }
    else if (csqRssiDbm < 0)
    {
        (void)memset(&pseudoQuality, 0, sizeof(pseudoQuality));
        pseudoQuality.valid   = APP_TRUE;
        pseudoQuality.rssiDbm = csqRssiDbm;
        pseudoQuality.rsrpDbm = (noCellAfterProbe == APP_TRUE)
                              ? APP_COMM_EXTREME_WEAK_RSRP_DBM
                              : APP_COMM_RSRP_WEAK_DBM;
        pseudoQuality.snrDb   = (noCellAfterProbe == APP_TRUE)
                              ? APP_COMM_EXTREME_WEAK_SNR_DB
                              : 1;
        haveQuality = APP_TRUE;
    }

    if (haveQuality == APP_TRUE)
    {
        App_CommSignalMeasureAndUpdate(&pseudoQuality);
    }

    if (noCellAfterProbe == APP_TRUE)
    {
        g_appCommExtremeWeakLatched = APP_TRUE;
    }

    /*
     * pre-attach abort는 배터리 관점에서
     * "이번 wake에서는 붙여봐야 비싸다"는 강한 신호이므로
     * service all-failed와 유사하게 cost/downshift에 반영한다.
     */
    App_CommSuccessUpdate(0u, APP_TRUE);
    App_CommLadderOnServiceResult(APP_TRUE);   /* abort도 약전계 세션 완료로 사다리 반영 */
    App_CommParamRecompose();

    APP_LOGD("COMM", "pre-attach abort applied: csq=%d noCell=%u signal=%s success=%s cost=%u extremeWeak=%u abort=%u",
             (int)csqRssiDbm,
             (unsigned)noCellAfterProbe,
             (g_appCommSignalState == APP_COMM_SIGNAL_WEAK) ? "WEAK" : "STRONG",
             (g_appCommSuccessState == APP_COMM_SUCCESS_LOW) ? "LOW" : "HIGH",
             (unsigned)g_appCommServiceCostScore,
             (unsigned)g_appCommExtremeWeakLatched,
             (unsigned)App_CommShouldAbortRemainingServerTx());
}

void App_CommAdaptivePolicySetDefaults(AppCommAdaptivePolicy_t *p_policy)
{
    if (p_policy == NULL) { return; }

    (void)memset(p_policy, 0, sizeof(*p_policy));
    p_policy->strongReportingHours = (uint8_t)APP_COMM_ADAPTIVE_STRONG_REPORT_HOURS;
    p_policy->weakReportingHours   = (uint8_t)APP_COMM_ADAPTIVE_WEAK_REPORT_HOURS;
    p_policy->flags                = (uint8_t)(APP_COMM_ADAPTIVE_BYPASS_NIGHT_ONLY_DEFAULT
                                               ? APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY : 0u);
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    p_policy->strongTxPeriodMin    = (uint8_t)APP_COMM_ADAPTIVE_STRONG_TX_PERIOD_MIN;
    p_policy->weakTxPeriodMin      = (uint8_t)APP_COMM_ADAPTIVE_WEAK_TX_PERIOD_MIN;
    p_policy->strongMeterPeriodMin = (uint8_t)APP_COMM_ADAPTIVE_STRONG_METER_PERIOD_MIN;
    p_policy->weakMeterPeriodMin   = (uint8_t)APP_COMM_ADAPTIVE_WEAK_METER_PERIOD_MIN;
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
    p_policy->flags                = p_device->reserved[APP_COMM_DEVICECFG_FLAGS_IDX];
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    p_policy->strongTxPeriodMin    = p_device->reserved[APP_COMM_DEVICECFG_STRONG_TX_MIN_IDX];
    p_policy->weakTxPeriodMin      = p_device->reserved[APP_COMM_DEVICECFG_WEAK_TX_MIN_IDX];
    p_policy->strongMeterPeriodMin = p_device->reserved[APP_COMM_DEVICECFG_STRONG_METER_MIN_IDX];
    p_policy->weakMeterPeriodMin   = p_device->reserved[APP_COMM_DEVICECFG_WEAK_METER_MIN_IDX];
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
    AppDeviceConfig_t cfg;
    AppStatus_t status;

    app_comm_adaptive_policy_ensure_loaded();
    if (p_policy == NULL) { return; }

    g_appCommAdaptivePolicy = *p_policy;
    app_comm_adaptive_policy_sanitize(&g_appCommAdaptivePolicy);
    g_appCommAdaptiveLoaded = APP_TRUE;

    status = App_DeviceConfigLoad(&cfg);
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_NOT_INITIALIZED))
    {
        return;
    }

    cfg.reserved[APP_COMM_DEVICECFG_MARKER_IDX]        = APP_COMM_DEVICECFG_MARKER_VALUE;
    cfg.reserved[APP_COMM_DEVICECFG_STRONG_REPORT_IDX] = g_appCommAdaptivePolicy.strongReportingHours;
    cfg.reserved[APP_COMM_DEVICECFG_WEAK_REPORT_IDX]   = g_appCommAdaptivePolicy.weakReportingHours;
    cfg.reserved[APP_COMM_DEVICECFG_FLAGS_IDX]         = g_appCommAdaptivePolicy.flags;
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    cfg.reserved[APP_COMM_DEVICECFG_STRONG_TX_MIN_IDX]    = g_appCommAdaptivePolicy.strongTxPeriodMin;
    cfg.reserved[APP_COMM_DEVICECFG_WEAK_TX_MIN_IDX]      = g_appCommAdaptivePolicy.weakTxPeriodMin;
    cfg.reserved[APP_COMM_DEVICECFG_STRONG_METER_MIN_IDX] = g_appCommAdaptivePolicy.strongMeterPeriodMin;
    cfg.reserved[APP_COMM_DEVICECFG_WEAK_METER_MIN_IDX]   = g_appCommAdaptivePolicy.weakMeterPeriodMin;
#endif
    (void)App_DeviceConfigSave(&cfg);
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
    app_comm_adaptive_policy_ensure_loaded();
    return (uint8_t)((g_appCommAdaptivePolicy.flags & APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY) != 0u);
}

uint8_t App_CommIsExtremeWeak(void)
{
    return g_appCommExtremeWeakLatched;
}

uint8_t App_CommShouldAbortRemainingServerTx(void)
{
    if (app_comm_is_effective_weak() != APP_TRUE)
    {
        return APP_FALSE;
    }

    return (g_appCommServiceCostScore >= APP_COMM_COST_ABORT_REMAINING_TX_THRESHOLD)
           ? APP_TRUE : APP_FALSE;
}

void App_CommSignalMeasureAndUpdate(const AppBc95Quality_t *p_quality)
{
    AppCommSignalState_t prevState;
    uint8_t weakNow;
    uint8_t strongRecover;
    uint8_t extremeWeakNow;

    if ((p_quality == NULL) || (p_quality->valid != APP_TRUE)) { return; }

    prevState = g_appCommSignalState;

    weakNow = (uint8_t)(((p_quality->rssiDbm <= APP_COMM_RSSI_WEAK_DBM) ||
                         (p_quality->rsrpDbm <= APP_COMM_RSRP_WEAK_DBM)) ? APP_TRUE : APP_FALSE);

    strongRecover = (uint8_t)(((p_quality->rssiDbm > (APP_COMM_RSSI_WEAK_DBM + APP_COMM_SIGNAL_RECOVER_MARGIN_DB)) &&
                               (p_quality->rsrpDbm > (APP_COMM_RSRP_WEAK_DBM + APP_COMM_SIGNAL_RECOVER_MARGIN_DB)))
                               ? APP_TRUE : APP_FALSE);

    extremeWeakNow = (uint8_t)(((p_quality->rsrpDbm <= APP_COMM_EXTREME_WEAK_RSRP_DBM) ||
                                (p_quality->snrDb   <= APP_COMM_EXTREME_WEAK_SNR_DB))
                                ? APP_TRUE : APP_FALSE);
    if (weakNow == APP_TRUE)
    {
        g_appCommSignalState = APP_COMM_SIGNAL_WEAK;
    }
    else if (strongRecover == APP_TRUE)
    {
        g_appCommSignalState = APP_COMM_SIGNAL_STRONG;
    }
    /* 중간 영역에서는 기존 상태 유지: sparse sample 환경에서 flap 방지 */

    if (extremeWeakNow == APP_TRUE)
    {
        g_appCommExtremeWeakLatched = APP_TRUE;
    }
    else if ((strongRecover == APP_TRUE) &&
             (g_appCommServiceCostScore <= APP_COMM_COST_STRONG_EXIT_THRESHOLD))
    {
        g_appCommExtremeWeakLatched = APP_FALSE;
    }
    APP_LOGN("COMM", "[[Signal]] %s -> %s (RSSI=%d RSRP=%d SNR=%d cost=%u extremeWeak=%u)",
         (prevState == APP_COMM_SIGNAL_STRONG) ? "STRONG" : "WEAK",
         (g_appCommSignalState == APP_COMM_SIGNAL_STRONG) ? "STRONG" : "WEAK",
         p_quality->rssiDbm, p_quality->rsrpDbm, p_quality->snrDb,
         (unsigned)g_appCommServiceCostScore,
         (unsigned)g_appCommExtremeWeakLatched);
}

void App_CommApplyServiceSessionCost(const AppCommServiceSessionMetrics_t *p_metrics)
{
    if (p_metrics == NULL) { return; }

    if (p_metrics->attachElapsedMs >= APP_COMM_ATTACH_SLOW_MS)
    {
        app_comm_cost_score_add(APP_COMM_COST_ATTACH_SLOW_PENALTY);
    }

    if ((p_metrics->dnsAttempts > 1u) ||
        (p_metrics->dnsElapsedMs >= APP_COMM_DNS_SLOW_MS))
    {
        app_comm_cost_score_add(APP_COMM_COST_DNS_SLOW_PENALTY);
    }

    if (p_metrics->udpSendAttempts > 1u)
    {
        app_comm_cost_score_add(APP_COMM_COST_UDP_RETRY_PENALTY);
    }

    if (p_metrics->extremeWeak == APP_TRUE)
    {
        g_appCommExtremeWeakLatched = APP_TRUE;
    }
    else if ((g_appCommSignalState == APP_COMM_SIGNAL_STRONG) &&
             (g_appCommServiceCostScore <= APP_COMM_COST_STRONG_EXIT_THRESHOLD))
    {
        g_appCommExtremeWeakLatched = APP_FALSE;
    }

    APP_LOGN("COMM", "[[SessionCost]] attach=%lums dns=%lums dnsTry=%u udpTry=%u extremeWeak=%u => cost=%u abort=%u",
             (unsigned long)p_metrics->attachElapsedMs,
             (unsigned long)p_metrics->dnsElapsedMs,
             (unsigned)p_metrics->dnsAttempts,
             (unsigned)p_metrics->udpSendAttempts,
             (unsigned)p_metrics->extremeWeak,
             (unsigned)g_appCommServiceCostScore,
             (unsigned)App_CommShouldAbortRemainingServerTx());
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

    /*
     * v2 cost-score:
     * - mgmt는 제외, service TX 결과만 사용
     * - 악화는 빠르게, 회복은 느리게
     */
    if (allFailed == APP_TRUE)
    {
        app_comm_cost_score_add(APP_COMM_COST_FAIL_PENALTY);
    }
    else if ((g_appCommSignalState == APP_COMM_SIGNAL_WEAK) &&
             (attemptUsedIdx >= APP_COMM_SERVICE_EXPENSIVE_ATTEMPT_IDX))
    {
        app_comm_cost_score_add(APP_COMM_COST_RETRY_PENALTY);
    }
    else if ((g_appCommSignalState == APP_COMM_SIGNAL_WEAK) ||
             (attemptUsedIdx >= APP_COMM_SERVICE_EXPENSIVE_ATTEMPT_IDX))
    {
        app_comm_cost_score_add(APP_COMM_COST_WEAK_SUCCESS_PENALTY);
    }
    else
    {
        app_comm_cost_score_sub(APP_COMM_COST_GOOD_RECOVERY_REWARD);
    }

    APP_LOGD("COMM", "success update: attemptIdx=%u allFailed=%u goodStreak=%u/%u badStreak=%u/%u cost=%u",
         attemptUsedIdx, allFailed, g_appCommGoodStreak, APP_COMM_SUCCESS_STREAK_NEEDED,
         g_appCommBadStreak, APP_COMM_SUCCESS_STREAK_NEEDED, (unsigned)g_appCommServiceCostScore);
}

AppCommSignalState_t  App_CommGetSignalState(void)  { return g_appCommSignalState; }
AppCommSuccessState_t App_CommGetSuccessState(void) { return g_appCommSuccessState; }

void App_CommParamRecompose(void)
{
    AppMeterServerFormatOptions_t opt;
    AppCommAdaptivePolicy_t policy;
    uint8_t targetReportingHours;
    uint8_t targetNightOnly;
    uint8_t effectiveWeak;

    app_comm_adaptive_policy_ensure_loaded();
    policy = g_appCommAdaptivePolicy;

    if (App_MeterServerOptionsLoad(&opt) == APP_STATUS_NOT_INITIALIZED)
    {
        /* defaults already applied by Load(); fall through to recompute below */
    }

    effectiveWeak = app_comm_is_effective_weak();

    if (effectiveWeak == APP_TRUE)
    {
        /* [REPORT-LADDER] 약전계 지속 시 지원 주기 사다리(2..24h) */
        targetReportingHours = app_comm_ladder_weak_hours();
    }
    else
    {
        targetReportingHours = policy.strongReportingHours;
    }

    if ((policy.flags & APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY) != 0u)
    {
        targetNightOnly = APP_FALSE;
    }
    else
    {
        /* [REPORT-LADDER] nightOnly는 사다리 최상단(24h) 도달부터 적용 */
        targetNightOnly = ((effectiveWeak == APP_TRUE) &&
                           (s_commLadderActive == APP_TRUE) &&
                           (app_comm_ladder_weak_hours() >= APP_COMM_LADDER_TOP_HOURS))
                          ? APP_TRUE : APP_FALSE;
    }

    if ((opt.reportingPeriodHours != targetReportingHours) ||
        (opt.nightOnly != targetNightOnly))
    {
        App_MeterServerOptionsSetTxPeriods(&opt,
                                           opt.meteringPeriodHours,
                                           targetReportingHours);   /* [MGMT-FIXED] adaptive policy에서 mgmt 제외 */
        App_MeterServerOptionsSetNightOnly(&opt,
                                           targetNightOnly,
                                           (uint8_t)APP_COMM_NIGHT_START_HOUR,
                                           (uint8_t)APP_COMM_NIGHT_END_HOUR);
        (void)App_MeterServerOptionsUpdate(&opt);
    }

    APP_LOGD("COMM", "param applied: signal=%s effectiveWeak=%u success=%s cost=%u extremeWeak=%u abort=%u reportPeriod=%uh nightOnly=%u txOv=%lu meterOv=%lu",
         (g_appCommSignalState == APP_COMM_SIGNAL_WEAK) ? "WEAK" : "STRONG",
         (unsigned)effectiveWeak,
         (g_appCommSuccessState == APP_COMM_SUCCESS_LOW) ? "LOW" : "HIGH",
         (unsigned)g_appCommServiceCostScore,
         (unsigned)g_appCommExtremeWeakLatched,
         (unsigned)App_CommShouldAbortRemainingServerTx(),
         opt.reportingPeriodHours, opt.nightOnly,
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
    /* [REPORT-LADDER] TEST7은 사다리 훅을 호출하지 않으므로(워밍업 보호)
     * 기대 주기는 항상 기준(weak 슬롯)값, nightOnly는 최상단 도달 전까지 FALSE */
    { "B_weakSignal",    -102, -112,  0,         APP_FALSE, 2,    APP_TRUE,  APP_FALSE, APP_COMM_PERIOD_WEAK_HOURS,                APP_FALSE },
    { "C_lowSuccess",     -70,  -75,  3,         APP_TRUE,  2,    APP_TRUE,  APP_TRUE,  APP_COMM_PERIOD_WEAK_HOURS,                APP_FALSE },
    { "D_combo_weak_low",-102, -112,  3,         APP_TRUE,  2,    APP_TRUE,  APP_TRUE,  APP_COMM_PERIOD_WEAK_HOURS,                APP_FALSE },
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
        APP_LOGD("TEST7", "test=TEST7,summary=ALL_COMPLETE,total=%u,pass=%u,fail=%u",
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
        App_CommLadderReset();   /* 케이스 간 사다리 상태 독립 */
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
    APP_LOGD("TEST7", "test=TEST7,seq=%lu,case=%s,rssi=%d,rsrp=%d,attemptIdx=%u,allFailed=%u,"
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

    APP_LOGD("TEST7", "test=TEST7,seq=%lu,expSignal=%s,expSuccess=%s,expPeriodH=%u,expNightOnly=%u,"
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
            APP_LOGD("TEST7", "test=TEST7,summary=LAP_COMPLETE,lap=%lu,total=%lu,pass=%u,fail=%u",
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

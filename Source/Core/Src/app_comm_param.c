/* app_comm_param.c */
#include "app_comm_param.h"
#include "app_build_config.h"
#include "app_meter_server_format.h"

static AppCommSignalState_t  g_appCommSignalState  = APP_COMM_SIGNAL_STRONG;
static AppCommSuccessState_t g_appCommSuccessState = APP_COMM_SUCCESS_HIGH;
static uint8_t g_appCommGoodStreak = 0u;
static uint8_t g_appCommBadStreak  = 0u;

void App_CommSignalMeasureAndUpdate(const AppBc95Quality_t *p_quality)
{
    if ((p_quality == NULL) || (p_quality->valid != APP_TRUE)) { return; }

    if ((p_quality->rssiDbm <= APP_COMM_RSSI_ENTER_WEAK_DBM) ||
        (p_quality->rsrpDbm <= APP_COMM_RSRP_ENTER_WEAK_DBM))
    {
        g_appCommSignalState = APP_COMM_SIGNAL_WEAK;
    }
    else if ((p_quality->rssiDbm >= APP_COMM_RSSI_EXIT_WEAK_DBM) &&
             (p_quality->rsrpDbm >= APP_COMM_RSRP_EXIT_WEAK_DBM))
    {
        g_appCommSignalState = APP_COMM_SIGNAL_STRONG;
    }
    /* 데드존이면 이전 상태 유지 (분기 없음) */
}

void App_CommSuccessUpdate(uint8_t attemptUsedIdx, uint8_t allFailed)
{
    uint8_t isGood = ((allFailed != APP_TRUE) && (attemptUsedIdx <= APP_COMM_GOOD_MAX_ATTEMPT_IDX))
                     ? APP_TRUE : APP_FALSE;

    if (isGood == APP_TRUE)
    {
        g_appCommGoodStreak++;
        g_appCommBadStreak = 0u;
        if (g_appCommGoodStreak >= APP_COMM_SUCCESS_STREAK_NEEDED)
        {
            g_appCommSuccessState = APP_COMM_SUCCESS_HIGH;
        }
    }
    else
    {
        g_appCommBadStreak++;
        g_appCommGoodStreak = 0u;
        if (g_appCommBadStreak >= APP_COMM_SUCCESS_STREAK_NEEDED)
        {
            g_appCommSuccessState = APP_COMM_SUCCESS_LOW;
        }
    }
}

AppCommSignalState_t  App_CommGetSignalState(void)  { return g_appCommSignalState; }
AppCommSuccessState_t App_CommGetSuccessState(void) { return g_appCommSuccessState; }

void App_CommParamRecompose(void)
{
    AppMeterServerFormatOptions_t opt;
    uint8_t targetReportingHours;
    uint8_t targetNightOnly;

    if (App_MeterServerOptionsLoad(&opt) == APP_STATUS_NOT_INITIALIZED)
    {
        /* defaults already applied by Load(); fall through to recompute below */
    }

    targetReportingHours = (g_appCommSignalState == APP_COMM_SIGNAL_WEAK)
                           ? (uint8_t)APP_COMM_PERIOD_WEAK_HOURS
                           : (uint8_t)APP_POLICY_DEFAULT_REPORTING_PERIOD_HOURS;

    targetNightOnly = (g_appCommSuccessState == APP_COMM_SUCCESS_LOW) ? APP_TRUE : APP_FALSE;

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
}

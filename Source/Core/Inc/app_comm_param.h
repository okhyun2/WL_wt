/* app_comm_param.h */
#ifndef APP_COMM_PARAM_H
#define APP_COMM_PARAM_H

#include "app_nbiot.h"

typedef enum { APP_COMM_SIGNAL_STRONG = 0, APP_COMM_SIGNAL_WEAK } AppCommSignalState_t;
typedef enum { APP_COMM_SUCCESS_HIGH = 0, APP_COMM_SUCCESS_LOW }  AppCommSuccessState_t;

typedef struct
{
    uint32_t attachElapsedMs;
    uint32_t dnsElapsedMs;
    uint8_t  dnsAttempts;
    uint8_t  udpSendAttempts;
    uint8_t  extremeWeak;
} AppCommServiceSessionMetrics_t;

#define APP_COMM_ADAPTIVE_FLAG_BYPASS_NIGHT_ONLY   (1u << 0)

typedef struct
{
    uint8_t strongReportingHours;
    uint8_t weakReportingHours;
    uint8_t flags;
#if (APP_EPC_TEST_MODE_SIGNAL_WEAK_ENABLE == APP_TRUE)
    uint8_t strongTxPeriodMin;
    uint8_t weakTxPeriodMin;
    uint8_t strongMeterPeriodMin;
    uint8_t weakMeterPeriodMin;
#endif
} AppCommAdaptivePolicy_t;

void App_CommSignalMeasureAndUpdate(const AppBc95Quality_t *p_quality);
void App_CommSuccessUpdate(uint8_t attemptUsedIdx, uint8_t allFailed);
/* [REPORT-LADDER] 약전계 리포트 주기 사다리: 세션 결과 반영 / 강제 리셋(자기검증용) */
void App_CommLadderOnServiceResult(uint8_t allFailed);
/* [REPORT-LADDER v3] platform4(CON) 반영 판정용 래퍼 -- 동작은 동일, 시그니처 구분용 */
void App_CommLadderOnServiceResult2(uint8_t allFailed);
void App_CommLadderReset(void);
AppCommSignalState_t  App_CommGetSignalState(void);
AppCommSuccessState_t App_CommGetSuccessState(void);
void App_CommParamRecompose(void);
void App_CommOnPreAttachAbort(const AppBc95Quality_t *p_quality, int16_t csqRssiDbm, uint8_t noCellAfterProbe);
void App_CommApplyServiceSessionCost(const AppCommServiceSessionMetrics_t *p_metrics);
uint8_t App_CommIsExtremeWeak(void);
uint8_t App_CommShouldAbortRemainingServerTx(void);
void App_CommAdaptivePolicySetDefaults(AppCommAdaptivePolicy_t *p_policy);
void App_CommAdaptivePolicyGet(AppCommAdaptivePolicy_t *p_policy);
void App_CommAdaptivePolicySet(const AppCommAdaptivePolicy_t *p_policy);
uint32_t App_CommGetTxPeriodOverrideMs(void);
uint32_t App_CommGetMeterPeriodOverrideMs(void);
uint8_t App_CommShouldBypassNightOnlyGate(void);

#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE) && (APP_EPC_ACTIVE_TEST_ID == 7u)
void App_CommTest7RunCycle(void);
#endif

#endif

#ifndef APP_FSM_H
#define APP_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"
#include "app_log.h"
#include "app_fsm_state_defs.h"

typedef enum
{
    APP_FSM_COMPONENT_DEBUG = 0,
    APP_FSM_COMPONENT_HOUSEKEEPING,
    APP_FSM_COMPONENT_POWER,
    APP_FSM_COMPONENT_METER,
    APP_FSM_COMPONENT_NFC,
#if 0	//Temp support
    APP_FSM_COMPONENT_AUX,
#endif
    APP_FSM_COMPONENT_NBIOT,
    APP_FSM_COMPONENT_SERVER,
    APP_FSM_COMPONENT_RTC,
    APP_FSM_COMPONENT_LPTIM,
    APP_FSM_COMPONENT_WATCHDOG,
    APP_FSM_COMPONENT_STORAGE,
    APP_FSM_COMPONENT_COUNT
} AppFsmComponentId_t;

typedef enum
{
    APP_FSM_DECISION_BOOT = 0,
    APP_FSM_DECISION_RUN_ACTIVE,
    APP_FSM_DECISION_ALLOW_IDLE,
    APP_FSM_DECISION_REQUIRE_SAFE
} AppFsmDecision_t;

typedef enum
{
    APP_FSM_CONTROL_RESET_BOOT = 1u
} AppFsmControlOp_t;

typedef struct
{
    AppFsmComponentId_t id;
    uint8_t initialized;
    uint8_t state;
    uint8_t busy;
    uint8_t eventPending;
    const char *p_name;
    uint32_t intervalMs;
    uint32_t runCount;
    uint32_t lastRunTickMs;
    uint32_t lastActionTickMs;
    AppStatus_t lastStatus;
} AppFsmComponentContext_t;

typedef struct
{
    AppFsmDecision_t decision;
    uint8_t currentState;
    uint8_t lastQueuedState;
    uint8_t lastDequeFromFront;
    uint8_t lowPowerRequested;
    uint32_t transitionCount;
    uint32_t processedMessageCount;
    uint32_t queueEmptyStopCount;
    uint32_t lastCommandTickMs;
    uint32_t lastStateTickMs;
    uint32_t lastCommandEventParam;
    uint32_t lastCommandParam0;
    uint32_t lastLoopDispatchCount;
    uint32_t loopCount;
} AppFsmSummary_t;

typedef struct
{
    uint8_t initialized;
    AppFsmComponentContext_t components[APP_FSM_COMPONENT_COUNT];
    AppFsmSummary_t summary;
} AppFsmContext_t;

AppStatus_t App_FsmInit(void);
AppStatus_t App_FsmRun(void);
AppStatus_t App_FsmQueueStateFront(uint8_t nextState, uint32_t eventParam, uint32_t param0);
AppStatus_t App_FsmQueueStateBack(uint8_t nextState, uint32_t eventParam, uint32_t param0);
AppStatus_t App_FsmRequestResetBoot(void);
const AppFsmContext_t *App_FsmGetContext(void);
const AppFsmSummary_t *App_FsmGetSummary(void);
const AppFsmComponentContext_t *App_FsmGetComponent(AppFsmComponentId_t id);
const char *App_FsmGetComponentName(AppFsmComponentId_t id);
const char *App_FsmGetStateName(uint8_t state);
const char *App_FsmGetCurrentStateString(void);
AppFsmDecision_t App_FsmGetDecision(void);
const char *App_FsmGetDecisionString(void);

#ifdef DEBUG
#define APP_FSM_DEBUG_PRINT(module, fmt, ...)                         \
    do                                                                 \
    {                                                                  \
        (void)APP_LOGD((module), (fmt), ##__VA_ARGS__);                \
    } while (0)
#else
#define APP_FSM_DEBUG_PRINT(module, fmt, ...)                         \
    do                                                                 \
    {                                                                  \
    } while (0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_FSM_H */

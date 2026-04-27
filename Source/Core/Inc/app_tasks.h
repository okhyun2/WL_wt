#ifndef APP_TASKS_H
#define APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_scheduler.h"
#include "app_log.h"
#include "app_task_state_defs.h"

    typedef enum
    {
        APP_TASK_ID_DEBUG = 0,
        APP_TASK_ID_WATCHDOG,
        APP_TASK_ID_HOUSEKEEPING,
        APP_TASK_ID_POWER,
        APP_TASK_ID_STORAGE,
        APP_TASK_ID_METER,
        APP_TASK_ID_NFC,
        APP_TASK_ID_ESI,
        APP_TASK_ID_AUX,
        APP_TASK_ID_NBIOT,
        APP_TASK_ID_SERVER,
        APP_TASK_ID_RTC,
        APP_TASK_ID_MAIN,
        APP_TASK_ID_COUNT
    } AppTaskId_t;

    typedef struct
    {
        AppTaskId_t id;
        uint8_t initialized;
        uint8_t state;
        uint8_t busy;
        uint8_t eventPending;
        const char *p_name;
        uint32_t periodMs;
        uint32_t runCount;
        uint32_t lastRunTickMs;
        uint32_t lastActionTickMs;
        uint32_t lastHeartbeatTickMs;
        AppStatus_t lastStatus;
        AppSchedulerTaskHandle_t schedulerHandle;
    } AppTaskModuleContext_t;

    typedef struct
    {
        uint8_t initialized;
        AppTaskModuleContext_t modules[APP_TASK_ID_COUNT];
    } AppTasksContext_t;

    typedef enum
    {
        APP_TASK_MAIN_DECISION_BOOT = 0,
        APP_TASK_MAIN_DECISION_MONITOR,
        APP_TASK_MAIN_DECISION_RUN_ACTIVE,
        APP_TASK_MAIN_DECISION_ALLOW_IDLE,
        APP_TASK_MAIN_DECISION_REQUIRE_SAFE
    } AppTaskMainDecision_t;

    typedef struct
    {
        uint8_t state;
        uint8_t busy;
        uint8_t eventPending;
        uint8_t alive;
        AppStatus_t lastStatus;
        uint32_t lastHeartbeatTickMs;
        uint32_t heartbeatCount;
    } AppTaskMainMonitor_t;

    typedef struct
    {
        AppTaskMainDecision_t decision;
        uint32_t aliveCount;
        uint32_t busyCount;
        uint32_t staleCount;
        uint32_t processedMessageCount;
        uint32_t lastEvaluationTickMs;
    } AppTaskMainSummary_t;

    typedef struct
    {
        uint8_t initialized;
        uint8_t externalFeedEnabled;
        uint8_t lastServiceOk;
        uint32_t iwdgRefreshCount;
        uint32_t externalFeedCount;
        uint32_t lastIwdgRefreshTickMs;
        uint32_t lastExternalFeedTickMs;
        uint32_t lastServiceTickMs;
        AppStatus_t lastStatus;
    } AppTaskWatchdogSummary_t;

    AppStatus_t App_TasksInit(void);
    AppStatus_t App_TasksRegisterAll(void);
    const AppTasksContext_t *App_TasksGetContext(void);
    const AppTaskModuleContext_t *App_TasksGetModuleContext(AppTaskId_t id);
    AppTaskModuleContext_t *App_TasksGetModuleContextMutable(AppTaskId_t id);
    const char *App_TasksGetName(AppTaskId_t id);
    const char *App_TasksGetStateName(AppTaskId_t id, uint8_t state);
    AppTaskId_t App_TasksFindIdBySchedulerHandle(AppSchedulerTaskHandle_t handle);
    AppStatus_t App_TasksCompleteRun(AppTaskModuleContext_t *p_module, AppStatus_t status);
    AppStatus_t App_TasksPublishMessage(AppTaskId_t sourceId, uint8_t type, uint32_t param0, uint32_t param1);
AppStatus_t App_TasksDebugStateTransition(const AppTaskModuleContext_t *p_module, uint8_t nextState, const char *p_file, uint32_t line);

    const AppTaskMainMonitor_t *App_TaskMainGetMonitor(AppTaskId_t id);
    const AppTaskMainSummary_t *App_TaskMainGetSummary(void);
    AppTaskMainDecision_t App_TaskMainGetDecision(void);
    const char *App_TaskMainGetDecisionString(void);
    const AppTaskWatchdogSummary_t *App_TaskWatchdogGetSummary(void);

#ifdef DEBUG
#define APP_TASK_SET_STATE(p_module, next_state)                                                       \
    do                                                                                                 \
    {                                                                                                  \
        (void)App_TasksDebugStateTransition((p_module), (uint8_t)(next_state), __FILE__, __LINE__);   \
        (p_module)->state = (uint8_t)(next_state);                                                     \
    } while (0)

#define APP_TASK_DEBUG_PRINT(module, fmt, ...)                                                         \
    do                                                                                                 \
    {                                                                                                  \
        (void)APP_LOGD((module), (fmt), ##__VA_ARGS__);                                                \
    } while (0)
#else
#define APP_TASK_SET_STATE(p_module, next_state)                                                       \
    do                                                                                                 \
    {                                                                                                  \
        (p_module)->state = (uint8_t)(next_state);                                                     \
    } while (0)

#define APP_TASK_DEBUG_PRINT(module, fmt, ...)                                                         \
    do                                                                                                 \
    {                                                                                                  \
    } while (0)
#endif

    AppStatus_t App_TaskDebug(void *p_context);
    AppStatus_t App_TaskWatchdog(void *p_context);
    AppStatus_t App_TaskHousekeeping(void *p_context);
    AppStatus_t App_TaskPower(void *p_context);
    AppStatus_t App_TaskStorage(void *p_context);
    AppStatus_t App_TaskMeter(void *p_context);
    AppStatus_t App_TaskNfc(void *p_context);
    AppStatus_t App_TaskEsi(void *p_context);
    AppStatus_t App_TaskAux(void *p_context);
    AppStatus_t App_TaskNbiot(void *p_context);
    AppStatus_t App_TaskServer(void *p_context);
    AppStatus_t App_TaskRtc(void *p_context);
    AppStatus_t App_TaskMain(void *p_context);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */

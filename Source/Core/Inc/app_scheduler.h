#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

#define APP_SCHEDULER_TASK_HANDLE_INVALID    (0xFFu)

typedef uint8_t AppSchedulerTaskHandle_t;
typedef AppStatus_t (*AppSchedulerTaskHandler_t)(void *p_context);

typedef struct
{
    uint8_t inUse;
    uint8_t enabled;
    uint8_t runImmediately;
    const char *p_name;
    AppSchedulerTaskHandler_t handler;
    void *p_context;
    uint32_t periodMs;
    uint32_t nextReleaseTickMs;
    uint32_t lastRunTickMs;
    uint32_t runCount;
    AppStatus_t lastStatus;
} AppSchedulerTask_t;

typedef struct
{
    uint8_t initialized;
    uint8_t taskCount;
    uint8_t lastDispatchCount;
    uint32_t loopCount;
    uint32_t totalDispatchCount;
    uint32_t idleCount;
    uint32_t lastRunTickMs;
    AppStatus_t lastStatus;
} AppSchedulerContext_t;

AppStatus_t App_SchedulerInit(void);
AppStatus_t App_SchedulerRegisterTask(const char *p_name,
                                      AppSchedulerTaskHandler_t handler,
                                      void *p_context,
                                      uint32_t periodMs,
                                      uint8_t runImmediately,
                                      AppSchedulerTaskHandle_t *p_handle);
AppStatus_t App_SchedulerSetTaskEnabled(AppSchedulerTaskHandle_t handle, uint8_t enabled);
AppStatus_t App_SchedulerRunOnce(void);
const AppSchedulerContext_t *App_SchedulerGetContext(void);
const AppSchedulerTask_t *App_SchedulerGetTask(AppSchedulerTaskHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */

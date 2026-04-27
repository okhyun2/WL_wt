#ifndef APP_TASKS_H
#define APP_TASKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_scheduler.h"

/**
 * @file    app_tasks.h
 * @brief   Cooperative task registry and pseudo task implementations.
 */

/**
 * @brief Application task identifiers.
 */
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
    APP_TASK_ID_COUNT
} AppTaskId_t;

/**
 * @brief Runtime context for one application task.
 */
typedef struct
{
    AppTaskId_t id;
    uint8_t initialized;
    uint8_t state;
    uint8_t busy;
    uint8_t eventPending;
    const char *p_name;
    uint32_t runCount;
    uint32_t lastRunTickMs;
    uint32_t lastActionTickMs;
    AppStatus_t lastStatus;
    AppSchedulerTaskHandle_t schedulerHandle;
} AppTaskModuleContext_t;

/**
 * @brief Runtime context for all registered application tasks.
 */
typedef struct
{
    uint8_t initialized;
    AppTaskModuleContext_t modules[APP_TASK_ID_COUNT];
} AppTasksContext_t;

/**
 * @brief Initialize task module contexts.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_TasksInit(void);

/**
 * @brief Register all baseline tasks in the cooperative scheduler.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_TasksRegisterAll(void);

/**
 * @brief Get immutable global task context.
 *
 * @return Pointer to task context.
 */
const AppTasksContext_t *App_TasksGetContext(void);

/**
 * @brief Get immutable context for one application task.
 *
 * @param id Task identifier.
 * @return Pointer to module context, or NULL if id is invalid.
 */
const AppTaskModuleContext_t *App_TasksGetModuleContext(AppTaskId_t id);

/**
 * @brief Get task display name.
 *
 * @param id Task identifier.
 * @return Constant task name string.
 */
const char *App_TasksGetName(AppTaskId_t id);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */

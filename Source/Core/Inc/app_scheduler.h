#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_scheduler.h
 * @brief   Tick-based cooperative task scheduler.
 */

/** @brief Invalid scheduler task handle value. */
#define APP_SCHEDULER_TASK_HANDLE_INVALID    (0xFFu)

/**
 * @brief Scheduler task handle type.
 */
typedef uint8_t AppSchedulerTaskHandle_t;

/**
 * @brief Cooperative task handler prototype.
 *
 * @param p_context User-provided task context pointer.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
typedef AppStatus_t (*AppSchedulerTaskHandler_t)(void *p_context);

/**
 * @brief One registered task descriptor.
 */
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

/**
 * @brief Scheduler runtime context.
 */
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

/**
 * @brief Initialize the scheduler core.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_SchedulerInit(void);

/**
 * @brief Register one cooperative task in the static task table.
 *
 * @param p_name Task name string.
 * @param handler Task callback.
 * @param p_context User context pointer.
 * @param periodMs Task period in milliseconds.
 * @param runImmediately APP_TRUE to dispatch on first scheduler run.
 * @param p_handle Output task handle.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_SchedulerRegisterTask(const char *p_name,
                                      AppSchedulerTaskHandler_t handler,
                                      void *p_context,
                                      uint32_t periodMs,
                                      uint8_t runImmediately,
                                      AppSchedulerTaskHandle_t *p_handle);

/**
 * @brief Enable or disable a registered task.
 *
 * @param handle Task handle.
 * @param enabled APP_TRUE to enable, APP_FALSE to disable.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_SchedulerSetTaskEnabled(AppSchedulerTaskHandle_t handle, uint8_t enabled);

/**
 * @brief Execute all tasks that are due at the current tick.
 *
 * @return APP_STATUS_OK if all dispatched tasks succeed, otherwise the last
 *         non-OK task status encountered during the loop.
 */
AppStatus_t App_SchedulerRunOnce(void);

/**
 * @brief Get immutable scheduler context.
 *
 * @return Pointer to scheduler context.
 */
const AppSchedulerContext_t *App_SchedulerGetContext(void);

/**
 * @brief Get immutable descriptor for one registered task.
 *
 * @param handle Task handle.
 * @return Pointer to task descriptor, or NULL when handle is invalid.
 */
const AppSchedulerTask_t *App_SchedulerGetTask(AppSchedulerTaskHandle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */

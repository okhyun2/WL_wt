#include "app_scheduler.h"

#include <string.h>

#include "app_build_config.h"

static AppSchedulerTask_t g_appSchedulerTasks[APP_SCHEDULER_MAX_TASKS];
static AppSchedulerContext_t g_appSchedulerContext;

static uint8_t App_SchedulerIsTaskDue(uint32_t nowTick, uint32_t releaseTick)
{
    return (((int32_t)(nowTick - releaseTick)) >= 0) ? APP_TRUE : APP_FALSE;
}

static void App_SchedulerUpdateNextRelease(AppSchedulerTask_t *p_task, uint32_t nowTick)
{
    if ((p_task == NULL) || (p_task->periodMs == 0u))
    {
        return;
    }

    do
    {
        p_task->nextReleaseTickMs += p_task->periodMs;
    } while (App_SchedulerIsTaskDue(nowTick, p_task->nextReleaseTickMs) == APP_TRUE);
}

AppStatus_t App_SchedulerInit(void)
{
    (void)memset(g_appSchedulerTasks, 0, sizeof(g_appSchedulerTasks));
    (void)memset(&g_appSchedulerContext, 0, sizeof(g_appSchedulerContext));
    g_appSchedulerContext.initialized = APP_TRUE;
    g_appSchedulerContext.lastStatus = APP_STATUS_OK;
    return APP_STATUS_OK;
}

AppStatus_t App_SchedulerRegisterTask(const char *p_name,
                                      AppSchedulerTaskHandler_t handler,
                                      void *p_context,
                                      uint32_t periodMs,
                                      uint8_t runImmediately,
                                      AppSchedulerTaskHandle_t *p_handle)
{
    uint32_t index;
    uint32_t releaseTick;

    APP_RETURN_IF_FALSE(g_appSchedulerContext.initialized == APP_TRUE, APP_STATUS_SCHEDULER_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_name != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((handler != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((periodMs > 0u), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_handle != NULL), APP_STATUS_INVALID_PARAM);

    *p_handle = APP_SCHEDULER_TASK_HANDLE_INVALID;
    releaseTick = HAL_GetTick() + periodMs;

    for (index = 0u; index < APP_SCHEDULER_MAX_TASKS; index++)
    {
        if (g_appSchedulerTasks[index].inUse == APP_FALSE)
        {
            g_appSchedulerTasks[index].inUse = APP_TRUE;
            g_appSchedulerTasks[index].enabled = APP_TRUE;
            g_appSchedulerTasks[index].runImmediately = runImmediately;
            g_appSchedulerTasks[index].p_name = p_name;
            g_appSchedulerTasks[index].handler = handler;
            g_appSchedulerTasks[index].p_context = p_context;
            g_appSchedulerTasks[index].periodMs = periodMs;
            g_appSchedulerTasks[index].nextReleaseTickMs = (runImmediately == APP_TRUE) ? HAL_GetTick() : releaseTick;
            g_appSchedulerTasks[index].lastRunTickMs = 0u;
            g_appSchedulerTasks[index].runCount = 0u;
            g_appSchedulerTasks[index].lastStatus = APP_STATUS_OK;
            g_appSchedulerContext.taskCount++;
            *p_handle = (AppSchedulerTaskHandle_t)index;
            return APP_STATUS_OK;
        }
    }

    return APP_STATUS_SCHEDULER_TASK_LIMIT_REACHED;
}

AppStatus_t App_SchedulerSetTaskEnabled(AppSchedulerTaskHandle_t handle, uint8_t enabled)
{
    APP_RETURN_IF_FALSE(g_appSchedulerContext.initialized == APP_TRUE, APP_STATUS_SCHEDULER_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((handle < APP_SCHEDULER_MAX_TASKS), APP_STATUS_SCHEDULER_TASK_INVALID);
    APP_RETURN_IF_FALSE(g_appSchedulerTasks[handle].inUse == APP_TRUE, APP_STATUS_SCHEDULER_TASK_INVALID);

    g_appSchedulerTasks[handle].enabled = enabled;
    if (enabled == APP_TRUE)
    {
        g_appSchedulerTasks[handle].nextReleaseTickMs = HAL_GetTick() + g_appSchedulerTasks[handle].periodMs;
    }

    return APP_STATUS_OK;
}

AppStatus_t App_SchedulerRunOnce(void)
{
    uint32_t index;
    uint32_t nowTick;
    uint8_t dispatchCount;
    AppStatus_t finalStatus;

    APP_RETURN_IF_FALSE(g_appSchedulerContext.initialized == APP_TRUE, APP_STATUS_SCHEDULER_NOT_INITIALIZED);

    nowTick = HAL_GetTick();
    dispatchCount = 0u;
    finalStatus = APP_STATUS_OK;
    g_appSchedulerContext.loopCount++;
    g_appSchedulerContext.lastRunTickMs = nowTick;

    for (index = 0u; index < APP_SCHEDULER_MAX_TASKS; index++)
    {
        AppSchedulerTask_t *p_task;
        AppStatus_t taskStatus;

        p_task = &g_appSchedulerTasks[index];
        if ((p_task->inUse != APP_TRUE) || (p_task->enabled != APP_TRUE))
        {
            continue;
        }
        if (App_SchedulerIsTaskDue(nowTick, p_task->nextReleaseTickMs) != APP_TRUE)
        {
            continue;
        }

        taskStatus = p_task->handler(p_task->p_context); //Do task
        p_task->lastStatus = taskStatus;
        p_task->lastRunTickMs = nowTick;
        p_task->runCount++;
        dispatchCount++;
        g_appSchedulerContext.totalDispatchCount++;
        App_SchedulerUpdateNextRelease(p_task, nowTick);

        if (taskStatus != APP_STATUS_OK)
        {
            finalStatus = taskStatus;
        }
    }

    g_appSchedulerContext.lastDispatchCount = dispatchCount;
    if (dispatchCount == 0u)
    {
        g_appSchedulerContext.idleCount++;
    }
    g_appSchedulerContext.lastStatus = finalStatus;

    return finalStatus;
}

const AppSchedulerContext_t *App_SchedulerGetContext(void)
{
    return &g_appSchedulerContext;
}

const AppSchedulerTask_t *App_SchedulerGetTask(AppSchedulerTaskHandle_t handle)
{
    if ((handle >= APP_SCHEDULER_MAX_TASKS) || (g_appSchedulerTasks[handle].inUse != APP_TRUE))
    {
        return NULL;
    }

    return &g_appSchedulerTasks[handle];
}

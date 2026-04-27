#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_msgq.h"
#include "app_scheduler.h"
#include "app_system.h"

static AppTaskMainMonitor_t g_appTaskMainMonitors[APP_TASK_ID_COUNT];
static AppTaskMainSummary_t g_appTaskMainSummary;
static uint8_t g_appTaskMainRequireSafe;

static uint32_t App_TaskMainGetTimeoutMs(AppTaskId_t id)
{
    const AppTaskModuleContext_t *p_module;
    const AppSchedulerTask_t *p_task;
    uint32_t timeoutMs;
    uint32_t candidate;

    timeoutMs = APP_TASK_MAIN_HEARTBEAT_GRACE_MS;
    p_module = App_TasksGetModuleContext(id);
    if (p_module == NULL)
    {
        return timeoutMs;
    }

    p_task = App_SchedulerGetTask(p_module->schedulerHandle);
    if (p_task != NULL)
    {
        candidate = (p_task->periodMs * APP_TASK_MAIN_STALE_FACTOR) + APP_TASK_MAIN_STALE_MARGIN_MS;
        if (candidate > timeoutMs)
        {
            timeoutMs = candidate;
        }
    }

    return timeoutMs;
}

static uint8_t App_TaskMainIsAlive(AppTaskId_t id, uint32_t nowTick)
{
    uint32_t timeoutMs;
    const AppTaskMainMonitor_t *p_monitor;

    p_monitor = App_TaskMainGetMonitor(id);
    if ((p_monitor == NULL) || (p_monitor->heartbeatCount == 0u))
    {
        return APP_FALSE;
    }

    timeoutMs = App_TaskMainGetTimeoutMs(id);
    return (((int32_t)(nowTick - p_monitor->lastHeartbeatTickMs)) <= (int32_t)timeoutMs) ? APP_TRUE : APP_FALSE;
}

AppStatus_t App_TaskMain(void *p_context)
{
    AppTaskModuleContext_t *p_module;
    AppMsgqMessage_t message;
    AppStatus_t queueStatus;
    uint32_t nowTick;
    uint32_t id;
    uint32_t drainedCount;
    uint8_t anyEvent;
    uint8_t requireSafe;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    nowTick = HAL_GetTick();

    switch (p_module->state)
    {
        case APP_TASK_MAIN_STATE_INIT:
            /* PSEUDO external interface:
             * - App_MsgqPop(...)
             * - App_SystemOnBeforeStopEnter()
             * - App_SystemOnAfterStopExit()
             * - App_SystemSetNbiotPowered(APP_TRUE/APP_FALSE)
             */
            (void)memset(g_appTaskMainMonitors, 0, sizeof(g_appTaskMainMonitors));
            (void)memset(&g_appTaskMainSummary, 0, sizeof(g_appTaskMainSummary));
            g_appTaskMainRequireSafe = APP_FALSE;
            g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_BOOT;
            APP_TASK_SET_STATE(p_module, APP_TASK_MAIN_STATE_COLLECT);
            break;

        case APP_TASK_MAIN_STATE_COLLECT:
            drainedCount = 0u;
            while (drainedCount < APP_MSGQ_MAIN_DRAIN_PER_RUN)
            {
                queueStatus = App_MsgqPop(&message);
                if (queueStatus == APP_STATUS_MSGQ_EMPTY)
                {
                    break;
                }
                APP_RETURN_IF_FALSE(queueStatus == APP_STATUS_OK, queueStatus);

                if ((message.type == APP_MSGQ_MSG_TASK_HEARTBEAT) &&
                    ((uint32_t)message.sourceId < (uint32_t)APP_TASK_ID_COUNT) &&
                    ((AppTaskId_t)message.sourceId != APP_TASK_ID_MAIN))
                {
                    AppTaskMainMonitor_t *p_monitor;

                    p_monitor = &g_appTaskMainMonitors[message.sourceId];
                    p_monitor->state = (uint8_t)(message.param0 & 0xFFu);
                    p_monitor->busy = ((message.param1 & 0x0001u) != 0u) ? APP_TRUE : APP_FALSE;
                    p_monitor->eventPending = ((message.param1 & 0x0002u) != 0u) ? APP_TRUE : APP_FALSE;
                    p_monitor->lastStatus = (AppStatus_t)((message.param1 >> 16) & 0xFFFFu);
                    p_monitor->lastHeartbeatTickMs = message.tickMs;
                    p_monitor->heartbeatCount++;
                    p_monitor->alive = APP_TRUE;
                    g_appTaskMainSummary.processedMessageCount++;
                }

                drainedCount++;
            }
            APP_TASK_SET_STATE(p_module, APP_TASK_MAIN_STATE_EVALUATE);
            break;

        case APP_TASK_MAIN_STATE_EVALUATE:
            g_appTaskMainSummary.aliveCount = 0u;
            g_appTaskMainSummary.busyCount = 0u;
            g_appTaskMainSummary.staleCount = 0u;
            g_appTaskMainSummary.lastEvaluationTickMs = nowTick;
            g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_MONITOR;
            anyEvent = APP_FALSE;
            requireSafe = APP_FALSE;

            for (id = 0u; id < (uint32_t)APP_TASK_ID_COUNT; id++)
            {
                AppTaskId_t taskId;

                taskId = (AppTaskId_t)id;
                if (taskId == APP_TASK_ID_MAIN)
                {
                    continue;
                }

                g_appTaskMainMonitors[id].alive = App_TaskMainIsAlive(taskId, nowTick);
                if (g_appTaskMainMonitors[id].alive == APP_TRUE)
                {
                    g_appTaskMainSummary.aliveCount++;
                    if (g_appTaskMainMonitors[id].busy == APP_TRUE)
                    {
                        g_appTaskMainSummary.busyCount++;
                    }
                    if (g_appTaskMainMonitors[id].eventPending == APP_TRUE)
                    {
                        anyEvent = APP_TRUE;
                    }
                }
                else
                {
                    g_appTaskMainSummary.staleCount++;
                    if ((taskId == APP_TASK_ID_WATCHDOG) || (taskId == APP_TASK_ID_POWER))
                    {
                        requireSafe = APP_TRUE;
                    }
                }
            }

            g_appTaskMainRequireSafe = requireSafe;
            p_module->busy = (g_appTaskMainSummary.busyCount != 0u) ? APP_TRUE : APP_FALSE;
            p_module->eventPending = anyEvent;
            APP_TASK_SET_STATE(p_module, APP_TASK_MAIN_STATE_DECIDE);
            break;

        case APP_TASK_MAIN_STATE_DECIDE:
        default:
            /* PSEUDO overall decision logic:
             * - If watchdog/power heartbeat is stale, force safe fallback path.
             * - If all active tasks are idle and no event is pending, allow idle/STOP candidate.
             * - If meter/storage/server path is busy, keep run mode active.
             */
            if (g_appTaskMainRequireSafe == APP_TRUE)
            {
                g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_REQUIRE_SAFE;
            }
            else if ((g_appTaskMainSummary.busyCount == 0u) && (p_module->eventPending == APP_FALSE))
            {
                g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_ALLOW_IDLE;
            }
            else
            {
                g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_RUN_ACTIVE;
            }

            p_module->lastActionTickMs = nowTick;
#ifdef DEBUG
            APP_TASK_DEBUG_PRINT("MAIN",
                                 "decision=%s alive=%lu busy=%lu stale=%lu pending=%u",
                                 App_TaskMainGetDecisionString(),
                                 (unsigned long)g_appTaskMainSummary.aliveCount,
                                 (unsigned long)g_appTaskMainSummary.busyCount,
                                 (unsigned long)g_appTaskMainSummary.staleCount,
                                 (unsigned int)p_module->eventPending);
#endif
            APP_TASK_SET_STATE(p_module, APP_TASK_MAIN_STATE_COLLECT);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

const AppTaskMainMonitor_t *App_TaskMainGetMonitor(AppTaskId_t id)
{
    if ((uint32_t)id >= (uint32_t)APP_TASK_ID_COUNT)
    {
        return NULL;
    }

    return &g_appTaskMainMonitors[id];
}

const AppTaskMainSummary_t *App_TaskMainGetSummary(void)
{
    return &g_appTaskMainSummary;
}

AppTaskMainDecision_t App_TaskMainGetDecision(void)
{
    return g_appTaskMainSummary.decision;
}

const char *App_TaskMainGetDecisionString(void)
{
    switch (g_appTaskMainSummary.decision)
    {
        case APP_TASK_MAIN_DECISION_BOOT:         return "BOOT";
        case APP_TASK_MAIN_DECISION_MONITOR:      return "MONITOR";
        case APP_TASK_MAIN_DECISION_RUN_ACTIVE:   return "RUN_ACTIVE";
        case APP_TASK_MAIN_DECISION_ALLOW_IDLE:   return "ALLOW_IDLE";
        case APP_TASK_MAIN_DECISION_REQUIRE_SAFE: return "REQUIRE_SAFE";
        default:                                  return "UNKNOWN";
    }
}

#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_msgq.h"
#include "app_scheduler.h"
#include "app_system.h"

static AppTaskMainMonitor_t g_appTaskMainMonitors[APP_TASK_ID_COUNT];
static AppTaskMainSummary_t g_appTaskMainSummary;
static AppTaskMainStorageResponse_t g_appTaskMainStorageResponse;
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

static void App_TaskMainBeginStorageRequest(uint8_t operation,
                                            uint8_t backend,
                                            uint32_t requestTickMs,
                                            uint32_t userData0,
                                            uint32_t userData1)
{
    g_appTaskMainStorageResponse.initialized = APP_TRUE;
    g_appTaskMainStorageResponse.responseReady = APP_FALSE;
    g_appTaskMainStorageResponse.backend = backend;
    g_appTaskMainStorageResponse.operation = operation;
    g_appTaskMainStorageResponse.status = APP_STATUS_OK;
    g_appTaskMainStorageResponse.requestTickMs = requestTickMs;
    g_appTaskMainStorageResponse.userData0 = userData0;
    g_appTaskMainStorageResponse.userData1 = userData1;
    g_appTaskMainStorageResponse.sequence = 0u;
}

static void App_TaskMainHandleStorageResponse(const AppMsgqMessage_t *p_message)
{
    if (p_message == NULL)
    {
        return;
    }

    g_appTaskMainStorageResponse.initialized = APP_TRUE;
    g_appTaskMainStorageResponse.responseReady = APP_TRUE;
    g_appTaskMainStorageResponse.backend = p_message->reserved1;
    g_appTaskMainStorageResponse.operation = p_message->reserved0;
    g_appTaskMainStorageResponse.status = (AppStatus_t)p_message->param0;
    g_appTaskMainStorageResponse.requestTickMs = p_message->tickMs;
    g_appTaskMainStorageResponse.userData0 = p_message->param1;
    g_appTaskMainStorageResponse.userData1 = p_message->param2;
    g_appTaskMainStorageResponse.sequence = p_message->param3;
    g_appTaskMainStorageResponse.responseCount++;
}

static void App_TaskMainHandleHeartbeat(const AppMsgqMessage_t *p_message)
{
    AppTaskMainMonitor_t *p_monitor;

    if ((p_message == NULL) || ((uint32_t)p_message->sourceId >= (uint32_t)APP_TASK_ID_COUNT) ||
        ((AppTaskId_t)p_message->sourceId == APP_TASK_ID_MAIN))
    {
        return;
    }

    p_monitor = &g_appTaskMainMonitors[p_message->sourceId];
    p_monitor->state = (uint8_t)(p_message->param0 & 0xFFu);
    p_monitor->busy = ((p_message->param1 & 0x0001u) != 0u) ? APP_TRUE : APP_FALSE;
    p_monitor->eventPending = ((p_message->param1 & 0x0002u) != 0u) ? APP_TRUE : APP_FALSE;
    p_monitor->lastStatus = (AppStatus_t)((p_message->param1 >> 16) & 0xFFFFu);
    p_monitor->lastHeartbeatTickMs = p_message->tickMs;
    p_monitor->heartbeatCount++;
    p_monitor->alive = APP_TRUE;
}

static void App_TaskMainHandleEventLikeMessage(const AppMsgqMessage_t *p_message)
{
    AppTaskMainMonitor_t *p_monitor;

    if ((p_message == NULL) || ((uint32_t)p_message->sourceId >= (uint32_t)APP_TASK_ID_COUNT) ||
        ((AppTaskId_t)p_message->sourceId == APP_TASK_ID_MAIN))
    {
        return;
    }

    p_monitor = &g_appTaskMainMonitors[p_message->sourceId];
    p_monitor->eventPending = APP_TRUE;
    p_monitor->alive = APP_TRUE;
    p_monitor->lastHeartbeatTickMs = p_message->tickMs;
    p_monitor->heartbeatCount++;
}

static AppStatus_t App_TaskMainTakeRelevantMessage(AppMsgqMessage_t *p_message)
{
    AppStatus_t status;

    status = App_MsgqTakeFirstByType(APP_MSGQ_TYPE_STORAGE_RESPONSE, p_message);
    if (status == APP_STATUS_OK)
    {
        return APP_STATUS_OK;
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    status = App_MsgqTakeFirstByType(APP_MSGQ_MSG_TASK_HEARTBEAT, p_message);
    if (status == APP_STATUS_OK)
    {
        return APP_STATUS_OK;
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    status = App_MsgqTakeFirstByType(APP_MSGQ_MSG_TASK_EVENT, p_message);
    if (status == APP_STATUS_OK)
    {
        return APP_STATUS_OK;
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    status = App_MsgqTakeFirstByType(APP_MSGQ_MSG_TASK_ALERT, p_message);
    if (status == APP_STATUS_OK)
    {
        return APP_STATUS_OK;
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    return APP_STATUS_MSGQ_EMPTY;
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
            (void)memset(g_appTaskMainMonitors, 0, sizeof(g_appTaskMainMonitors));
            (void)memset(&g_appTaskMainSummary, 0, sizeof(g_appTaskMainSummary));
            (void)memset(&g_appTaskMainStorageResponse, 0, sizeof(g_appTaskMainStorageResponse));
            g_appTaskMainRequireSafe = APP_FALSE;
            g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_BOOT;
            APP_TASK_SET_STATE(p_module, APP_TASK_MAIN_STATE_COLLECT);
            break;

        case APP_TASK_MAIN_STATE_COLLECT:
            drainedCount = 0u;
            while (drainedCount < APP_MSGQ_MAIN_DRAIN_PER_RUN)
            {
                queueStatus = App_TaskMainTakeRelevantMessage(&message);
                if (queueStatus == APP_STATUS_MSGQ_EMPTY)
                {
                    break;
                }
                APP_RETURN_IF_FALSE(queueStatus == APP_STATUS_OK, queueStatus);

                if ((message.type == APP_MSGQ_MSG_TASK_HEARTBEAT) &&
                    ((uint32_t)message.sourceId < (uint32_t)APP_TASK_ID_COUNT) &&
                    ((AppTaskId_t)message.sourceId != APP_TASK_ID_MAIN))
                {
                    App_TaskMainHandleHeartbeat(&message);
                    g_appTaskMainSummary.processedMessageCount++;
                }
                else if ((message.type == APP_MSGQ_TYPE_STORAGE_RESPONSE) &&
                         (message.sourceId == (uint8_t)APP_TASK_ID_STORAGE))
                {
                    App_TaskMainHandleStorageResponse(&message);
                    g_appTaskMainSummary.processedMessageCount++;
                }
                else if ((message.type == APP_MSGQ_MSG_TASK_EVENT) || (message.type == APP_MSGQ_MSG_TASK_ALERT))
                {
                    App_TaskMainHandleEventLikeMessage(&message);
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
            if (g_appTaskMainRequireSafe == APP_TRUE)
            {
                g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_REQUIRE_SAFE;
            }
            else if ((g_appTaskMainSummary.busyCount == 0u) && (p_module->eventPending == APP_FALSE))
            {
                g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_ALLOW_IDLE;
                //don't enter stop mode.
                //g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_RUN_ACTIVE;
            }
            else
            {
                g_appTaskMainSummary.decision = APP_TASK_MAIN_DECISION_RUN_ACTIVE;
            }

            p_module->lastActionTickMs = nowTick;
            APP_TASK_DEBUG_PRINT("MAIN", "decision=%s alive=%lu busy=%lu stale=%lu pending=%u",
                                 App_TaskMainGetDecisionString(),
                                 (unsigned long)g_appTaskMainSummary.aliveCount,
                                 (unsigned long)g_appTaskMainSummary.busyCount,
                                 (unsigned long)g_appTaskMainSummary.staleCount,
                                 (unsigned int)p_module->eventPending);
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

const AppTaskMainStorageResponse_t *App_TaskMainGetStorageResponse(void)
{
    return &g_appTaskMainStorageResponse;
}

AppStatus_t App_TaskMainRequestStorageSave(AppStorageTarget_t backend, uint32_t userData0, uint32_t userData1)
{
    AppMsgqMessage_t message;
    uint32_t requestTickMs;

    APP_RETURN_IF_FALSE((backend == APP_STORAGE_TARGET_EEPROM) || (backend == APP_STORAGE_TARGET_FLASH) || (backend == APP_STORAGE_TARGET_BOTH), APP_STATUS_INVALID_PARAM);

    requestTickMs = HAL_GetTick();
    App_TaskMainBeginStorageRequest((uint8_t)APP_STORAGE_QUEUE_OP_SAVE,
                                    (uint8_t)backend,
                                    requestTickMs,
                                    userData0,
                                    userData1);

    (void)memset(&message, 0, sizeof(message));
    message.type = APP_MSGQ_TYPE_STORAGE_REQUEST;
    message.sourceId = (uint8_t)APP_TASK_ID_MAIN;
    message.reserved0 = (uint8_t)APP_STORAGE_QUEUE_OP_SAVE;
    message.reserved1 = (uint8_t)backend;
    message.tickMs = requestTickMs;
    message.param0 = userData0;
    message.param1 = userData1;
    return App_MsgqPush(&message);
}

AppStatus_t App_TaskMainRequestStorageLoad(AppStorageTarget_t backend)
{
    AppMsgqMessage_t message;
    uint32_t requestTickMs;

    APP_RETURN_IF_FALSE((backend == APP_STORAGE_TARGET_EEPROM) || (backend == APP_STORAGE_TARGET_FLASH), APP_STATUS_INVALID_PARAM);

    requestTickMs = HAL_GetTick();
    App_TaskMainBeginStorageRequest((uint8_t)APP_STORAGE_QUEUE_OP_LOAD,
                                    (uint8_t)backend,
                                    requestTickMs,
                                    0u,
                                    0u);

    (void)memset(&message, 0, sizeof(message));
    message.type = APP_MSGQ_TYPE_STORAGE_REQUEST;
    message.sourceId = (uint8_t)APP_TASK_ID_MAIN;
    message.reserved0 = (uint8_t)APP_STORAGE_QUEUE_OP_LOAD;
    message.reserved1 = (uint8_t)backend;
    message.tickMs = requestTickMs;
    return App_MsgqPush(&message);
}

AppStatus_t App_TaskMainRequestPowerResetBoot(void)
{
    AppMsgqMessage_t message;

    (void)memset(&message, 0, sizeof(message));
    message.type = APP_MSGQ_TYPE_POWER_REQUEST;
    message.sourceId = (uint8_t)APP_TASK_ID_MAIN;
    message.reserved0 = (uint8_t)APP_POWER_QUEUE_OP_RESET_BOOT;
    message.tickMs = HAL_GetTick();
    return App_MsgqPush(&message);
}

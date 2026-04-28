#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_msgq.h"

static AppTasksContext_t g_appTasksContext;

static uint32_t App_TasksMaxU32(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

const char *App_TasksGetName(AppTaskId_t id)
{
    switch (id)
    {
        case APP_TASK_ID_DEBUG:        return "debug";
        case APP_TASK_ID_WATCHDOG:     return "watchdog";
        case APP_TASK_ID_HOUSEKEEPING: return "housekeeping";
        case APP_TASK_ID_POWER:        return "power";
        case APP_TASK_ID_STORAGE:      return "storage";
        case APP_TASK_ID_METER:        return "meter";
        case APP_TASK_ID_NFC:          return "nfc";
        case APP_TASK_ID_ESI:          return "esi";
        case APP_TASK_ID_AUX:          return "aux";
        case APP_TASK_ID_NBIOT:        return "nbiot";
        case APP_TASK_ID_SERVER:       return "server";
        case APP_TASK_ID_RTC:          return "rtc";
        case APP_TASK_ID_MAIN:         return "main";
        default:                       return "unknown";
    }
}

const char *App_TasksGetStateName(AppTaskId_t id, uint8_t state)
{
    switch (id)
    {
        case APP_TASK_ID_DEBUG:
            return (state == APP_TASK_DEBUG_STATE_INIT) ? "INIT" : "POLL";

        case APP_TASK_ID_WATCHDOG:
            switch (state)
            {
                case APP_TASK_WATCHDOG_STATE_INIT: return "INIT";
                case APP_TASK_WATCHDOG_STATE_FEED_EXTERNAL: return "FEED_EXTERNAL";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_HOUSEKEEPING:
            switch (state)
            {
                case APP_TASK_HOUSEKEEPING_STATE_INIT: return "INIT";
                case APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT: return "SNAPSHOT";
                case APP_TASK_HOUSEKEEPING_STATE_ROTATE: return "ROTATE";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_POWER:
            switch (state)
            {
                case APP_TASK_POWER_STATE_INIT: return "INIT";
                case APP_TASK_POWER_STATE_EVALUATE: return "EVALUATE";
                case APP_TASK_POWER_STATE_DECIDE_IDLE: return "DECIDE_IDLE";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_STORAGE:
            switch (state)
            {
                case APP_TASK_STORAGE_STATE_INIT: return "INIT";
                case APP_TASK_STORAGE_STATE_SCAN_QUEUE: return "SCAN_QUEUE";
                case APP_TASK_STORAGE_STATE_COMMIT_ONE: return "COMMIT_ONE";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_METER:
            switch (state)
            {
                case APP_TASK_METER_STATE_INIT: return "INIT";
                case APP_TASK_METER_STATE_WAIT_TRIGGER: return "WAIT_TRIGGER";
                case APP_TASK_METER_STATE_SEND_REQUEST: return "SEND_REQUEST";
                case APP_TASK_METER_STATE_PARSE_REPLY: return "PARSE_REPLY";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_NFC:
            switch (state)
            {
                case APP_TASK_NFC_STATE_INIT: return "INIT";
                case APP_TASK_NFC_STATE_WAIT_EVENT: return "WAIT_EVENT";
                case APP_TASK_NFC_STATE_EXCHANGE: return "EXCHANGE";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_ESI:
            switch (state)
            {
                case APP_TASK_ESI_STATE_INIT: return "INIT";
                case APP_TASK_ESI_STATE_WAIT_INTERRUPT: return "WAIT_INTERRUPT";
                case APP_TASK_ESI_STATE_READ_COEFFICIENT: return "READ_COEFF";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_AUX:
            switch (state)
            {
                case APP_TASK_AUX_STATE_INIT: return "INIT";
                case APP_TASK_AUX_STATE_TRIGGER_MEASURE: return "TRIGGER_MEASURE";
                case APP_TASK_AUX_STATE_READ_RESULT: return "READ_RESULT";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_NBIOT:
            switch (state)
            {
                case APP_TASK_NBIOT_STATE_INIT: return "INIT";
                case APP_TASK_NBIOT_STATE_DECIDE_WAKE: return "DECIDE_WAKE";
                case APP_TASK_NBIOT_STATE_POWER_ON: return "POWER_ON";
                case APP_TASK_NBIOT_STATE_EXCHANGE_AT: return "EXCHANGE_AT";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_SERVER:
            switch (state)
            {
                case APP_TASK_SERVER_STATE_INIT: return "INIT";
                case APP_TASK_SERVER_STATE_PREPARE_PACKET: return "PREPARE_PACKET";
                case APP_TASK_SERVER_STATE_REQUEST_SEND: return "REQUEST_SEND";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_RTC:
            switch (state)
            {
                case APP_TASK_RTC_STATE_INIT: return "INIT";
                case APP_TASK_RTC_STATE_CHECK_SCHEDULE: return "CHECK_SCHEDULE";
                case APP_TASK_RTC_STATE_APPLY_SYNC: return "APPLY_SYNC";
                default: return "UNKNOWN";
            }

        case APP_TASK_ID_MAIN:
            switch (state)
            {
                case APP_TASK_MAIN_STATE_INIT: return "INIT";
                case APP_TASK_MAIN_STATE_COLLECT: return "COLLECT";
                case APP_TASK_MAIN_STATE_EVALUATE: return "EVALUATE";
                case APP_TASK_MAIN_STATE_DECIDE: return "DECIDE";
                default: return "UNKNOWN";
            }

        default:
            return "UNKNOWN";
    }
}

AppTaskId_t App_TasksFindIdBySchedulerHandle(AppSchedulerTaskHandle_t handle)
{
    uint32_t index;

    for (index = 0u; index < (uint32_t)APP_TASK_ID_COUNT; index++)
    {
        if (g_appTasksContext.modules[index].schedulerHandle == handle)
        {
            return (AppTaskId_t)index;
        }
    }

    return APP_TASK_ID_COUNT;
}


AppStatus_t App_TasksDebugStateTransition(const AppTaskModuleContext_t *p_module, uint8_t nextState, const char *p_file, uint32_t line)
{
#ifdef DEBUG
    const char *p_oldState;
    const char *p_newState;

    if (p_module == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    if (p_module->state != nextState)
    {
        p_oldState = App_TasksGetStateName(p_module->id, p_module->state);
        p_newState = App_TasksGetStateName(p_module->id, nextState);
        (void)APP_LOGD("TASK", "%s state %s -> %s (%s:%lu)",
                       (p_module->p_name != NULL) ? p_module->p_name : "unknown",
                       p_oldState,
                       p_newState,
                       (p_file != NULL) ? p_file : "-",
                       (unsigned long)line);
    }
#else
    (void)p_module;
    (void)nextState;
    (void)p_file;
    (void)line;
#endif
    return APP_STATUS_OK;
}

AppStatus_t App_TasksPublishMessage(AppTaskId_t sourceId, uint8_t type, uint32_t param0, uint32_t param1)
{
    AppMsgqMessage_t message;

    (void)memset(&message, 0, sizeof(message));
    message.type = type;
    message.sourceId = (uint8_t)sourceId;
    message.tickMs = HAL_GetTick();
    message.param0 = param0;
    message.param1 = param1;
    return App_MsgqPush(&message);
}

AppStatus_t App_TasksCompleteRun(AppTaskModuleContext_t *p_module, AppStatus_t status)
{
    uint32_t nowTick;
    uint32_t publishIntervalMs;
    uint32_t packedFlags;

    if (p_module == NULL)
    {
        return status;
    }

    nowTick = HAL_GetTick();
    p_module->initialized = APP_TRUE;
    p_module->lastStatus = status;
    p_module->lastRunTickMs = nowTick;
    p_module->runCount++;

    packedFlags = ((uint32_t)(status & 0xFFFFu) << 16);
    packedFlags |= ((p_module->busy == APP_TRUE) ? 0x0001u : 0u);
    packedFlags |= ((p_module->eventPending == APP_TRUE) ? 0x0002u : 0u);

    publishIntervalMs = App_TasksMaxU32(p_module->periodMs, APP_TASK_HEARTBEAT_MIN_INTERVAL_MS);
    if ((p_module->lastHeartbeatTickMs == 0u) || ((nowTick - p_module->lastHeartbeatTickMs) >= publishIntervalMs))
    {
        if (App_TasksPublishMessage(p_module->id,
                                    APP_MSGQ_TYPE_TASK_HEARTBEAT,
                                    (uint32_t)p_module->state,
                                    packedFlags) == APP_STATUS_OK)
        {
            p_module->lastHeartbeatTickMs = nowTick;
        }
#ifdef DEBUG
        else
        {
            (void)APP_LOGW("TASK", "%s heartbeat publish failed", (p_module->p_name != NULL) ? p_module->p_name : "unknown");
        }
#endif
    }

#ifdef DEBUG
    if (status != APP_STATUS_OK)
    {
        (void)APP_LOGW("TASK", "%s returned status=%lu",
                       (p_module->p_name != NULL) ? p_module->p_name : "unknown",
                       (unsigned long)status);
    }
#endif

    return status;
}

static AppStatus_t App_TasksRegisterOne(AppTaskId_t id,
                                        AppSchedulerTaskHandler_t handler,
                                        uint32_t periodMs)
{
    AppTaskModuleContext_t *p_module;
    AppStatus_t status;

    APP_RETURN_IF_FALSE((uint32_t)id < (uint32_t)APP_TASK_ID_COUNT, APP_STATUS_INVALID_PARAM);
    p_module = &g_appTasksContext.modules[id];
    p_module->periodMs = periodMs;

    status = App_SchedulerRegisterTask(p_module->p_name,
                                       handler,
                                       p_module,
                                       periodMs,
                                       APP_SCHEDULER_RUN_IMMEDIATE,
                                       &p_module->schedulerHandle);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    return APP_STATUS_OK;
}

AppStatus_t App_TasksInit(void)
{
    uint32_t index;

    (void)memset(&g_appTasksContext, 0, sizeof(g_appTasksContext));
    for (index = 0u; index < (uint32_t)APP_TASK_ID_COUNT; index++)
    {
        g_appTasksContext.modules[index].id = (AppTaskId_t)index;
        g_appTasksContext.modules[index].p_name = App_TasksGetName((AppTaskId_t)index);
        g_appTasksContext.modules[index].lastStatus = APP_STATUS_NOT_INITIALIZED;
        g_appTasksContext.modules[index].schedulerHandle = APP_SCHEDULER_TASK_HANDLE_INVALID;
    }
    g_appTasksContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_TasksRegisterAll(void)
{
    APP_RETURN_IF_FALSE(g_appTasksContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_DEBUG, App_TaskDebug, APP_SCHEDULER_TASK_DEBUG_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_WATCHDOG, App_TaskWatchdog, APP_SCHEDULER_TASK_WATCHDOG_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_HOUSEKEEPING, App_TaskHousekeeping, APP_SCHEDULER_TASK_HOUSEKEEPING_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_POWER, App_TaskPower, APP_SCHEDULER_TASK_POWER_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_STORAGE, App_TaskStorage, APP_SCHEDULER_TASK_STORAGE_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_METER, App_TaskMeter, APP_SCHEDULER_TASK_METER_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_NFC, App_TaskNfc, APP_SCHEDULER_TASK_NFC_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_ESI, App_TaskEsi, APP_SCHEDULER_TASK_ESI_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_AUX, App_TaskAux, APP_SCHEDULER_TASK_AUX_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_NBIOT, App_TaskNbiot, APP_SCHEDULER_TASK_NBIOT_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_SERVER, App_TaskServer, APP_SCHEDULER_TASK_SERVER_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_RTC, App_TaskRtc, APP_SCHEDULER_TASK_RTC_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_MAIN, App_TaskMain, APP_SCHEDULER_TASK_MAIN_PERIOD_MS) == APP_STATUS_OK, APP_STATUS_SCHEDULER_INIT_FAILED);

    return APP_STATUS_OK;
}

const AppTasksContext_t *App_TasksGetContext(void)
{
    return &g_appTasksContext;
}

const AppTaskModuleContext_t *App_TasksGetModuleContext(AppTaskId_t id)
{
    if ((uint32_t)id >= (uint32_t)APP_TASK_ID_COUNT)
    {
        return NULL;
    }
    return &g_appTasksContext.modules[id];
}

AppTaskModuleContext_t *App_TasksGetModuleContextMutable(AppTaskId_t id)
{
    if ((uint32_t)id >= (uint32_t)APP_TASK_ID_COUNT)
    {
        return NULL;
    }
    return &g_appTasksContext.modules[id];
}

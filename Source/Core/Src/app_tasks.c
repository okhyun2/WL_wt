#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_msgq.h"

static AppTasksContext_t g_appTasksContext;

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
            switch (state)
            {
                case APP_TASK_DEBUG_STATE_INIT: return "INIT";
                case APP_TASK_DEBUG_STATE_POLL: return "POLL";
                default: return "UNKNOWN";
            }

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
                case APP_TASK_POWER_STATE_WAIT_REQUEST: return "WAIT_REQUEST";
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
                case APP_TASK_MAIN_STATE_DISPATCH: return "DISPATCH";
                case APP_TASK_MAIN_STATE_BOOT: return "BOOT";
                case APP_TASK_MAIN_STATE_IDLE: return "IDLE";
                case APP_TASK_MAIN_STATE_DEBUG_POLL: return "DEBUG_POLL";
                case APP_TASK_MAIN_STATE_HOUSEKEEPING_INIT: return "HK_INIT";
                case APP_TASK_MAIN_STATE_HOUSEKEEPING_SNAPSHOT: return "HK_SNAPSHOT";
                case APP_TASK_MAIN_STATE_HOUSEKEEPING_ROTATE: return "HK_ROTATE";
                case APP_TASK_MAIN_STATE_POWER_INIT: return "POWER_INIT";
                case APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST: return "POWER_WAIT_REQUEST";
                case APP_TASK_MAIN_STATE_METER_INIT: return "METER_INIT";
                case APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER: return "METER_WAIT_TRIGGER";
                case APP_TASK_MAIN_STATE_METER_SEND_REQUEST: return "METER_SEND_REQUEST";
                case APP_TASK_MAIN_STATE_METER_PARSE_REPLY: return "METER_PARSE_REPLY";
                case APP_TASK_MAIN_STATE_NFC_INIT: return "NFC_INIT";
                case APP_TASK_MAIN_STATE_NFC_WAIT_EVENT: return "NFC_WAIT_EVENT";
                case APP_TASK_MAIN_STATE_NFC_EXCHANGE: return "NFC_EXCHANGE";
                case APP_TASK_MAIN_STATE_AUX_INIT: return "AUX_INIT";
                case APP_TASK_MAIN_STATE_AUX_TRIGGER_MEASURE: return "AUX_TRIGGER_MEASURE";
                case APP_TASK_MAIN_STATE_AUX_READ_RESULT: return "AUX_READ_RESULT";
                case APP_TASK_MAIN_STATE_NBIOT_INIT: return "NBIOT_INIT";
                case APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE: return "NBIOT_DECIDE_WAKE";
                case APP_TASK_MAIN_STATE_NBIOT_POWER_ON: return "NBIOT_POWER_ON";
                case APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT: return "NBIOT_EXCHANGE_AT";
                case APP_TASK_MAIN_STATE_SERVER_INIT: return "SERVER_INIT";
                case APP_TASK_MAIN_STATE_SERVER_PREPARE_PACKET: return "SERVER_PREPARE_PACKET";
                case APP_TASK_MAIN_STATE_SERVER_REQUEST_SEND: return "SERVER_REQUEST_SEND";
                case APP_TASK_MAIN_STATE_RTC_INIT: return "RTC_INIT";
                case APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE: return "RTC_CHECK_SCHEDULE";
                case APP_TASK_MAIN_STATE_RTC_APPLY_SYNC: return "RTC_APPLY_SYNC";
                case APP_TASK_MAIN_STATE_FAULT: return "FAULT";
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
    return App_MsgqPushBack(&message);
}

AppStatus_t App_TasksPublishStateCommand(AppTaskId_t sourceId, uint8_t nextState, uint8_t pushFront, uint32_t param0, uint32_t param1)
{
    AppMsgqMessage_t message;

    (void)memset(&message, 0, sizeof(message));
    message.type = APP_MSGQ_TYPE_STATE_COMMAND;
    message.sourceId = (uint8_t)sourceId;
    message.reserved0 = nextState;
    message.reserved1 = pushFront;
    message.tickMs = HAL_GetTick();
    message.param0 = param0;
    message.param1 = param1;

    return (pushFront == APP_TRUE) ? App_MsgqPushFront(&message) : App_MsgqPushBack(&message);
}

AppStatus_t App_TasksCompleteRun(AppTaskModuleContext_t *p_module, AppStatus_t status)
{
    uint32_t nowTick;

    if (p_module == NULL)
    {
        return status;
    }

    nowTick = HAL_GetTick();
    p_module->initialized = APP_TRUE;
    p_module->lastStatus = status;
    p_module->lastRunTickMs = nowTick;
    p_module->lastHeartbeatTickMs = nowTick;
    p_module->runCount++;

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

    g_appTasksContext.modules[APP_TASK_ID_DEBUG].periodMs = APP_SCHEDULER_TASK_DEBUG_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_WATCHDOG].periodMs = APP_SCHEDULER_TASK_WATCHDOG_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_HOUSEKEEPING].periodMs = APP_SCHEDULER_TASK_HOUSEKEEPING_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_POWER].periodMs = APP_SCHEDULER_TASK_POWER_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_STORAGE].periodMs = APP_SCHEDULER_TASK_STORAGE_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_METER].periodMs = APP_SCHEDULER_TASK_METER_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_NFC].periodMs = APP_SCHEDULER_TASK_NFC_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_AUX].periodMs = APP_SCHEDULER_TASK_AUX_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_NBIOT].periodMs = APP_SCHEDULER_TASK_NBIOT_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_SERVER].periodMs = APP_SCHEDULER_TASK_SERVER_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_RTC].periodMs = APP_SCHEDULER_TASK_RTC_PERIOD_MS;
    g_appTasksContext.modules[APP_TASK_ID_MAIN].periodMs = APP_SCHEDULER_TASK_MAIN_PERIOD_MS;

    g_appTasksContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_TasksRegisterAll(void)
{
    APP_RETURN_IF_FALSE(g_appTasksContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_MAIN, App_TaskMain, APP_SCHEDULER_TASK_MAIN_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
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

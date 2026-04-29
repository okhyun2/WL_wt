#include "app_tasks.h"
#include "app_task_state_defs.h"

#include <string.h>

#include "app_build_config.h"
#include "app_debug.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_system.h"

#define APP_TASK_MAIN_RESET_HOLD_MS    (1000u)

static AppTaskMainMonitor_t g_appTaskMainMonitors[APP_TASK_ID_COUNT];
static AppTaskMainSummary_t g_appTaskMainSummary;
static AppTaskMainStorageResponse_t g_appTaskMainStorageResponse;

static AppTaskModuleContext_t *App_TaskMainGetVirtualModule(AppTaskId_t id)
{
    return App_TasksGetModuleContextMutable(id);
}

static void App_TaskMainUpdateMonitor(AppTaskId_t id)
{
    AppTaskModuleContext_t *p_module;
    AppTaskMainMonitor_t *p_monitor;

    if ((uint32_t)id >= (uint32_t)APP_TASK_ID_COUNT)
    {
        return;
    }

    p_module = App_TaskMainGetVirtualModule(id);
    if (p_module == NULL)
    {
        return;
    }

    p_monitor = &g_appTaskMainMonitors[id];
    p_monitor->state = p_module->state;
    p_monitor->busy = p_module->busy;
    p_monitor->eventPending = p_module->eventPending;
    p_monitor->alive = APP_TRUE;
    p_monitor->lastStatus = p_module->lastStatus;
    p_monitor->lastHeartbeatTickMs = HAL_GetTick();
    p_monitor->heartbeatCount++;
}

static void App_TaskMainSetDecision(AppTaskMainDecision_t decision, AppTaskModuleContext_t *p_mainModule)
{
    g_appTaskMainSummary.decision = decision;
    g_appTaskMainSummary.lastEvaluationTickMs = HAL_GetTick();
    g_appTaskMainSummary.currentState = (p_mainModule != NULL) ? p_mainModule->state : APP_TASK_MAIN_STATE_INIT;
    g_appTaskMainSummary.lastStateTickMs = HAL_GetTick();
    g_appTaskMainSummary.aliveCount = 1u;
    g_appTaskMainSummary.busyCount = ((p_mainModule != NULL) && (p_mainModule->busy == APP_TRUE)) ? 1u : 0u;
    g_appTaskMainSummary.staleCount = 0u;
}

static void App_TaskMainMarkVirtualState(AppTaskId_t id,
                                         uint8_t nextState,
                                         uint8_t busy,
                                         uint8_t eventPending,
                                         AppStatus_t status)
{
    AppTaskModuleContext_t *p_module;

    p_module = App_TaskMainGetVirtualModule(id);
    if (p_module == NULL)
    {
        return;
    }

    p_module->state = nextState;
    p_module->busy = busy;
    p_module->eventPending = eventPending;
    p_module->lastActionTickMs = HAL_GetTick();
    (void)App_TasksCompleteRun(p_module, status);
    App_TaskMainUpdateMonitor(id);
}

static void App_TaskMainSignalEvent(AppTaskId_t id)
{
    AppTaskModuleContext_t *p_module;

    p_module = App_TaskMainGetVirtualModule(id);
    if (p_module != NULL)
    {
        p_module->eventPending = APP_TRUE;
        p_module->lastActionTickMs = HAL_GetTick();
        App_TaskMainUpdateMonitor(id);
    }
}

static uint8_t App_TaskMainIsValidState(uint8_t state)
{
    switch (state)
    {
        case APP_TASK_MAIN_STATE_BOOT:
        case APP_TASK_MAIN_STATE_IDLE:
        case APP_TASK_MAIN_STATE_DEBUG_POLL:
        case APP_TASK_MAIN_STATE_HOUSEKEEPING_INIT:
        case APP_TASK_MAIN_STATE_HOUSEKEEPING_SNAPSHOT:
        case APP_TASK_MAIN_STATE_HOUSEKEEPING_ROTATE:
        case APP_TASK_MAIN_STATE_POWER_INIT:
        case APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST:
        case APP_TASK_MAIN_STATE_METER_INIT:
        case APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER:
        case APP_TASK_MAIN_STATE_METER_SEND_REQUEST:
        case APP_TASK_MAIN_STATE_METER_PARSE_REPLY:
        case APP_TASK_MAIN_STATE_NFC_INIT:
        case APP_TASK_MAIN_STATE_NFC_WAIT_EVENT:
        case APP_TASK_MAIN_STATE_NFC_EXCHANGE:
        case APP_TASK_MAIN_STATE_AUX_INIT:
        case APP_TASK_MAIN_STATE_AUX_TRIGGER_MEASURE:
        case APP_TASK_MAIN_STATE_AUX_READ_RESULT:
        case APP_TASK_MAIN_STATE_NBIOT_INIT:
        case APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE:
        case APP_TASK_MAIN_STATE_NBIOT_POWER_ON:
        case APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT:
        case APP_TASK_MAIN_STATE_SERVER_INIT:
        case APP_TASK_MAIN_STATE_SERVER_PREPARE_PACKET:
        case APP_TASK_MAIN_STATE_SERVER_REQUEST_SEND:
        case APP_TASK_MAIN_STATE_RTC_INIT:
        case APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE:
        case APP_TASK_MAIN_STATE_RTC_APPLY_SYNC:
        case APP_TASK_MAIN_STATE_FAULT:
            return APP_TRUE;

        default:
            return APP_FALSE;
    }
}

static uint8_t App_TaskMainIsPeriodicDue(AppTaskId_t id)
{
    const AppTaskModuleContext_t *p_module;
    uint32_t nowTick;

    p_module = App_TasksGetModuleContext(id);
    if ((p_module == NULL) || (p_module->periodMs == 0u))
    {
        return APP_FALSE;
    }

    nowTick = HAL_GetTick();
    if (p_module->lastRunTickMs == 0u)
    {
        return APP_TRUE;
    }

    return ((nowTick - p_module->lastRunTickMs) >= p_module->periodMs) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_TaskMainMapVirtualToState(AppTaskId_t id, uint8_t virtualState)
{
    switch (id)
    {
        case APP_TASK_ID_DEBUG:
            return APP_TASK_MAIN_STATE_DEBUG_POLL;

        case APP_TASK_ID_HOUSEKEEPING:
            switch (virtualState)
            {
                case APP_TASK_HOUSEKEEPING_STATE_INIT: return APP_TASK_MAIN_STATE_HOUSEKEEPING_INIT;
                case APP_TASK_HOUSEKEEPING_STATE_ROTATE: return APP_TASK_MAIN_STATE_HOUSEKEEPING_ROTATE;
                case APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT:
                default: return APP_TASK_MAIN_STATE_HOUSEKEEPING_SNAPSHOT;
            }

        case APP_TASK_ID_POWER:
            return (virtualState == APP_TASK_POWER_STATE_INIT) ? APP_TASK_MAIN_STATE_POWER_INIT : APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST;

        case APP_TASK_ID_METER:
            switch (virtualState)
            {
                case APP_TASK_METER_STATE_INIT: return APP_TASK_MAIN_STATE_METER_INIT;
                case APP_TASK_METER_STATE_SEND_REQUEST: return APP_TASK_MAIN_STATE_METER_SEND_REQUEST;
                case APP_TASK_METER_STATE_PARSE_REPLY: return APP_TASK_MAIN_STATE_METER_PARSE_REPLY;
                case APP_TASK_METER_STATE_WAIT_TRIGGER:
                default: return APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER;
            }

        case APP_TASK_ID_NFC:
            switch (virtualState)
            {
                case APP_TASK_NFC_STATE_INIT: return APP_TASK_MAIN_STATE_NFC_INIT;
                case APP_TASK_NFC_STATE_EXCHANGE: return APP_TASK_MAIN_STATE_NFC_EXCHANGE;
                case APP_TASK_NFC_STATE_WAIT_EVENT:
                default: return APP_TASK_MAIN_STATE_NFC_WAIT_EVENT;
            }

        case APP_TASK_ID_AUX:
            switch (virtualState)
            {
                case APP_TASK_AUX_STATE_INIT: return APP_TASK_MAIN_STATE_AUX_INIT;
                case APP_TASK_AUX_STATE_READ_RESULT: return APP_TASK_MAIN_STATE_AUX_READ_RESULT;
                case APP_TASK_AUX_STATE_TRIGGER_MEASURE:
                default: return APP_TASK_MAIN_STATE_AUX_TRIGGER_MEASURE;
            }

        case APP_TASK_ID_NBIOT:
            switch (virtualState)
            {
                case APP_TASK_NBIOT_STATE_INIT: return APP_TASK_MAIN_STATE_NBIOT_INIT;
                case APP_TASK_NBIOT_STATE_POWER_ON: return APP_TASK_MAIN_STATE_NBIOT_POWER_ON;
                case APP_TASK_NBIOT_STATE_EXCHANGE_AT: return APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT;
                case APP_TASK_NBIOT_STATE_DECIDE_WAKE:
                default: return APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE;
            }

        case APP_TASK_ID_SERVER:
            switch (virtualState)
            {
                case APP_TASK_SERVER_STATE_INIT: return APP_TASK_MAIN_STATE_SERVER_INIT;
                case APP_TASK_SERVER_STATE_REQUEST_SEND: return APP_TASK_MAIN_STATE_SERVER_REQUEST_SEND;
                case APP_TASK_SERVER_STATE_PREPARE_PACKET:
                default: return APP_TASK_MAIN_STATE_SERVER_PREPARE_PACKET;
            }

        case APP_TASK_ID_RTC:
            switch (virtualState)
            {
                case APP_TASK_RTC_STATE_INIT: return APP_TASK_MAIN_STATE_RTC_INIT;
                case APP_TASK_RTC_STATE_APPLY_SYNC: return APP_TASK_MAIN_STATE_RTC_APPLY_SYNC;
                case APP_TASK_RTC_STATE_CHECK_SCHEDULE:
                default: return APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE;
            }

        default:
            return APP_TASK_MAIN_STATE_FAULT;
    }
}

static AppStatus_t App_TaskMainQueueInitialStates(void)
{
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_POWER_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_HOUSEKEEPING_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_METER_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_NFC_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_AUX_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_NBIOT_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_SERVER_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_RTC_INIT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskMainServiceDebugConsole(void)
{
    AppStatus_t status;

    App_TaskMainMarkVirtualState(APP_TASK_ID_DEBUG, APP_TASK_DEBUG_STATE_POLL, APP_FALSE, APP_FALSE, APP_STATUS_OK);
    status = App_DebugConsoleProcess();
    App_TaskMainMarkVirtualState(APP_TASK_ID_DEBUG, APP_TASK_DEBUG_STATE_POLL, APP_FALSE, APP_FALSE, status);
    return status;
}

static AppStatus_t App_TaskMainScheduleOnePeriodicState(void)
{
    static const AppTaskId_t periodicOrder[] =
    {
        APP_TASK_ID_HOUSEKEEPING,
        APP_TASK_ID_METER,
        APP_TASK_ID_NFC,
        APP_TASK_ID_AUX,
        APP_TASK_ID_NBIOT,
        APP_TASK_ID_SERVER,
        APP_TASK_ID_RTC
    };
    uint32_t index;
    const AppTaskModuleContext_t *p_module;
    uint8_t nextState;

    for (index = 0u; index < (sizeof(periodicOrder) / sizeof(periodicOrder[0])); index++)
    {
        if (App_TaskMainIsPeriodicDue(periodicOrder[index]) != APP_TRUE)
        {
            continue;
        }

        p_module = App_TasksGetModuleContext(periodicOrder[index]);
        if (p_module == NULL)
        {
            continue;
        }

        nextState = App_TaskMainMapVirtualToState(periodicOrder[index], p_module->state);
        if (App_TaskMainIsValidState(nextState) != APP_TRUE)
        {
            nextState = APP_TASK_MAIN_STATE_FAULT;
        }

        return App_TaskMainQueueStateCommandBack(nextState, 0u, 0u);
    }

    return APP_STATUS_MSGQ_EMPTY;
}

static void App_TaskMainExecuteResetBoot(void)
{
    (void)APP_LOGW("MAIN", "executing reset boot-hold=%lu ms",
                         (unsigned long)APP_TASK_MAIN_RESET_HOLD_MS);
    App_HwSetChargeBoot0(GPIO_PIN_SET);
    HAL_Delay(APP_TASK_MAIN_RESET_HOLD_MS);
    __disable_irq();
    NVIC_SystemReset();
    while (1)
    {
    }
}

static AppStatus_t App_TaskMainExecuteState(AppTaskModuleContext_t *p_mainModule, uint8_t currentState)
{
    AppStatus_t status;
    AppTaskModuleContext_t *p_virtual;

    APP_RETURN_IF_FALSE((p_mainModule != NULL), APP_STATUS_INVALID_PARAM);

    p_mainModule->busy = (currentState == APP_TASK_MAIN_STATE_IDLE) ? APP_FALSE : APP_TRUE;
    p_mainModule->eventPending = (currentState == APP_TASK_MAIN_STATE_IDLE) ? APP_FALSE : APP_TRUE;
    p_mainModule->lastActionTickMs = HAL_GetTick();
    g_appTaskMainSummary.currentState = currentState;
    g_appTaskMainSummary.lastStateTickMs = HAL_GetTick();
    g_appTaskMainSummary.lowPowerRequested = APP_FALSE;
    APP_RETURN_IF_FALSE(App_SystemRequestLowPower(APP_FALSE) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);

    switch (currentState)
    {
        case APP_TASK_MAIN_STATE_BOOT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_DEBUG, APP_TASK_DEBUG_STATE_POLL, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_HOUSEKEEPING, APP_TASK_HOUSEKEEPING_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_POWER, APP_TASK_POWER_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_METER, APP_TASK_METER_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_NFC, APP_TASK_NFC_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_AUX, APP_TASK_AUX_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_NBIOT, APP_TASK_NBIOT_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_SERVER, APP_TASK_SERVER_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainMarkVirtualState(APP_TASK_ID_RTC, APP_TASK_RTC_STATE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueInitialStates() == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_BOOT, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_DEBUG_POLL:
            APP_RETURN_IF_FALSE(App_TaskMainServiceDebugConsole() == APP_STATUS_OK, APP_STATUS_UART_RX_FAILED);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_HOUSEKEEPING_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_HOUSEKEEPING, APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_HOUSEKEEPING_SNAPSHOT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_HOUSEKEEPING, APP_TASK_HOUSEKEEPING_STATE_ROTATE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandBack(APP_TASK_MAIN_STATE_HOUSEKEEPING_ROTATE, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_HOUSEKEEPING_ROTATE:
            App_TaskMainMarkVirtualState(APP_TASK_ID_HOUSEKEEPING, APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_POWER_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_POWER, APP_TASK_POWER_STATE_WAIT_REQUEST, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST:
            App_TaskMainMarkVirtualState(APP_TASK_ID_POWER, APP_TASK_POWER_STATE_WAIT_REQUEST, APP_TRUE, APP_TRUE, APP_STATUS_OK);
            if (g_appTaskMainSummary.lastCommandParam0 == (uint32_t)APP_POWER_QUEUE_OP_RESET_BOOT)
            {
                App_TaskMainExecuteResetBoot();
            }
            App_TaskMainMarkVirtualState(APP_TASK_ID_POWER, APP_TASK_POWER_STATE_WAIT_REQUEST, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_METER_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_METER, APP_TASK_METER_STATE_WAIT_TRIGGER, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER:
            p_virtual = App_TaskMainGetVirtualModule(APP_TASK_ID_METER);
            if ((p_virtual != NULL) && (p_virtual->eventPending == APP_TRUE))
            {
                App_TaskMainMarkVirtualState(APP_TASK_ID_METER, APP_TASK_METER_STATE_SEND_REQUEST, APP_TRUE, APP_TRUE, APP_STATUS_OK);
                APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_METER_SEND_REQUEST, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            }
            else
            {
                App_TaskMainMarkVirtualState(APP_TASK_ID_METER, APP_TASK_METER_STATE_WAIT_TRIGGER, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            }
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_METER_SEND_REQUEST:
            App_TaskMainMarkVirtualState(APP_TASK_ID_METER, APP_TASK_METER_STATE_PARSE_REPLY, APP_TRUE, APP_TRUE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_METER_PARSE_REPLY, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_METER_PARSE_REPLY:
            App_TaskMainMarkVirtualState(APP_TASK_ID_METER, APP_TASK_METER_STATE_WAIT_TRIGGER, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NFC_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_NFC, APP_TASK_NFC_STATE_WAIT_EVENT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NFC_WAIT_EVENT:
            p_virtual = App_TaskMainGetVirtualModule(APP_TASK_ID_NFC);
            if ((p_virtual != NULL) && (p_virtual->eventPending == APP_TRUE))
            {
                App_TaskMainMarkVirtualState(APP_TASK_ID_NFC, APP_TASK_NFC_STATE_EXCHANGE, APP_TRUE, APP_TRUE, APP_STATUS_OK);
                APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_NFC_EXCHANGE, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            }
            else
            {
                App_TaskMainMarkVirtualState(APP_TASK_ID_NFC, APP_TASK_NFC_STATE_WAIT_EVENT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            }
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NFC_EXCHANGE:
            App_TaskMainMarkVirtualState(APP_TASK_ID_NFC, APP_TASK_NFC_STATE_WAIT_EVENT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_AUX_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_AUX, APP_TASK_AUX_STATE_TRIGGER_MEASURE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_AUX_TRIGGER_MEASURE:
            App_TaskMainMarkVirtualState(APP_TASK_ID_AUX, APP_TASK_AUX_STATE_READ_RESULT, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_AUX_READ_RESULT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_AUX_READ_RESULT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_AUX, APP_TASK_AUX_STATE_TRIGGER_MEASURE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NBIOT_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_NBIOT, APP_TASK_NBIOT_STATE_DECIDE_WAKE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE:
            App_TaskMainMarkVirtualState(APP_TASK_ID_NBIOT, APP_TASK_NBIOT_STATE_POWER_ON, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_NBIOT_POWER_ON, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NBIOT_POWER_ON:
            (void)App_SystemSetNbiotPowered(APP_TRUE);
            App_TaskMainMarkVirtualState(APP_TASK_ID_NBIOT, APP_TASK_NBIOT_STATE_EXCHANGE_AT, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT:
            (void)App_SystemSetNbiotPowered(APP_FALSE);
            App_TaskMainMarkVirtualState(APP_TASK_ID_NBIOT, APP_TASK_NBIOT_STATE_DECIDE_WAKE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_SERVER_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_SERVER, APP_TASK_SERVER_STATE_PREPARE_PACKET, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_SERVER_PREPARE_PACKET:
            App_TaskMainMarkVirtualState(APP_TASK_ID_SERVER, APP_TASK_SERVER_STATE_REQUEST_SEND, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_SERVER_REQUEST_SEND, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_SERVER_REQUEST_SEND:
            App_TaskMainMarkVirtualState(APP_TASK_ID_SERVER, APP_TASK_SERVER_STATE_PREPARE_PACKET, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_RTC_INIT:
            App_TaskMainMarkVirtualState(APP_TASK_ID_RTC, APP_TASK_RTC_STATE_CHECK_SCHEDULE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE:
            App_TaskMainMarkVirtualState(APP_TASK_ID_RTC, APP_TASK_RTC_STATE_APPLY_SYNC, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            APP_RETURN_IF_FALSE(App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_RTC_APPLY_SYNC, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_RTC_APPLY_SYNC:
            App_TaskMainMarkVirtualState(APP_TASK_ID_RTC, APP_TASK_RTC_STATE_CHECK_SCHEDULE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_RUN_ACTIVE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_FAULT:
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_REQUIRE_SAFE, p_mainModule);
            break;

        case APP_TASK_MAIN_STATE_IDLE:
        default:
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_ALLOW_IDLE, p_mainModule);
            break;
    }

    p_mainModule->busy = APP_FALSE;
    p_mainModule->eventPending = APP_FALSE;
    APP_TASK_SET_STATE(p_mainModule, APP_TASK_MAIN_STATE_DISPATCH);
    g_appTaskMainSummary.currentState = APP_TASK_MAIN_STATE_DISPATCH;
    g_appTaskMainSummary.lastStateTickMs = HAL_GetTick();
    return APP_STATUS_OK;
}

AppStatus_t App_TaskMain(void *p_context)
{
    AppTaskModuleContext_t *p_mainModule;
    AppMsgqMessage_t message;
    AppStatus_t status;
    uint8_t nextState;

    p_mainModule = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_mainModule != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_mainModule->state)
    {
        case APP_TASK_MAIN_STATE_INIT:
            (void)memset(g_appTaskMainMonitors, 0, sizeof(g_appTaskMainMonitors));
            (void)memset(&g_appTaskMainSummary, 0, sizeof(g_appTaskMainSummary));
            (void)memset(&g_appTaskMainStorageResponse, 0, sizeof(g_appTaskMainStorageResponse));
            APP_TASK_SET_STATE(p_mainModule, APP_TASK_MAIN_STATE_DISPATCH);
            g_appTaskMainSummary.currentState = APP_TASK_MAIN_STATE_DISPATCH;
            g_appTaskMainSummary.lastStateTickMs = HAL_GetTick();
            (void)App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_BOOT, 0u, 0u);
            break;

        case APP_TASK_MAIN_STATE_DISPATCH:
            APP_RETURN_IF_FALSE(App_TaskMainServiceDebugConsole() == APP_STATUS_OK, APP_STATUS_UART_RX_FAILED);

            status = App_MsgqTakeFirstByType(APP_MSGQ_TYPE_STATE_COMMAND, &message);
            if (status == APP_STATUS_OK)
            {
                nextState = message.reserved0;
                if (App_TaskMainIsValidState(nextState) != APP_TRUE)
                {
                    nextState = APP_TASK_MAIN_STATE_FAULT;
                }

                if (message.param0 != 0u)
                {
                    if ((nextState == APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER) ||
                        (nextState == APP_TASK_MAIN_STATE_METER_SEND_REQUEST) ||
                        (nextState == APP_TASK_MAIN_STATE_METER_PARSE_REPLY))
                    {
                        App_TaskMainSignalEvent(APP_TASK_ID_METER);
                    }
                    else if ((nextState == APP_TASK_MAIN_STATE_NFC_WAIT_EVENT) ||
                             (nextState == APP_TASK_MAIN_STATE_NFC_EXCHANGE))
                    {
                        App_TaskMainSignalEvent(APP_TASK_ID_NFC);
                    }
                    else if ((nextState == APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE) ||
                             (nextState == APP_TASK_MAIN_STATE_RTC_APPLY_SYNC))
                    {
                        App_TaskMainSignalEvent(APP_TASK_ID_RTC);
                    }
                    else if ((nextState == APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE) ||
                             (nextState == APP_TASK_MAIN_STATE_NBIOT_POWER_ON) ||
                             (nextState == APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT))
                    {
                        App_TaskMainSignalEvent(APP_TASK_ID_NBIOT);
                    }
                }

                g_appTaskMainSummary.lastQueuedState = nextState;
                g_appTaskMainSummary.lastDequeFromFront = message.reserved1;
                g_appTaskMainSummary.lastCommandTickMs = message.tickMs;
                g_appTaskMainSummary.lastCommandParam0 = message.param0;
                g_appTaskMainSummary.lastCommandParam1 = message.param1;
                g_appTaskMainSummary.processedMessageCount++;
                g_appTaskMainSummary.transitionCount++;
                APP_TASK_SET_STATE(p_mainModule, nextState);
                APP_RETURN_IF_FALSE(App_TaskMainExecuteState(p_mainModule, nextState) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
                break;
            }
            APP_RETURN_IF_FALSE((status == APP_STATUS_MSGQ_EMPTY), status);

            status = App_TaskMainScheduleOnePeriodicState();
            if (status == APP_STATUS_OK)
            {
                break;
            }
            #if (APP_RTC_WAKEUP_PERIOD_MS != 0)
            APP_RETURN_IF_FALSE((status == APP_STATUS_MSGQ_EMPTY), status);

            g_appTaskMainSummary.queueEmptyStopCount++;
            g_appTaskMainSummary.lowPowerRequested = APP_TRUE;
            APP_RETURN_IF_FALSE(App_SystemRequestLowPower(APP_TRUE) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_mainModule, APP_TASK_MAIN_STATE_IDLE);
            g_appTaskMainSummary.currentState = APP_TASK_MAIN_STATE_IDLE;
            g_appTaskMainSummary.lastStateTickMs = HAL_GetTick();
            App_TaskMainSetDecision(APP_TASK_MAIN_DECISION_ALLOW_IDLE, p_mainModule);
            #endif
            break;

        case APP_TASK_MAIN_STATE_IDLE:
            p_mainModule->busy = APP_FALSE;
            p_mainModule->eventPending = APP_FALSE;
            APP_TASK_SET_STATE(p_mainModule, APP_TASK_MAIN_STATE_DISPATCH);
            g_appTaskMainSummary.currentState = APP_TASK_MAIN_STATE_DISPATCH;
            g_appTaskMainSummary.lastStateTickMs = HAL_GetTick();
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskMainExecuteState(p_mainModule, p_mainModule->state) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            break;
    }

    App_TaskMainUpdateMonitor(APP_TASK_ID_MAIN);
    return App_TasksCompleteRun(p_mainModule, APP_STATUS_OK);
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

uint8_t App_TaskMainGetState(void)
{
    return g_appTaskMainSummary.currentState;
}

const char *App_TaskMainGetStateString(void)
{
    return App_TasksGetStateName(APP_TASK_ID_MAIN, g_appTaskMainSummary.currentState);
}

const AppTaskMainStorageResponse_t *App_TaskMainGetStorageResponse(void)
{
    return &g_appTaskMainStorageResponse;
}

AppStatus_t App_TaskMainRequestStorageSave(AppStorageTarget_t backend, uint32_t userData0, uint32_t userData1)
{
    (void)backend;
    (void)userData0;
    (void)userData1;
    return APP_STATUS_INVALID_PARAM;
}

AppStatus_t App_TaskMainRequestStorageLoad(AppStorageTarget_t backend)
{
    (void)backend;
    return APP_STATUS_INVALID_PARAM;
}

AppStatus_t App_TaskMainRequestPowerResetBoot(void)
{
    return App_TaskMainQueueStateCommandFront(APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST,
                                              (uint32_t)APP_POWER_QUEUE_OP_RESET_BOOT,
                                              0u);
}

AppStatus_t App_TaskMainQueueStateCommandFront(uint8_t nextState, uint32_t param0, uint32_t param1)
{
    APP_RETURN_IF_FALSE(App_TaskMainIsValidState(nextState) == APP_TRUE, APP_STATUS_INVALID_PARAM);
    return App_TasksPublishStateCommand(APP_TASK_ID_MAIN, nextState, APP_TRUE, param0, param1);
}

AppStatus_t App_TaskMainQueueStateCommandBack(uint8_t nextState, uint32_t param0, uint32_t param1)
{
    APP_RETURN_IF_FALSE(App_TaskMainIsValidState(nextState) == APP_TRUE, APP_STATUS_INVALID_PARAM);
    return App_TasksPublishStateCommand(APP_TASK_ID_MAIN, nextState, APP_FALSE, param0, param1);
}

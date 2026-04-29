#include "app_fsm.h"

#include <string.h>

#include "app_build_config.h"
#include "app_debug.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_system.h"

static AppFsmContext_t g_appFsmContext;

static const char *App_FsmGetDecisionNameInternal(AppFsmDecision_t decision)
{
    switch (decision)
    {
        case APP_FSM_DECISION_BOOT:         return "BOOT";
        case APP_FSM_DECISION_RUN_ACTIVE:   return "RUN_ACTIVE";
        case APP_FSM_DECISION_ALLOW_IDLE:   return "ALLOW_IDLE";
        case APP_FSM_DECISION_REQUIRE_SAFE: return "REQUIRE_SAFE";
        default:                            return "UNKNOWN";
    }
}

static uint8_t App_FsmIsValidState(uint8_t state)
{
    switch (state)
    {
        case APP_FSM_STATE_INIT:
        case APP_FSM_STATE_BOOT:
        case APP_FSM_STATE_IDLE:
        case APP_FSM_STATE_DEBUG_POLL:
        case APP_FSM_STATE_HOUSEKEEPING_INIT:
        case APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT:
        case APP_FSM_STATE_HOUSEKEEPING_ROTATE:
        case APP_FSM_STATE_POWER_INIT:
        case APP_FSM_STATE_POWER_WAIT_REQUEST:
        case APP_FSM_STATE_METER_INIT:
        case APP_FSM_STATE_METER_WAIT_TRIGGER:
        case APP_FSM_STATE_METER_SEND_REQUEST:
        case APP_FSM_STATE_METER_PARSE_REPLY:
        case APP_FSM_STATE_NFC_INIT:
        case APP_FSM_STATE_NFC_WAIT_EVENT:
        case APP_FSM_STATE_NFC_EXCHANGE:
        case APP_FSM_STATE_AUX_INIT:
        case APP_FSM_STATE_AUX_TRIGGER_MEASURE:
        case APP_FSM_STATE_AUX_READ_RESULT:
        case APP_FSM_STATE_NBIOT_INIT:
        case APP_FSM_STATE_NBIOT_DECIDE_WAKE:
        case APP_FSM_STATE_NBIOT_POWER_ON:
        case APP_FSM_STATE_NBIOT_EXCHANGE_AT:
        case APP_FSM_STATE_SERVER_INIT:
        case APP_FSM_STATE_SERVER_PREPARE_PACKET:
        case APP_FSM_STATE_SERVER_REQUEST_SEND:
        case APP_FSM_STATE_RTC_INIT:
        case APP_FSM_STATE_RTC_WAKE_SERVICE:
        case APP_FSM_STATE_RTC_APPLY_SYNC:
        case APP_FSM_STATE_LPTIM_INIT:
        case APP_FSM_STATE_LPTIM_WAKE_SERVICE:
        case APP_FSM_STATE_LPTIM_APPLY_SYNC:
        case APP_FSM_STATE_FAULT:
            return APP_TRUE;

        default:
            return APP_FALSE;
    }
}

static const char *App_FsmGetComponentNameInternal(AppFsmComponentId_t id)
{
    switch (id)
    {
        case APP_FSM_COMPONENT_DEBUG:        return "debug";
        case APP_FSM_COMPONENT_HOUSEKEEPING: return "housekeeping";
        case APP_FSM_COMPONENT_POWER:        return "power";
        case APP_FSM_COMPONENT_METER:        return "meter";
        case APP_FSM_COMPONENT_NFC:          return "nfc";
        case APP_FSM_COMPONENT_AUX:          return "aux";
        case APP_FSM_COMPONENT_NBIOT:        return "nbiot";
        case APP_FSM_COMPONENT_SERVER:       return "server";
        case APP_FSM_COMPONENT_RTC:          return "rtc";
        default:                             return "unknown";
    }
}

static uint32_t App_FsmGetComponentInterval(AppFsmComponentId_t id)
{
    switch (id)
    {
        case APP_FSM_COMPONENT_DEBUG:        return APP_FSM_DEBUG_POLL_PERIOD_MS;
        case APP_FSM_COMPONENT_HOUSEKEEPING: return APP_FSM_HOUSEKEEPING_PERIOD_MS;
        case APP_FSM_COMPONENT_POWER:        return 0u;
        case APP_FSM_COMPONENT_METER:        return APP_FSM_METER_PERIOD_MS;
        case APP_FSM_COMPONENT_NFC:          return APP_FSM_NFC_PERIOD_MS;
        case APP_FSM_COMPONENT_AUX:          return APP_FSM_AUX_PERIOD_MS;
        case APP_FSM_COMPONENT_NBIOT:        return APP_FSM_NBIOT_PERIOD_MS;
        case APP_FSM_COMPONENT_SERVER:       return APP_FSM_SERVER_PERIOD_MS;
        case APP_FSM_COMPONENT_RTC:          return APP_FSM_RTC_PERIOD_MS;
        default:                             return 0u;
    }
}

static AppFsmComponentContext_t *App_FsmGetComponentMutable(AppFsmComponentId_t id)
{
    if ((uint32_t)id >= (uint32_t)APP_FSM_COMPONENT_COUNT)
    {
        return NULL;
    }

    return &g_appFsmContext.components[id];
}

static void App_FsmSetDecision(AppFsmDecision_t decision)
{
    g_appFsmContext.summary.decision = decision;
}

static void App_FsmCompleteComponentRun(AppFsmComponentContext_t *p_component, AppStatus_t status)
{
    uint32_t nowTick;

    if (p_component == NULL)
    {
        return;
    }

    nowTick = HAL_GetTick();
    p_component->initialized = APP_TRUE;
    p_component->lastStatus = status;
    p_component->lastRunTickMs = nowTick;
    p_component->lastActionTickMs = nowTick;
    p_component->runCount++;
}

static void App_FsmMarkComponent(AppFsmComponentId_t id,
                                 uint8_t state,
                                 uint8_t busy,
                                 uint8_t eventPending,
                                 AppStatus_t status)
{
    AppFsmComponentContext_t *p_component;

    p_component = App_FsmGetComponentMutable(id);
    if (p_component == NULL)
    {
        return;
    }

    p_component->state = state;
    p_component->busy = busy;
    p_component->eventPending = eventPending;
    App_FsmCompleteComponentRun(p_component, status);
}

static void App_FsmSignalEventForState(uint8_t nextState)
{
    AppFsmComponentContext_t *p_component;
    AppFsmComponentId_t componentId;

    componentId = APP_FSM_COMPONENT_COUNT;
    switch (nextState)
    {
        case APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT:
        case APP_FSM_STATE_HOUSEKEEPING_ROTATE:
            componentId = APP_FSM_COMPONENT_HOUSEKEEPING;
            break;

        case APP_FSM_STATE_POWER_WAIT_REQUEST:
            componentId = APP_FSM_COMPONENT_POWER;
            break;

        case APP_FSM_STATE_METER_WAIT_TRIGGER:
        case APP_FSM_STATE_METER_SEND_REQUEST:
        case APP_FSM_STATE_METER_PARSE_REPLY:
            componentId = APP_FSM_COMPONENT_METER;
            break;

        case APP_FSM_STATE_NFC_WAIT_EVENT:
        case APP_FSM_STATE_NFC_EXCHANGE:
            componentId = APP_FSM_COMPONENT_NFC;
            break;

        case APP_FSM_STATE_AUX_TRIGGER_MEASURE:
        case APP_FSM_STATE_AUX_READ_RESULT:
            componentId = APP_FSM_COMPONENT_AUX;
            break;

        case APP_FSM_STATE_NBIOT_DECIDE_WAKE:
        case APP_FSM_STATE_NBIOT_POWER_ON:
        case APP_FSM_STATE_NBIOT_EXCHANGE_AT:
            componentId = APP_FSM_COMPONENT_NBIOT;
            break;

        case APP_FSM_STATE_SERVER_PREPARE_PACKET:
        case APP_FSM_STATE_SERVER_REQUEST_SEND:
            componentId = APP_FSM_COMPONENT_SERVER;
            break;

        case APP_FSM_STATE_RTC_WAKE_SERVICE:
        case APP_FSM_STATE_RTC_APPLY_SYNC:
            componentId = APP_FSM_COMPONENT_RTC;
            break;

        default:
            break;
    }

    if ((uint32_t)componentId >= (uint32_t)APP_FSM_COMPONENT_COUNT)
    {
        return;
    }

    p_component = App_FsmGetComponentMutable(componentId);
    if (p_component == NULL)
    {
        return;
    }

    p_component->eventPending = APP_TRUE;
    p_component->lastActionTickMs = HAL_GetTick();
}

static AppStatus_t App_FsmPublishStateCommand(uint8_t nextState, uint8_t pushFront, uint32_t eventParam, uint32_t param0)
{
    AppMsgqMessage_t message;

    APP_RETURN_IF_FALSE(App_FsmIsValidState(nextState) == APP_TRUE, APP_STATUS_INVALID_PARAM);

    (void)memset(&message, 0, sizeof(message));
    message.type = APP_MSGQ_TYPE_STATE_COMMAND;
    message.nextState = nextState;
    message.pushFront = pushFront;
    message.tickMs = HAL_GetTick();
    message.eventParam = eventParam;
    message.param0 = param0;

    return (pushFront == APP_TRUE) ? App_MsgqPushFront(&message) : App_MsgqPushBack(&message);
}

static uint8_t App_FsmIsPeriodicDue(AppFsmComponentId_t id)
{
    const AppFsmComponentContext_t *p_component;
    uint32_t nowTick;

    p_component = App_FsmGetComponent(id);
    if ((p_component == NULL) || (p_component->intervalMs == 0u))
    {
        return APP_FALSE;
    }

    nowTick = HAL_GetTick();
    if (p_component->lastRunTickMs == 0u)
    {
        return APP_TRUE;
    }

    return ((nowTick - p_component->lastRunTickMs) >= p_component->intervalMs) ? APP_TRUE : APP_FALSE;
}

static uint8_t App_FsmMapComponentToState(AppFsmComponentId_t id)
{
    const AppFsmComponentContext_t *p_component;

    p_component = App_FsmGetComponent(id);
    if (p_component == NULL)
    {
        return APP_FSM_STATE_FAULT;
    }

    switch (id)
    {
        case APP_FSM_COMPONENT_HOUSEKEEPING:
            switch (p_component->state)
            {
                case APP_FSM_STATE_HOUSEKEEPING_INIT:     return APP_FSM_STATE_HOUSEKEEPING_INIT;
                case APP_FSM_STATE_HOUSEKEEPING_ROTATE:   return APP_FSM_STATE_HOUSEKEEPING_ROTATE;
                case APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT:
                default:                                  return APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT;
            }

        case APP_FSM_COMPONENT_POWER:
            return (p_component->state == APP_FSM_STATE_POWER_INIT) ? APP_FSM_STATE_POWER_INIT : APP_FSM_STATE_POWER_WAIT_REQUEST;

        case APP_FSM_COMPONENT_METER:
            switch (p_component->state)
            {
                case APP_FSM_STATE_METER_INIT:           return APP_FSM_STATE_METER_INIT;
                case APP_FSM_STATE_METER_SEND_REQUEST:   return APP_FSM_STATE_METER_SEND_REQUEST;
                case APP_FSM_STATE_METER_PARSE_REPLY:    return APP_FSM_STATE_METER_PARSE_REPLY;
                case APP_FSM_STATE_METER_WAIT_TRIGGER:
                default:                                 return APP_FSM_STATE_METER_WAIT_TRIGGER;
            }

        case APP_FSM_COMPONENT_NFC:
            switch (p_component->state)
            {
                case APP_FSM_STATE_NFC_INIT:            return APP_FSM_STATE_NFC_INIT;
                case APP_FSM_STATE_NFC_EXCHANGE:        return APP_FSM_STATE_NFC_EXCHANGE;
                case APP_FSM_STATE_NFC_WAIT_EVENT:
                default:                                return APP_FSM_STATE_NFC_WAIT_EVENT;
            }

        case APP_FSM_COMPONENT_AUX:
            switch (p_component->state)
            {
                case APP_FSM_STATE_AUX_INIT:            return APP_FSM_STATE_AUX_INIT;
                case APP_FSM_STATE_AUX_READ_RESULT:     return APP_FSM_STATE_AUX_READ_RESULT;
                case APP_FSM_STATE_AUX_TRIGGER_MEASURE:
                default:                                return APP_FSM_STATE_AUX_TRIGGER_MEASURE;
            }

        case APP_FSM_COMPONENT_NBIOT:
            switch (p_component->state)
            {
                case APP_FSM_STATE_NBIOT_INIT:          return APP_FSM_STATE_NBIOT_INIT;
                case APP_FSM_STATE_NBIOT_POWER_ON:      return APP_FSM_STATE_NBIOT_POWER_ON;
                case APP_FSM_STATE_NBIOT_EXCHANGE_AT:   return APP_FSM_STATE_NBIOT_EXCHANGE_AT;
                case APP_FSM_STATE_NBIOT_DECIDE_WAKE:
                default:                                return APP_FSM_STATE_NBIOT_DECIDE_WAKE;
            }

        case APP_FSM_COMPONENT_SERVER:
            switch (p_component->state)
            {
                case APP_FSM_STATE_SERVER_INIT:             return APP_FSM_STATE_SERVER_INIT;
                case APP_FSM_STATE_SERVER_REQUEST_SEND:     return APP_FSM_STATE_SERVER_REQUEST_SEND;
                case APP_FSM_STATE_SERVER_PREPARE_PACKET:
                default:                                    return APP_FSM_STATE_SERVER_PREPARE_PACKET;
            }

        case APP_FSM_COMPONENT_RTC:
            switch (p_component->state)
            {
                case APP_FSM_STATE_RTC_INIT:                return APP_FSM_STATE_RTC_INIT;
                case APP_FSM_STATE_RTC_APPLY_SYNC:          return APP_FSM_STATE_RTC_APPLY_SYNC;
                case APP_FSM_STATE_RTC_WAKE_SERVICE:
                default:                                    return APP_FSM_STATE_RTC_WAKE_SERVICE;
            }

        case APP_FSM_COMPONENT_LPTIM:
            switch (p_component->state)
            {
                case APP_FSM_STATE_LPTIM_INIT:                return APP_FSM_STATE_LPTIM_INIT;
                case APP_FSM_STATE_LPTIM_APPLY_SYNC:          return APP_FSM_STATE_LPTIM_APPLY_SYNC;
                case APP_FSM_STATE_LPTIM_WAKE_SERVICE:
                default:                                    return APP_FSM_STATE_LPTIM_WAKE_SERVICE;
            }

        default:
            return APP_FSM_STATE_FAULT;
    }
}

static AppStatus_t App_FsmFindDuePeriodicState(uint8_t *p_state)
{
    static const AppFsmComponentId_t periodicOrder[] =
    {
        //APP_FSM_COMPONENT_POWER,
        APP_FSM_COMPONENT_HOUSEKEEPING
        //APP_FSM_COMPONENT_METER,
        //APP_FSM_COMPONENT_NFC,
        //APP_FSM_COMPONENT_AUX,
        //APP_FSM_COMPONENT_NBIOT,
        //APP_FSM_COMPONENT_SERVER,
        //APP_FSM_COMPONENT_RTC
    };
    uint32_t index;

    APP_RETURN_IF_FALSE((p_state != NULL), APP_STATUS_INVALID_PARAM);

    for (index = 0u; index < (sizeof(periodicOrder) / sizeof(periodicOrder[0])); index++)
    {
        if (App_FsmIsPeriodicDue(periodicOrder[index]) == APP_TRUE)
        {
            *p_state = App_FsmMapComponentToState(periodicOrder[index]);
            return APP_STATUS_OK;
        }
    }

    return APP_STATUS_MSGQ_EMPTY;
}

static AppStatus_t App_FsmServiceDebugConsole(void)
{
    AppStatus_t status;

    App_FsmMarkComponent(APP_FSM_COMPONENT_DEBUG, APP_FSM_STATE_DEBUG_POLL, APP_FALSE, APP_FALSE, APP_STATUS_OK);
    status = App_DebugConsoleProcess();
    App_FsmMarkComponent(APP_FSM_COMPONENT_DEBUG, APP_FSM_STATE_DEBUG_POLL, APP_FALSE, APP_FALSE, status);
    return status;
}

static void App_FsmExecuteResetBoot(void)
{
    (void)APP_LOGW("FSM", "executing reset boot-hold=%lu ms",
                         (unsigned long)APP_FSM_BOOT_RESET_HOLD_MS);
    App_HwSetChargeBoot0(GPIO_PIN_SET);
    HAL_Delay(APP_FSM_BOOT_RESET_HOLD_MS);
    __disable_irq();
    NVIC_SystemReset();
    while (1)
    {
    }
}

static AppStatus_t App_FsmExecuteState(uint8_t currentState, uint32_t commandParam0)
{
    AppFsmComponentContext_t *p_component;

    APP_RETURN_IF_FALSE(App_FsmIsValidState(currentState) == APP_TRUE, APP_STATUS_INVALID_PARAM);

    (void)APP_LOGD("FSM", "executing state:%s, commandParam:%d", App_FsmGetStateName(currentState), commandParam0);

    g_appFsmContext.summary.currentState = currentState;
    g_appFsmContext.summary.lastStateTickMs = HAL_GetTick();
    g_appFsmContext.summary.lastLoopDispatchCount = 1u;
    g_appFsmContext.summary.lowPowerRequested = APP_FALSE;
    APP_RETURN_IF_FALSE(App_SystemRequestLowPower(APP_FALSE) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);

    switch (currentState)
    {
        case APP_FSM_STATE_BOOT:
            App_FsmMarkComponent(APP_FSM_COMPONENT_DEBUG, APP_FSM_STATE_DEBUG_POLL, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_HOUSEKEEPING, APP_FSM_STATE_HOUSEKEEPING_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_POWER, APP_FSM_STATE_POWER_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_AUX, APP_FSM_STATE_AUX_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_SERVER, APP_FSM_STATE_SERVER_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_RTC, APP_FSM_STATE_RTC_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_LPTIM, APP_FSM_STATE_LPTIM_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);

            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_DEBUG_POLL, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_HOUSEKEEPING_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_POWER_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_METER_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NFC_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_AUX_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_SERVER_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_RTC_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_LPTIM_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);

            App_FsmSetDecision(APP_FSM_DECISION_BOOT);
            break;

        case APP_FSM_STATE_DEBUG_POLL:
            APP_RETURN_IF_FALSE(App_FsmServiceDebugConsole() == APP_STATUS_OK, APP_STATUS_UART_RX_FAILED);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_HOUSEKEEPING_INIT:
            App_FsmMarkComponent(APP_FSM_COMPONENT_HOUSEKEEPING, APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_HouseKeepingSnapshot() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmMarkComponent(APP_FSM_COMPONENT_HOUSEKEEPING, APP_FSM_STATE_HOUSEKEEPING_ROTATE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_HOUSEKEEPING_ROTATE:
            App_FsmMarkComponent(APP_FSM_COMPONENT_HOUSEKEEPING, APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_POWER_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_PowerInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_POWER_WAIT_REQUEST:
            if (commandParam0 == (uint32_t)APP_FSM_CONTROL_RESET_BOOT)
            {
                App_FsmExecuteResetBoot();
            }
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_MeterInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_WAIT_TRIGGER:
            p_component = App_FsmGetComponentMutable(APP_FSM_COMPONENT_METER);
            if ((p_component != NULL) && (p_component->eventPending == APP_TRUE))
            {
                App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_SEND_REQUEST, APP_TRUE, APP_TRUE, APP_STATUS_OK);
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_METER_SEND_REQUEST, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            }
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_SEND_REQUEST:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_METER_PARSE_REPLY, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_PARSE_REPLY:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_MeterParseReplay() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NFC_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_NfcInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NFC_WAIT_EVENT:
            p_component = App_FsmGetComponentMutable(APP_FSM_COMPONENT_NFC);
            if ((p_component != NULL) && (p_component->eventPending == APP_TRUE))
            {
                App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_EXCHANGE, APP_TRUE, APP_TRUE, APP_STATUS_OK);
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NFC_EXCHANGE, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            }
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NFC_EXCHANGE:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_NfcExchange() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_AUX_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_AuxInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_AUX_TRIGGER_MEASURE:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_AUX_READ_RESULT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_AUX_READ_RESULT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_AuxReadResult() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NBIOT_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_NbiotInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NBIOT_DECIDE_WAKE:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_POWER_ON, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NBIOT_POWER_ON:
            (void)App_SystemSetNbiotPowered(APP_TRUE);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_EXCHANGE_AT, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NBIOT_EXCHANGE_AT:
            (void)App_SystemSetNbiotPowered(APP_FALSE);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_SERVER_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_ServerInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_SERVER_PREPARE_PACKET:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_SERVER_REQUEST_SEND, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_SERVER_REQUEST_SEND:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_ServerRequestSend() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_RTC_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_RTC_WAKE_SERVICE:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_RTC_APPLY_SYNC, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_RTC_APPLY_SYNC:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcApplySync() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_LPTIM_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcInit() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_LPTIM_WAKE_SERVICE:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_LPTIM_APPLY_SYNC, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_LPTIM_APPLY_SYNC:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcApplySync() == APP_STATUS_OK, APP_STATUS_FAIL);
            */
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_FAULT:
            App_FsmSetDecision(APP_FSM_DECISION_REQUIRE_SAFE);
            break;

        case APP_FSM_STATE_IDLE:
        case APP_FSM_STATE_INIT:
        default:
            App_FsmSetDecision(APP_FSM_DECISION_ALLOW_IDLE);
            break;
    }

    return APP_STATUS_OK;
}

AppStatus_t App_FsmInit(void)
{
    uint32_t index;

    (void)memset(&g_appFsmContext, 0, sizeof(g_appFsmContext));

    for (index = 0u; index < (uint32_t)APP_FSM_COMPONENT_COUNT; index++)
    {
        g_appFsmContext.components[index].id = (AppFsmComponentId_t)index;
        g_appFsmContext.components[index].p_name = App_FsmGetComponentNameInternal((AppFsmComponentId_t)index);
        g_appFsmContext.components[index].intervalMs = App_FsmGetComponentInterval((AppFsmComponentId_t)index);
        g_appFsmContext.components[index].state = APP_FSM_STATE_INIT;
        g_appFsmContext.components[index].lastStatus = APP_STATUS_NOT_INITIALIZED;
    }

    g_appFsmContext.summary.decision = APP_FSM_DECISION_BOOT;
    g_appFsmContext.summary.currentState = APP_FSM_STATE_INIT;
    g_appFsmContext.initialized = APP_TRUE;

    return App_FsmQueueStateBack(APP_FSM_STATE_BOOT, 0u, 0u);
}

AppStatus_t App_FsmRun(void)
{
    AppStatus_t status;
    AppMsgqMessage_t message;
    uint8_t nextState;

    APP_RETURN_IF_FALSE(g_appFsmContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    g_appFsmContext.summary.loopCount++;
    g_appFsmContext.summary.lastLoopDispatchCount = 0u;

    status = App_FsmServiceDebugConsole();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_MsgqTakeFirstByType(APP_MSGQ_TYPE_STATE_COMMAND, &message);
    if (status == APP_STATUS_OK)
    {
        nextState = message.nextState;
        (void)APP_LOGI("FSM", "GetMsgq state:%s", App_FsmGetStateName(nextState));

        if (App_FsmIsValidState(nextState) != APP_TRUE)
        {
            nextState = APP_FSM_STATE_FAULT;
        }

        if (message.eventParam != 0u)
        {
            App_FsmSignalEventForState(nextState);
        }

        g_appFsmContext.summary.lastQueuedState = nextState;
        g_appFsmContext.summary.lastDequeFromFront = message.pushFront;
        g_appFsmContext.summary.lastCommandTickMs = message.tickMs;
        g_appFsmContext.summary.lastCommandEventParam = message.eventParam;
        g_appFsmContext.summary.lastCommandParam0 = message.param0;
        g_appFsmContext.summary.processedMessageCount++;
        g_appFsmContext.summary.transitionCount++;
        return App_FsmExecuteState(nextState, message.param0);
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    status = App_FsmFindDuePeriodicState(&nextState);
    if (status == APP_STATUS_OK)
    {
        (void)APP_LOGD("FSM", "Periodic state:%s", App_FsmGetStateName(nextState));
        g_appFsmContext.summary.lastQueuedState = nextState;
        g_appFsmContext.summary.lastDequeFromFront = APP_FALSE;
        g_appFsmContext.summary.lastCommandTickMs = HAL_GetTick();
        g_appFsmContext.summary.lastCommandEventParam = 0u;
        g_appFsmContext.summary.lastCommandParam0 = 0u;
        g_appFsmContext.summary.transitionCount++;
        return App_FsmExecuteState(nextState, 0u);
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    g_appFsmContext.summary.currentState = APP_FSM_STATE_IDLE;
    g_appFsmContext.summary.lastStateTickMs = HAL_GetTick();
    g_appFsmContext.summary.queueEmptyStopCount++;
    g_appFsmContext.summary.lowPowerRequested = APP_TRUE;
    App_FsmSetDecision(APP_FSM_DECISION_ALLOW_IDLE);
    return App_SystemRequestLowPower(APP_TRUE);
}

AppStatus_t App_FsmQueueStateFront(uint8_t nextState, uint32_t eventParam, uint32_t param0)
{
    return App_FsmPublishStateCommand(nextState, APP_TRUE, eventParam, param0);
}

AppStatus_t App_FsmQueueStateBack(uint8_t nextState, uint32_t eventParam, uint32_t param0)
{
    return App_FsmPublishStateCommand(nextState, APP_FALSE, eventParam, param0);
}

AppStatus_t App_FsmRequestResetBoot(void)
{
    return App_FsmQueueStateBack(APP_FSM_STATE_POWER_WAIT_REQUEST,
                                  0u,
                                  (uint32_t)APP_FSM_CONTROL_RESET_BOOT);
}

const AppFsmContext_t *App_FsmGetContext(void)
{
    return &g_appFsmContext;
}

const AppFsmSummary_t *App_FsmGetSummary(void)
{
    return &g_appFsmContext.summary;
}

const AppFsmComponentContext_t *App_FsmGetComponent(AppFsmComponentId_t id)
{
    if ((uint32_t)id >= (uint32_t)APP_FSM_COMPONENT_COUNT)
    {
        return NULL;
    }

    return &g_appFsmContext.components[id];
}

const char *App_FsmGetComponentName(AppFsmComponentId_t id)
{
    return App_FsmGetComponentNameInternal(id);
}

const char *App_FsmGetStateName(uint8_t state)
{
    switch (state)
    {
        case APP_FSM_STATE_INIT:                    return "INIT";
        case APP_FSM_STATE_BOOT:                    return "BOOT";
        case APP_FSM_STATE_IDLE:                    return "IDLE";
        case APP_FSM_STATE_DEBUG_POLL:              return "DEBUG_POLL";
        case APP_FSM_STATE_HOUSEKEEPING_INIT:       return "HOUSEKEEPING_INIT";
        case APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT:   return "HOUSEKEEPING_SNAPSHOT";
        case APP_FSM_STATE_HOUSEKEEPING_ROTATE:     return "HOUSEKEEPING_ROTATE";
        case APP_FSM_STATE_POWER_INIT:              return "POWER_INIT";
        case APP_FSM_STATE_POWER_WAIT_REQUEST:      return "POWER_WAIT_REQUEST";
        case APP_FSM_STATE_METER_INIT:              return "METER_INIT";
        case APP_FSM_STATE_METER_WAIT_TRIGGER:      return "METER_WAIT_TRIGGER";
        case APP_FSM_STATE_METER_SEND_REQUEST:      return "METER_SEND_REQUEST";
        case APP_FSM_STATE_METER_PARSE_REPLY:       return "METER_PARSE_REPLY";
        case APP_FSM_STATE_NFC_INIT:                return "NFC_INIT";
        case APP_FSM_STATE_NFC_WAIT_EVENT:          return "NFC_WAIT_EVENT";
        case APP_FSM_STATE_NFC_EXCHANGE:            return "NFC_EXCHANGE";
        case APP_FSM_STATE_AUX_INIT:                return "AUX_INIT";
        case APP_FSM_STATE_AUX_TRIGGER_MEASURE:     return "AUX_TRIGGER_MEASURE";
        case APP_FSM_STATE_AUX_READ_RESULT:         return "AUX_READ_RESULT";
        case APP_FSM_STATE_NBIOT_INIT:              return "NBIOT_INIT";
        case APP_FSM_STATE_NBIOT_DECIDE_WAKE:       return "NBIOT_DECIDE_WAKE";
        case APP_FSM_STATE_NBIOT_POWER_ON:          return "NBIOT_POWER_ON";
        case APP_FSM_STATE_NBIOT_EXCHANGE_AT:       return "NBIOT_EXCHANGE_AT";
        case APP_FSM_STATE_SERVER_INIT:             return "SERVER_INIT";
        case APP_FSM_STATE_SERVER_PREPARE_PACKET:   return "SERVER_PREPARE_PACKET";
        case APP_FSM_STATE_SERVER_REQUEST_SEND:     return "SERVER_REQUEST_SEND";
        case APP_FSM_STATE_RTC_INIT:                return "RTC_INIT";
        case APP_FSM_STATE_RTC_WAKE_SERVICE:        return "RTC_WAKE_SERVICE";
        case APP_FSM_STATE_RTC_APPLY_SYNC:          return "RTC_APPLY_SYNC";
        case APP_FSM_STATE_LPTIM_INIT:              return "LPTIM_INIT";
        case APP_FSM_STATE_LPTIM_WAKE_SERVICE:      return "LPTIM_WAKE_SERVICE";
        case APP_FSM_STATE_LPTIM_APPLY_SYNC:        return "LPTIM_APPLY_SYNC";
        case APP_FSM_STATE_FAULT:                   return "FAULT";
        default:                                    return "UNKNOWN";
    }
}

const char *App_FsmGetCurrentStateString(void)
{
    return App_FsmGetStateName(g_appFsmContext.summary.currentState);
}

AppFsmDecision_t App_FsmGetDecision(void)
{
    return g_appFsmContext.summary.decision;
}

const char *App_FsmGetDecisionString(void)
{
    return App_FsmGetDecisionNameInternal(g_appFsmContext.summary.decision);
}

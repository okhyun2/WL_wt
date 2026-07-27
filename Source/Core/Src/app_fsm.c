#include "app_fsm.h"

#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_debug.h"
#include "app_hw.h"
#include "app_gpio_lp.h"
#include "app_meter.h"
#include "app_msgq.h"
#include "app_system.h"
#include "main.h"
#include "nfc_lowpower.h"
#include "nfc_secure_auth.h"
#include "nfc_user_command.h"
#include "app_nfc_seoul_format.h"
#include "app_nbiot.h"
#include "app_meter_storage.h"
#include "app_meter_server_format.h"
#include "app_aux.h"
#include "app_clock.h"

typedef struct
{
    uint8_t initialized;
    uint8_t enabled;
    uint8_t periodHours;
    uint8_t rtcReadyLogged;
    uint32_t nextDueDateKey;
    uint32_t nextDueMsOfDay;
    uint32_t lastDispatchedSlotKey;
} AppFsmMeterScheduleContext_t;

typedef struct
{
    uint8_t initialized;
    uint8_t enabled;
    uint8_t periodHours;
    uint8_t rtcReadyLogged;
    uint8_t offsetApplied;
    uint32_t nextDueDateKey;
    uint32_t nextDueMsOfDay;
    uint32_t lastDispatchedDateKey;
    uint32_t lastDispatchedMsOfDay;
    uint32_t fixedOffsetMs;
    uint32_t currentJitterMs;
} AppFsmTxScheduleContext_t;

static AppFsmContext_t g_appFsmContext;
static AppFsmMeterScheduleContext_t g_appFsmMeterSchedule;
static AppFsmTxScheduleContext_t g_appFsmTxSchedule;
extern NFC_LP_Handle_t g_nfcLpHandle;

static void App_FsmMarkComponent(AppFsmComponentId_t id,
                                 uint8_t state,
                                 uint8_t busy,
                                 uint8_t eventPending,
                                 AppStatus_t status);
static AppStatus_t App_FsmHandleRtcWakeRouting(uint32_t rtcAlarmFlags);
static AppStatus_t App_FsmMeterProbeAndStore(void);
static AppStatus_t App_FsmMeterScheduleConsumeDueNow(void);
static AppStatus_t App_FsmTxScheduleConsumeDueNow(void);

NFC_NTP53321_Handle_t g_nfcTagHandle;
NFC_AUTH_Handle_t g_nfcAuthHandle;
NFC_CMD_Handle_t g_nfcCmdHandle;
NFC_LP_Handle_t g_nfcLpHandle;
static volatile uint8_t g_nfcIrqPending;
static volatile NFC_WakeupEvent_t g_nfcWakeEvent = NFC_WAKEUP_EVENT_UNKNOWN;
static uint8_t g_nfcReady = APP_FALSE;
static uint8_t g_appFsmInitialBootRoutineQueued = APP_FALSE;
static uint8_t g_appFsmWakeCollectionPending = APP_FALSE;
static uint8_t g_appFsmFirstAttachCycleDone = APP_FALSE;
static uint8_t g_appFsmRtcMeterWakePending = APP_FALSE;
static uint8_t g_appFsmRtcTxWakePending = APP_FALSE;

static const uint8_t g_nfcMasterKey[NFC_AUTH_KEY_SIZE] = APP_NFC_MASTER_KEY_BYTES;
static const uint8_t g_nfcAdminKey[NFC_AUTH_KEY_SIZE] = APP_NFC_ADMIN_KEY_BYTES;

static void App_FsmNfcWakeupCallback(NFC_WakeupEvent_t event)
{
    g_nfcWakeEvent = event;
}

static AppStatus_t App_FsmMeterReinitUart(uint32_t settleDelayMs)
{
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);

    App_GpioLpRestoreMeterUartPins();

    (void)HAL_UART_AbortReceive_IT(APP_UART_METER_HANDLE);
    __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    HAL_Delay(APP_SELFTEST_UART_METER_REINIT_PREP_DELAY_MS);

    __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    HAL_Delay(settleDelayMs);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmMeterReceiveBlocking(uint8_t *p_buffer,
                                               uint16_t length,
                                               uint32_t timeoutMs)
{
    HAL_StatusTypeDef halStatus;

    APP_RETURN_IF_FALSE(p_buffer != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(length > 0u, APP_STATUS_INVALID_PARAM);

    __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive(APP_UART_METER_HANDLE,
                                 p_buffer,
                                 length,
                                 timeoutMs);
    if (halStatus == HAL_TIMEOUT)
    {
        (void)HAL_UART_AbortReceive(APP_UART_METER_HANDLE);
        __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE,
                              UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
        __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);
        return APP_STATUS_UART_TIMEOUT;
    }
    APP_RETURN_IF_FALSE(halStatus == HAL_OK, APP_STATUS_UART_RX_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmMeterProbeAndStore(void)
{
#if defined(SUPPORT_METER_NORMAL)
    static const uint8_t meterWakeFrame[] = { 0x10, 0x5B, 0x01, 0x5C, 0x16 };
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {0};
    uint8_t storageEnabledPrev;
    AppStatus_t status;

    APP_LOGI("FSM", "[[MeterWake]] scheduled meter probe start");
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_SET);
    HAL_Delay(50u);

    status = App_FsmMeterReinitUart(APP_SELFTEST_UART_METER_REINIT_SETTLE_DELAY_MS);
    if (status != APP_STATUS_OK)
    {
        App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        return status;
    }

    APP_RETURN_IF_HAL_ERROR(HAL_UART_Transmit(APP_UART_METER_HANDLE,
                                              (uint8_t *)meterWakeFrame,
                                              (uint16_t)sizeof(meterWakeFrame),
                                              APP_SELFTEST_UART_TIMEOUT_MS),
                            APP_STATUS_UART_TX_FAILED);

    status = App_FsmMeterReceiveBlocking(meterReply,
                                         APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN,
                                         APP_SELFTEST_UART_REPLY_METER_NORMAL_TIMEOUT_MS);

    HAL_Delay(100u);
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    App_LogHexDump(APP_LOG_LEVEL_INFO,
                   "FSM",
                   (const uint8_t *)meterReply,
                   APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);

    storageEnabledPrev = App_MeterIsStorageEnabled();
    App_MeterSetStorageEnabled(APP_TRUE);
    status = App_MeterProcessReceivedData((const uint8_t *)meterReply,
                                          APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN);
    App_MeterSetStorageEnabled(storageEnabledPrev);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    APP_LOGI("FSM", "[[MeterWake]] scheduled meter record stored");
    return APP_STATUS_OK;
#elif defined(SUPPORT_METER_SC1xxx)
    uint8_t meterReply[APP_SELFTEST_UART_RX_BUFFER_SIZE] = {0};
    uint8_t storageEnabledPrev;
    AppStatus_t status;

    APP_LOGI("FSM", "[[MeterWake]] scheduled SC1xxx meter probe start");

    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    HAL_Delay(125u);
    HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_SET);
    HAL_Delay(125u);
    HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    HAL_Delay(125u);
    HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_SET);
    HAL_Delay(125u);
    HAL_GPIO_WritePin(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    HAL_Delay(300u);

    status = App_FsmMeterReinitUart(APP_SELFTEST_UART_METER_REINIT_SETTLE_DELAY_MS);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_FsmMeterReceiveBlocking(meterReply,
                                         APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN,
                                         APP_SELFTEST_UART_REPLY_METER_SC1xxx_TIMEOUT_MS);
    HAL_Delay(100u);
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    App_LogHexDump(APP_LOG_LEVEL_INFO,
                   "FSM",
                   (const uint8_t *)meterReply,
                   APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);

    storageEnabledPrev = App_MeterIsStorageEnabled();
    App_MeterSetStorageEnabled(APP_TRUE);
    status = App_MeterSC1xxxProcessReceivedData((const uint8_t *)meterReply,
                                                APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN);
    App_MeterSetStorageEnabled(storageEnabledPrev);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    APP_LOGI("FSM", "[[MeterWake]] scheduled SC1xxx meter record stored");
    return APP_STATUS_OK;
#else
    return APP_STATUS_INVALID_PARAM;
#endif
}

AppStatus_t App_MeterInit(void)
{
    // Meter spec. keep low
    App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);

    return APP_STATUS_OK;
}

AppStatus_t App_NfcInit(void)
{
    if (g_nfcReady == APP_TRUE)
    {
        return APP_STATUS_OK;
    }

    if (NFC_NTP53321_Init(&g_nfcTagHandle, APP_I2C_NFC_HANDLE) != NFC_RESULT_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    if (NFC_AUTH_Init(&g_nfcAuthHandle,
                      &g_nfcTagHandle,
                      g_nfcMasterKey,
                      g_nfcAdminKey) != NFC_AUTH_RESULT_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    if (NFC_CMD_Init(&g_nfcCmdHandle,
                     &g_nfcTagHandle,
                     &g_nfcAuthHandle) != NFC_CMD_RESULT_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    if (NFC_LP_Init(&g_nfcLpHandle, &g_nfcTagHandle) != NFC_RESULT_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    NFC_LP_RegisterCallbacks(&g_nfcLpHandle, App_FsmNfcWakeupCallback, NULL);

    if (App_NfcSeoulInit(&g_nfcTagHandle) != APP_STATUS_OK)
    {
        return APP_STATUS_INIT_FAILED;
    }

    g_nfcIrqPending = APP_FALSE;
    g_nfcWakeEvent = NFC_WAKEUP_EVENT_UNKNOWN;
    g_nfcReady = APP_TRUE;
    APP_LOGI("FSM", "trace nfc module init done ready=%u", (unsigned int)g_nfcReady);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmNfcProcessWakeEvent(void)
{
    NFC_AUTH_Result_t authStatus;
    NFC_CMD_Result_t cmdStatus;
    AppNfcSeoulProcessResult_t seoulResult;

    if (g_nfcReady != APP_TRUE)
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    if (g_nfcIrqPending == APP_TRUE)
    {
        g_nfcIrqPending = APP_FALSE;
        NFC_NTP53321_NotifyDeferredEdEvent(&g_nfcTagHandle);
        if (NFC_LP_HandleWakeup(&g_nfcLpHandle) != NFC_RESULT_OK)
        {
            return APP_STATUS_FATAL;
        }
    }

    if (g_nfcWakeEvent != NFC_WAKEUP_EVENT_ED_PIN)
    {
        return APP_STATUS_OK;
    }

    g_nfcWakeEvent = NFC_WAKEUP_EVENT_UNKNOWN;

    if (App_NfcSeoulRetrySramMirrorOnField() != APP_STATUS_OK)
    {
        APP_LOGW("FSM", "trace nfc field retry sram sync deferred");
    }

    if (App_NfcSeoulProcessTag(&seoulResult) != APP_STATUS_OK)
    {
        return APP_STATUS_FATAL;
    }

    if (seoulResult.handled == APP_TRUE)
    {
        APP_LOGI("FSM",
                 "trace nfc handled req=%02X%02X rsp=%02X%02X comm=%u",
                 (unsigned int)seoulResult.requestCmd1,
                 (unsigned int)seoulResult.requestCmd2,
                 (unsigned int)seoulResult.responseCmd1,
                 (unsigned int)seoulResult.responseCmd2,
                 (unsigned int)seoulResult.commRequested);
        if (seoulResult.commRequested == APP_TRUE)
        {
            if (App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u) == APP_STATUS_OK)
            {
                App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT,
                                     APP_FSM_STATE_NBIOT_DECIDE_WAKE,
                                     APP_TRUE,
                                     APP_FALSE,
                                     APP_STATUS_OK);
                APP_LOGI("FSM", "trace nbiot queued by nfc rset");
            }
        }
        return APP_STATUS_OK;
    }

    if (NFC_AUTH_IsSessionValid(&g_nfcAuthHandle) == true)
    {
        cmdStatus = NFC_CMD_Process(&g_nfcCmdHandle);
        if ((cmdStatus != NFC_CMD_RESULT_OK) &&
            (cmdStatus != NFC_CMD_RESULT_NOT_AUTH) &&
            (cmdStatus != NFC_CMD_RESULT_INVALID_CMD) &&
            (cmdStatus != NFC_CMD_RESULT_INVALID_LEN) &&
            (cmdStatus != NFC_CMD_RESULT_INVALID_MAGIC))
        {
            return APP_STATUS_FATAL;
        }
    }
    else
    {
        authStatus = NFC_AUTH_ProcessNFCEvent(&g_nfcAuthHandle, NFC_WAKEUP_EVENT_ED_PIN);
        if ((authStatus != NFC_AUTH_RESULT_OK) &&
            (authStatus != NFC_AUTH_RESULT_FAIL) &&
            (authStatus != NFC_AUTH_RESULT_LOCKED) &&
            (authStatus != NFC_AUTH_RESULT_INVALID_STATE))
        {
            return APP_STATUS_FATAL;
        }
    }

    return APP_STATUS_OK;
}

void App_FsmNfcEdIrqHandler(void)
{
    g_nfcIrqPending = APP_TRUE;
    APP_LOGI("FSM", "trace nfc ed irq pending=1");
}

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

static const char *App_FsmNfcGetWakeEventName(NFC_WakeupEvent_t event)
{
    switch (event)
    {
        case NFC_WAKEUP_EVENT_ED_PIN:  return "ED_PIN";
        case NFC_WAKEUP_EVENT_RTC:     return "RTC";
        case NFC_WAKEUP_EVENT_UNKNOWN: return "UNKNOWN";
        default:                       return "UNSPEC";
    }
}

static const char *App_FsmNfcGetDriverStateName(NFC_DriverState_t state)
{
    switch (state)
    {
        case NFC_STATE_UNINITIALIZED: return "UNINITIALIZED";
        case NFC_STATE_IDLE:          return "IDLE";
        case NFC_STATE_ACTIVE:        return "ACTIVE";
        case NFC_STATE_STOP:          return "STOP";
        case NFC_STATE_ERROR:         return "ERROR";
        default:                      return "UNKNOWN";
    }
}

static const char *App_FsmNfcGetAuthStateName(NFC_AUTH_State_t state)
{
    switch (state)
    {
        case NFC_AUTH_STATE_IDLE:          return "IDLE";
        case NFC_AUTH_STATE_CONNECTING:    return "CONNECTING";
        case NFC_AUTH_STATE_CHALLENGING:   return "CHALLENGING";
        case NFC_AUTH_STATE_VERIFYING:     return "VERIFYING";
        case NFC_AUTH_STATE_AUTHENTICATED: return "AUTHENTICATED";
        case NFC_AUTH_STATE_FAILED:        return "FAILED";
        case NFC_AUTH_STATE_LOCKED:        return "LOCKED";
        default:                           return "UNKNOWN";
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
        case APP_FSM_STATE_NFC_RELEASE:
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
        case APP_FSM_STATE_RTC_READY:
        case APP_FSM_STATE_LPTIM_INIT:
        case APP_FSM_STATE_LPTIM_WAKE_SERVICE:
        case APP_FSM_STATE_LPTIM_APPLY_SYNC:
        case APP_FSM_STATE_LPTIM_READY:
        case APP_FSM_STATE_WATCHDOG_INIT:
        case APP_FSM_STATE_WATCHDOG_FEED:
        case APP_FSM_STATE_STORAGE_INIT:
        case APP_FSM_STATE_STORAGE_SERVICE:
        case APP_FSM_STATE_STORAGE_RELEASE:
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
        case APP_FSM_COMPONENT_LPTIM:        return "lptim";
        case APP_FSM_COMPONENT_WATCHDOG:     return "watchdog";
        case APP_FSM_COMPONENT_STORAGE:      return "storage";
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
        case APP_FSM_COMPONENT_LPTIM:        return APP_FSM_LPTIM_PERIOD_MS;
        case APP_FSM_COMPONENT_WATCHDOG:     return APP_FSM_WATCHDOG_PERIOD_MS;
        case APP_FSM_COMPONENT_STORAGE:      return APP_FSM_STORAGE_PERIOD_MS;
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
    uint8_t oldState;
    uint8_t oldBusy;
    uint8_t oldEventPending;

    p_component = App_FsmGetComponentMutable(id);
    if (p_component == NULL)
    {
        return;
    }

    oldState = p_component->state;
    oldBusy = p_component->busy;
    oldEventPending = p_component->eventPending;

    p_component->state = state;
    p_component->busy = busy;
    p_component->eventPending = eventPending;
    App_FsmCompleteComponentRun(p_component, status);

    if (((id == APP_FSM_COMPONENT_NFC) ||
         (id == APP_FSM_COMPONENT_NBIOT) ||
         (id == APP_FSM_COMPONENT_SERVER) ||
         (id == APP_FSM_COMPONENT_METER)) &&
        ((oldState != state) || (oldBusy != busy) || (oldEventPending != eventPending)))
    {
        APP_LOGI("FSM",
                 "trace comp=%s %s->%s busy:%u->%u evt:%u->%u status=%lu",
                 App_FsmGetComponentNameInternal(id),
                 App_FsmGetStateName(oldState),
                 App_FsmGetStateName(state),
                 (unsigned int)oldBusy,
                 (unsigned int)busy,
                 (unsigned int)oldEventPending,
                 (unsigned int)eventPending,
                 (unsigned long)status);
    }
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
        case APP_FSM_STATE_NFC_RELEASE:
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
        case APP_FSM_STATE_RTC_READY:
            componentId = APP_FSM_COMPONENT_RTC;
            break;

        case APP_FSM_STATE_WATCHDOG_INIT:
        case APP_FSM_STATE_WATCHDOG_FEED:
            componentId = APP_FSM_COMPONENT_WATCHDOG;
            break;

        case APP_FSM_STATE_STORAGE_INIT:
            componentId = APP_FSM_COMPONENT_STORAGE;
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

static uint8_t App_FsmIsSignalEventPending(void)
{
    AppFsmComponentContext_t *p_component;
    AppFsmComponentId_t componentId;

    for (componentId = APP_FSM_COMPONENT_DEBUG; componentId < APP_FSM_COMPONENT_COUNT; componentId++)
    {
        p_component = App_FsmGetComponentMutable(componentId);
        if (p_component->eventPending != 0)
        {
            APP_LOGD("FSM", "%s is eventpending", App_FsmGetComponentNameInternal(componentId));
            return APP_TRUE;
        }
    }
    return APP_FALSE;
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
                case APP_FSM_STATE_NFC_WAIT_EVENT:      return APP_FSM_STATE_NFC_WAIT_EVENT;
                case APP_FSM_STATE_NFC_RELEASE:
                default:                                return APP_FSM_STATE_NFC_RELEASE;
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
                case APP_FSM_STATE_RTC_WAKE_SERVICE:        return APP_FSM_STATE_RTC_WAKE_SERVICE;
                case APP_FSM_STATE_RTC_APPLY_SYNC:          return APP_FSM_STATE_RTC_APPLY_SYNC;
                case APP_FSM_STATE_RTC_READY:
                default:                                    return APP_FSM_STATE_RTC_READY;
            }

        case APP_FSM_COMPONENT_LPTIM:
            switch (p_component->state)
            {
                case APP_FSM_STATE_LPTIM_INIT:                return APP_FSM_STATE_LPTIM_INIT;
                case APP_FSM_STATE_LPTIM_APPLY_SYNC:          return APP_FSM_STATE_LPTIM_APPLY_SYNC;
                case APP_FSM_STATE_LPTIM_READY:               return APP_FSM_STATE_LPTIM_READY;
                case APP_FSM_STATE_LPTIM_WAKE_SERVICE:
                default:                                    return APP_FSM_STATE_LPTIM_WAKE_SERVICE;
            }

        case APP_FSM_COMPONENT_WATCHDOG:
            switch (p_component->state)
            {
                case APP_FSM_STATE_WATCHDOG_INIT:               return APP_FSM_STATE_WATCHDOG_INIT;
                case APP_FSM_STATE_WATCHDOG_FEED:               return APP_FSM_STATE_WATCHDOG_FEED;
                default:                                    return APP_FSM_STATE_WATCHDOG_FEED;
            }

        case APP_FSM_COMPONENT_STORAGE:
            switch (p_component->state)
            {
                case APP_FSM_STATE_STORAGE_INIT:               return APP_FSM_STATE_STORAGE_INIT;
                case APP_FSM_STATE_STORAGE_SERVICE:            return APP_FSM_STATE_STORAGE_SERVICE;
                case APP_FSM_STATE_STORAGE_RELEASE:
                default:                                    return APP_FSM_STATE_STORAGE_RELEASE;
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
        APP_FSM_COMPONENT_HOUSEKEEPING,
        //APP_FSM_COMPONENT_METER,
        APP_FSM_COMPONENT_NFC,
        //APP_FSM_COMPONENT_AUX,
        //APP_FSM_COMPONENT_NBIOT,
        //APP_FSM_COMPONENT_SERVER,
        //APP_FSM_COMPONENT_RTC
        //APP_FSM_COMPONENT_LPTIM
        APP_FSM_COMPONENT_WATCHDOG
        //APP_FSM_COMPONENT_STORAGE
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

static uint8_t App_FsmMeterScheduleDaysInMonth(uint16_t year, uint8_t month)
{
    switch (month)
    {
        case 1u:
        case 3u:
        case 5u:
        case 7u:
        case 8u:
        case 10u:
        case 12u:
            return 31u;
        case 4u:
        case 6u:
        case 9u:
        case 11u:
            return 30u;
        case 2u:
            return (((year % 4u) == 0u) && (((year % 100u) != 0u) || ((year % 400u) == 0u))) ? 29u : 28u;
        default:
            return 31u;
    }
}

static uint32_t App_FsmMeterScheduleDateKey(const AppDateTime_t *p_now)
{
    APP_RETURN_IF_FALSE(p_now != NULL, 0u);
    return ((uint32_t)p_now->year * 10000u) + ((uint32_t)p_now->month * 100u) + (uint32_t)p_now->day;
}

static uint32_t App_FsmMeterScheduleMsOfDay(const AppDateTime_t *p_now)
{
    APP_RETURN_IF_FALSE(p_now != NULL, 0u);
    return ((uint32_t)p_now->hour * 3600000u) +
           ((uint32_t)p_now->minute * 60000u) +
           ((uint32_t)p_now->second * 1000u);
}

static void App_FsmMeterScheduleAddOneDay(AppDateTime_t *p_time)
{
    uint8_t daysInMonth;

    if (p_time == NULL)
    {
        return;
    }

    daysInMonth = App_FsmMeterScheduleDaysInMonth(p_time->year, p_time->month);
    p_time->day++;
    if (p_time->day <= daysInMonth)
    {
        return;
    }

    p_time->day = 1u;
    p_time->month++;
    if (p_time->month <= 12u)
    {
        return;
    }

    p_time->month = 1u;
    p_time->year++;
}

static AppStatus_t App_FsmMeterScheduleLoadPeriod(uint8_t *p_periodHours)
{
    AppMeterServerFormatOptions_t options;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_periodHours != NULL, APP_STATUS_INVALID_PARAM);

    status = App_MeterServerOptionsLoad(&options);
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_NOT_INITIALIZED))
    {
        return status;
    }

    *p_periodHours = App_MeterServerOptionsNormalizePeriod(options.meteringPeriodHours);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmScheduleComposeDateTime(uint32_t dueDateKey,
                                                  uint32_t dueMsOfDay,
                                                  AppDateTime_t *p_dueTime)
{
    APP_RETURN_IF_FALSE(p_dueTime != NULL, APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(dueMsOfDay < (24u * 3600000u), APP_STATUS_INVALID_PARAM);

    (void)memset(p_dueTime, 0, sizeof(*p_dueTime));
    p_dueTime->year = (uint16_t)(dueDateKey / 10000u);
    p_dueTime->month = (uint8_t)((dueDateKey / 100u) % 100u);
    p_dueTime->day = (uint8_t)(dueDateKey % 100u);
    p_dueTime->hour = (uint8_t)(dueMsOfDay / 3600000u);
    p_dueTime->minute = (uint8_t)((dueMsOfDay % 3600000u) / 60000u);
    p_dueTime->second = (uint8_t)((dueMsOfDay % 60000u) / 1000u);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmSchedulePlanNextAbsolute(const AppDateTime_t *p_now,
                                                   uint8_t periodHours,
                                                   uint32_t offsetMs,
                                                   uint32_t *p_dueDateKey,
                                                   uint32_t *p_dueMsOfDay)
{
    AppDateTime_t dueTime;
    uint32_t nowMsOfDay;
    uint32_t periodMs;
    uint32_t nextMs;

    APP_RETURN_IF_FALSE((p_now != NULL) && (p_dueDateKey != NULL) && (p_dueMsOfDay != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE(App_MeterServerOptionsIsPeriodSupported(periodHours) == APP_TRUE, APP_STATUS_INVALID_PARAM);

    dueTime = *p_now;
    periodMs = (uint32_t)periodHours * 3600000u;
    nowMsOfDay = App_FsmMeterScheduleMsOfDay(p_now);
    nextMs = ((nowMsOfDay / periodMs) + 1u) * periodMs;

    if (nextMs >= (24u * 3600000u))
    {
        nextMs = 0u;
        App_FsmMeterScheduleAddOneDay(&dueTime);
    }

    nextMs += offsetMs;
    while (nextMs >= (24u * 3600000u))
    {
        nextMs -= (24u * 3600000u);
        App_FsmMeterScheduleAddOneDay(&dueTime);
    }

    *p_dueDateKey = App_FsmMeterScheduleDateKey(&dueTime);
    *p_dueMsOfDay = nextMs;
    return APP_STATUS_OK;
}

static uint32_t App_FsmTxScheduleCalcJitterMs(uint32_t dueDateKey,
                                              uint32_t slotBaseMsOfDay,
                                              uint8_t periodHours)
{
#if (APP_NBIOT_XMIT_JITTER_MAX_SEC == 0u)
    (void)dueDateKey;
    (void)slotBaseMsOfDay;
    (void)periodHours;
    return 0u;
#else
    uint32_t periodMs;
    uint32_t slotIndex;
    uint32_t seed;

    periodMs = (uint32_t)periodHours * 3600000u;
    slotIndex = (periodMs != 0u) ? (slotBaseMsOfDay / periodMs) : 0u;
    seed = App_ClockGetDeviceUidHash() ^ dueDateKey ^ (slotIndex * 2654435761u) ^ ((uint32_t)periodHours << 24);
    return (seed % (APP_NBIOT_XMIT_JITTER_MAX_SEC + 1u)) * 1000u;
#endif
}

static AppStatus_t App_FsmTxSchedulePlanNext(const AppDateTime_t *p_now,
                                             uint8_t periodHours,
                                             uint32_t fixedOffsetMs,
                                             uint32_t *p_dueDateKey,
                                             uint32_t *p_dueMsOfDay,
                                             uint32_t *p_jitterMs)
{
    AppDateTime_t dueTime;
    uint32_t baseDueDateKey;
    uint32_t baseDueMsOfDay;
    uint32_t jitterMs;
    uint32_t nextMs;
    AppStatus_t status;

    APP_RETURN_IF_FALSE((p_now != NULL) && (p_dueDateKey != NULL) && (p_dueMsOfDay != NULL), APP_STATUS_INVALID_PARAM);

    status = App_FsmSchedulePlanNextAbsolute(p_now,
                                             periodHours,
                                             0u,
                                             &baseDueDateKey,
                                             &baseDueMsOfDay);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_FsmScheduleComposeDateTime(baseDueDateKey, baseDueMsOfDay, &dueTime);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    jitterMs = App_FsmTxScheduleCalcJitterMs(baseDueDateKey, baseDueMsOfDay, periodHours);
    nextMs = baseDueMsOfDay + fixedOffsetMs + jitterMs;
    while (nextMs >= (24u * 3600000u))
    {
        nextMs -= (24u * 3600000u);
        App_FsmMeterScheduleAddOneDay(&dueTime);
    }

    *p_dueDateKey = App_FsmMeterScheduleDateKey(&dueTime);
    *p_dueMsOfDay = nextMs;
    if (p_jitterMs != NULL)
    {
        *p_jitterMs = jitterMs;
    }
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmMeterSchedulePlanNext(const AppDateTime_t *p_now,
                                                uint8_t periodHours,
                                                uint32_t *p_dueDateKey,
                                                uint32_t *p_dueMsOfDay)
{
    return App_FsmSchedulePlanNextAbsolute(p_now,
                                           periodHours,
                                           0u,
                                           p_dueDateKey,
                                           p_dueMsOfDay);
}

static AppStatus_t App_FsmMeterScheduleEnsureInitialized(void)
{
    AppDateTime_t now;
    AppStatus_t status;
    uint8_t periodHours;

    if (IsUpdatedRTC() != APP_TRUE)
    {
        if (g_appFsmMeterSchedule.rtcReadyLogged != APP_FALSE)
        {
            APP_LOGW("FSM", "[[MeterSchedule]] RTC invalid -> metering schedule disabled");
        }
        g_appFsmMeterSchedule.initialized = APP_FALSE;
        g_appFsmMeterSchedule.enabled = APP_FALSE;
        g_appFsmMeterSchedule.rtcReadyLogged = APP_FALSE;
        return APP_STATUS_NOT_INITIALIZED;
    }

    status = RTC_GetTime(&now);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_FsmMeterScheduleLoadPeriod(&periodHours);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    if ((g_appFsmMeterSchedule.initialized == APP_TRUE) &&
        (g_appFsmMeterSchedule.enabled == APP_TRUE) &&
        (g_appFsmMeterSchedule.periodHours == periodHours) &&
        (g_appFsmMeterSchedule.rtcReadyLogged == APP_TRUE))
    {
        return APP_STATUS_OK;
    }

    g_appFsmMeterSchedule.periodHours = periodHours;
    status = App_FsmMeterSchedulePlanNext(&now,
                                          g_appFsmMeterSchedule.periodHours,
                                          &g_appFsmMeterSchedule.nextDueDateKey,
                                          &g_appFsmMeterSchedule.nextDueMsOfDay);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    g_appFsmMeterSchedule.initialized = APP_TRUE;
    g_appFsmMeterSchedule.enabled = APP_TRUE;
    g_appFsmMeterSchedule.rtcReadyLogged = APP_TRUE;

    APP_LOGI("FSM", "[[MeterSchedule]] enabled period=%uh next=%lu %02lu:%02lu:%02lu",
             (unsigned)g_appFsmMeterSchedule.periodHours,
             (unsigned long)g_appFsmMeterSchedule.nextDueDateKey,
             (unsigned long)(g_appFsmMeterSchedule.nextDueMsOfDay / 3600000u),
             (unsigned long)((g_appFsmMeterSchedule.nextDueMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmMeterSchedule.nextDueMsOfDay % 60000u) / 1000u));
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmCheckMeterScheduledState(uint8_t *p_state)
{
    AppDateTime_t now;
    AppStatus_t status;
    uint32_t nowDateKey;
    uint32_t nowMsOfDay;
    uint32_t dueSlotKey;

    APP_RETURN_IF_FALSE(p_state != NULL, APP_STATUS_INVALID_PARAM);

    status = App_FsmMeterScheduleEnsureInitialized();
    if (status == APP_STATUS_NOT_INITIALIZED)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = RTC_GetTime(&now);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    nowDateKey = App_FsmMeterScheduleDateKey(&now);
    nowMsOfDay = App_FsmMeterScheduleMsOfDay(&now);

    if ((nowDateKey < g_appFsmMeterSchedule.nextDueDateKey) ||
        ((nowDateKey == g_appFsmMeterSchedule.nextDueDateKey) &&
         (nowMsOfDay < g_appFsmMeterSchedule.nextDueMsOfDay)))
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    dueSlotKey = (g_appFsmMeterSchedule.nextDueDateKey * 100u) +
                 (g_appFsmMeterSchedule.nextDueMsOfDay / ((uint32_t)g_appFsmMeterSchedule.periodHours * 3600000u));
    if (dueSlotKey == g_appFsmMeterSchedule.lastDispatchedSlotKey)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    g_appFsmMeterSchedule.lastDispatchedSlotKey = dueSlotKey;
    status = App_FsmMeterSchedulePlanNext(&now,
                                          g_appFsmMeterSchedule.periodHours,
                                          &g_appFsmMeterSchedule.nextDueDateKey,
                                          &g_appFsmMeterSchedule.nextDueMsOfDay);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    App_FsmSignalEventForState(APP_FSM_STATE_METER_WAIT_TRIGGER);
    APP_LOGI("FSM", "[[MeterSchedule]] due slot=%lu -> queue meter read, next=%lu %02lu:%02lu:%02lu",
             (unsigned long)dueSlotKey,
             (unsigned long)g_appFsmMeterSchedule.nextDueDateKey,
             (unsigned long)(g_appFsmMeterSchedule.nextDueMsOfDay / 3600000u),
             (unsigned long)((g_appFsmMeterSchedule.nextDueMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmMeterSchedule.nextDueMsOfDay % 60000u) / 1000u));

    *p_state = APP_FSM_STATE_METER_WAIT_TRIGGER;
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmMeterScheduleConsumeDueNow(void)
{
    AppDateTime_t now;
    AppStatus_t status;
    uint32_t nowDateKey;
    uint32_t nowMsOfDay;
    uint32_t dueSlotKey;

    status = App_FsmMeterScheduleEnsureInitialized();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = RTC_GetTime(&now);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    nowDateKey = App_FsmMeterScheduleDateKey(&now);
    nowMsOfDay = App_FsmMeterScheduleMsOfDay(&now);

    if ((nowDateKey < g_appFsmMeterSchedule.nextDueDateKey) ||
        ((nowDateKey == g_appFsmMeterSchedule.nextDueDateKey) &&
         (nowMsOfDay < g_appFsmMeterSchedule.nextDueMsOfDay)))
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    dueSlotKey = (g_appFsmMeterSchedule.nextDueDateKey * 100u) +
                 (g_appFsmMeterSchedule.nextDueMsOfDay / ((uint32_t)g_appFsmMeterSchedule.periodHours * 3600000u));
    if (dueSlotKey == g_appFsmMeterSchedule.lastDispatchedSlotKey)
    {
        return APP_STATUS_OK;
    }

    g_appFsmMeterSchedule.lastDispatchedSlotKey = dueSlotKey;
    status = App_FsmMeterSchedulePlanNext(&now,
                                          g_appFsmMeterSchedule.periodHours,
                                          &g_appFsmMeterSchedule.nextDueDateKey,
                                          &g_appFsmMeterSchedule.nextDueMsOfDay);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    APP_LOGI("FSM", "[[MeterSchedule]] alarm consume slot=%lu -> next=%lu %02lu:%02lu:%02lu",
             (unsigned long)dueSlotKey,
             (unsigned long)g_appFsmMeterSchedule.nextDueDateKey,
             (unsigned long)(g_appFsmMeterSchedule.nextDueMsOfDay / 3600000u),
             (unsigned long)((g_appFsmMeterSchedule.nextDueMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmMeterSchedule.nextDueMsOfDay % 60000u) / 1000u));
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmTxScheduleLoadPeriod(uint8_t *p_periodHours)
{
    AppMeterServerFormatOptions_t options;
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_periodHours != NULL, APP_STATUS_INVALID_PARAM);

    status = App_MeterServerOptionsLoad(&options);
    if ((status != APP_STATUS_OK) && (status != APP_STATUS_NOT_INITIALIZED))
    {
        return status;
    }

    *p_periodHours = App_MeterServerOptionsNormalizePeriod(options.reportingPeriodHours);
    return APP_STATUS_OK;
}



static AppStatus_t App_FsmTxScheduleEnsureInitialized(void)
{
    AppDateTime_t now;
    AppStatus_t status;
    uint8_t periodHours;

    if (IsUpdatedRTC() != APP_TRUE)
    {
        if (g_appFsmTxSchedule.rtcReadyLogged != APP_FALSE)
        {
            APP_LOGW("FSM", "[[TxSchedule]] RTC invalid -> tx schedule disabled");
        }
        g_appFsmTxSchedule.initialized = APP_FALSE;
        g_appFsmTxSchedule.enabled = APP_FALSE;
        g_appFsmTxSchedule.rtcReadyLogged = APP_FALSE;
        return APP_STATUS_NOT_INITIALIZED;
    }

    status = RTC_GetTime(&now);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = App_FsmTxScheduleLoadPeriod(&periodHours);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
    APP_RETURN_IF_FALSE(App_MeterServerOptionsIsPeriodSupported(periodHours) == APP_TRUE, APP_STATUS_INVALID_PARAM);

    if (g_appFsmTxSchedule.offsetApplied != APP_TRUE)
    {
        g_appFsmTxSchedule.fixedOffsetMs =
            (App_ClockGetDeviceUidHash() % (APP_NBIOT_XMIT_OFFSET_MAX_SEC + 1u)) * 1000u;
        g_appFsmTxSchedule.offsetApplied = APP_TRUE;
    }

    if ((g_appFsmTxSchedule.initialized == APP_TRUE) &&
        (g_appFsmTxSchedule.enabled == APP_TRUE) &&
        (g_appFsmTxSchedule.periodHours == periodHours) &&
        (g_appFsmTxSchedule.rtcReadyLogged == APP_TRUE))
    {
        return APP_STATUS_OK;
    }

    g_appFsmTxSchedule.periodHours = periodHours;
    status = App_FsmTxSchedulePlanNext(&now,
                                       g_appFsmTxSchedule.periodHours,
                                       g_appFsmTxSchedule.fixedOffsetMs,
                                       &g_appFsmTxSchedule.nextDueDateKey,
                                       &g_appFsmTxSchedule.nextDueMsOfDay,
                                       &g_appFsmTxSchedule.currentJitterMs);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    g_appFsmTxSchedule.initialized = APP_TRUE;
    g_appFsmTxSchedule.enabled = APP_TRUE;
    g_appFsmTxSchedule.rtcReadyLogged = APP_TRUE;

    APP_LOGI("FSM", "[[TxSchedule]] enabled period=%uh next=%lu %02lu:%02lu:%02lu offset=%lu jitter=%lu",
             (unsigned)g_appFsmTxSchedule.periodHours,
             (unsigned long)g_appFsmTxSchedule.nextDueDateKey,
             (unsigned long)(g_appFsmTxSchedule.nextDueMsOfDay / 3600000u),
             (unsigned long)((g_appFsmTxSchedule.nextDueMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmTxSchedule.nextDueMsOfDay % 60000u) / 1000u),
             (unsigned long)g_appFsmTxSchedule.fixedOffsetMs,
             (unsigned long)g_appFsmTxSchedule.currentJitterMs);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmTxScheduleCheckDue(uint8_t *p_due)
{
    AppDateTime_t now;
    AppStatus_t status;
    uint32_t nowDateKey;
    uint32_t nowMsOfDay;

    APP_RETURN_IF_FALSE(p_due != NULL, APP_STATUS_INVALID_PARAM);
    *p_due = APP_FALSE;

    status = App_FsmTxScheduleEnsureInitialized();
    if (status == APP_STATUS_NOT_INITIALIZED)
    {
        return APP_STATUS_OK;
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    status = RTC_GetTime(&now);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    nowDateKey = App_FsmMeterScheduleDateKey(&now);
    nowMsOfDay = App_FsmMeterScheduleMsOfDay(&now);

    if ((nowDateKey < g_appFsmTxSchedule.nextDueDateKey) ||
        ((nowDateKey == g_appFsmTxSchedule.nextDueDateKey) &&
         (nowMsOfDay < g_appFsmTxSchedule.nextDueMsOfDay)))
    {
        return APP_STATUS_OK;
    }

    if ((g_appFsmTxSchedule.lastDispatchedDateKey == g_appFsmTxSchedule.nextDueDateKey) &&
        (g_appFsmTxSchedule.lastDispatchedMsOfDay == g_appFsmTxSchedule.nextDueMsOfDay))
    {
        return APP_STATUS_OK;
    }

    *p_due = APP_TRUE;
    g_appFsmTxSchedule.lastDispatchedDateKey = g_appFsmTxSchedule.nextDueDateKey;
    g_appFsmTxSchedule.lastDispatchedMsOfDay = g_appFsmTxSchedule.nextDueMsOfDay;

    status = App_FsmTxSchedulePlanNext(&now,
                                       g_appFsmTxSchedule.periodHours,
                                       g_appFsmTxSchedule.fixedOffsetMs,
                                       &g_appFsmTxSchedule.nextDueDateKey,
                                       &g_appFsmTxSchedule.nextDueMsOfDay,
                                       &g_appFsmTxSchedule.currentJitterMs);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    APP_LOGI("FSM", "[[TxSchedule]] due %lu %02lu:%02lu:%02lu -> next=%lu %02lu:%02lu:%02lu period=%uh offset=%lu jitter=%lu",
             (unsigned long)g_appFsmTxSchedule.lastDispatchedDateKey,
             (unsigned long)(g_appFsmTxSchedule.lastDispatchedMsOfDay / 3600000u),
             (unsigned long)((g_appFsmTxSchedule.lastDispatchedMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmTxSchedule.lastDispatchedMsOfDay % 60000u) / 1000u),
             (unsigned long)g_appFsmTxSchedule.nextDueDateKey,
             (unsigned long)(g_appFsmTxSchedule.nextDueMsOfDay / 3600000u),
             (unsigned long)((g_appFsmTxSchedule.nextDueMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmTxSchedule.nextDueMsOfDay % 60000u) / 1000u),
             (unsigned)g_appFsmTxSchedule.periodHours,
             (unsigned long)g_appFsmTxSchedule.fixedOffsetMs,
             (unsigned long)g_appFsmTxSchedule.currentJitterMs);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmTxScheduleConsumeDueNow(void)
{
    AppDateTime_t now;
    AppStatus_t status;
    uint32_t nowDateKey;
    uint32_t nowMsOfDay;

    status = App_FsmTxScheduleEnsureInitialized();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = RTC_GetTime(&now);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    nowDateKey = App_FsmMeterScheduleDateKey(&now);
    nowMsOfDay = App_FsmMeterScheduleMsOfDay(&now);

    if ((nowDateKey < g_appFsmTxSchedule.nextDueDateKey) ||
        ((nowDateKey == g_appFsmTxSchedule.nextDueDateKey) &&
         (nowMsOfDay < g_appFsmTxSchedule.nextDueMsOfDay)))
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    if ((g_appFsmTxSchedule.lastDispatchedDateKey == g_appFsmTxSchedule.nextDueDateKey) &&
        (g_appFsmTxSchedule.lastDispatchedMsOfDay == g_appFsmTxSchedule.nextDueMsOfDay))
    {
        return APP_STATUS_OK;
    }

    g_appFsmTxSchedule.lastDispatchedDateKey = g_appFsmTxSchedule.nextDueDateKey;
    g_appFsmTxSchedule.lastDispatchedMsOfDay = g_appFsmTxSchedule.nextDueMsOfDay;

    status = App_FsmTxSchedulePlanNext(&now,
                                       g_appFsmTxSchedule.periodHours,
                                       g_appFsmTxSchedule.fixedOffsetMs,
                                       &g_appFsmTxSchedule.nextDueDateKey,
                                       &g_appFsmTxSchedule.nextDueMsOfDay,
                                       &g_appFsmTxSchedule.currentJitterMs);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    APP_LOGI("FSM", "[[TxSchedule]] alarm consume %lu %02lu:%02lu:%02lu -> next=%lu %02lu:%02lu:%02lu offset=%lu jitter=%lu",
             (unsigned long)g_appFsmTxSchedule.lastDispatchedDateKey,
             (unsigned long)(g_appFsmTxSchedule.lastDispatchedMsOfDay / 3600000u),
             (unsigned long)((g_appFsmTxSchedule.lastDispatchedMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmTxSchedule.lastDispatchedMsOfDay % 60000u) / 1000u),
             (unsigned long)g_appFsmTxSchedule.nextDueDateKey,
             (unsigned long)(g_appFsmTxSchedule.nextDueMsOfDay / 3600000u),
             (unsigned long)((g_appFsmTxSchedule.nextDueMsOfDay % 3600000u) / 60000u),
             (unsigned long)((g_appFsmTxSchedule.nextDueMsOfDay % 60000u) / 1000u),
             (unsigned long)g_appFsmTxSchedule.fixedOffsetMs,
             (unsigned long)g_appFsmTxSchedule.currentJitterMs);
    return APP_STATUS_OK;
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
    APP_LOGW("FSM", "executing reset boot-hold=%lu ms", (unsigned long)APP_FSM_BOOT_RESET_HOLD_MS);
    App_HwSetChargeBoot0(GPIO_PIN_SET);
    HAL_Delay(APP_FSM_BOOT_RESET_HOLD_MS);
    __disable_irq();
    NVIC_SystemReset();
    while (1)
    {
    }
}

static AppStatus_t App_FsmHandleFatalLowPower(const char *p_reason, AppStatus_t fatalStatus)
{
    AppStatus_t powerOffStatus;

    APP_LOGW("FSM", "[[FatalLP]] %s status=%d -> power off NB-IoT and request no-wake STOP",
             (p_reason != NULL) ? p_reason : "fatal",
             (int)fatalStatus);

    powerOffStatus = App_NBIoTCarrierPowerOffMandatory();
    if (powerOffStatus != APP_STATUS_OK)
    {
        APP_LOGW("FSM", "[[FatalLP]] mandatory power off failed status=%d -> direct power cut",
                 (int)powerOffStatus);
        (void)App_SystemSetNbiotPowered(APP_FALSE);
    }

    g_appFsmWakeCollectionPending = APP_FALSE;
    App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT,
                         APP_FSM_STATE_NBIOT_INIT,
                         APP_FALSE,
                         APP_FALSE,
                         fatalStatus);
    g_appFsmContext.summary.lowPowerRequested = APP_TRUE;
    App_FsmSetDecision(APP_FSM_DECISION_ALLOW_IDLE);

    APP_RETURN_IF_FALSE(App_SystemRequestLowPowerNoWake(APP_TRUE) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmHandleWakeupLowPower(const char *p_reason, AppStatus_t fatalStatus)
{
    AppStatus_t powerOffStatus;

    APP_LOGW("FSM", "[[WakeupLP]] %s status=%d -> power off NB-IoT and request wakeup STOP",
             (p_reason != NULL) ? p_reason : "fatal",
             (int)fatalStatus);

    powerOffStatus = App_NBIoTCarrierPowerOffMandatory();
    if (powerOffStatus != APP_STATUS_OK)
    {
        APP_LOGW("FSM", "[[WakeupLP]] mandatory power off failed status=%d -> direct power cut",
                 (int)powerOffStatus);
        (void)App_SystemSetNbiotPowered(APP_FALSE);
    }

    g_appFsmWakeCollectionPending = APP_FALSE;
    App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT,
                         APP_FSM_STATE_NBIOT_INIT,
                         APP_FALSE,
                         APP_FALSE,
                         fatalStatus);
    g_appFsmContext.summary.lowPowerRequested = APP_TRUE;
    App_FsmSetDecision(APP_FSM_DECISION_ALLOW_IDLE);

    APP_RETURN_IF_FALSE(App_SystemRequestLowPowerNoWake(APP_FALSE) == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_FsmExecuteState(uint8_t currentState, uint32_t commandParam0)
{
    AppFsmComponentContext_t *p_component;

    APP_RETURN_IF_FALSE(App_FsmIsValidState(currentState) == APP_TRUE, APP_STATUS_INVALID_PARAM);

    APP_LOGD("FSM", "executing state:%s, commandParam:%d", App_FsmGetStateName(currentState), commandParam0);

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
            App_FsmMarkComponent(APP_FSM_COMPONENT_WATCHDOG, APP_FSM_STATE_WATCHDOG_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmMarkComponent(APP_FSM_COMPONENT_STORAGE, APP_FSM_STATE_STORAGE_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);

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
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_WATCHDOG_INIT, APP_TRUE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_STORAGE_INIT, APP_FALSE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);

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
                APP_RETURN_IF_FALSE(App_HouseKeepingSnapshot() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            App_FsmMarkComponent(APP_FSM_COMPONENT_HOUSEKEEPING, APP_FSM_STATE_HOUSEKEEPING_ROTATE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_HOUSEKEEPING_ROTATE:
            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_HOUSEKEEPING, APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_POWER_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_PowerInit() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            App_FsmMarkComponent(APP_FSM_COMPONENT_POWER, APP_FSM_STATE_POWER_WAIT_REQUEST, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_POWER_WAIT_REQUEST:
            if (commandParam0 == (uint32_t)APP_FSM_CONTROL_RESET_BOOT)
            {
                App_FsmExecuteResetBoot();
            }
            App_FsmMarkComponent(APP_FSM_COMPONENT_POWER, APP_FSM_STATE_POWER_WAIT_REQUEST, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_INIT:
            App_MeterInit();

            App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_WAIT_TRIGGER, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_WAIT_TRIGGER:
            p_component = App_FsmGetComponentMutable(APP_FSM_COMPONENT_METER);
            if ((p_component != NULL) && (p_component->eventPending == APP_TRUE))
            {
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_METER_SEND_REQUEST, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
                App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_SEND_REQUEST, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            }
            else
            {
                App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_SEND_REQUEST, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            }
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_SEND_REQUEST:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_METER_PARSE_REPLY, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_PARSE_REPLY, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_METER_PARSE_REPLY:
        {
            AppStatus_t meterStatus;

            meterStatus = App_FsmMeterProbeAndStore();
            if (meterStatus != APP_STATUS_OK)
            {
                APP_LOGW("FSM", "[[MeterWake]] scheduled meter probe failed status=%ld", (long)meterStatus);
            }

            g_appFsmRtcMeterWakePending = APP_FALSE;
            App_FsmMarkComponent(APP_FSM_COMPONENT_METER,
                                 APP_FSM_STATE_METER_INIT,
                                 APP_FALSE,
                                 APP_FALSE,
                                 meterStatus);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;
        }

        case APP_FSM_STATE_NFC_INIT:
            App_SystemPrepareNfcStandbyForStop();

            App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_WAIT_EVENT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NFC_WAIT_EVENT:
            p_component = App_FsmGetComponentMutable(APP_FSM_COMPONENT_NFC);
            if ((p_component != NULL) && ((p_component->eventPending == APP_TRUE) || (g_nfcIrqPending == APP_TRUE)))
            {
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NFC_EXCHANGE, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
                App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_EXCHANGE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            }
            else
            {
                (void)App_NfcSeoulServiceTestMode();
                //Clear eventPending
                App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_WAIT_EVENT, APP_FALSE, APP_FALSE, APP_STATUS_OK);

            }
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NFC_EXCHANGE:
            APP_RETURN_IF_FALSE(App_FsmNfcProcessWakeEvent() == APP_STATUS_OK, APP_STATUS_FATAL);
            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_WAIT_EVENT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NFC_RELEASE:
            App_FsmMarkComponent(APP_FSM_COMPONENT_NFC, APP_FSM_STATE_NFC_RELEASE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);

        case APP_FSM_STATE_AUX_INIT:
            if (SHTC3_Init(APP_I2C_AUX_HANDLE) != HAL_OK)
            {
                APP_LOGE("AUX", "SHT3 fail!");
            }
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
                APP_RETURN_IF_FALSE(App_NbiotInit() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NBIOT_DECIDE_WAKE:
        {
            uint8_t txDue = APP_FALSE;

            if ((g_appFsmRtcTxWakePending != APP_TRUE) && (g_appFsmFirstAttachCycleDone == APP_TRUE))
            {
                APP_RETURN_IF_FALSE(App_FsmTxScheduleCheckDue(&txDue) == APP_STATUS_OK, APP_STATUS_FATAL);
                if (txDue != APP_TRUE)
                {
                    g_appFsmWakeCollectionPending = APP_FALSE;
                    App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
                    App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
                    APP_LOGI("FSM", "[[TxSchedule]] skip attach/send before due");
                    break;
                }
            }

            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_POWER_ON, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_POWER_ON, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;
        }

        case APP_FSM_STATE_NBIOT_POWER_ON:
            (void)App_SystemSetNbiotPowered(APP_TRUE);
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_EXCHANGE_AT, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_EXCHANGE_AT, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_NBIOT_EXCHANGE_AT:
        {
            uint8_t isFirstBootRoutine = (g_appFsmFirstAttachCycleDone != APP_TRUE) ? APP_TRUE : APP_FALSE;
            AppStatus_t attachStatus;

#if (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE)
            if ((isFirstBootRoutine != APP_TRUE) && (g_appFsmWakeCollectionPending == APP_TRUE))
            {
                APP_LOGI("FSM", "[[WakeRoutine]] collect device data before attach");
                APP_RETURN_IF_FALSE(App_SystemRunWakeDataCollection() == APP_STATUS_OK, APP_STATUS_FATAL);
                g_appFsmWakeCollectionPending = APP_FALSE;
            }
#endif /* APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE */

            attachStatus = App_NBIoTCarrierAttachMandatory();
            if (attachStatus != APP_STATUS_OK)
            {
                //return App_FsmHandleFatalLowPower("attach fatal", attachStatus);
                return App_FsmHandleWakeupLowPower("attach fail", attachStatus);
            }
            App_NBIoTReadIdentity(APP_TRUE);
            App_NBIoTReadQuality(APP_TRUE);

#ifdef SUPPORT_SELFTEST
            if (isFirstBootRoutine == APP_TRUE)
            {
                APP_LOGI("FSM", "[[BootRoutine]] attach success -> run full selftest");
                APP_RETURN_IF_FALSE(App_SystemRunBootSelfTest() == APP_STATUS_OK, APP_STATUS_FATAL);
            }
#else
#if (APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE == APP_TRUE)
            if (isFirstBootRoutine == APP_TRUE)
            {
                APP_LOGI("FSM", "[[BootRoutine]] attach success -> collect device data before first send");
                APP_RETURN_IF_FALSE(App_SystemRunWakeDataCollection() == APP_STATUS_OK, APP_STATUS_FATAL);
            }
#endif /* APP_WAKE_DATA_COLLECTION_ALWAYS_ENABLE */
#endif // SUPPORT_SELFTEST

            App_NBIoTTransmitUdp();

            APP_RETURN_IF_FALSE(App_NBIoTCarrierPowerOffMandatory() == APP_STATUS_OK, APP_STATUS_FATAL);
            g_appFsmFirstAttachCycleDone = APP_TRUE;
            g_appFsmRtcTxWakePending = APP_FALSE;

            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;
        }

        case APP_FSM_STATE_SERVER_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_ServerInit() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            App_FsmMarkComponent(APP_FSM_COMPONENT_SERVER, APP_FSM_STATE_SERVER_PREPARE_PACKET, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_SERVER_PREPARE_PACKET:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_SERVER_REQUEST_SEND, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_SERVER, APP_FSM_STATE_SERVER_REQUEST_SEND, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_SERVER_REQUEST_SEND:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_ServerRequestSend() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_SERVER, APP_FSM_STATE_SERVER_INIT, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_RTC_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcInit() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            App_FsmMarkComponent(APP_FSM_COMPONENT_RTC, APP_FSM_STATE_RTC_WAKE_SERVICE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_RTC_WAKE_SERVICE:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_RTC_APPLY_SYNC, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_RTC, APP_FSM_STATE_RTC_APPLY_SYNC, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_RTC_APPLY_SYNC:
        {
            uint32_t rtcAlarmFlags;

            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcApplySync() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            rtcAlarmFlags = App_SystemConsumeRtcAlarmFlags();
            if ((rtcAlarmFlags & WAKEUP_MASK_RTC_ALARM) != 0u)
            {
                APP_RETURN_IF_FALSE(App_FsmHandleRtcWakeRouting(rtcAlarmFlags) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            }
            else if (g_appFsmRtcTxWakePending == APP_TRUE)
            {
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
                App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
                APP_LOGI("FSM", "[[AlarmCollision]] short deferred wake -> queue transmit path only");
            }
            else if (g_appFsmWakeCollectionPending != APP_TRUE)
            {
                g_appFsmWakeCollectionPending = APP_TRUE;
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
                App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
                APP_LOGI("FSM", "[[WakeRoutine]] RTC/WUT wake -> queue collect/attach/send/poweroff");
            }
            // Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_RTC, APP_FSM_STATE_RTC_READY, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;
        }

        case APP_FSM_STATE_RTC_READY:
            App_FsmMarkComponent(APP_FSM_COMPONENT_RTC, APP_FSM_STATE_RTC_READY, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_LPTIM_INIT:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcInit() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            App_FsmMarkComponent(APP_FSM_COMPONENT_LPTIM, APP_FSM_STATE_LPTIM_WAKE_SERVICE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_LPTIM_WAKE_SERVICE:
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_LPTIM_APPLY_SYNC, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            App_FsmMarkComponent(APP_FSM_COMPONENT_LPTIM, APP_FSM_STATE_LPTIM_APPLY_SYNC, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_LPTIM_APPLY_SYNC:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcApplySync() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            if (g_appFsmWakeCollectionPending != APP_TRUE)
            {
                g_appFsmWakeCollectionPending = APP_TRUE;
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
                App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
                APP_LOGI("FSM", "[[WakeRoutine]] LPTIM wake -> queue collect/attach/send/poweroff");
            }
            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_LPTIM, APP_FSM_STATE_LPTIM_READY, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;
        case APP_FSM_STATE_LPTIM_READY:
            App_FsmMarkComponent(APP_FSM_COMPONENT_LPTIM, APP_FSM_STATE_LPTIM_READY, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_WATCHDOG_INIT:
            APP_LOGD("FSM", "state:%s", App_FsmGetStateName(currentState));
            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_WATCHDOG_FEED, APP_TRUE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL); //eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_WATCHDOG, APP_FSM_STATE_WATCHDOG_FEED, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_WATCHDOG_FEED:
            APP_LOGD("FSM", "state:%s", App_FsmGetStateName(currentState));
            App_HwFeedEWD();
            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_WATCHDOG, APP_FSM_STATE_WATCHDOG_FEED, APP_FALSE, APP_FALSE, APP_STATUS_OK); //release eventPending. can entry stop mode
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_STORAGE_INIT:
            AppDeviceConfig_t cfg;
            AppMeterServerFormatOptions_t opt;

            if (App_DeviceConfigLoad(&cfg) == APP_STATUS_NOT_INITIALIZED)
            {
                App_DeviceConfigSave(&cfg);
            }
            cfg.bootCount++;
            App_DeviceConfigSave(&cfg);
            App_DeviceConfigDump(&cfg);

            if (App_MeterServerOptionsLoad(&opt) == APP_STATUS_NOT_INITIALIZED)
            {
                App_MeterServerOptionsSave(&opt);
            }
            App_MeterServerOptionsDump(&opt);

            APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_STORAGE_SERVICE, APP_TRUE, APP_FALSE) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL); //eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_STORAGE, APP_FSM_STATE_STORAGE_SERVICE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_STORAGE_SERVICE:
            /* pseudo code*/
            /*
                do something;
                APP_RETURN_IF_FALSE(App_RtcApplySync() == APP_STATUS_OK, APP_STATUS_FATAL);
            */
            //APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_STORAGE_XXXXX, 0u, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
            //Clear eventPending
            App_FsmMarkComponent(APP_FSM_COMPONENT_STORAGE, APP_FSM_STATE_STORAGE_RELEASE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
            App_FsmSetDecision(APP_FSM_DECISION_RUN_ACTIVE);
            break;

        case APP_FSM_STATE_STORAGE_RELEASE:
            if (g_appFsmInitialBootRoutineQueued != APP_TRUE)
            {
                g_appFsmInitialBootRoutineQueued = APP_TRUE;
                APP_RETURN_IF_FALSE(App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u) == APP_STATUS_OK, APP_STATUS_MSGQ_FULL);
                App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
                APP_LOGI("FSM", "[[BootRoutine]] queue first attach/selftest/send/poweroff cycle");
            }
            App_FsmMarkComponent(APP_FSM_COMPONENT_STORAGE, APP_FSM_STATE_STORAGE_RELEASE, APP_FALSE, APP_FALSE, APP_STATUS_OK);
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
    g_appFsmInitialBootRoutineQueued = APP_FALSE;
    g_appFsmWakeCollectionPending = APP_FALSE;
    g_appFsmFirstAttachCycleDone = APP_FALSE;
    g_appFsmRtcMeterWakePending = APP_FALSE;
    g_appFsmRtcTxWakePending = APP_FALSE;
    (void)memset(&g_appFsmMeterSchedule, 0, sizeof(g_appFsmMeterSchedule));
    (void)memset(&g_appFsmTxSchedule, 0, sizeof(g_appFsmTxSchedule));

    return App_FsmQueueStateBack(APP_FSM_STATE_BOOT, 0u, 0u);
}

AppStatus_t App_FsmGetNextMeterDueTime(AppDateTime_t *p_dueTime)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_dueTime != NULL, APP_STATUS_INVALID_PARAM);

    status = App_FsmMeterScheduleEnsureInitialized();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    return App_FsmScheduleComposeDateTime(g_appFsmMeterSchedule.nextDueDateKey,
                                          g_appFsmMeterSchedule.nextDueMsOfDay,
                                          p_dueTime);
}

AppStatus_t App_FsmGetNextTxDueTime(AppDateTime_t *p_dueTime)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE(p_dueTime != NULL, APP_STATUS_INVALID_PARAM);

    status = App_FsmTxScheduleEnsureInitialized();
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    return App_FsmScheduleComposeDateTime(g_appFsmTxSchedule.nextDueDateKey,
                                          g_appFsmTxSchedule.nextDueMsOfDay,
                                          p_dueTime);
}

void App_FsmInvalidateRtcSchedules(void)
{
    g_appFsmMeterSchedule.initialized = APP_FALSE;
    g_appFsmMeterSchedule.enabled = APP_FALSE;
    g_appFsmMeterSchedule.rtcReadyLogged = APP_FALSE;
    g_appFsmMeterSchedule.nextDueDateKey = 0u;
    g_appFsmMeterSchedule.nextDueMsOfDay = 0u;

    g_appFsmTxSchedule.initialized = APP_FALSE;
    g_appFsmTxSchedule.enabled = APP_FALSE;
    g_appFsmTxSchedule.rtcReadyLogged = APP_FALSE;
    g_appFsmTxSchedule.nextDueDateKey = 0u;
    g_appFsmTxSchedule.nextDueMsOfDay = 0u;

    APP_LOGI("FSM", "[[RtcSync]] invalidate meter/tx due cache -> alarms will be recalculated on next STOP entry");
}

static AppStatus_t App_FsmHandleRtcWakeRouting(uint32_t rtcAlarmFlags)
{
    AppStatus_t status;
    uint8_t hasAlarmA;
    uint8_t hasAlarmB;

    hasAlarmA = ((rtcAlarmFlags & WAKEUP_FLAG_RTC_ALARM_A) != 0u) ? APP_TRUE : APP_FALSE;
    hasAlarmB = ((rtcAlarmFlags & WAKEUP_FLAG_RTC_ALARM_B) != 0u) ? APP_TRUE : APP_FALSE;

    if (hasAlarmA == APP_TRUE)
    {
        status = App_FsmMeterScheduleConsumeDueNow();
        APP_RETURN_IF_FALSE((status == APP_STATUS_OK) ||
                            (status == APP_STATUS_MSGQ_EMPTY) ||
                            (status == APP_STATUS_NOT_INITIALIZED),
                            status);
    }

    if (hasAlarmB == APP_TRUE)
    {
        status = App_FsmTxScheduleConsumeDueNow();
        APP_RETURN_IF_FALSE((status == APP_STATUS_OK) ||
                            (status == APP_STATUS_MSGQ_EMPTY) ||
                            (status == APP_STATUS_NOT_INITIALIZED),
                            status);
    }

    if ((hasAlarmA == APP_TRUE) && (hasAlarmB == APP_TRUE))
    {
        g_appFsmRtcMeterWakePending = APP_TRUE;
        g_appFsmRtcTxWakePending = APP_TRUE;
        g_appFsmWakeCollectionPending = APP_FALSE;

        status = App_SystemRequestShortRtcWakeup(APP_RTC_ALARM_COLLISION_TX_DELAY_SEC);
        if (status != APP_STATUS_OK)
        {
            APP_LOGW("FSM", "[[AlarmCollision]] deferred tx short wake request failed (status=%ld)", (long)status);
        }

        status = App_FsmQueueStateBack(APP_FSM_STATE_METER_WAIT_TRIGGER, APP_TRUE, 0u);
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_WAIT_TRIGGER, APP_TRUE, APP_FALSE, APP_STATUS_OK);
        APP_LOGI("FSM", "[[AlarmCollision]] AlarmA+AlarmB -> meter first, defer tx to short next cycle");
        return APP_STATUS_OK;
    }

    if (hasAlarmA == APP_TRUE)
    {
        g_appFsmRtcMeterWakePending = APP_TRUE;
        status = App_FsmQueueStateBack(APP_FSM_STATE_METER_WAIT_TRIGGER, APP_TRUE, 0u);
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        App_FsmMarkComponent(APP_FSM_COMPONENT_METER, APP_FSM_STATE_METER_WAIT_TRIGGER, APP_TRUE, APP_FALSE, APP_STATUS_OK);
        APP_LOGI("FSM", "[[AlarmA]] RTC wake -> queue meter read only (same-slot duplicate suppressed)");
    }

    if (hasAlarmB == APP_TRUE)
    {
        g_appFsmRtcTxWakePending = APP_TRUE;
        g_appFsmWakeCollectionPending = APP_FALSE;
        status = App_FsmQueueStateBack(APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, 0u);
        APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);
        App_FsmMarkComponent(APP_FSM_COMPONENT_NBIOT, APP_FSM_STATE_NBIOT_DECIDE_WAKE, APP_TRUE, APP_FALSE, APP_STATUS_OK);
        APP_LOGI("FSM", "[[AlarmB]] RTC wake -> queue transmit path only (next due advanced)");
    }

    return APP_STATUS_OK;
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
        APP_LOGD("FSM", "GetMsgq state:%s", App_FsmGetStateName(nextState));

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

    status = App_FsmCheckMeterScheduledState(&nextState);
    if (status == APP_STATUS_OK)
    {
        APP_LOGI("FSM", "Scheduled meter state:%s", App_FsmGetStateName(nextState));
        g_appFsmContext.summary.lastQueuedState = nextState;
        g_appFsmContext.summary.lastDequeFromFront = APP_FALSE;
        g_appFsmContext.summary.lastCommandTickMs = HAL_GetTick();
        g_appFsmContext.summary.lastCommandEventParam = APP_TRUE;
        g_appFsmContext.summary.lastCommandParam0 = 0u;
        g_appFsmContext.summary.transitionCount++;
        return App_FsmExecuteState(nextState, 0u);
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    status = App_FsmFindDuePeriodicState(&nextState);
    if (status == APP_STATUS_OK)
    {
        APP_LOGD("FSM", "Periodic state:%s", App_FsmGetStateName(nextState));
        g_appFsmContext.summary.lastQueuedState = nextState;
        g_appFsmContext.summary.lastDequeFromFront = APP_FALSE;
        g_appFsmContext.summary.lastCommandTickMs = HAL_GetTick();
        g_appFsmContext.summary.lastCommandEventParam = 0u;
        g_appFsmContext.summary.lastCommandParam0 = 0u;
        g_appFsmContext.summary.transitionCount++;
        return App_FsmExecuteState(nextState, 0u);
    }
    APP_RETURN_IF_FALSE(status == APP_STATUS_MSGQ_EMPTY, status);

    if(App_FsmIsSignalEventPending())
    {
        APP_LOGD("FSM", "EventPending. don't stop allow");
        return App_SystemRequestLowPower(APP_FALSE);
    }
    else
    {
        APP_LOGD("FSM", "No EventPending. stop allow");
    }

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
        case APP_FSM_STATE_NFC_RELEASE:             return "NFC_RELEASE";
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
        case APP_FSM_STATE_RTC_READY:               return "RTC_READY";
        case APP_FSM_STATE_LPTIM_INIT:              return "LPTIM_INIT";
        case APP_FSM_STATE_LPTIM_WAKE_SERVICE:      return "LPTIM_WAKE_SERVICE";
        case APP_FSM_STATE_LPTIM_APPLY_SYNC:        return "LPTIM_APPLY_SYNC";
        case APP_FSM_STATE_LPTIM_READY:             return "LPTIM_READY";
        case APP_FSM_STATE_WATCHDOG_INIT:           return "WATCHDOG_INIT";
        case APP_FSM_STATE_WATCHDOG_FEED:           return "WATCHDOG_FEED";
        case APP_FSM_STATE_STORAGE_INIT:            return "STORAGE_INIT";
        case APP_FSM_STATE_STORAGE_SERVICE:         return "STORAGE_SERVICE";
        case APP_FSM_STATE_STORAGE_RELEASE:         return "STORAGE_RELEASE";
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

AppStatus_t App_FsmNfcCliExecute(const char *p_subcommand,
                                 char *p_response,
                                 uint16_t response_length)
{
    const char *subcommand;
    int32_t written;

    APP_RETURN_IF_FALSE((p_response != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((response_length > 0u), APP_STATUS_INVALID_PARAM);

    p_response[0] = '\0';
    subcommand = (p_subcommand != NULL) ? p_subcommand : "";
    while (*subcommand == ' ')
    {
        subcommand++;
    }

    if ((subcommand[0] == '\0') || (strcmp(subcommand, "help") == 0))
    {
        written = snprintf(p_response, response_length,
                           "nfc: help|status|init|uid|driver|auth|cmd|lp|wake|exchange|logout");
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (strcmp(subcommand, "wake") == 0)
    {
        AppStatus_t queueStatus;

        queueStatus = App_FsmQueueStateBack(APP_FSM_STATE_NFC_WAIT_EVENT, APP_TRUE, 0u);
        written = snprintf(p_response, response_length,
                           (queueStatus == APP_STATUS_OK) ? "nfc wait-event queued" : "nfc wait-event queue failed status=%lu",
                           (queueStatus == APP_STATUS_OK) ? 0ul : (unsigned long)queueStatus);
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return queueStatus;
    }

    if (strcmp(subcommand, "exchange") == 0)
    {
        AppStatus_t queueStatus;

        queueStatus = App_FsmQueueStateBack(APP_FSM_STATE_NFC_EXCHANGE, APP_TRUE, 0u);
        written = snprintf(p_response, response_length,
                           (queueStatus == APP_STATUS_OK) ? "nfc exchange queued" : "nfc exchange queue failed status=%lu",
                           (queueStatus == APP_STATUS_OK) ? 0ul : (unsigned long)queueStatus);
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return queueStatus;
    }

    if (strcmp(subcommand, "status") == 0)
    {
        written = snprintf(p_response, response_length,
                           "nfc ready=%u irq=%u wake=%s driver=%s auth=%s session=%u ed=%u",
                           (unsigned int)g_nfcReady,
                           (unsigned int)g_nfcIrqPending,
                           App_FsmNfcGetWakeEventName(g_nfcWakeEvent),
                           App_FsmNfcGetDriverStateName(g_nfcTagHandle.state),
                           App_FsmNfcGetAuthStateName(g_nfcAuthHandle.state),
                           (unsigned int)NFC_AUTH_IsSessionValid(&g_nfcAuthHandle),
                           (unsigned int)NFC_NTP53321_IsEDTriggered(&g_nfcTagHandle));
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (g_nfcReady != APP_TRUE)
    {
        written = snprintf(p_response, response_length, "nfc not ready; run 'nfc init'");
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_NOT_INITIALIZED;
    }

    if (strcmp(subcommand, "uid") == 0)
    {
        uint8_t uid[7] = {0};
        NFC_Result_t uidStatus;

        uidStatus = NFC_NTP53321_GetUID(&g_nfcTagHandle, uid);
        if (uidStatus != NFC_RESULT_OK)
        {
            written = snprintf(p_response, response_length, "nfc uid read failed status=%u", (unsigned int)uidStatus);
            APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
            return APP_STATUS_FATAL;
        }

        written = snprintf(p_response, response_length,
                           "nfc uid=%02X%02X%02X%02X%02X%02X%02X",
                           uid[0], uid[1], uid[2], uid[3], uid[4], uid[5], uid[6]);
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (strcmp(subcommand, "driver") == 0)
    {
        NFC_NTP53321_Stats_t stats;

        memset(&stats, 0, sizeof(stats));
        NFC_NTP53321_GetStats(&g_nfcTagHandle, &stats);
        written = snprintf(p_response, response_length,
                           "nfc drv=%s mode=%u wake=%lu i2c_err=%lu last=%lu",
                           App_FsmNfcGetDriverStateName(g_nfcTagHandle.state),
                           (unsigned int)g_nfcTagHandle.ed_mode,
                           (unsigned long)stats.wakeup_count,
                           (unsigned long)stats.i2c_error_count,
                           (unsigned long)stats.last_wakeup_tick);
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (strcmp(subcommand, "auth") == 0)
    {
        NFC_AUTH_Stats_t stats;

        memset(&stats, 0, sizeof(stats));
        NFC_AUTH_GetStats(&g_nfcAuthHandle, &stats);
        written = snprintf(p_response, response_length,
                           "nfc auth=%s session=%u fail_now=%u att=%lu ok=%lu fail=%lu lock=%lu",
                           App_FsmNfcGetAuthStateName(g_nfcAuthHandle.state),
                           (unsigned int)NFC_AUTH_IsSessionValid(&g_nfcAuthHandle),
                           (unsigned int)g_nfcAuthHandle.fail_count,
                           (unsigned long)stats.total_attempts,
                           (unsigned long)stats.success_count,
                           (unsigned long)stats.fail_count,
                           (unsigned long)stats.lock_count);
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (strcmp(subcommand, "cmd") == 0)
    {
        written = snprintf(p_response, response_length,
                           "nfc cmd ok=%lu fail=%lu noauth=%lu interval=%us thr=%d.%dC",
                           (unsigned long)g_nfcCmdHandle.cmd_success_count,
                           (unsigned long)g_nfcCmdHandle.cmd_fail_count,
                           (unsigned long)g_nfcCmdHandle.cmd_no_auth_count,
                           (unsigned int)g_nfcCmdHandle.config.report_interval_sec,
                           (int)(g_nfcCmdHandle.config.temp_threshold_x10 / 10),
                           (int)(g_nfcCmdHandle.config.temp_threshold_x10 % 10));
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (strcmp(subcommand, "lp") == 0)
    {
        NFC_LP_Stats_t stats;

        memset(&stats, 0, sizeof(stats));
        NFC_LP_GetStats(&g_nfcLpHandle, &stats);
        written = snprintf(p_response, response_length,
                           "nfc lp wake=%lu active=%lu sleep=%lu avg=%lu.%02luuA",
                           (unsigned long)stats.total_wakeups,
                           (unsigned long)stats.total_active_ms,
                           (unsigned long)stats.total_sleep_ms,
                           (unsigned long)(stats.avg_current_x100 / 100u),
                           (unsigned long)(stats.avg_current_x100 % 100u));
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return APP_STATUS_OK;
    }

    if (strcmp(subcommand, "logout") == 0)
    {
        NFC_AUTH_Result_t authStatus;

        authStatus = NFC_AUTH_InvalidateSession(&g_nfcAuthHandle);
        written = snprintf(p_response, response_length,
                           (authStatus == NFC_AUTH_RESULT_OK) ? "nfc session cleared" : "nfc logout failed status=%u",
                           (unsigned int)authStatus);
        APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
        return (authStatus == NFC_AUTH_RESULT_OK) ? APP_STATUS_OK : APP_STATUS_FATAL;
    }

    written = snprintf(p_response, response_length,
                       "unknown nfc command; use: status|init|uid|driver|auth|cmd|lp|wake|exchange|logout");
    APP_RETURN_IF_FALSE((written >= 0), APP_STATUS_INIT_FAILED);
    return APP_STATUS_INVALID_PARAM;
}

#include "app_debug.h"

#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_fsm.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_system.h"
#include "app_selftest.h"

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
static const char g_appDebugPrompt[] = APP_DEBUG_CONSOLE_PROMPT;
#define APP_DEBUG_CONSOLE_RX_FIFO_SIZE    (128u)
static volatile uint8_t g_appDebugRxItByte;
static volatile uint8_t g_appDebugRxFifo[APP_DEBUG_CONSOLE_RX_FIFO_SIZE];
static volatile uint16_t g_appDebugRxFifoHead;
static volatile uint16_t g_appDebugRxFifoTail;
static volatile uint32_t g_appDebugRxFifoOverflowCount;
static volatile uint8_t g_appDebugRxInterruptArmed;
#endif
static AppDebugConsoleContext_t g_appDebugConsoleContext;

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
static AppStatus_t App_DebugConsoleWriteLine(const char *p_text);
static AppStatus_t App_DebugConsoleStartRxInterrupt(void);
static uint8_t App_DebugConsolePopRxByte(uint8_t *p_rxByte);
static void App_DebugConsolePushRxByteFromIsr(uint8_t rxByte);
static AppStatus_t App_DebugConsolePrintSelfTestSummary(const char *p_prefix)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppSelfTestContext_t *p_context;
    int32_t formattedLength;

    p_context = App_SelfTestGetContext();
    APP_RETURN_IF_FALSE((p_context != NULL), APP_STATUS_NOT_INITIALIZED);

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "%s status=%lu pass=%lu fail=%lu last_tick=%lu running=%u",
                               (p_prefix != NULL) ? p_prefix : "selftest",
                               (unsigned long)p_context->lastSequenceStatus,
                               (unsigned long)p_context->passCount,
                               (unsigned long)p_context->failCount,
                               (unsigned long)p_context->lastRunTickMs,
                               (unsigned int)p_context->running);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    return App_DebugConsoleWriteLine(txBuffer);
}
#endif

#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
static AppStatus_t App_DebugConsoleStartRxInterrupt(void)
{
    HAL_StatusTypeDef halStatus;

    if (g_appDebugConsoleContext.initialized != APP_TRUE)
    {
        return APP_STATUS_NOT_INITIALIZED;
    }

    if (g_appDebugRxInterruptArmed == APP_TRUE)
    {
        return APP_STATUS_OK;
    }

    __HAL_UART_CLEAR_FLAG(APP_UART_DEBUG_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_DEBUG_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    halStatus = HAL_UART_Receive_IT(APP_UART_DEBUG_HANDLE, (uint8_t *)&g_appDebugRxItByte, 1u);
    if ((halStatus != HAL_OK) && (halStatus != HAL_BUSY))
    {
        return APP_STATUS_UART_RX_FAILED;
    }

    g_appDebugRxInterruptArmed = APP_TRUE;
    return APP_STATUS_OK;
}

static uint8_t App_DebugConsolePopRxByte(uint8_t *p_rxByte)
{
    uint16_t tail;
    uint16_t nextTail;

    if (p_rxByte == NULL)
    {
        return APP_FALSE;
    }

    tail = g_appDebugRxFifoTail;
    if (tail == g_appDebugRxFifoHead)
    {
        return APP_FALSE;
    }

    *p_rxByte = g_appDebugRxFifo[tail];
    nextTail = (uint16_t)(tail + 1u);
    if (nextTail >= APP_DEBUG_CONSOLE_RX_FIFO_SIZE)
    {
        nextTail = 0u;
    }
    g_appDebugRxFifoTail = nextTail;
    return APP_TRUE;
}

static void App_DebugConsolePushRxByteFromIsr(uint8_t rxByte)
{
    uint16_t head;
    uint16_t nextHead;

    head = g_appDebugRxFifoHead;
    nextHead = (uint16_t)(head + 1u);
    if (nextHead >= APP_DEBUG_CONSOLE_RX_FIFO_SIZE)
    {
        nextHead = 0u;
    }

    if (nextHead == g_appDebugRxFifoTail)
    {
        g_appDebugRxFifoOverflowCount++;
        return;
    }

    g_appDebugRxFifo[head] = rxByte;
    g_appDebugRxFifoHead = nextHead;
}

static AppStatus_t App_DebugConsoleWriteLine(const char *p_text)
{
    AppStatus_t status;

    status = App_DebugConsoleWriteString(p_text);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    return App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_EOL);
}

static AppStatus_t App_DebugConsolePrintComponentTable(void)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppFsmSummary_t *p_fsmSummary;
    uint32_t index;
    int32_t formattedLength;
    uint32_t nowTick;

    p_fsmSummary = App_FsmGetSummary();
    nowTick = HAL_GetTick();

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "fsm=%s decision=%s disp=%lu loop=%lu q=%u",
                               App_FsmGetCurrentStateString(),
                               App_FsmGetDecisionString(),
                               (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                               (unsigned long)p_fsmSummary->loopCount,
                               (unsigned int)App_MsgqGetCount());
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    for (index = 0u; index < (uint32_t)APP_FSM_COMPONENT_COUNT; index++)
    {
        const AppFsmComponentContext_t *p_component;
        uint32_t ageMs;

        p_component = App_FsmGetComponent((AppFsmComponentId_t)index);
        if (p_component == NULL)
        {
            continue;
        }

        ageMs = (p_component->lastRunTickMs != 0u) ? (nowTick - p_component->lastRunTickMs) : 0u;
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "[%02lu] %-12s run=%lu int=%lu state=%-24s busy=%u evt=%u age=%lu last=%lu",
                                   (unsigned long)index,
                                   (p_component->p_name != NULL) ? p_component->p_name : "-",
                                   (unsigned long)p_component->runCount,
                                   (unsigned long)p_component->intervalMs,
                                   App_FsmGetStateName(p_component->state),
                                   (unsigned int)p_component->busy,
                                   (unsigned int)p_component->eventPending,
                                   (unsigned long)ageMs,
                                   (unsigned long)p_component->lastStatus);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_DebugConsoleParseState(const char *p_token, uint8_t *p_state)
{
    APP_RETURN_IF_FALSE((p_token != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_state != NULL), APP_STATUS_INVALID_PARAM);

    if (strcmp(p_token, "boot") == 0)
    {
        *p_state = APP_FSM_STATE_BOOT;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "idle") == 0) || (strcmp(p_token, "stop") == 0))
    {
        *p_state = APP_FSM_STATE_IDLE;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "debug") == 0) || (strcmp(p_token, "dbg") == 0))
    {
        *p_state = APP_FSM_STATE_DEBUG_POLL;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "power") == 0) || (strcmp(p_token, "reset") == 0))
    {
        *p_state = APP_FSM_STATE_POWER_WAIT_REQUEST;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "hk") == 0) || (strcmp(p_token, "housekeeping") == 0))
    {
        *p_state = APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "meter") == 0)
    {
        *p_state = APP_FSM_STATE_METER_WAIT_TRIGGER;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "nfc") == 0)
    {
        *p_state = APP_FSM_STATE_NFC_WAIT_EVENT;
        return APP_STATUS_OK;
    }
#if 0	//Temp support
    if (strcmp(p_token, "aux") == 0)
    {
        *p_state = APP_FSM_STATE_AUX_TRIGGER_MEASURE;
        return APP_STATUS_OK;
    }
#endif
    if (strcmp(p_token, "nbiot") == 0)
    {
        *p_state = APP_FSM_STATE_NBIOT_DECIDE_WAKE;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "server") == 0)
    {
        *p_state = APP_FSM_STATE_SERVER_PREPARE_PACKET;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "rtc") == 0)
    {
        *p_state = APP_FSM_STATE_RTC_WAKE_SERVICE;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "fault") == 0) || (strcmp(p_token, "safe") == 0))
    {
        *p_state = APP_FSM_STATE_FAULT;
        return APP_STATUS_OK;
    }

    return APP_STATUS_INVALID_PARAM;
}

static AppStatus_t App_DebugConsoleExecuteCommand(const char *p_command)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppSystemContext_t *p_systemContext;
    const AppClockContext_t *p_clockContext;
    const AppErrorRecord_t *p_errorRecord;
    const AppFsmSummary_t *p_fsmSummary;
    const AppMsgqContext_t *p_msgqContext;
    int32_t formattedLength;
    AppStatus_t status;

    if ((p_command == NULL) || (p_command[0] == '\0'))
    {
        return APP_STATUS_OK;
    }

    p_systemContext = App_SystemGetContext();
    p_clockContext = App_ClockGetContext();
    p_errorRecord = App_ErrorGetLast();
    p_fsmSummary = App_FsmGetSummary();
    p_msgqContext = App_MsgqGetContext();

    if (strcmp(p_command, "help") == 0)
    {
        if (App_DebugConsoleWriteLine("help                     : show command list") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("ver                      : show firmware version") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("status                   : show system summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("clock                    : show boot clock summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("error                    : show last error record") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("fsm                      : show state-machine summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("components               : show component table") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("q                        : show queue status") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("lp                       : show low-power state and wake source") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc help                 : show NFC CLI commands") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc status|driver|auth   : show NFC state/statistics") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc cmd|lp|uid           : show NFC command/lp/uid info") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc init|wake|exchange   : control NFC FSM/module") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("nfc logout               : invalidate NFC session") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("selftest                 : run self-test sequence") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("selftest status          : show last self-test summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("resetboot                : queue resetboot request") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm                       : show current FSM state") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm front <state>         : push state command to front") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm back <state>          : push state command to rear") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("echo on                  : enable console echo") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("echo off                 : disable console echo") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        return APP_STATUS_OK;
    }

    if (strcmp(p_command, "ver") == 0)
    {
        formattedLength = snprintf(txBuffer, sizeof(txBuffer), "%s v%s", APP_NAME_STRING, App_SystemGetVersionString());
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "status") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "boot=%lu loop=%lu idle=%lu disp=%lu fsm=%s q=%u lp=%s stop_req=%u stop=%lu wake=%s",
                                   (unsigned long)p_systemContext->bootStage,
                                   (unsigned long)p_systemContext->loopCounter,
                                   (unsigned long)p_systemContext->idleCounter,
                                   (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                                   App_FsmGetCurrentStateString(),
                                   (unsigned int)App_MsgqGetCount(),
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   App_SystemGetWakeSourceString());
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "clock") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "src=%lu sys=%lu hclk=%lu pclk1=%lu pclk2=%lu msi=%lu lse=%u flash=%lu",
                                   (unsigned long)p_clockContext->sysclkSource,
                                   (unsigned long)p_clockContext->sysclkHz,
                                   (unsigned long)p_clockContext->hclkHz,
                                   (unsigned long)p_clockContext->pclk1Hz,
                                   (unsigned long)p_clockContext->pclk2Hz,
                                   (unsigned long)p_clockContext->msiRange,
                                   (unsigned int)p_clockContext->lseReady,
                                   (unsigned long)p_clockContext->flashLatency);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "error") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "last_error=%lu file=%s line=%lu",
                                   (unsigned long)p_errorRecord->code,
                                   p_errorRecord->file,
                                   (unsigned long)p_errorRecord->line);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strcmp(p_command, "components") == 0) || (strcmp(p_command, "mods") == 0))
    {
        return App_DebugConsolePrintComponentTable();
    }

    if ((strcmp(p_command, "fsm") == 0) || (strcmp(p_command, "main") == 0))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "fsm=%s decision=%s trans=%lu msg=%lu idle_req=%lu cmd_tick=%lu state_tick=%lu",
                                   App_FsmGetCurrentStateString(),
                                   App_FsmGetDecisionString(),
                                   (unsigned long)p_fsmSummary->transitionCount,
                                   (unsigned long)p_fsmSummary->processedMessageCount,
                                   (unsigned long)p_fsmSummary->queueEmptyStopCount,
                                   (unsigned long)p_fsmSummary->lastCommandTickMs,
                                   (unsigned long)p_fsmSummary->lastStateTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strcmp(p_command, "q") == 0) || (strcmp(p_command, "queue") == 0))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "q count=%u push_f=%lu push_b=%lu pop_f=%lu pop_b=%lu ovf=%lu",
                                   (unsigned int)p_msgqContext->count,
                                   (unsigned long)p_msgqContext->pushFrontCount,
                                   (unsigned long)p_msgqContext->pushBackCount,
                                   (unsigned long)p_msgqContext->popFrontCount,
                                   (unsigned long)p_msgqContext->popBackCount,
                                   (unsigned long)p_msgqContext->overflowCount);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "lp") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "lp=%s disp=%lu stop_req=%u qual=%u cand=%lu stop=%lu rtc=%lu lptim=%lu sleep=%lu wake=%s req_tick=%lu wake_tick=%lu",
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned int)p_systemContext->stopQualificationCount,
                                   (unsigned long)p_systemContext->stopCandidateCount,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   (unsigned long)p_systemContext->rtcWakeEventCount,
                                   (unsigned long)p_systemContext->lptimWakeEventCount,
                                   (unsigned long)p_systemContext->sleepEntryCount,
                                   App_SystemGetWakeSourceString(),
                                   (unsigned long)p_systemContext->lastStopRequestTickMs,
                                   (unsigned long)p_systemContext->lastWakeTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "selftest") == 0)
    {
        status = App_SelfTestRunBootSequence();
        if (status != APP_STATUS_OK)
        {
            return App_DebugConsolePrintSelfTestSummary("selftest done");
        }
        return App_DebugConsolePrintSelfTestSummary("selftest done");
    }

    if (strcmp(p_command, "selftest status") == 0)
    {
        return App_DebugConsolePrintSelfTestSummary("selftest");
    }

    if (strcmp(p_command, "resetboot") == 0)
    {
        status = App_FsmRequestResetBoot();
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "resetboot failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }
        return App_DebugConsoleWriteLine("resetboot queued");
    }

    if ((strcmp(p_command, "nfc") == 0) || (strncmp(p_command, "nfc ", 4) == 0))
    {
        const char *p_subcommand;

        p_subcommand = (p_command[3] == '\0') ? "" : &p_command[4];
        status = App_FsmNfcCliExecute(p_subcommand, txBuffer, (uint16_t)sizeof(txBuffer));
        if ((status != APP_STATUS_OK) && (txBuffer[0] == '\0'))
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "nfc command failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        }
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "sm") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "sm=%s queued=%u last=%u front=%u",
                                   App_FsmGetCurrentStateString(),
                                   (unsigned int)App_MsgqGetCount(),
                                   (unsigned int)p_fsmSummary->lastQueuedState,
                                   (unsigned int)p_fsmSummary->lastDequeFromFront);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strncmp(p_command, "sm front ", 9) == 0) || (strncmp(p_command, "sm back ", 8) == 0))
    {
        char stateToken[24];
        uint8_t nextState;
        uint8_t pushFront;

        pushFront = (p_command[3] == 'f') ? APP_TRUE : APP_FALSE;
        if (sscanf(p_command, pushFront == APP_TRUE ? "sm front %23s" : "sm back %23s", stateToken) != 1)
        {
#if 0	//Temp support
            return App_DebugConsoleWriteLine("usage: sm <front|back> <boot|idle|debug|power|hk|meter|nfc|aux|nbiot|server|rtc|fault>");
#endif
            return App_DebugConsoleWriteLine("usage: sm <front|back> <boot|idle|debug|power|hk|meter|nfc|nbiot|server|rtc|fault>");
        }

        status = App_DebugConsoleParseState(stateToken, &nextState);
        if (status != APP_STATUS_OK)
        {
#if 0	//Temp support
            return App_DebugConsoleWriteLine("state: boot, idle, debug, power, hk, meter, nfc, aux, nbiot, server, rtc, fault");
#endif
            return App_DebugConsoleWriteLine("state: boot, idle, debug, power, hk, meter, nfc, nbiot, server, rtc, fault");
        }

        status = (pushFront == APP_TRUE)
                 ? App_FsmQueueStateFront(nextState, 0u, 0u)
                 : App_FsmQueueStateBack(nextState, 0u, 0u);
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "sm queue failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }

        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "sm queued mode=%s state=%s",
                                   (pushFront == APP_TRUE) ? "front" : "back",
                                   App_FsmGetStateName(nextState));
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "echo on") == 0)
    {
        g_appDebugConsoleContext.echoEnabled = APP_TRUE;
        return App_DebugConsoleWriteLine("echo enabled");
    }

    if (strcmp(p_command, "echo off") == 0)
    {
        g_appDebugConsoleContext.echoEnabled = APP_FALSE;
        return App_DebugConsoleWriteLine("echo disabled");
    }

    g_appDebugConsoleContext.unknownCommandCount++;
    return App_DebugConsoleWriteLine("unknown command; type 'help'");
}

static AppStatus_t App_DebugConsoleCommitLine(void)
{
    AppStatus_t status;

    g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = '\0';

    if (g_appDebugConsoleContext.echoEnabled == APP_TRUE)
    {
        status = App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_EOL);
        if (status != APP_STATUS_OK)
        {
            return status;
        }
    }

    status = App_DebugConsoleExecuteCommand(g_appDebugConsoleContext.rxLine);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appDebugConsoleContext.commandCount++;
    g_appDebugConsoleContext.rxLength = 0u;
    g_appDebugConsoleContext.rxLine[0] = '\0';

    return App_DebugConsolePrintPrompt();
}

static AppStatus_t App_DebugConsoleHandleByte(uint8_t rxByte)
{
    static const uint8_t backspaceSequence[] = {'\b', ' ', '\b'};

    if ((rxByte == '\n') && (g_appDebugConsoleContext.ignoreLineFeed == APP_TRUE))
    {
        g_appDebugConsoleContext.ignoreLineFeed = APP_FALSE;
        return APP_STATUS_OK;
    }

    if ((rxByte == '\r') || (rxByte == '\n'))
    {
        g_appDebugConsoleContext.ignoreLineFeed = (rxByte == '\r') ? APP_TRUE : APP_FALSE;
        return App_DebugConsoleCommitLine();
    }

    if ((rxByte == 0x08u) || (rxByte == 0x7Fu))
    {
        if (g_appDebugConsoleContext.rxLength > 0u)
        {
            g_appDebugConsoleContext.rxLength--;
            g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = '\0';
            if (g_appDebugConsoleContext.echoEnabled == APP_TRUE)
            {
                return App_DebugConsoleWrite(backspaceSequence, (uint16_t)sizeof(backspaceSequence));
            }
        }
        return APP_STATUS_OK;
    }

    if ((rxByte < 0x20u) || (rxByte > 0x7Eu))
    {
        return APP_STATUS_OK;
    }

    if (g_appDebugConsoleContext.rxLength >= (APP_DEBUG_CONSOLE_RX_LINE_SIZE - 1u))
    {
        g_appDebugConsoleContext.rxLength = 0u;
        g_appDebugConsoleContext.rxLine[0] = '\0';
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine("input overflow") == APP_STATUS_OK, APP_STATUS_BUFFER_OVERFLOW);
        return App_DebugConsolePrintPrompt();
    }

    g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = (char)rxByte;
    g_appDebugConsoleContext.rxLength++;
    g_appDebugConsoleContext.rxLine[g_appDebugConsoleContext.rxLength] = '\0';

    if (g_appDebugConsoleContext.echoEnabled == APP_TRUE)
    {
        return App_DebugConsoleWrite(&rxByte, 1u);
    }

    return APP_STATUS_OK;
}

#endif

AppStatus_t App_DebugConsoleInit(void)
{
    (void)memset(&g_appDebugConsoleContext, 0, sizeof(g_appDebugConsoleContext));
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    g_appDebugConsoleContext.echoEnabled = APP_TRUE;
    g_appDebugRxItByte = 0u;
    g_appDebugRxFifoHead = 0u;
    g_appDebugRxFifoTail = 0u;
    g_appDebugRxFifoOverflowCount = 0u;
    g_appDebugRxInterruptArmed = APP_FALSE;
#else
    g_appDebugConsoleContext.echoEnabled = APP_FALSE;
#endif
    g_appDebugConsoleContext.initialized = APP_TRUE;
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    return App_DebugConsoleStartRxInterrupt();
#else
    return APP_STATUS_OK;
#endif
}

AppStatus_t App_DebugConsoleProcess(void)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    AppStatus_t status;
    uint8_t rxByte;

    APP_RETURN_IF_FALSE(g_appDebugConsoleContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    status = App_DebugConsoleStartRxInterrupt();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    while (App_DebugConsolePopRxByte(&rxByte) == APP_TRUE)
    {
        AppStatus_t byteStatus;

        byteStatus = App_DebugConsoleHandleByte(rxByte);
        if (byteStatus != APP_STATUS_OK)
        {
            return byteStatus;
        }
    }
#endif

    return APP_STATUS_OK;
}

AppStatus_t App_DebugConsoleWrite(const uint8_t *p_data, uint16_t length)
{
    APP_RETURN_IF_FALSE(g_appDebugConsoleContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(((p_data != NULL) || (length == 0u)), APP_STATUS_INVALID_PARAM);

    if (length == 0u)
    {
        return APP_STATUS_OK;
    }

    APP_RETURN_IF_HAL_ERROR(HAL_UART_Transmit(APP_UART_DEBUG_HANDLE,
                                              (uint8_t *)p_data,
                                              length,
                                              APP_DEBUG_UART_TIMEOUT_MS),
                            APP_STATUS_UART_TX_FAILED);
    return APP_STATUS_OK;
}

AppStatus_t App_DebugConsoleWriteString(const char *p_text)
{
    APP_RETURN_IF_FALSE((p_text != NULL), APP_STATUS_INVALID_PARAM);
    return App_DebugConsoleWrite((const uint8_t *)p_text, (uint16_t)strlen(p_text));
}

AppStatus_t App_DebugConsolePrintPrompt(void)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    return App_DebugConsoleWrite((const uint8_t *)g_appDebugPrompt, (uint16_t)(sizeof(g_appDebugPrompt) - 1u));
#else
    return APP_STATUS_OK;
#endif
}


void App_DebugConsoleOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    HAL_StatusTypeDef halStatus;

    if ((p_huart == NULL) || (p_huart != APP_UART_DEBUG_HANDLE))
    {
        return;
    }

    App_DebugConsolePushRxByteFromIsr(g_appDebugRxItByte);
    g_appDebugRxInterruptArmed = APP_FALSE;
    halStatus = HAL_UART_Receive_IT(APP_UART_DEBUG_HANDLE, (uint8_t *)&g_appDebugRxItByte, 1u);
    if ((halStatus == HAL_OK) || (halStatus == HAL_BUSY))
    {
        g_appDebugRxInterruptArmed = APP_TRUE;
    }
#else
    (void)p_huart;
#endif
}

void App_DebugConsoleOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
#if (APP_BUILD_CLI_ENABLED == APP_TRUE)
    HAL_StatusTypeDef halStatus;

    if ((p_huart == NULL) || (p_huart != APP_UART_DEBUG_HANDLE))
    {
        return;
    }

    __HAL_UART_CLEAR_FLAG(APP_UART_DEBUG_HANDLE,
                          UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_DEBUG_HANDLE, UART_RXDATA_FLUSH_REQUEST);
    g_appDebugRxInterruptArmed = APP_FALSE;
    halStatus = HAL_UART_Receive_IT(APP_UART_DEBUG_HANDLE, (uint8_t *)&g_appDebugRxItByte, 1u);
    if ((halStatus == HAL_OK) || (halStatus == HAL_BUSY))
    {
        g_appDebugRxInterruptArmed = APP_TRUE;
    }
#else
    (void)p_huart;
#endif
}

const AppDebugConsoleContext_t *App_DebugConsoleGetContext(void)
{
    return &g_appDebugConsoleContext;
}

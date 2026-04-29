#include "app_debug.h"

#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_scheduler.h"
#include "app_system.h"
#include "app_tasks.h"

static const char g_appDebugPrompt[] = APP_DEBUG_CONSOLE_PROMPT;
static AppDebugConsoleContext_t g_appDebugConsoleContext;

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

static AppStatus_t App_DebugConsolePrintTaskTable(void)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppSchedulerContext_t *p_schedulerContext;
    uint32_t index;
    int32_t formattedLength;
    uint32_t nowTick;

    p_schedulerContext = App_SchedulerGetContext();
    nowTick = HAL_GetTick();

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "sched tasks=%u dispatch=%u idle=%lu loop=%lu main=%s q=%u",
                               (unsigned int)((p_schedulerContext != NULL) ? p_schedulerContext->taskCount : 0u),
                               (unsigned int)((p_schedulerContext != NULL) ? p_schedulerContext->lastDispatchCount : 0u),
                               (unsigned long)((p_schedulerContext != NULL) ? p_schedulerContext->idleCount : 0u),
                               (unsigned long)((p_schedulerContext != NULL) ? p_schedulerContext->loopCount : 0u),
                               App_TaskMainGetStateString(),
                               (unsigned int)App_MsgqGetCount());
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    for (index = 0u; index < (uint32_t)APP_TASK_ID_COUNT; index++)
    {
        const AppTaskModuleContext_t *p_module;
        const char *p_stateName;
        uint32_t ageMs;

        p_module = App_TasksGetModuleContext((AppTaskId_t)index);
        if (p_module == NULL)
        {
            continue;
        }

        p_stateName = App_TasksGetStateName((AppTaskId_t)index, p_module->state);
        ageMs = (p_module->lastRunTickMs != 0u) ? (nowTick - p_module->lastRunTickMs) : 0u;

        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "[%02lu] %-12s run=%lu period=%lu state=%-20s busy=%u evt=%u age=%lu last=%lu",
                                   (unsigned long)index,
                                   (p_module->p_name != NULL) ? p_module->p_name : "-",
                                   (unsigned long)p_module->runCount,
                                   (unsigned long)p_module->periodMs,
                                   p_stateName,
                                   (unsigned int)p_module->busy,
                                   (unsigned int)p_module->eventPending,
                                   (unsigned long)ageMs,
                                   (unsigned long)p_module->lastStatus);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_DebugConsoleParseMainState(const char *p_token, uint8_t *p_state)
{
    APP_RETURN_IF_FALSE((p_token != NULL), APP_STATUS_INVALID_PARAM);
    APP_RETURN_IF_FALSE((p_state != NULL), APP_STATUS_INVALID_PARAM);

    if (strcmp(p_token, "boot") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_BOOT;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "idle") == 0) || (strcmp(p_token, "stop") == 0))
    {
        *p_state = APP_TASK_MAIN_STATE_IDLE;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "debug") == 0) || (strcmp(p_token, "dbg") == 0))
    {
        *p_state = APP_TASK_MAIN_STATE_DEBUG_POLL;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "power") == 0) || (strcmp(p_token, "reset") == 0))
    {
        *p_state = APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "hk") == 0) || (strcmp(p_token, "housekeeping") == 0))
    {
        *p_state = APP_TASK_MAIN_STATE_HOUSEKEEPING_SNAPSHOT;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "meter") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "nfc") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_NFC_WAIT_EVENT;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "aux") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_AUX_TRIGGER_MEASURE;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "nbiot") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "server") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_SERVER_PREPARE_PACKET;
        return APP_STATUS_OK;
    }
    if (strcmp(p_token, "rtc") == 0)
    {
        *p_state = APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE;
        return APP_STATUS_OK;
    }
    if ((strcmp(p_token, "fault") == 0) || (strcmp(p_token, "safe") == 0))
    {
        *p_state = APP_TASK_MAIN_STATE_FAULT;
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
    const AppSchedulerContext_t *p_schedulerContext;
    const AppTaskMainSummary_t *p_mainSummary;
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
    p_schedulerContext = App_SchedulerGetContext();
    p_mainSummary = App_TaskMainGetSummary();
    p_msgqContext = App_MsgqGetContext();

    if (strcmp(p_command, "help") == 0)
    {
        if (App_DebugConsoleWriteLine("help                     : show command list") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("ver                      : show firmware version") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("status                   : show system/debug state") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("clock                    : show boot clock summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("error                    : show last error record") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("tasks                    : show unified state-machine table") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("main                     : show main state-machine summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("q                        : show deque status") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("lp                       : show low-power state and wake source") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("resetboot                : queue resetboot request") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("sm                       : show current state-machine state") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
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
                                   "boot=%lu loop=%lu idle=%lu dispatch=%u main=%s q=%u lp=%s stop_req=%u stop=%lu wake=%s",
                                   (unsigned long)p_systemContext->bootStage,
                                   (unsigned long)p_systemContext->loopCounter,
                                   (unsigned long)p_systemContext->idleCounter,
                                   (unsigned int)((p_schedulerContext != NULL) ? p_schedulerContext->lastDispatchCount : 0u),
                                   App_TaskMainGetStateString(),
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

    if (strcmp(p_command, "tasks") == 0)
    {
        return App_DebugConsolePrintTaskTable();
    }

    if (strcmp(p_command, "main") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "main=%s decision=%s trans=%lu msg=%lu qempty_stop=%lu cmd_tick=%lu state_tick=%lu",
                                   App_TaskMainGetStateString(),
                                   App_TaskMainGetDecisionString(),
                                   (unsigned long)p_mainSummary->transitionCount,
                                   (unsigned long)p_mainSummary->processedMessageCount,
                                   (unsigned long)p_mainSummary->queueEmptyStopCount,
                                   (unsigned long)p_mainSummary->lastCommandTickMs,
                                   (unsigned long)p_mainSummary->lastStateTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if ((strcmp(p_command, "q") == 0) || (strcmp(p_command, "queue") == 0))
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "q count=%u front_push=%lu back_push=%lu front_pop=%lu back_pop=%lu ovf=%lu",
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
                                   "lp=%s dispatch=%u stop_req=%u qual=%u cand=%lu stop=%lu rtc=%lu sleep=%lu wake=%s req_tick=%lu wake_tick=%lu",
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned int)((p_schedulerContext != NULL) ? p_schedulerContext->lastDispatchCount : 0u),
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned int)p_systemContext->stopQualificationCount,
                                   (unsigned long)p_systemContext->stopCandidateCount,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   (unsigned long)p_systemContext->rtcWakeEventCount,
                                   (unsigned long)p_systemContext->sleepEntryCount,
                                   App_SystemGetWakeSourceString(),
                                   (unsigned long)p_systemContext->lastStopRequestTickMs,
                                   (unsigned long)p_systemContext->lastWakeTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "resetboot") == 0)
    {
        status = App_TaskMainRequestPowerResetBoot();
        if (status != APP_STATUS_OK)
        {
            formattedLength = snprintf(txBuffer, sizeof(txBuffer), "resetboot failed status=%lu", (unsigned long)status);
            APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
            return App_DebugConsoleWriteLine(txBuffer);
        }
        return App_DebugConsoleWriteLine("resetboot queued");
    }

    if (strcmp(p_command, "sm") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "sm=%s queued=%u last=%u front=%u",
                                   App_TaskMainGetStateString(),
                                   (unsigned int)App_MsgqGetCount(),
                                   (unsigned int)p_mainSummary->lastQueuedState,
                                   (unsigned int)p_mainSummary->lastDequeFromFront);
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
            return App_DebugConsoleWriteLine("usage: sm <front|back> <boot|idle|debug|power|hk|meter|nfc|aux|nbiot|server|rtc|fault>");
        }

        status = App_DebugConsoleParseMainState(stateToken, &nextState);
        if (status != APP_STATUS_OK)
        {
            return App_DebugConsoleWriteLine("state: boot, idle, debug, power, hk, meter, nfc, aux, nbiot, server, rtc, fault");
        }

        status = (pushFront == APP_TRUE)
                 ? App_TaskMainQueueStateCommandFront(nextState, 0u, 0u)
                 : App_TaskMainQueueStateCommandBack(nextState, 0u, 0u);
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
                                   App_TasksGetStateName(APP_TASK_ID_MAIN, nextState));
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

AppStatus_t App_DebugConsoleInit(void)
{
    (void)memset(&g_appDebugConsoleContext, 0, sizeof(g_appDebugConsoleContext));
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
    g_appDebugConsoleContext.echoEnabled = APP_TRUE;
    g_appDebugConsoleContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_DebugConsoleProcess(void)
{
    HAL_StatusTypeDef halStatus;
    uint8_t rxByte;

    APP_RETURN_IF_FALSE(g_appDebugConsoleContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    while (1)
    {
        halStatus = HAL_UART_Receive(APP_UART_DEBUG_HANDLE, &rxByte, 1u, 0u);
        if (halStatus == HAL_TIMEOUT)
        {
            break;
        }
        if (halStatus != HAL_OK)
        {
            return APP_STATUS_UART_RX_FAILED;
        }
        {
            AppStatus_t byteStatus;

            byteStatus = App_DebugConsoleHandleByte(rxByte);
            if (byteStatus != APP_STATUS_OK)
            {
                return byteStatus;
            }
        }
    }

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
    return App_DebugConsoleWrite((const uint8_t *)g_appDebugPrompt, (uint16_t)(sizeof(g_appDebugPrompt) - 1u));
}

const AppDebugConsoleContext_t *App_DebugConsoleGetContext(void)
{
    return &g_appDebugConsoleContext;
}

#include "app_debug.h"

#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_hw.h"
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
    const AppTaskMainSummary_t *p_mainSummary;
    uint32_t index;
    int32_t formattedLength;
    uint32_t nowTick;

    p_schedulerContext = App_SchedulerGetContext();
    p_mainSummary = App_TaskMainGetSummary();
    nowTick = HAL_GetTick();

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "sched tasks=%u dispatch=%u idle=%lu loop=%lu main=%s alive=%lu busy=%lu stale=%lu",
                               (unsigned int)p_schedulerContext->taskCount,
                               (unsigned int)p_schedulerContext->lastDispatchCount,
                               (unsigned long)p_schedulerContext->idleCount,
                               (unsigned long)p_schedulerContext->loopCount,
                               App_TaskMainGetDecisionString(),
                               (unsigned long)p_mainSummary->aliveCount,
                               (unsigned long)p_mainSummary->busyCount,
                               (unsigned long)p_mainSummary->staleCount);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);

    for (index = 0u; index < APP_SCHEDULER_MAX_TASKS; index++)
    {
        const AppSchedulerTask_t *p_task;
        AppTaskId_t taskId;
        const AppTaskModuleContext_t *p_module;
        const AppTaskMainMonitor_t *p_monitor;
        const char *p_stateName;
        uint32_t ageMs;

        p_task = App_SchedulerGetTask((AppSchedulerTaskHandle_t)index);
        if (p_task == NULL)
        {
            continue;
        }

        taskId = App_TasksFindIdBySchedulerHandle((AppSchedulerTaskHandle_t)index);
        p_module = ((uint32_t)taskId < (uint32_t)APP_TASK_ID_COUNT) ? App_TasksGetModuleContext(taskId) : NULL;
        p_monitor = ((uint32_t)taskId < (uint32_t)APP_TASK_ID_COUNT) ? App_TaskMainGetMonitor(taskId) : NULL;
        p_stateName = ((p_module != NULL) && ((uint32_t)taskId < (uint32_t)APP_TASK_ID_COUNT)) ? App_TasksGetStateName(taskId, p_module->state) : "-";
        ageMs = ((p_monitor != NULL) && (p_monitor->heartbeatCount != 0u)) ? (nowTick - p_monitor->lastHeartbeatTickMs) : 0u;

        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "[%02lu] %-12s en=%u run=%lu state=%-12s busy=%u alive=%u age=%lu last=%lu",
                                   (unsigned long)index,
                                   (p_task->p_name != NULL) ? p_task->p_name : "-",
                                   (unsigned int)p_task->enabled,
                                   (unsigned long)p_task->runCount,
                                   p_stateName,
                                   (unsigned int)((p_module != NULL) ? p_module->busy : 0u),
                                   (unsigned int)((p_monitor != NULL) ? p_monitor->alive : 0u),
                                   (unsigned long)ageMs,
                                   (unsigned long)p_task->lastStatus);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        APP_RETURN_IF_FALSE(App_DebugConsoleWriteLine(txBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_DebugConsoleExecuteCommand(const char *p_command)
{
    char txBuffer[APP_DEBUG_CONSOLE_TX_BUFFER_SIZE];
    const AppSystemContext_t *p_systemContext;
    const AppClockContext_t *p_clockContext;
    const AppErrorRecord_t *p_errorRecord;
    const AppDebugConsoleContext_t *p_debugContext;
    const AppTaskMainSummary_t *p_mainSummary;
    const AppTaskWatchdogSummary_t *p_watchdogSummary;
    int32_t formattedLength;

    if ((p_command == NULL) || (p_command[0] == '\0'))
    {
        return APP_STATUS_OK;
    }

    p_systemContext = App_SystemGetContext();
    p_clockContext = App_ClockGetContext();
    p_errorRecord = App_ErrorGetLast();
    p_debugContext = App_DebugConsoleGetContext();
    p_mainSummary = App_TaskMainGetSummary();
    p_watchdogSummary = App_TaskWatchdogGetSummary();

    if (strcmp(p_command, "help") == 0)
    {
        if (App_DebugConsoleWriteLine("help       : show command list") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("ver        : show firmware version") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("status     : show system/debug state") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("clock      : show boot clock summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("error      : show last error record") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("tasks      : show scheduler and task state") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("main       : show main-task decision summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("wdog       : show watchdog service summary") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("lp         : show low-power state and wake source") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("echo on    : enable console echo") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
        if (App_DebugConsoleWriteLine("echo off   : disable console echo") != APP_STATUS_OK) { return APP_STATUS_UART_TX_FAILED; }
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
                                   "boot=%lu loop=%lu idle=%lu lp=%s stop_req=%u qual=%u cand=%lu stop=%lu dry=%lu sleep=%lu wake=%s",
                                   (unsigned long)p_systemContext->bootStage,
                                   (unsigned long)p_systemContext->loopCounter,
                                   (unsigned long)p_systemContext->idleCounter,
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned int)p_systemContext->stopQualificationCount,
                                   (unsigned long)p_systemContext->stopCandidateCount,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   (unsigned long)p_systemContext->stopDryRunCount,
                                   (unsigned long)p_systemContext->sleepEntryCount,
                                   App_SystemGetWakeSourceString());
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "clock") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "src=%lu sys=%lu hclk=%lu pclk1=%lu pclk2=%lu msi=%lu lsi=%u flash=%lu",
                                   (unsigned long)p_clockContext->sysclkSource,
                                   (unsigned long)p_clockContext->sysclkHz,
                                   (unsigned long)p_clockContext->hclkHz,
                                   (unsigned long)p_clockContext->pclk1Hz,
                                   (unsigned long)p_clockContext->pclk2Hz,
                                   (unsigned long)p_clockContext->msiRange,
                                   (unsigned int)p_clockContext->lsiReady,
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
                                   "main=%s alive=%lu busy=%lu stale=%lu msg=%lu tick=%lu",
                                   App_TaskMainGetDecisionString(),
                                   (unsigned long)p_mainSummary->aliveCount,
                                   (unsigned long)p_mainSummary->busyCount,
                                   (unsigned long)p_mainSummary->staleCount,
                                   (unsigned long)p_mainSummary->processedMessageCount,
                                   (unsigned long)p_mainSummary->lastEvaluationTickMs);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "wdog") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "wdog init=%u ok=%u iwdg=%lu ext=%lu last=%lu ext_last=%lu status=%lu",
                                   (unsigned int)p_watchdogSummary->initialized,
                                   (unsigned int)p_watchdogSummary->lastServiceOk,
                                   (unsigned long)p_watchdogSummary->iwdgRefreshCount,
                                   (unsigned long)p_watchdogSummary->externalFeedCount,
                                   (unsigned long)p_watchdogSummary->lastServiceTickMs,
                                   (unsigned long)p_watchdogSummary->lastExternalFeedTickMs,
                                   (unsigned long)p_watchdogSummary->lastStatus);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);
        return App_DebugConsoleWriteLine(txBuffer);
    }

    if (strcmp(p_command, "lp") == 0)
    {
        formattedLength = snprintf(txBuffer,
                                   sizeof(txBuffer),
                                   "lp=%s stop_req=%u qual=%u cand=%lu stop=%lu dry=%lu sleep=%lu wake=%s req_tick=%lu wake_tick=%lu",
                                   App_SystemGetLowPowerModeString(),
                                   (unsigned int)p_systemContext->stopRequested,
                                   (unsigned int)p_systemContext->stopQualificationCount,
                                   (unsigned long)p_systemContext->stopCandidateCount,
                                   (unsigned long)p_systemContext->stopEntryCount,
                                   (unsigned long)p_systemContext->stopDryRunCount,
                                   (unsigned long)p_systemContext->sleepEntryCount,
                                   App_SystemGetWakeSourceString(),
                                   (unsigned long)p_systemContext->lastStopRequestTickMs,
                                   (unsigned long)p_systemContext->lastWakeTickMs);
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
            AppStatus_t status;
            status = App_DebugConsoleHandleByte(rxByte);
            if (status != APP_STATUS_OK)
            {
                return status;
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

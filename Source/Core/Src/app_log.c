#include "app_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_debug.h"

/**
 * @file    app_log.c
 * @brief   UART1-based logging implementation.
 */

/** @brief Logging runtime context. */
static AppLogContext_t g_appLogContext;

/**
 * @brief Convert log level to short text.
 *
 * @param level Log severity.
 * @return Constant level string.
 */
static const char *App_LogLevelToString(AppLogLevel_t level)
{
    switch (level)
    {
        case APP_LOG_LEVEL_TRACE:
            return "TRACE";

        case APP_LOG_LEVEL_DEBUG:
            return "DEBUG";

        case APP_LOG_LEVEL_INFO:
            return "INFO";

        case APP_LOG_LEVEL_WARN:
            return "WARN";

        case APP_LOG_LEVEL_ERROR:
            return "ERROR";

        default:
            return "NONE";
    }
}

static const char* get_local_time_str(void)
{
    static char buf[16];  // "hhmmss.mmm\0" = 13 bytes, 여유있게 16

    uint32_t tick = HAL_GetTick();

    uint32_t ms   =  tick % 1000;
    uint32_t sec  = (tick / 1000)    % 60;
    uint32_t min  = (tick / 60000)   % 60;
    uint32_t hour = (tick / 3600000) % 24;

    snprintf(buf, sizeof(buf), "%02lu%02lu%02lu.%03lu", hour, min, sec, ms);

    return buf;
}

AppStatus_t App_LogInit(void)
{
    (void)memset(&g_appLogContext, 0, sizeof(g_appLogContext));

    APP_RETURN_IF_FALSE(App_DebugConsoleGetContext()->initialized == APP_TRUE, APP_STATUS_LOG_INIT_FAILED);

    g_appLogContext.minimumLevel = APP_LOG_LEVEL;
    g_appLogContext.initialized = APP_TRUE;

    return APP_STATUS_OK;
}

AppStatus_t App_LogSetLevel(AppLogLevel_t level)
{
    APP_RETURN_IF_FALSE(g_appLogContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(level <= APP_LOG_LEVEL_NONE, APP_STATUS_INVALID_PARAM);

    g_appLogContext.minimumLevel = level;

    return APP_STATUS_OK;
}

const AppLogContext_t *App_LogGetContext(void)
{
    return &g_appLogContext;
}

AppStatus_t App_LogWrite(AppLogLevel_t level, const char *p_module, const char *p_message)
{
    char txBuffer[APP_LOG_BUFFER_SIZE];
    int32_t formattedLength;
    const char *p_levelString;
    const char *p_safeModule;
    const char *p_safeMessage;

    APP_RETURN_IF_FALSE(g_appLogContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(level <= APP_LOG_LEVEL_NONE, APP_STATUS_INVALID_PARAM);

    if ((level < g_appLogContext.minimumLevel) || (level == APP_LOG_LEVEL_NONE))
    {
        return APP_STATUS_OK;
    }

    p_levelString = App_LogLevelToString(level);
    p_safeModule = (p_module != NULL) ? p_module : "APP";
    p_safeMessage = (p_message != NULL) ? p_message : "";

    formattedLength = snprintf(txBuffer,
                               sizeof(txBuffer),
                               "[%s][%s][%s] %s%s",
                                get_local_time_str(),
                               p_levelString,
                               p_safeModule,
                               p_safeMessage,
                               APP_DEBUG_CONSOLE_EOL);
    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);

    if ((uint32_t)formattedLength >= (uint32_t)sizeof(txBuffer))
    {
        txBuffer[sizeof(txBuffer) - 3u] = '.';
        txBuffer[sizeof(txBuffer) - 2u] = '\r';
        txBuffer[sizeof(txBuffer) - 1u] = '\n';
        return App_DebugConsoleWrite((const uint8_t *)txBuffer, (uint16_t)sizeof(txBuffer));
    }

    return App_DebugConsoleWrite((const uint8_t *)txBuffer, (uint16_t)formattedLength);
}

AppStatus_t App_LogPrintf(AppLogLevel_t level, const char *p_module, const char *p_format, ...)
{
    char messageBuffer[APP_LOG_BUFFER_SIZE];
    int32_t formattedLength;
    va_list args;

    APP_RETURN_IF_FALSE((p_format != NULL), APP_STATUS_INVALID_PARAM);

    va_start(args, p_format);
    formattedLength = vsnprintf(messageBuffer, sizeof(messageBuffer), p_format, args);
    va_end(args);

    APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);

    return App_LogWrite(level, p_module, messageBuffer);
}

AppStatus_t App_LogHexDump(AppLogLevel_t level, const char *p_module, const uint8_t *p_data, uint16_t length)
{
    uint16_t offset;

    APP_RETURN_IF_FALSE((p_data != NULL), APP_STATUS_INVALID_PARAM);

    for (offset = 0u; offset < length; offset = (uint16_t)(offset + APP_LOG_HEXDUMP_BYTES_PER_LINE))
    {
        char lineBuffer[APP_LOG_BUFFER_SIZE];
        uint16_t lineIndex;
        uint16_t remaining;
        int32_t formattedLength;

        remaining = (uint16_t)(length - offset);
        if (remaining > APP_LOG_HEXDUMP_BYTES_PER_LINE)
        {
            remaining = APP_LOG_HEXDUMP_BYTES_PER_LINE;
        }

        formattedLength = snprintf(lineBuffer, sizeof(lineBuffer), "%04X :", (unsigned int)offset);
        APP_RETURN_IF_FALSE((formattedLength >= 0), APP_STATUS_INIT_FAILED);

        for (lineIndex = 0u; lineIndex < remaining; lineIndex++)
        {
            int32_t nextLength;

            nextLength = snprintf(&lineBuffer[strlen(lineBuffer)],
                                  sizeof(lineBuffer) - strlen(lineBuffer),
                                  " %02X",
                                  p_data[offset + lineIndex]);
            APP_RETURN_IF_FALSE((nextLength >= 0), APP_STATUS_INIT_FAILED);
        }

        APP_RETURN_IF_FALSE(App_LogWrite(level, p_module, lineBuffer) == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    }

    return APP_STATUS_OK;
}

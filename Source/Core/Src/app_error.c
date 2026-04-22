#include "app_error.h"

/**
 * @file    app_error.c
 * @brief   Error storage and fatal trap implementation.
 */

/** @brief Global last error record. */
static AppErrorRecord_t g_appLastError =
{
    APP_STATUS_OK,
    "none",
    0u
};

void App_ErrorInit(void)
{
    g_appLastError.code = APP_STATUS_OK;
    g_appLastError.file = "none";
    g_appLastError.line = 0u;
}

void App_ErrorRecord(AppStatus_t code, const char *file, uint32_t line)
{
    g_appLastError.code = code;
    g_appLastError.file = file;
    g_appLastError.line = line;
}

const AppErrorRecord_t *App_ErrorGetLast(void)
{
    return &g_appLastError;
}

void App_ErrorTrap(void)
{
    __disable_irq();

    while (1)
    {
        __NOP();
    }
}

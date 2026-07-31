#include "app_debug.h"

#include <stdio.h>
#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_dualboot.h"
#include "app_fsm.h"
#include "app_hw.h"
#include "app_msgq.h"
#include "app_system.h"
#include "app_selftest.h"
#include "app_meter_storage.h"
#include "app_meter_server_format.h"
#include "app_aux.h"
#include "app_nbiot.h"
#include "app_nfc_seoul_format.h"

static AppDebugConsoleContext_t g_appDebugConsoleContext;

AppStatus_t App_DebugConsoleInit(void)
{
    (void)memset(&g_appDebugConsoleContext, 0, sizeof(g_appDebugConsoleContext));
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
    g_appDebugConsoleContext.echoEnabled = APP_FALSE;
    g_appDebugConsoleContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_DebugConsoleProcess(void)
{
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
    return APP_STATUS_OK;
}


void App_DebugConsoleOnUartRxCompleteIsr(UART_HandleTypeDef *p_huart)
{
    (void)p_huart;
}

void App_DebugConsoleOnUartErrorIsr(UART_HandleTypeDef *p_huart)
{
    (void)p_huart;
}

const AppDebugConsoleContext_t *App_DebugConsoleGetContext(void)
{
    return &g_appDebugConsoleContext;
}

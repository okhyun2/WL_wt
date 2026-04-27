#ifndef APP_DEBUG_H
#define APP_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_build_config.h"
#include "app_error.h"

/**
 * @file    app_debug.h
 * @brief   UART1-based debug console interface.
 */

/**
 * @brief Debug console runtime context.
 */
typedef struct
{
    uint8_t initialized;
    uint8_t echoEnabled;
    uint8_t ignoreLineFeed;
    uint16_t rxLength;
    uint32_t commandCount;
    uint32_t unknownCommandCount;
    char rxLine[APP_DEBUG_CONSOLE_RX_LINE_SIZE];
} AppDebugConsoleContext_t;

/**
 * @brief Initialize the debug console over USART1.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_DebugConsoleInit(void);

/**
 * @brief Process pending UART RX characters and execute CLI commands.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_DebugConsoleProcess(void);

/**
 * @brief Send raw bytes to debug console.
 *
 * @param p_data Byte array pointer.
 * @param length Number of bytes to transmit.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_DebugConsoleWrite(const uint8_t *p_data, uint16_t length);

/**
 * @brief Send a zero-terminated string to the debug console.
 *
 * @param p_text String pointer.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_DebugConsoleWriteString(const char *p_text);

/**
 * @brief Print the console prompt.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_DebugConsolePrintPrompt(void);

/**
 * @brief Get immutable debug console context.
 *
 * @return Pointer to internal context.
 */
const AppDebugConsoleContext_t *App_DebugConsoleGetContext(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DEBUG_H */

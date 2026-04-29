#ifndef APP_LOG_H
#define APP_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_log.h
 * @brief   UART1-based logging interface.
 */

/**
 * @brief Logging severity level.
 */
typedef enum
{
    APP_LOG_LEVEL_TRACE = 0,
    APP_LOG_LEVEL_DEBUG,
    APP_LOG_LEVEL_INFO,
    APP_LOG_LEVEL_WARN,
    APP_LOG_LEVEL_ERROR,
    APP_LOG_LEVEL_NONE
} AppLogLevel_t;

/**
 * define print log level. print >= defined_level
 */ 
#define APP_LOG_LEVEL (APP_LOG_LEVEL_INFO)

/**
 * @brief Logging runtime context.
 */
typedef struct
{
    uint8_t initialized;
    AppLogLevel_t minimumLevel;
} AppLogContext_t;

/**
 * @brief Initialize the logging system.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_LogInit(void);

/**
 * @brief Set minimum log output level.
 *
 * @param level New minimum level.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_LogSetLevel(AppLogLevel_t level);

/**
 * @brief Get immutable logging context.
 *
 * @return Pointer to internal context.
 */
const AppLogContext_t *App_LogGetContext(void);

/**
 * @brief Write plain text log entry.
 *
 * @param level Log level.
 * @param p_module Module short name.
 * @param p_message Zero-terminated message string.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_LogWrite(AppLogLevel_t level, const char *p_module, const char *p_message);

/**
 * @brief Write formatted log entry.
 *
 * @param level Log level.
 * @param p_module Module short name.
 * @param p_format printf-style format string.
 * @param ... Format arguments.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_LogPrintf(AppLogLevel_t level, const char *p_module, const char *p_format, ...);

/**
 * @brief Write hex dump log entries.
 *
 * @param level Log level.
 * @param p_module Module short name.
 * @param p_data Data pointer.
 * @param length Data length.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_LogHexDump(AppLogLevel_t level, const char *p_module, const uint8_t *p_data, uint16_t length);

#define APP_LOGT(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_TRACE, (module), (fmt), ##__VA_ARGS__)
#define APP_LOGD(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_DEBUG, (module), (fmt), ##__VA_ARGS__)
#define APP_LOGI(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_INFO,  (module), (fmt), ##__VA_ARGS__)
#define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
#define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */

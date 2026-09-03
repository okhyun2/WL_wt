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
    APP_LOG_LEVEL_NOTICE,
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

#if (APP_BUILD_IS_PRODUCTION == APP_TRUE)
 #if APP_LOG_LEVEL == (APP_LOG_LEVEL_TRACE)
    #define APP_LOGT(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_TRACE, (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGD(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_DEBUG, (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGI(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_INFO,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGN(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_NOTICE,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
 #elif APP_LOG_LEVEL == (APP_LOG_LEVEL_DEBUG)
    #define APP_LOGT(module, fmt, ...)
    #define APP_LOGD(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_DEBUG, (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGI(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_INFO,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGN(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_NOTICE,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
 #elif APP_LOG_LEVEL == (APP_LOG_LEVEL_INFO)
    #define APP_LOGT(module, fmt, ...)
    #define APP_LOGD(module, fmt, ...)
    #define APP_LOGI(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_INFO,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGN(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_NOTICE,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
 #elif APP_LOG_LEVEL == (APP_LOG_LEVEL_NOTICE)
    #define APP_LOGT(module, fmt, ...)
    #define APP_LOGD(module, fmt, ...)
    #define APP_LOGI(module, fmt, ...)    
    #define APP_LOGN(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_NOTICE,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
 #elif APP_LOG_LEVEL == (APP_LOG_LEVEL_WARN)
    #define APP_LOGT(module, fmt, ...)
    #define APP_LOGD(module, fmt, ...)
    #define APP_LOGI(module, fmt, ...)
    #define APP_LOGN(module, fmt, ...)
    #define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
    #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
 #elif APP_LOG_LEVEL == (APP_LOG_LEVEL_ERROR)
    #define APP_LOGT(module, fmt, ...)
    #define APP_LOGD(module, fmt, ...)
    #define APP_LOGI(module, fmt, ...)
    #define APP_LOGN(module, fmt, ...)
    #define APP_LOGW(module, fmt, ...)
    #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
 #elif APP_LOG_LEVEL == (APP_LOG_LEVEL_NONE)
    #define APP_LOGT(module, fmt, ...)
    #define APP_LOGD(module, fmt, ...)
    #define APP_LOGI(module, fmt, ...)
    #define APP_LOGN(module, fmt, ...)
    #define APP_LOGW(module, fmt, ...)
    #define APP_LOGE(module, fmt, ...)
 #else
    #error "unknown log print level";
 #endif
#else
 #define APP_LOGT(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_TRACE, (module), (fmt), ##__VA_ARGS__)
 #define APP_LOGD(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_DEBUG, (module), (fmt), ##__VA_ARGS__)
 #define APP_LOGI(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_INFO,  (module), (fmt), ##__VA_ARGS__)
 #define APP_LOGN(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_NOTICE,  (module), (fmt), ##__VA_ARGS__)
 #define APP_LOGW(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_WARN,  (module), (fmt), ##__VA_ARGS__)
 #define APP_LOGE(module, fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_ERROR, (module), (fmt), ##__VA_ARGS__)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * @brief EPC 시험 전용 로그 매크로 (APP_LOGI 바로 다음에 위치).
 *        APP_EPC_TEST_MODE_ENABLE == APP_FALSE 이면 이 매크로 자체가
 *        정의되지 않는다. 호출부도 반드시 동일 조건으로 감싸야 하며,
 *        감싸지 않으면 빌드 시점에 "정의되지 않은 심볼" 에러로 즉시 검출된다.
 */
#if (APP_EPC_TEST_MODE_ENABLE == APP_TRUE)
    #define EPC_LOGI(fmt, ...)    App_LogPrintf(APP_LOG_LEVEL_INFO, APP_EPC_LOG_TAG, (fmt), ##__VA_ARGS__)
#endif
///////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* APP_LOG_H */

#ifndef APP_ERROR_H
#define APP_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @file    app_error.h
 * @brief   Common application status codes and fatal error handling.
 */

typedef enum
{
    APP_STATUS_OK = 0,
    APP_STATUS_INVALID_PARAM,
    APP_STATUS_NOT_INITIALIZED,
    APP_STATUS_HW_HANDLE_INVALID,
    APP_STATUS_CLOCK_NOT_INITIALIZED,
    APP_STATUS_CLOCK_SOURCE_INVALID,
    APP_STATUS_CLOCK_VERIFY_FAILED,
    APP_STATUS_DEBUG_INIT_FAILED,
    APP_STATUS_LOG_INIT_FAILED,
    APP_STATUS_UART_TX_FAILED,
    APP_STATUS_UART_RX_FAILED,
    APP_STATUS_UART_RX_ERROR,
    APP_STATUS_BUFFER_OVERFLOW,
    APP_STATUS_GPIO_LP_INIT_FAILED,
    APP_STATUS_GPIO_LP_STATE_INVALID,
    APP_STATUS_GPIO_LP_STOP_PREP_FAILED,
    APP_STATUS_GPIO_LP_STOP_RECOVER_FAILED,
    APP_STATUS_SELFTEST_INIT_FAILED,
    APP_STATUS_SELFTEST_FAILED,
    APP_STATUS_SELFTEST_DEVICE_NOT_READY,
    APP_STATUS_SELFTEST_TIMEOUT,
    APP_STATUS_SCHEDULER_NOT_INITIALIZED,
    APP_STATUS_SCHEDULER_INIT_FAILED,
    APP_STATUS_SCHEDULER_TASK_INVALID,
    APP_STATUS_SCHEDULER_TASK_LIMIT_REACHED,
    APP_STATUS_MSGQ_NOT_INITIALIZED,
    APP_STATUS_MSGQ_FULL,
    APP_STATUS_MSGQ_EMPTY,
    APP_STATUS_EVENTPENDING_EMPTY,
    APP_STATUS_INIT_FAILED,
    APP_STATUS_FATAL
} AppStatus_t;

typedef struct
{
    AppStatus_t code;
    const char *file;
    uint32_t line;
} AppErrorRecord_t;

void App_ErrorInit(void);
void App_ErrorRecord(AppStatus_t code, const char *file, uint32_t line);
const AppErrorRecord_t *App_ErrorGetLast(void);
void App_ErrorTrap(void);

#define APP_RETURN_IF_FALSE(expr, err_code)                    \
    do                                                         \
    {                                                          \
        if (!(expr))                                           \
        {                                                      \
            App_ErrorRecord((err_code), __FILE__, __LINE__);   \
            return (err_code);                                 \
        }                                                      \
    } while (0)

#define APP_RETURN_IF_HAL_ERROR(expr, err_code)                \
    do                                                         \
    {                                                          \
        if ((expr) != HAL_OK)                                  \
        {                                                      \
            App_ErrorRecord((err_code), __FILE__, __LINE__);   \
            return (err_code);                                 \
        }                                                      \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* APP_ERROR_H */

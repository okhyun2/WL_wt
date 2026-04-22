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

/**
 * @brief Application status/error codes.
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
    APP_STATUS_INIT_FAILED,
    APP_STATUS_FATAL
} AppStatus_t;

/**
 * @brief Last captured error information.
 */
typedef struct
{
    AppStatus_t code;
    const char *file;
    uint32_t line;
} AppErrorRecord_t;

/**
 * @brief Initialize the application error recorder.
 */
void App_ErrorInit(void);

/**
 * @brief Save the latest error information.
 *
 * @param code Error/status code.
 * @param file Source file name.
 * @param line Source line number.
 */
void App_ErrorRecord(AppStatus_t code, const char *file, uint32_t line);

/**
 * @brief Get the last stored error record.
 *
 * @return Pointer to internal immutable error record.
 */
const AppErrorRecord_t *App_ErrorGetLast(void);

/**
 * @brief Enter non-returning fatal error trap.
 */
void App_ErrorTrap(void);

/**
 * @brief Record error and return when expression is false.
 */
#define APP_RETURN_IF_FALSE(expr, err_code)                    \
    do                                                         \
    {                                                          \
        if (!(expr))                                           \
        {                                                      \
            App_ErrorRecord((err_code), __FILE__, __LINE__);   \
            return (err_code);                                 \
        }                                                      \
    } while (0)

/**
 * @brief Record error and return when HAL API fails.
 */
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

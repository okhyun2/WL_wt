#ifndef APP_SYSTEM_H
#define APP_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_system.h
 * @brief   Application bootstrap and top-level superloop interface.
 */

/**
 * @brief Boot stage indicator.
 */
typedef enum
{
    APP_BOOT_STAGE_RESET = 0,
    APP_BOOT_STAGE_HAL_READY,
    APP_BOOT_STAGE_CLOCK_READY,
    APP_BOOT_STAGE_PERIPH_READY,
    APP_BOOT_STAGE_DEBUG_READY,
    APP_BOOT_STAGE_LOG_READY,
    APP_BOOT_STAGE_APP_READY
} AppBootStage_t;

/**
 * @brief Global runtime context for the application.
 */
typedef struct
{
    uint8_t initialized;
    uint8_t debugReady;
    uint8_t logReady;
    AppBootStage_t bootStage;
    uint32_t bootSysClockHz;
    uint32_t loopCounter;
} AppSystemContext_t;

/**
 * @brief Initialize application software layer after CubeMX peripheral init.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_SystemInit(void);

/**
 * @brief Execute one application superloop cycle.
 */
void App_SystemProcess(void);

/**
 * @brief Get immutable runtime context.
 *
 * @return Pointer to runtime context.
 */
const AppSystemContext_t *App_SystemGetContext(void);

/**
 * @brief Get firmware version string.
 *
 * @return Version string pointer.
 */
const char *App_SystemGetVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_H */

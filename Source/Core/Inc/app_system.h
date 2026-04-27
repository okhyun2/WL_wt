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

typedef enum
{
    APP_BOOT_STAGE_RESET = 0,
    APP_BOOT_STAGE_HAL_READY,
    APP_BOOT_STAGE_CLOCK_READY,
    APP_BOOT_STAGE_PERIPH_READY,
    APP_BOOT_STAGE_GPIO_LP_READY,
    APP_BOOT_STAGE_DEBUG_READY,
    APP_BOOT_STAGE_LOG_READY,
    APP_BOOT_STAGE_SELFTEST_DONE,
    APP_BOOT_STAGE_SCHEDULER_READY,
    APP_BOOT_STAGE_APP_READY
} AppBootStage_t;

typedef struct
{
    uint8_t initialized;
    uint8_t debugReady;
    uint8_t logReady;
    uint8_t selfTestCompleted;
    uint8_t selfTestFailed;
    uint8_t schedulerReady;
    AppBootStage_t bootStage;
    AppStatus_t selfTestStatus;
    AppStatus_t schedulerStatus;
    uint32_t bootSysClockHz;
    uint32_t loopCounter;
    uint32_t idleCounter;
} AppSystemContext_t;

AppStatus_t App_SystemInit(void);
void App_SystemProcess(void);
AppStatus_t App_SystemOnBeforeStopEnter(void);
AppStatus_t App_SystemOnAfterStopExit(void);
AppStatus_t App_SystemPrepareForStop(void);
AppStatus_t App_SystemRecoverFromStop(void);
AppStatus_t App_SystemSetNbiotPowered(uint8_t powered);
const AppSystemContext_t *App_SystemGetContext(void);
const char *App_SystemGetVersionString(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_H */

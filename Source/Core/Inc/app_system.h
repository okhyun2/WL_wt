#ifndef APP_SYSTEM_H
#define APP_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_system.h
 * @brief   Application bootstrap, low-power control, and top-level superloop interface.
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

typedef enum
{
    APP_SYSTEM_LP_MODE_RUN = 0,
    APP_SYSTEM_LP_MODE_SLEEP,
    APP_SYSTEM_LP_MODE_STOP
} AppSystemLowPowerMode_t;

typedef enum
{
    APP_SYSTEM_WAKE_SRC_NONE     = 0x00000000u,
    APP_SYSTEM_WAKE_SRC_NBIOT_RI = 0x00000001u,
    APP_SYSTEM_WAKE_SRC_NFC_ED   = 0x00000002u,
    APP_SYSTEM_WAKE_SRC_REED     = 0x00000004u,
    APP_SYSTEM_WAKE_SRC_ESI_INT  = 0x00000008u,
    APP_SYSTEM_WAKE_SRC_RTC      = 0x00000010u,
    APP_SYSTEM_WAKE_SRC_UNKNOWN  = 0x80000000u
} AppSystemWakeSource_t;

typedef struct
{
    uint8_t initialized;
    uint8_t debugReady;
    uint8_t logReady;
    uint8_t selfTestCompleted;
    uint8_t selfTestFailed;
    uint8_t schedulerReady;
    uint8_t stopRequested;
    AppBootStage_t bootStage;
    AppStatus_t selfTestStatus;
    AppStatus_t schedulerStatus;
    uint32_t bootSysClockHz;
    uint32_t loopCounter;
    uint32_t idleCounter;
    uint32_t sleepEntryCount;
    uint32_t stopEntryCount;
    uint32_t wakeSourceMask;
    uint32_t lastWakeTickMs;
    uint32_t lastSleepEntryTickMs;
    uint32_t lastStopEntryTickMs;
    AppSystemLowPowerMode_t lastLowPowerMode;
} AppSystemContext_t;

AppStatus_t App_SystemInit(void);
void App_SystemProcess(void);
AppStatus_t App_SystemOnBeforeStopEnter(void);
AppStatus_t App_SystemOnAfterStopExit(void);
AppStatus_t App_SystemPrepareForStop(void);
AppStatus_t App_SystemRecoverFromStop(void);
AppStatus_t App_SystemSetNbiotPowered(uint8_t powered);
AppStatus_t App_SystemRequestLowPower(uint8_t allowStop);
void App_SystemNotifyWakeSource(uint32_t sourceMask);
uint32_t App_SystemGetWakeSourceMask(void);
const AppSystemContext_t *App_SystemGetContext(void);
const char *App_SystemGetVersionString(void);
const char *App_SystemGetWakeSourceString(void);
const char *App_SystemGetLowPowerModeString(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SYSTEM_H */

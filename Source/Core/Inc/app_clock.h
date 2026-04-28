#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_clock.h
 * @brief   Boot-time clock validation and runtime clock context.
 */

/**
 * @brief Supported system clock source type.
 */
typedef enum
{
    APP_CLOCK_SOURCE_UNKNOWN = 0,
    APP_CLOCK_SOURCE_MSI,
    APP_CLOCK_SOURCE_HSI16,
    APP_CLOCK_SOURCE_HSE,
    APP_CLOCK_SOURCE_PLL
} AppClockSource_t;

/**
 * @brief Captured clock tree information.
 */
typedef struct
{
    uint8_t initialized;
    uint8_t lseReady;
    uint32_t msiRange;
    uint32_t flashLatency;
    uint32_t sysclkHz;
    uint32_t hclkHz;
    uint32_t pclk1Hz;
    uint32_t pclk2Hz;
    AppClockSource_t sysclkSource;
} AppClockContext_t;

/**
 * @brief Validate SystemClock_Config() result and capture clock context.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_ClockInit(void);

/**
 * @brief Restore boot clock tree after STOP wake-up and refresh clock context.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_ClockRecoverAfterStop(void);

/**
 * @brief Check whether clock context was initialized.
 *
 * @return APP_TRUE when initialized, otherwise APP_FALSE.
 */
uint8_t App_ClockIsInitialized(void);

/**
 * @brief Get immutable clock context.
 *
 * @return Pointer to internal clock context.
 */
const AppClockContext_t *App_ClockGetContext(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOCK_H */

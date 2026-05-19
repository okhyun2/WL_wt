#ifndef APP_GPIO_LP_H
#define APP_GPIO_LP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"
#include "main.h"

/**
 * @file    app_gpio_lp.h
 * @brief   Board-specific low-power GPIO and peripheral clock policy.
 */

/**
 * @brief SWD policy selection.
 */
typedef enum
{
    APP_GPIO_LP_SWD_KEEP = 0,
    APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION
} AppGpioLpSwdPolicy_t;

/**
 * @brief Peripheral clock mask for stop preparation.
 */
typedef enum
{
    APP_GPIO_LP_CLK_NONE      = 0x00000000u,
    APP_GPIO_LP_CLK_ADC1      = 0x00000001u,
    APP_GPIO_LP_CLK_CRC       = 0x00000002u,
    APP_GPIO_LP_CLK_USART1    = 0x00000004u,
    APP_GPIO_LP_CLK_USART2    = 0x00000008u,
    APP_GPIO_LP_CLK_LPUART1   = 0x00000010u,
    APP_GPIO_LP_CLK_I2C2      = 0x00000020u,
    APP_GPIO_LP_CLK_LPTIM1    = 0x00000040u,
    APP_GPIO_LP_CLK_SYSCFG    = 0x00000080u
} AppGpioLpClockMask_t;

/**
 * @brief Low-power GPIO policy configuration.
 */
typedef struct
{
    AppGpioLpSwdPolicy_t swdPolicy;
    uint32_t stopClockDisableMask;
} AppGpioLpConfig_t;

/**
 * @brief Runtime context for low-power GPIO handling.
 */
typedef struct
{
    uint8_t initialized;
    uint8_t stopPrepared;
    uint8_t nbiotPowered;
    uint32_t lastDisabledClockMask;
    AppGpioLpConfig_t config;
} AppGpioLpContext_t;

void App_GpioLpConfigAnalogNoPull(GPIO_TypeDef *gpioPort, uint32_t pinMask);
void App_GpioLpConfigOutput(GPIO_TypeDef *gpioPort, uint32_t pinMask, GPIO_PinState pinState);
void App_GpioLpRestoreMeterUartPins(void);

/**
 * @brief Fill default board-specific low-power policy.
 *
 * @param p_config Target configuration structure.
 */
void App_GpioLpGetDefaultConfig(AppGpioLpConfig_t *p_config);

/**
 * @brief Initialize low-power GPIO module.
 *
 * @param p_config Policy configuration pointer.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpInit(const AppGpioLpConfig_t *p_config);

/**
 * @brief Apply one-time run-state base policy for unused pins.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpApplyRunBaseState(void);

/**
 * @brief Update current NB-IoT module power state.
 *
 * @param powered APP_TRUE when module is powered, APP_FALSE otherwise.
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpSetNbiotPowered(uint8_t powered);

/**
 * @brief Apply GPIO/clock policy immediately before STOP entry.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpOnBeforeStopEnter(void);

/**
 * @brief Restore GPIO/clock policy immediately after STOP wake-up.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpOnAfterStopExit(void);

/**
 * @brief Backward-compatible alias for STOP preparation.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpPrepareForStop(void);

/**
 * @brief Backward-compatible alias for STOP recovery.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_GpioLpRecoverFromStop(void);

/**
 * @brief Get immutable low-power GPIO context.
 *
 * @return Pointer to internal context.
 */
const AppGpioLpContext_t *App_GpioLpGetContext(void);

#ifdef DEBUG
void GPIO_DumpAll(void);
#endif //DEBUG

#ifdef __cplusplus
}
#endif

#endif /* APP_GPIO_LP_H */

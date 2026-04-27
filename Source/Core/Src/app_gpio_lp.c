#include "app_gpio_lp.h"

#include <string.h>

#include "app_build_config.h"

/**
 * @file    app_gpio_lp.c
 * @brief   Board-specific low-power GPIO and peripheral clock policy.
 */

/** @brief Truly unconnected pins on GPIOA. */
#define APP_GPIO_LP_UNUSED_PORTA_MASK \
    (GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_12)

/** @brief Truly unconnected pins on GPIOB. */
#define APP_GPIO_LP_UNUSED_PORTB_MASK \
    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | \
     GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/** @brief Truly unconnected pins on GPIOC. */
#define APP_GPIO_LP_UNUSED_PORTC_MASK \
    (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | \
     GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13)

/** @brief Truly unconnected pins on GPIOD. */
#define APP_GPIO_LP_UNUSED_PORTD_MASK \
    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | \
     GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | \
     GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/** @brief Alternate-function pin speed when restoring communications. */
#define APP_GPIO_LP_AF_SPEED_HIGH      GPIO_SPEED_FREQ_VERY_HIGH
/** @brief Push-pull output speed for static control pins. */
#define APP_GPIO_LP_OUT_SPEED_LOW      GPIO_SPEED_FREQ_LOW

/** @brief Runtime context instance. */
static AppGpioLpContext_t g_appGpioLpContext;

/**
 * @brief Enable all GPIO clocks used by board-level policy.
 */
static void App_GpioLpEnablePortClocks(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
}

/**
 * @brief Configure selected pins as analog/no-pull.
 *
 * @param gpioPort GPIO port.
 * @param pinMask Pin mask.
 */
static void App_GpioLpConfigAnalogNoPull(GPIO_TypeDef *gpioPort, uint32_t pinMask)
{
    GPIO_InitTypeDef gpioInit;

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = pinMask;
    gpioInit.Mode = GPIO_MODE_ANALOG;
    gpioInit.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(gpioPort, &gpioInit);
}

/**
 * @brief Configure selected pins as push-pull outputs.
 *
 * @param gpioPort GPIO port.
 * @param pinMask Pin mask.
 * @param pullMode GPIO pull mode.
 * @param pinState Initial level.
 */
static void App_GpioLpConfigOutput(GPIO_TypeDef *gpioPort,
                                   uint32_t pinMask,
                                   uint32_t pullMode,
                                   GPIO_PinState pinState)
{
    GPIO_InitTypeDef gpioInit;

    HAL_GPIO_WritePin(gpioPort, pinMask, pinState);

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = pinMask;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = pullMode;
    gpioInit.Speed = APP_GPIO_LP_OUT_SPEED_LOW;

    HAL_GPIO_Init(gpioPort, &gpioInit);
}

/**
 * @brief Configure selected pins as rising-edge EXTI inputs.
 *
 * @param gpioPort GPIO port.
 * @param pinMask Pin mask.
 */
static void App_GpioLpConfigExtiRising(GPIO_TypeDef *gpioPort, uint32_t pinMask)
{
    GPIO_InitTypeDef gpioInit;

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = pinMask;
    gpioInit.Mode = GPIO_MODE_IT_RISING;
    gpioInit.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(gpioPort, &gpioInit);
}

/**
 * @brief Apply SWD pin policy.
 */
static void App_GpioLpApplySwdPolicy(void)
{
    if (g_appGpioLpContext.config.swdPolicy == APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION)
    {
        App_GpioLpConfigAnalogNoPull(GPIOA, GPIO_PIN_13 | GPIO_PIN_14);
    }
}

/**
 * @brief Apply analog/no-pull to all truly unused board pins.
 */
static void App_GpioLpApplyUnusedPins(void)
{
    App_GpioLpConfigAnalogNoPull(GPIOA, APP_GPIO_LP_UNUSED_PORTA_MASK);
    App_GpioLpConfigAnalogNoPull(GPIOB, APP_GPIO_LP_UNUSED_PORTB_MASK);
    App_GpioLpConfigAnalogNoPull(GPIOC, APP_GPIO_LP_UNUSED_PORTC_MASK);
    App_GpioLpConfigAnalogNoPull(GPIOD, APP_GPIO_LP_UNUSED_PORTD_MASK);
}

/**
 * @brief Keep static control pins in their safe low-power run/stop state.
 */
static void App_GpioLpApplyStaticControlPins(void)
{
    App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PULLUP, GPIO_PIN_RESET);
    App_GpioLpConfigOutput(Charge_BOOT0_GPIO_Port, Charge_BOOT0_Pin, GPIO_PULLUP, GPIO_PIN_RESET);
}

/**
 * @brief Restore wake/event input pins that must remain active.
 */
static void App_GpioLpRestoreWakeInputs(void)
{
    App_GpioLpConfigExtiRising(NFC_ED_GPIO_Port, NFC_ED_Pin);
    App_GpioLpConfigExtiRising(REED_IN_GPIO_Port, REED_IN_Pin);
    App_GpioLpConfigExtiRising(ESI_Int_GPIO_Port, ESI_Int_Pin);
}

/**
 * @brief Isolate NB-IoT interface when the module is powered off.
 *
 * @note Prevents back-powering through UART/RI/RST lines.
 */
static void App_GpioLpIsolateNbiotInterface(void)
{
    App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PULLUP, GPIO_PIN_RESET);
    App_GpioLpConfigAnalogNoPull(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin);
    App_GpioLpConfigAnalogNoPull(NBIoT_RX_GPIO_Port, NBIoT_RX_Pin | NBIoT_TX_Pin);
    App_GpioLpConfigAnalogNoPull(NBIoT_RI_GPIO_Port, NBIoT_RI_Pin);
}

/**
 * @brief Restore NB-IoT control and interface pins after wake.
 */
static void App_GpioLpRestoreNbiotInterface(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_LPUART1_CLK_ENABLE();

    App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PULLUP, GPIO_PIN_RESET);
    App_GpioLpConfigOutput(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin, GPIO_PULLUP, GPIO_PIN_SET);

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = NBIoT_RX_Pin | NBIoT_TX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF6_LPUART1;
    HAL_GPIO_Init(NBIoT_RX_GPIO_Port, &gpioInit);

    if (g_appGpioLpContext.config.keepNbiotRiWakeWhenPowered == APP_TRUE)
    {
        App_GpioLpConfigExtiRising(NBIoT_RI_GPIO_Port, NBIoT_RI_Pin);
    }
    else
    {
        App_GpioLpConfigAnalogNoPull(NBIoT_RI_GPIO_Port, NBIoT_RI_Pin);
    }
}

/**
 * @brief Restore debug UART pins.
 */
static void App_GpioLpRestoreDebugUartPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Debug_TX_Pin | Debug_RX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF4_USART1;
    HAL_GPIO_Init(GPIOA, &gpioInit);
}

/**
 * @brief Restore meter UART pins.
 */
static void App_GpioLpRestoreMeterUartPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Meter_TX_Pin | Meter_RX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF4_USART2;
    HAL_GPIO_Init(GPIOA, &gpioInit);
}

/**
 * @brief Restore ESI I2C pins.
 */
static void App_GpioLpRestoreEsiI2cPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = ESI_SCL_Pin | ESI_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF1_I2C1;
    HAL_GPIO_Init(GPIOB, &gpioInit);
}

/**
 * @brief Restore NFC I2C pins.
 */
static void App_GpioLpRestoreNfcI2cPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C2_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = NFC_SCL_Pin | NFC_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF6_I2C2;
    HAL_GPIO_Init(GPIOB, &gpioInit);
}

/**
 * @brief Restore temperature/auxiliary I2C pins.
 */
static void App_GpioLpRestoreTempI2cPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_I2C3_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Temp_SCL_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF7_I2C3;
    HAL_GPIO_Init(Temp_SCL_GPIO_Port, &gpioInit);

    gpioInit.Pin = Temp_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = APP_GPIO_LP_AF_SPEED_HIGH;
    gpioInit.Alternate = GPIO_AF7_I2C3;
    HAL_GPIO_Init(Temp_SDA_GPIO_Port, &gpioInit);
}

/**
 * @brief Restore piezo PWM pin.
 */
static void App_GpioLpRestorePiezoPin(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Piezo_PWM_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(Piezo_PWM_GPIO_Port, &gpioInit);
}

/**
 * @brief Restore external watchdog feed pin.
 */
static void App_GpioLpRestoreExternalWatchdogPin(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM22_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = WD_FEED_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF4_TIM22;
    HAL_GPIO_Init(WD_FEED_GPIO_Port, &gpioInit);
}

/**
 * @brief Disable selected peripheral clocks.
 *
 * @param mask Peripheral clock mask.
 */
static void App_GpioLpDisablePeripheralClocks(uint32_t mask)
{
    if ((mask & APP_GPIO_LP_CLK_ADC1) != 0u)
    {
        __HAL_RCC_ADC1_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_CRC) != 0u)
    {
        __HAL_RCC_CRC_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_TIM3) != 0u)
    {
        __HAL_RCC_TIM3_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_TIM22) != 0u)
    {
        __HAL_RCC_TIM22_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_USART1) != 0u)
    {
        __HAL_RCC_USART1_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_USART2) != 0u)
    {
        __HAL_RCC_USART2_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_LPUART1) != 0u)
    {
        __HAL_RCC_LPUART1_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_I2C1) != 0u)
    {
        __HAL_RCC_I2C1_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_I2C2) != 0u)
    {
        __HAL_RCC_I2C2_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_I2C3) != 0u)
    {
        __HAL_RCC_I2C3_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_SYSCFG) != 0u)
    {
        __HAL_RCC_SYSCFG_CLK_DISABLE();
    }
}

void App_GpioLpGetDefaultConfig(AppGpioLpConfig_t *p_config)
{
    if (p_config == NULL)
    {
        return;
    }

    (void)memset(p_config, 0, sizeof(*p_config));

    p_config->swdPolicy = APP_GPIO_LP_SWD_KEEP;
    p_config->keepDebugUartPinsInStop = APP_FALSE;
    p_config->keepMeterUartPinsInStop = APP_FALSE;
    p_config->keepEsiI2cPinsInStop = APP_FALSE;
    p_config->keepNfcI2cPinsInStop = APP_FALSE;
    p_config->keepTempI2cPinsInStop = APP_FALSE;
    p_config->keepPiezoPinInStop = APP_FALSE;
    p_config->keepExternalWatchdogPinInStop = APP_TRUE;
    p_config->keepNbiotRiWakeWhenPowered = APP_FALSE;
    p_config->isolateNbiotInterfaceWhenPoweredOff = APP_TRUE;
    p_config->restoreNbiotInterfaceAfterWake = APP_TRUE;
    p_config->stopClockDisableMask =
        APP_GPIO_LP_CLK_ADC1 |
        APP_GPIO_LP_CLK_CRC |
        APP_GPIO_LP_CLK_TIM3 |
        APP_GPIO_LP_CLK_USART1 |
        APP_GPIO_LP_CLK_USART2 |
        APP_GPIO_LP_CLK_LPUART1 |
        APP_GPIO_LP_CLK_I2C1 |
        APP_GPIO_LP_CLK_I2C2 |
        APP_GPIO_LP_CLK_I2C3;
}

AppStatus_t App_GpioLpInit(const AppGpioLpConfig_t *p_config)
{
    APP_RETURN_IF_FALSE((p_config != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(&g_appGpioLpContext, 0, sizeof(g_appGpioLpContext));
    g_appGpioLpContext.config = *p_config;
    g_appGpioLpContext.nbiotPowered = APP_FALSE;
    g_appGpioLpContext.initialized = APP_TRUE;

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpApplyRunBaseState(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == APP_TRUE), APP_STATUS_NOT_INITIALIZED);

    App_GpioLpEnablePortClocks();
    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();
    App_GpioLpApplySwdPolicy();

    if ((g_appGpioLpContext.nbiotPowered == APP_FALSE) &&
        (g_appGpioLpContext.config.isolateNbiotInterfaceWhenPoweredOff == APP_TRUE))
    {
        App_GpioLpIsolateNbiotInterface();
    }

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpSetNbiotPowered(uint8_t powered)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == APP_TRUE), APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(((powered == APP_TRUE) || (powered == APP_FALSE)), APP_STATUS_INVALID_PARAM);

    g_appGpioLpContext.nbiotPowered = powered;

    App_GpioLpEnablePortClocks();

    if ((powered == APP_FALSE) &&
        (g_appGpioLpContext.config.isolateNbiotInterfaceWhenPoweredOff == APP_TRUE))
    {
        App_GpioLpIsolateNbiotInterface();
    }
    else if ((powered == APP_TRUE) &&
             (g_appGpioLpContext.config.restoreNbiotInterfaceAfterWake == APP_TRUE))
    {
        App_GpioLpRestoreNbiotInterface();
    }

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpOnBeforeStopEnter(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == APP_TRUE), APP_STATUS_NOT_INITIALIZED);

    App_GpioLpEnablePortClocks();

    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();
    App_GpioLpApplySwdPolicy();

    if (g_appGpioLpContext.config.keepDebugUartPinsInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(Debug_TX_GPIO_Port, Debug_TX_Pin | Debug_RX_Pin);
    }

    if (g_appGpioLpContext.config.keepMeterUartPinsInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(Meter_TX_GPIO_Port, Meter_TX_Pin | Meter_RX_Pin);
    }

    if (g_appGpioLpContext.config.keepEsiI2cPinsInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(ESI_SCL_GPIO_Port, ESI_SCL_Pin | ESI_SDA_Pin);
    }

    if (g_appGpioLpContext.config.keepNfcI2cPinsInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(NFC_SCL_GPIO_Port, NFC_SCL_Pin | NFC_SDA_Pin);
    }

    if (g_appGpioLpContext.config.keepTempI2cPinsInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(Temp_SCL_GPIO_Port, Temp_SCL_Pin);
        App_GpioLpConfigAnalogNoPull(Temp_SDA_GPIO_Port, Temp_SDA_Pin);
    }

    if (g_appGpioLpContext.config.keepPiezoPinInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin);
    }

    if (g_appGpioLpContext.config.keepExternalWatchdogPinInStop != APP_TRUE)
    {
        App_GpioLpConfigAnalogNoPull(WD_FEED_GPIO_Port, WD_FEED_Pin);
    }

    if ((g_appGpioLpContext.nbiotPowered == APP_FALSE) &&
        (g_appGpioLpContext.config.isolateNbiotInterfaceWhenPoweredOff == APP_TRUE))
    {
        App_GpioLpIsolateNbiotInterface();
    }
    else if (g_appGpioLpContext.config.keepNbiotRiWakeWhenPowered == APP_TRUE)
    {
        App_GpioLpConfigExtiRising(NBIoT_RI_GPIO_Port, NBIoT_RI_Pin);
    }

    App_GpioLpDisablePeripheralClocks(g_appGpioLpContext.config.stopClockDisableMask);

    g_appGpioLpContext.stopPrepared = APP_TRUE;

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpOnAfterStopExit(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == APP_TRUE), APP_STATUS_NOT_INITIALIZED);

    App_GpioLpEnablePortClocks();

    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();

    if (g_appGpioLpContext.config.keepDebugUartPinsInStop != APP_TRUE)
    {
        App_GpioLpRestoreDebugUartPins();
    }

    if (g_appGpioLpContext.config.keepMeterUartPinsInStop != APP_TRUE)
    {
        App_GpioLpRestoreMeterUartPins();
    }

    if (g_appGpioLpContext.config.keepEsiI2cPinsInStop != APP_TRUE)
    {
        App_GpioLpRestoreEsiI2cPins();
    }

    if (g_appGpioLpContext.config.keepNfcI2cPinsInStop != APP_TRUE)
    {
        App_GpioLpRestoreNfcI2cPins();
    }

    if (g_appGpioLpContext.config.keepTempI2cPinsInStop != APP_TRUE)
    {
        App_GpioLpRestoreTempI2cPins();
    }

    if (g_appGpioLpContext.config.keepPiezoPinInStop != APP_TRUE)
    {
        App_GpioLpRestorePiezoPin();
    }

    if (g_appGpioLpContext.config.keepExternalWatchdogPinInStop != APP_TRUE)
    {
        App_GpioLpRestoreExternalWatchdogPin();
    }

    if ((g_appGpioLpContext.nbiotPowered == APP_FALSE) &&
        (g_appGpioLpContext.config.isolateNbiotInterfaceWhenPoweredOff == APP_TRUE))
    {
        App_GpioLpIsolateNbiotInterface();
    }
    else if (g_appGpioLpContext.config.restoreNbiotInterfaceAfterWake == APP_TRUE)
    {
        App_GpioLpRestoreNbiotInterface();
    }

    App_GpioLpApplySwdPolicy();

    g_appGpioLpContext.stopPrepared = APP_FALSE;

    return APP_STATUS_OK;
}

const AppGpioLpContext_t *App_GpioLpGetContext(void)
{
    return &g_appGpioLpContext;
}

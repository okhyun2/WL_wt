#include "app_gpio_lp.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_log.h"

/**
 * @file    app_gpio_lp.c
 * @brief   Board-specific low-power GPIO and peripheral clock policy.
 */

/** @brief Truly unconnected pins on GPIOA based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTA_MASK \
    (GPIO_PIN_0 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_8 | GPIO_PIN_11 | GPIO_PIN_12)

/** @brief Truly unconnected pins on GPIOB based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTB_MASK \
    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | \
     GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/** @brief Truly unconnected pins on GPIOC based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTC_MASK \
    (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | \
     GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13)

/** @brief Truly unconnected pins on GPIOD based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTD_MASK \
    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | \
     GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | \
     GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/** @brief Runtime context instance. */
static AppGpioLpContext_t g_appGpioLpContext;

#ifdef DEBUG
static uint8_t App_GpioLpCanDebugLog(void)
{
    const AppLogContext_t *p_logContext;

    p_logContext = App_LogGetContext();
    return ((p_logContext != NULL) && (p_logContext->initialized == 1u)) ? 1u : 0u;
}
#endif

/**
 * @brief Enable all GPIO port clocks used by the board.
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
 * @param pinState Initial output state.
 */
static void App_GpioLpConfigOutput(GPIO_TypeDef *gpioPort,
                                   uint32_t pinMask,
                                   GPIO_PinState pinState)
{
    GPIO_InitTypeDef gpioInit;

    HAL_GPIO_WritePin(gpioPort, pinMask, pinState);

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = pinMask;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(gpioPort, &gpioInit);
}

/**
 * @brief Configure selected pins as rising-edge wake/event inputs.
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
 * @brief Configure selected pins as rising-edge wake/event inputs.
 *
 * @param gpioPort GPIO port.
 * @param pinMask Pin mask.
 */
static void App_GpioLpConfigExtiFalling(GPIO_TypeDef *gpioPort, uint32_t pinMask)
{
    GPIO_InitTypeDef gpioInit;

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = pinMask;
    gpioInit.Mode = GPIO_MODE_IT_FALLING;
    gpioInit.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(gpioPort, &gpioInit);
}


/**
 * @brief Restore debug UART pins.
 */
static void App_GpioLpRestoreDebugUartPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_USART1_CLK_ENABLE();

    /* 모든 에러 플래그 및 RX 버퍼 클리어 */
    __HAL_UART_CLEAR_FLAG(APP_UART_DEBUG_HANDLE, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_DEBUG_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Debug_TX_Pin | Debug_RX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF4_USART1;

    HAL_GPIO_Init(GPIOA, &gpioInit);

    /* USART1 수신(RX) 재활성화 */
    USART1->CR1 |= USART_CR1_RE;
}

/**
 * @brief Restore meter UART pins.
 */
static void App_GpioLpRestoreMeterUartPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_USART2_CLK_ENABLE();

    /* 모든 에러 플래그 및 RX 버퍼 클리어 */
    __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Meter_TX_Pin | Meter_RX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF4_USART2;

    HAL_GPIO_Init(GPIOA, &gpioInit);

    /* USART2 수신(RX) 재활성화 */
    USART2->CR1 |= USART_CR1_RE;
}

#if 0	//ESI support
/**
 * @brief Restore ESI I2C pins.
 */
static void App_GpioLpRestoreEsiI2cPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_I2C1_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = ESI_SCL_Pin | ESI_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF1_I2C1;

    HAL_GPIO_Init(GPIOB, &gpioInit);
}
#endif

/**
 * @brief Restore NFC I2C pins.
 */
static void App_GpioLpRestoreNfcI2cPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_I2C2_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = NFC_SCL_Pin | NFC_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF6_I2C2;

    HAL_GPIO_Init(GPIOB, &gpioInit);
}

#if 0	//Temp support
/**
 * @brief Restore auxiliary temperature I2C pins.
 */
static void App_GpioLpRestoreTempI2cPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_I2C3_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Temp_SCL_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF7_I2C3;
    HAL_GPIO_Init(Temp_SCL_GPIO_Port, &gpioInit);

    gpioInit.Pin = Temp_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF7_I2C3;
    HAL_GPIO_Init(Temp_SDA_GPIO_Port, &gpioInit);
}
#endif

/**
 * @brief Restore piezo gpio pin.
 */
static void App_GpioLpRestorePiezoPin(void)
{
    App_GpioLpConfigOutput(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin, GPIO_PIN_RESET);
}

/**
 * @brief Restore external watchdog feed PWM pin.
 */
static void App_GpioLpRestoreExternalWatchdogPin(void)
{
    App_GpioLpConfigOutput(WD_FEED_GPIO_Port, WD_FEED_Pin, GPIO_PIN_RESET);
}

extern UART_HandleTypeDef hlpuart1;
/**
 * @brief Restore NB-IoT interface when the module is powered.
 */
static void App_GpioLpRestoreNbiotInterface(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_LPUART1_CLK_ENABLE();

    App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PIN_SET);
    App_GpioLpConfigOutput(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin, GPIO_PIN_SET);

    /* 모든 에러 플래그 및 RX 버퍼 클리어 */
    __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF  | UART_CLEAR_PEF);
    __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = NBIoT_RX_Pin | NBIoT_TX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF6_LPUART1;

    HAL_GPIO_Init(NBIoT_RX_GPIO_Port, &gpioInit);

    /* LPUART1 수신(RX) 재활성화 */
    LPUART1->CR1 |= USART_CR1_RE;
}

/**
 * @brief Isolate NB-IoT interface to block reverse current when module power is off.
 */
static void App_GpioLpIsolateNbiotInterface(void)
{
    App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PIN_RESET);
    App_GpioLpConfigAnalogNoPull(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin);

    /* 진행 중인 송신 완료 대기 */
    while (!(LPUART1->ISR & USART_ISR_TC))
        ;
    /* LPUART1 수신(RX) 비활성화 - 핵심! */
    LPUART1->CR1 &= ~USART_CR1_RE; // 가짜 신호 차단

    App_GpioLpConfigAnalogNoPull(NBIoT_RX_GPIO_Port, NBIoT_RX_Pin | NBIoT_TX_Pin);
}

/**
 * @brief Apply SWD policy.
 */
static void App_GpioLpApplySwdPolicy(void)
{
    if (g_appGpioLpContext.config.swdPolicy == APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION)
    {
        App_GpioLpConfigAnalogNoPull(GPIOA, GPIO_PIN_13 | GPIO_PIN_14); //pin13:swdio, pin14:swdclk
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
 * @brief Apply static run-state control levels.
 */
static void App_GpioLpApplyStaticControlPins(void)
{
    App_GpioLpConfigOutput(Charge_BOOT0_GPIO_Port, Charge_BOOT0_Pin, GPIO_PIN_RESET);

    if (g_appGpioLpContext.nbiotPowered == APP_TRUE)
    {
        App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PIN_SET);
        App_GpioLpConfigOutput(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin, GPIO_PIN_SET);
    }
    else
    {
        App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PIN_RESET);
    }
}

/**
 * @brief Restore always-armed wake/event inputs.
 */
static void App_GpioLpRestoreWakeInputs(void)
{
    App_GpioLpConfigExtiFalling(NFC_ED_GPIO_Port, NFC_ED_Pin);
    App_GpioLpConfigExtiFalling(REED_IN_GPIO_Port, REED_IN_Pin);
#if 0	//ESI support
    App_GpioLpConfigExtiFalling(ESI_Int_GPIO_Port, ESI_Int_Pin);
#endif	
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

#if 0	//ESI support
    if ((mask & APP_GPIO_LP_CLK_I2C1) != 0u)
    {
        __HAL_RCC_I2C1_CLK_DISABLE();
    }
#endif

    if ((mask & APP_GPIO_LP_CLK_I2C2) != 0u)
    {
        __HAL_RCC_I2C2_CLK_DISABLE();
    }

#if 0	//Temp support
    if ((mask & APP_GPIO_LP_CLK_I2C3) != 0u)
    {
        __HAL_RCC_I2C3_CLK_DISABLE();
    }
#endif

    if ((mask & APP_GPIO_LP_CLK_SYSCFG) != 0u)
    {
        __HAL_RCC_SYSCFG_CLK_DISABLE();
    }
}

/**
 * @brief Re-enable selected peripheral clocks after STOP wake-up.
 *
 * @param mask Peripheral clock mask.
 */
static void App_GpioLpEnablePeripheralClocks(uint32_t mask)
{
    if ((mask & APP_GPIO_LP_CLK_ADC1) != 0u)
    {
        __HAL_RCC_ADC1_CLK_ENABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_CRC) != 0u)
    {
        __HAL_RCC_CRC_CLK_ENABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_USART1) != 0u)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_USART2) != 0u)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_LPUART1) != 0u)
    {
        __HAL_RCC_LPUART1_CLK_ENABLE();
    }

#if 0	//ESI support
    if ((mask & APP_GPIO_LP_CLK_I2C1) != 0u)
    {
        __HAL_RCC_I2C1_CLK_ENABLE();
    }
#endif

    if ((mask & APP_GPIO_LP_CLK_I2C2) != 0u)
    {
        __HAL_RCC_I2C2_CLK_ENABLE();
    }

#if 0	//Temp support
    if ((mask & APP_GPIO_LP_CLK_I2C3) != 0u)
    {
        __HAL_RCC_I2C3_CLK_ENABLE();
    }
#endif

    if ((mask & APP_GPIO_LP_CLK_SYSCFG) != 0u)
    {
        __HAL_RCC_SYSCFG_CLK_ENABLE();
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
    p_config->keepDebugUartPinsInStop = 0u;
    p_config->keepMeterUartPinsInStop = 0u;
#if 0	//ESI support
    p_config->keepEsiI2cPinsInStop = 0u;
#endif	
    p_config->keepNfcI2cPinsInStop = 0u;
#if 0	//Temp support
    p_config->keepTempI2cPinsInStop = 0u;
#endif
    p_config->keepPiezoPinInStop = 0u;
    p_config->keepExternalWatchdogPinInStop = 0u;
    p_config->stopClockDisableMask =
        APP_GPIO_LP_CLK_ADC1 |
        APP_GPIO_LP_CLK_CRC |
        APP_GPIO_LP_CLK_USART1 |
        APP_GPIO_LP_CLK_USART2 |
        APP_GPIO_LP_CLK_LPUART1 |
#if 0	//ESI support
        APP_GPIO_LP_CLK_I2C1 |
#endif
        APP_GPIO_LP_CLK_I2C2;
#if 0	//Temp support
        APP_GPIO_LP_CLK_I2C3;
#endif
}

AppStatus_t App_GpioLpInit(const AppGpioLpConfig_t *p_config)
{
    APP_RETURN_IF_FALSE((p_config != NULL), APP_STATUS_INVALID_PARAM);

    (void)memset(&g_appGpioLpContext, 0, sizeof(g_appGpioLpContext));
    g_appGpioLpContext.config = *p_config;
    g_appGpioLpContext.nbiotPowered = 0u;
    g_appGpioLpContext.initialized = 1u;

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpApplyRunBaseState(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == 1u), APP_STATUS_NOT_INITIALIZED);

    App_GpioLpEnablePortClocks();
    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();
    App_GpioLpApplySwdPolicy();

    if(g_appGpioLpContext.nbiotPowered == 0u)
    {
        App_GpioLpIsolateNbiotInterface();
    }
    else
    {
        App_GpioLpRestoreNbiotInterface();
    }

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpSetNbiotPowered(uint8_t powered)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == 1u), APP_STATUS_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE(((powered == 0u) || (powered == 1u)), APP_STATUS_INVALID_PARAM);

    g_appGpioLpContext.nbiotPowered = powered;

    App_GpioLpEnablePortClocks();

    if (powered == 0u)
    {
        App_GpioLpIsolateNbiotInterface();
    }
    else if (powered == 1u)
    {
        App_GpioLpRestoreNbiotInterface();
    }

#ifdef DEBUG
    if (App_GpioLpCanDebugLog() == 1u)
    {
        (void)APP_LOGD("GPIO", "NB-IoT power=%u", (unsigned int)powered);
    }
#endif

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpOnBeforeStopEnter(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == 1u), APP_STATUS_NOT_INITIALIZED);

    if (g_appGpioLpContext.stopPrepared == 1u)
    {
        return APP_STATUS_OK;
    }

    (void)APP_LOGW("GPIO", "STOP External interface pins(UART, I2C, gpios..)");

    App_GpioLpEnablePortClocks();
    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();
    App_GpioLpApplySwdPolicy();

    if (g_appGpioLpContext.config.keepDebugUartPinsInStop != 1u)
    {
        /* 진행 중인 송신 완료 대기 */
        while (!(USART1->ISR & USART_ISR_TC))
            ;
        /* USART1 수신(RX) 비활성화 - 핵심! */
        USART1->CR1 &= ~USART_CR1_RE; // 가짜 신호 차단

        App_GpioLpConfigAnalogNoPull(Debug_TX_GPIO_Port, Debug_TX_Pin | Debug_RX_Pin);
    }

    if (g_appGpioLpContext.config.keepMeterUartPinsInStop != 1u)
    {
        /* 진행 중인 송신 완료 대기 */
        while (!(USART2->ISR & USART_ISR_TC))
            ;
        /* USART2 수신(RX) 비활성화 - 핵심! */
        USART2->CR1 &= ~USART_CR1_RE; // 가짜 신호 차단

        App_GpioLpConfigAnalogNoPull(Meter_TX_GPIO_Port, Meter_TX_Pin | Meter_RX_Pin);
    }

#if 0	//ESI support
    if (g_appGpioLpContext.config.keepEsiI2cPinsInStop != 1u)
    {
        App_GpioLpConfigAnalogNoPull(ESI_SCL_GPIO_Port, ESI_SCL_Pin | ESI_SDA_Pin);
    }
#endif	

    if (g_appGpioLpContext.config.keepNfcI2cPinsInStop != 1u)
    {
        App_GpioLpConfigAnalogNoPull(NFC_SCL_GPIO_Port, NFC_SCL_Pin | NFC_SDA_Pin);
    }

#if 0	//Temp support
    if (g_appGpioLpContext.config.keepTempI2cPinsInStop != 1u)
    {
        App_GpioLpConfigAnalogNoPull(Temp_SCL_GPIO_Port, Temp_SCL_Pin);
        App_GpioLpConfigAnalogNoPull(Temp_SDA_GPIO_Port, Temp_SDA_Pin);
    }
#endif

    if (g_appGpioLpContext.config.keepPiezoPinInStop != 1u)
    {
        App_GpioLpConfigAnalogNoPull(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin);
    }

    if (g_appGpioLpContext.config.keepExternalWatchdogPinInStop != 1u)
    {
        App_GpioLpConfigAnalogNoPull(WD_FEED_GPIO_Port, WD_FEED_Pin);
    }

    if(g_appGpioLpContext.nbiotPowered == 0u)
    {
        App_GpioLpIsolateNbiotInterface();
    }

    App_GpioLpDisablePeripheralClocks(g_appGpioLpContext.config.stopClockDisableMask);
    g_appGpioLpContext.lastDisabledClockMask = g_appGpioLpContext.config.stopClockDisableMask;
    g_appGpioLpContext.stopPrepared = 1u;

#ifdef DEBUG
    if (App_GpioLpCanDebugLog() == 1u)
    {
        (void)APP_LOGD("GPIO",
                       "STOP prep done: nbiot=%u keep_dbg=%u keep_meter=%u clk_mask=0x%08lX",
                       (unsigned int)g_appGpioLpContext.nbiotPowered,
                       (unsigned int)g_appGpioLpContext.config.keepDebugUartPinsInStop,
                       (unsigned int)g_appGpioLpContext.config.keepMeterUartPinsInStop,
                       (unsigned long)g_appGpioLpContext.lastDisabledClockMask);
    }
#endif

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpOnAfterStopExit(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == 1u), APP_STATUS_NOT_INITIALIZED);

    App_GpioLpEnablePortClocks();
    App_GpioLpEnablePeripheralClocks(g_appGpioLpContext.lastDisabledClockMask);

    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();

    if (g_appGpioLpContext.config.keepDebugUartPinsInStop != 1u)
    {
        App_GpioLpRestoreDebugUartPins();
    }

    if (g_appGpioLpContext.config.keepMeterUartPinsInStop != 1u)
    {
        App_GpioLpRestoreMeterUartPins();
    }

#if 0	//ESI support
    if (g_appGpioLpContext.config.keepEsiI2cPinsInStop != 1u)
    {
        App_GpioLpRestoreEsiI2cPins();
    }
#endif	

    if (g_appGpioLpContext.config.keepNfcI2cPinsInStop != 1u)
    {
        App_GpioLpRestoreNfcI2cPins();
    }

#if 0	//Temp support
    if (g_appGpioLpContext.config.keepTempI2cPinsInStop != 1u)
    {
        App_GpioLpRestoreTempI2cPins();
    }
#endif

    if (g_appGpioLpContext.config.keepPiezoPinInStop != 1u)
    {
        App_GpioLpRestorePiezoPin();
    }

    if (g_appGpioLpContext.config.keepExternalWatchdogPinInStop != 1u)
    {
        App_GpioLpRestoreExternalWatchdogPin();
    }

    if(g_appGpioLpContext.nbiotPowered == 0u)
    {
        App_GpioLpIsolateNbiotInterface();
    }

    App_GpioLpApplySwdPolicy();

    g_appGpioLpContext.stopPrepared = 0u;

    (void)APP_LOGI("GPIO", "STOP recover external interface pins(UART, I2C, gpios..)");

#ifdef DEBUG
    if (App_GpioLpCanDebugLog() == 1u)
    {
        (void)APP_LOGD("GPIO",
                       "STOP recover done: nbiot=%u restored_mask=0x%08lX",
                       (unsigned int)g_appGpioLpContext.nbiotPowered,
                       (unsigned long)g_appGpioLpContext.lastDisabledClockMask);
    }
#endif

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpPrepareForStop(void)
{
    return App_GpioLpOnBeforeStopEnter();
}

AppStatus_t App_GpioLpRecoverFromStop(void)
{
    return App_GpioLpOnAfterStopExit();
}

const AppGpioLpContext_t *App_GpioLpGetContext(void)
{
    return &g_appGpioLpContext;
}

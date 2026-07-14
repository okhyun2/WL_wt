#include "app_gpio_lp.h"

#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_log.h"
#include "nfc_lowpower.h"
#include <stdio.h>

extern NFC_LP_Handle_t g_nfcLpHandle;

/**
 * @file    app_gpio_lp.c
 * @brief   Board-specific low-power GPIO and peripheral clock policy.
 */

/** @brief Truly unconnected pins on GPIOA based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTA_MASK \
    (GPIO_PIN_0 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_15)

/** @brief Truly unconnected pins on GPIOB based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTB_MASK \
    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | \
     GPIO_PIN_9 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/** @brief Truly unconnected pins on GPIOC based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTC_MASK \
    (GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | \
     GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13)

/** @brief Truly unconnected pins on GPIOD based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTD_MASK \
    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | \
     GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | \
     GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15)

/** @brief Truly unconnected pins on GPIOH based on current board pin map. */
#define APP_GPIO_LP_UNUSED_PORTH_MASK (GPIO_PIN_All)

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
    __HAL_RCC_GPIOH_CLK_ENABLE();
}

/**
 * @brief Disable all GPIO port clocks used by the board.
 */
static void App_GpioLpDisablePortClocks(void)
{
    __HAL_RCC_GPIOA_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();
    __HAL_RCC_GPIOC_CLK_DISABLE();
    __HAL_RCC_GPIOD_CLK_DISABLE();
    __HAL_RCC_GPIOH_CLK_DISABLE();
}


/**
 * @brief Configure selected pins as analog/no-pull.
 *
 * @param gpioPort GPIO port.
 * @param pinMask Pin mask.
 */
void App_GpioLpConfigAnalogNoPull(GPIO_TypeDef *gpioPort, uint32_t pinMask)
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
void App_GpioLpConfigOutput(GPIO_TypeDef *gpioPort, uint32_t pinMask, GPIO_PinState pinState)
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

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Debug_TX_Pin | Debug_RX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = GPIO_AF4_USART1;

    HAL_GPIO_Init(GPIOA, &gpioInit);

    /* USART1 수신(RX) 재활성화 */
    USART1->CR1 |= USART_CR1_RE;

    // wait stable condition. if not, received first byte is gabage data.
    {
        /* 핀 안정화 대기 */
        HAL_Delay(10);

        /* 모든 에러 플래그 클리어 */
        __HAL_UART_CLEAR_FLAG(APP_UART_DEBUG_HANDLE, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);

        /* 수신 버퍼 강제 플러시 */
        __HAL_UART_SEND_REQ(APP_UART_DEBUG_HANDLE, UART_RXDATA_FLUSH_REQUEST);

        /* IDLE 플래그 클리어 */
        __HAL_UART_CLEAR_IDLEFLAG(APP_UART_DEBUG_HANDLE);

        /* 최종 안정화 */
        HAL_Delay(5);
    }
}

/**
 * @brief Restore meter UART pins.
 */
void App_GpioLpRestoreMeterUartPins(void)
{
    GPIO_InitTypeDef gpioInit;

    __HAL_RCC_USART2_CLK_ENABLE();

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = Meter_TX_Pin | Meter_RX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF4_USART2;

    HAL_GPIO_Init(GPIOA, &gpioInit);

    /* USART2 수신(RX) 재활성화 */
    USART2->CR1 |= USART_CR1_RE;

    // wait stable condition. if not, received first byte is gabage data.
    {
        /* 핀 안정화 대기 */
        HAL_Delay(10);

        /* 모든 에러 플래그 클리어 */
        __HAL_UART_CLEAR_FLAG(APP_UART_METER_HANDLE, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);

        /* 수신 버퍼 강제 플러시 */
        __HAL_UART_SEND_REQ(APP_UART_METER_HANDLE, UART_RXDATA_FLUSH_REQUEST);

        /* IDLE 플래그 클리어 */
        __HAL_UART_CLEAR_IDLEFLAG(APP_UART_METER_HANDLE);

        /* 최종 안정화 */
        HAL_Delay(5);
    }
}

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
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF6_I2C2;

    HAL_GPIO_Init(GPIOB, &gpioInit);
}

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
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF7_I2C3;
    HAL_GPIO_Init(Temp_SCL_GPIO_Port, &gpioInit);

    gpioInit.Pin = Temp_SDA_Pin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF7_I2C3;
    HAL_GPIO_Init(Temp_SDA_GPIO_Port, &gpioInit);
}

/**
 * @brief Restore piezo PWM pin.
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

    (void)memset(&gpioInit, 0, sizeof(gpioInit));
    gpioInit.Pin = NBIoT_RX_Pin | NBIoT_TX_Pin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
    gpioInit.Alternate = GPIO_AF6_LPUART1;

    HAL_GPIO_Init(NBIoT_RX_GPIO_Port, &gpioInit);

    /* Power 핀 안정화 대기 */
    HAL_Delay(10);

    App_HwSetNbiotReset(GPIO_PIN_RESET);
    HAL_Delay(10);
    App_HwSetNbiotReset(GPIO_PIN_SET);

    /* LPUART1 수신(RX) 재활성화 */
    LPUART1->CR1 |= USART_CR1_RE;

    // wait stable condition. if not, received first byte is gabage data.
    {
        /* 핀 안정화 대기 */
        HAL_Delay(10);

        /* 모든 에러 플래그 클리어 */
        __HAL_UART_CLEAR_FLAG(APP_UART_NBIOT_HANDLE, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF | UART_CLEAR_PEF);

        /* 수신 버퍼 강제 플러시 */
        __HAL_UART_SEND_REQ(APP_UART_NBIOT_HANDLE, UART_RXDATA_FLUSH_REQUEST);

        /* IDLE 플래그 클리어 */
        __HAL_UART_CLEAR_IDLEFLAG(APP_UART_NBIOT_HANDLE);

        /* 최종 안정화 */
        HAL_Delay(5);
    }

    APP_LOGI("GPIO", "NB-IoT power on");
}

/**
 * @brief Isolate NB-IoT interface to block reverse current when module power is off.
 */
static void App_GpioLpIsolateNbiotInterface(void)
{
    APP_LOGI("GPIO", "NB-IoT power off");

    App_GpioLpConfigOutput(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, GPIO_PIN_RESET);
    App_GpioLpConfigAnalogNoPull(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin);

    /* 진행 중인 송신 완료 대기 */
    while (!(LPUART1->ISR & USART_ISR_TC))
        ;
    /* LPUART1 수신(RX) 비활성화 - 핵심! */
    LPUART1->CR1 &= ~USART_CR1_RE; // 가짜 신호 차단

    App_GpioLpConfigAnalogNoPull(NBIoT_RX_GPIO_Port, NBIoT_RX_Pin | NBIoT_TX_Pin);
    HAL_Delay(1000u);   /* 캐패시터 방전 */
}

/**
 * @brief Apply SWD policy.
 */
static void App_GpioLpApplySwdPolicy(void)
{
    if (g_appGpioLpContext.config.swdPolicy == APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION)
    {
        App_GpioLpConfigAnalogNoPull(GPIOA, GPIO_PIN_13 | GPIO_PIN_14); //pin13:swdio, pin14:swdclk
        App_GpioLpConfigAnalogNoPull(GPIOA, Debug_TX_Pin | Debug_RX_Pin); //debug uart tx,rx
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
    App_GpioLpConfigAnalogNoPull(GPIOH, APP_GPIO_LP_UNUSED_PORTH_MASK);
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

    if ((mask & APP_GPIO_LP_CLK_I2C2) != 0u)
    {
        __HAL_RCC_I2C2_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_I2C3) != 0u)
    {
        __HAL_RCC_I2C3_CLK_DISABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_LPTIM1) != 0u)
    {
        __HAL_RCC_LPTIM1_CLK_DISABLE();
    }

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

    if ((mask & APP_GPIO_LP_CLK_I2C2) != 0u)
    {
        __HAL_RCC_I2C2_CLK_ENABLE();
    }

    if ((mask & APP_GPIO_LP_CLK_I2C3) != 0u)
    {
        __HAL_RCC_I2C3_CLK_ENABLE();
    }

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
    p_config->stopClockDisableMask =
        APP_GPIO_LP_CLK_ADC1 |
        APP_GPIO_LP_CLK_CRC |
        APP_GPIO_LP_CLK_USART1 |
        APP_GPIO_LP_CLK_USART2 |
        APP_GPIO_LP_CLK_LPUART1 |
        APP_GPIO_LP_CLK_I2C2 |
        APP_GPIO_LP_CLK_I2C3 |
        APP_GPIO_LP_CLK_LPTIM1 |
        APP_GPIO_LP_CLK_SYSCFG;
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

    return APP_STATUS_OK;
}

AppStatus_t App_GpioLpOnBeforeStopEnter(void)
{
    APP_RETURN_IF_FALSE((g_appGpioLpContext.initialized == 1u), APP_STATUS_NOT_INITIALIZED);

    if (g_appGpioLpContext.stopPrepared == 1u)
    {
        return APP_STATUS_OK;
    }

    APP_LOGI("GPIO", "STOP External interface pins(UART, I2C, gpios..)");

    App_GpioLpEnablePortClocks();
    App_GpioLpApplyUnusedPins();
    App_GpioLpApplyStaticControlPins();
    App_GpioLpRestoreWakeInputs();
    App_GpioLpApplySwdPolicy();

    //Meter TX/RX pin
    {
        /* 진행 중인 송신 완료 대기 */
        while (!(USART2->ISR & USART_ISR_TC))
            ;
        /* USART2 수신(RX) 비활성화 - 핵심! */
        USART2->CR1 &= ~USART_CR1_RE; // 가짜 신호 차단

        //App_GpioLpConfigAnalogNoPull(Meter_TX_GPIO_Port, Meter_TX_Pin | Meter_RX_Pin);
        //Set UART pint to output low for buffer IC. If uart pin input, buffer ic unstable increase current.

        //App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin | Meter_RX_Pin, GPIO_PIN_RESET);
        //kiki0000. recommend by han
        App_GpioLpConfigOutput(Meter_TX_GPIO_Port, Meter_TX_Pin, GPIO_PIN_RESET);
        App_GpioLpConfigAnalogNoPull(Meter_TX_GPIO_Port, Meter_RX_Pin);
    }

    //NFC SCL/SDA pin
    {
        App_GpioLpConfigAnalogNoPull(NFC_SCL_GPIO_Port, NFC_SCL_Pin | NFC_SDA_Pin);
    }

    //Aux pin
    {
        App_GpioLpConfigAnalogNoPull(Temp_SCL_GPIO_Port, Temp_SCL_Pin);
        App_GpioLpConfigAnalogNoPull(Temp_SDA_GPIO_Port, Temp_SDA_Pin);
    }
	
    //Piezo pin
    {
        //App_GpioLpConfigAnalogNoPull(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin);
        //kiki0000. recommend by han
        App_GpioLpConfigOutput(Piezo_PWM_GPIO_Port, Piezo_PWM_Pin, GPIO_PIN_RESET);
    }

    //WD_Feed pin
    {
        App_GpioLpConfigAnalogNoPull(WD_FEED_GPIO_Port, WD_FEED_Pin);
    }

    if(g_appGpioLpContext.nbiotPowered == 0u)
    {
        App_GpioLpIsolateNbiotInterface();
    }

#if (APP_BUILD_IS_PRODUCTION == APP_FALSE)
#ifdef DEBUG
    //for debugging gpio port status
    //GPIO_DumpAll();
#endif //DEBUG
#endif

    //Debug TX/RX pin
    {
        /* 진행 중인 송신 완료 대기 */
        while (!(USART1->ISR & USART_ISR_TC))
            ;
        /* USART1 수신(RX) 비활성화 - 핵심! */
        USART1->CR1 &= ~USART_CR1_RE; // 가짜 신호 차단

        App_GpioLpConfigAnalogNoPull(Debug_TX_GPIO_Port, Debug_TX_Pin | Debug_RX_Pin);
    }


    App_GpioLpDisablePeripheralClocks(g_appGpioLpContext.config.stopClockDisableMask);
    g_appGpioLpContext.lastDisabledClockMask = g_appGpioLpContext.config.stopClockDisableMask;
    g_appGpioLpContext.stopPrepared = 1u;

#ifdef DEBUG
    if (App_GpioLpCanDebugLog() == 1u)
    {
        APP_LOGD("GPIO",
                       "STOP prep done: nbiot=%u clk_mask=0x%08lX",
                       (unsigned int)g_appGpioLpContext.nbiotPowered,
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

    App_GpioLpRestoreDebugUartPins();
    App_GpioLpRestoreMeterUartPins();
    App_GpioLpRestoreNfcI2cPins();
    App_GpioLpRestoreTempI2cPins();
    App_GpioLpRestorePiezoPin();
    App_GpioLpRestoreExternalWatchdogPin();

    if(g_appGpioLpContext.nbiotPowered == 0u)
    {
        App_GpioLpIsolateNbiotInterface();
    }

    App_GpioLpApplySwdPolicy();

    g_appGpioLpContext.stopPrepared = 0u;

    APP_LOGI("GPIO", "STOP recover external interface pins(UART, I2C, gpios..)");

#ifdef DEBUG
    if (App_GpioLpCanDebugLog() == 1u)
    {
        APP_LOGD("GPIO",
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

#ifdef DEBUG

static const char *modeStr(uint32_t m)
{
    switch (m & 0x3U) {
        case 0: return "INPUT  ";
        case 1: return "OUTPUT ";
        case 2: return "ALT-FN ";
        case 3: return "ANALOG ";
    }
    return "?";
}

static const char *otypeStr(uint32_t t)   { return (t & 1U) ? "OD" : "PP"; }

static const char *speedStr(uint32_t s)
{
    switch (s & 0x3U) {
        case 0: return "Low ";
        case 1: return "Med ";
        case 2: return "High";
        case 3: return "VHi ";
    }
    return "?";
}

static const char *pullStr(uint32_t p)
{
    switch (p & 0x3U) {
        case 0: return "NoPull";
        case 1: return "PullUp";
        case 2: return "PullDn";
        case 3: return "RSVD  ";
    }
    return "?";
}

/* 포트 클럭이 켜져 있는지 확인 (꺼져 있으면 레지스터 읽기 0 또는 fault) */
static int isPortClockEnabled(GPIO_TypeDef *GPIOx)
{
    uint32_t en = RCC->IOPENR;
    if (GPIOx == GPIOA) return (en & RCC_IOPENR_GPIOAEN) ? 1 : 0;
    if (GPIOx == GPIOB) return (en & RCC_IOPENR_GPIOBEN) ? 1 : 0;
    if (GPIOx == GPIOC) return (en & RCC_IOPENR_GPIOCEN) ? 1 : 0;
    if (GPIOx == GPIOD) return (en & RCC_IOPENR_GPIODEN) ? 1 : 0;
    if (GPIOx == GPIOH) return (en & RCC_IOPENR_GPIOHEN) ? 1 : 0;
    return 0;
}

/* 포트 클럭을 임시로 켜고, 원상복구 위해 이전 상태 반환 */
static int enablePortClockIfNeeded(GPIO_TypeDef *GPIOx)
{
    int wasOn = isPortClockEnabled(GPIOx);
    if (!wasOn) {
        if      (GPIOx == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
        else if (GPIOx == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
        else if (GPIOx == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
        else if (GPIOx == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
        else if (GPIOx == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
    }
    return wasOn;
}

static void restorePortClock(GPIO_TypeDef *GPIOx, int wasOn)
{
    if (wasOn) return;
    if      (GPIOx == GPIOA) __HAL_RCC_GPIOA_CLK_DISABLE();
    else if (GPIOx == GPIOB) __HAL_RCC_GPIOB_CLK_DISABLE();
    else if (GPIOx == GPIOC) __HAL_RCC_GPIOC_CLK_DISABLE();
    else if (GPIOx == GPIOD) __HAL_RCC_GPIOD_CLK_DISABLE();
    else if (GPIOx == GPIOH) __HAL_RCC_GPIOH_CLK_DISABLE();
}

void GPIO_DumpPort(GPIO_TypeDef *GPIOx, const char *portName)
{
    int wasOn = enablePortClockIfNeeded(GPIOx);

    /* 레지스터 스냅샷 (atomic하게 한 번에 읽음) */
    uint32_t moder   = GPIOx->MODER;
    uint32_t otyper  = GPIOx->OTYPER;
    uint32_t ospeedr = GPIOx->OSPEEDR;
    uint32_t pupdr   = GPIOx->PUPDR;
    uint32_t idr     = GPIOx->IDR;
    uint32_t odr     = GPIOx->ODR;
    uint32_t afrl    = GPIOx->AFR[0];
    uint32_t afrh    = GPIOx->AFR[1];

    APP_LOGI("GPIO", "=== %s (CLK:%s) ===", portName, wasOn ? "ON" : "OFF(temp on)");
    APP_LOGI("GPIO", "Pin | Mode    | OType | Speed | Pull   | AF | IDR ODR");
    APP_LOGI("GPIO", "----+---------+-------+-------+--------+----+--------");

    for (int i = 0; i < 16; i++) {
        uint32_t m  = (moder   >> (i * 2)) & 0x3U;
        uint32_t t  = (otyper  >> i)       & 0x1U;
        uint32_t s  = (ospeedr >> (i * 2)) & 0x3U;
        uint32_t p  = (pupdr   >> (i * 2)) & 0x3U;
        uint32_t in = (idr     >> i)       & 0x1U;
        uint32_t ou = (odr     >> i)       & 0x1U;
        uint32_t af = (i < 8) ? ((afrl >> (i * 4)) & 0xFU)
                              : ((afrh >> ((i - 8) * 4)) & 0xFU);

        APP_LOGI("GPIO", "P%s%-2d | %s | %s    | %s  | %s | %2lu | %lu   %lu",
               portName + 4,    /* "GPIOA" → "A" 추출용 */
               i,
               modeStr(m),
               (m == 1 || m == 2) ? otypeStr(t) : "--",
               (m == 1 || m == 2) ? speedStr(s) : "--- ",
               pullStr(p),
               (m == 2) ?  (unsigned long)af:0,
               (unsigned long)in, (unsigned long)ou);

    }

    /* 원본 레지스터 값도 함께 출력 (디버깅용) */
    APP_LOGI("GPIO", "RAW: MODER=0x%08lX OTYPER=0x%04lX OSPEEDR=0x%08lX PUPDR=0x%08lX",
           (unsigned long)moder, (unsigned long)otyper,
           (unsigned long)ospeedr, (unsigned long)pupdr);
    APP_LOGI("GPIO", "     AFRL =0x%08lX AFRH  =0x%08lX IDR=0x%04lX ODR=0x%04lX",
           (unsigned long)afrl, (unsigned long)afrh,
           (unsigned long)idr, (unsigned long)odr);

    restorePortClock(GPIOx, wasOn);
}

void GPIO_DumpAll(void)
{
    APP_LOGI("GPIO", "########## GPIO STATE DUMP ##########");
    APP_LOGI("GPIO", "RCC->IOPENR = 0x%08lX", (unsigned long)RCC->IOPENR);

    GPIO_DumpPort(GPIOA, "GPIOA");
    GPIO_DumpPort(GPIOB, "GPIOB");
    GPIO_DumpPort(GPIOC, "GPIOC");
    GPIO_DumpPort(GPIOD, "GPIOD");
    GPIO_DumpPort(GPIOH, "GPIOH");

    APP_LOGI("GPIO", "######################################");
}

#endif // DEBUG
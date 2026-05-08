#ifndef APP_HW_H
#define APP_HW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @file    app_hw.h
 * @brief   Logical hardware mapping for the provided CubeMX project.
 *
 * @note    This mapping follows the provided .ioc baseline:
 *          - USART1  : Debug console
 *          - USART2  : Meter interface
 *          - LPUART1 : NB-IoT
#if 0	//ESI support
 *          - I2C1    : ESI
#endif
 *          - I2C2    : NFC
 *          - I2C3    : Temperature/auxiliary
 */

/* Peripheral handles generated in main.c */
extern RTC_HandleTypeDef hrtc;
extern ADC_HandleTypeDef hadc;
extern CRC_HandleTypeDef hcrc;
extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern I2C_HandleTypeDef hi2c3;
extern UART_HandleTypeDef hlpuart1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

/** @brief Logical peripheral aliases. */
#define APP_RTC_HANDLE           (&hrtc)

#define APP_UART_DEBUG_HANDLE           (&huart1)
#define APP_UART_METER_HANDLE           (&huart2)
#define APP_UART_NBIOT_HANDLE           (&hlpuart1)

#if 0	//ESI support
#define APP_I2C_ESI_HANDLE              (&hi2c1)
#endif
#define APP_I2C_NFC_HANDLE              (&hi2c2)
#define APP_I2C_AUX_HANDLE              (&hi2c3)

#define APP_ADC_BATTERY_HANDLE          (&hadc)
#define APP_CRC_HANDLE                  (&hcrc)

/**
 * @brief Set EWD module feed pin.
 *
 */
static inline void App_HwFeedEWD(void)
{
    HAL_GPIO_WritePin(WD_FEED_GPIO_Port, WD_FEED_Pin, GPIO_PIN_SET);
    HAL_Delay(1); //>=100ns
    HAL_GPIO_WritePin(WD_FEED_GPIO_Port, WD_FEED_Pin, GPIO_PIN_RESET);
}


/**
 * @brief Set NB-IoT module power control pin.
 *
 * @param state GPIO_PIN_SET or GPIO_PIN_RESET.
 */
static inline void App_HwSetNbiotEnable(GPIO_PinState state)
{
    HAL_GPIO_WritePin(NBIoT_EN_GPIO_Port, NBIoT_EN_Pin, state);
}

/**
 * @brief Set NB-IoT module reset pin.
 *
 * @param state GPIO_PIN_SET or GPIO_PIN_RESET.
 */
static inline void App_HwSetNbiotReset(GPIO_PinState state)
{
    HAL_GPIO_WritePin(NBIoT_RST_GPIO_Port, NBIoT_RST_Pin, state);
}

/**
 * @brief Set BOOT0 helper pin state.
 *
 * @param state GPIO_PIN_SET or GPIO_PIN_RESET.
 */
static inline void App_HwSetChargeBoot0(GPIO_PinState state)
{
    HAL_GPIO_WritePin(Charge_BOOT0_GPIO_Port, Charge_BOOT0_Pin, state);
}

/**
 * @brief Read NFC event-detect interrupt line state.
 *
 * @return Current NFC_ED pin state.
 */
static inline GPIO_PinState App_HwReadNfcEvent(void)
{
    return HAL_GPIO_ReadPin(NFC_ED_GPIO_Port, NFC_ED_Pin);
}

#if 0	//ESI support
/**
 * @brief Read ESI interrupt line state.
 *
 * @return Current ESI interrupt pin state.
 */
static inline GPIO_PinState App_HwReadEsiInterrupt(void)
{
    return HAL_GPIO_ReadPin(ESI_Int_GPIO_Port, ESI_Int_Pin);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_HW_H */

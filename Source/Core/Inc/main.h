/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

void RTC_SetTime(int year, int month, int date, int hour, int min, int sec);
uint64_t RTC_GetTimeMs(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define NBIoT_RX_Pin GPIO_PIN_0
#define NBIoT_RX_GPIO_Port GPIOC
#define NBIoT_TX_Pin GPIO_PIN_1
#define NBIoT_TX_GPIO_Port GPIOC
#define NBIoT_EN_Pin GPIO_PIN_2
#define NBIoT_EN_GPIO_Port GPIOC
#define NBIoT_RST_Pin GPIO_PIN_3
#define NBIoT_RST_GPIO_Port GPIOC
#define NBIoT_RI_Pin GPIO_PIN_0
#define NBIoT_RI_GPIO_Port GPIOA
#define BAT_LEVEL_Pin GPIO_PIN_1
#define BAT_LEVEL_GPIO_Port GPIOA
#define Meter_TX_Pin GPIO_PIN_2
#define Meter_TX_GPIO_Port GPIOA
#define Meter_RX_Pin GPIO_PIN_3
#define Meter_RX_GPIO_Port GPIOA
#define NFC_ED_Pin GPIO_PIN_4
#define NFC_ED_GPIO_Port GPIOA
#define Piezo_PWM_Pin GPIO_PIN_7
#define Piezo_PWM_GPIO_Port GPIOA
#define NFC_SCL_Pin GPIO_PIN_10
#define NFC_SCL_GPIO_Port GPIOB
#define NFC_SDA_Pin GPIO_PIN_11
#define NFC_SDA_GPIO_Port GPIOB
#define Temp_SDA_Pin GPIO_PIN_9
#define Temp_SDA_GPIO_Port GPIOC
#define Temp_SCL_Pin GPIO_PIN_8
#define Temp_SCL_GPIO_Port GPIOA
#define Debug_TX_Pin GPIO_PIN_9
#define Debug_TX_GPIO_Port GPIOA
#define Debug_RX_Pin GPIO_PIN_10
#define Debug_RX_GPIO_Port GPIOA
#define REED_IN_Pin GPIO_PIN_15
#define REED_IN_GPIO_Port GPIOA
#if 0
#define ESI_Int_Pin GPIO_PIN_2
#define ESI_Int_GPIO_Port GPIOD
#endif
#define WD_FEED_Pin GPIO_PIN_5
#define WD_FEED_GPIO_Port GPIOB
#if 0
#define ESI_SCL_Pin GPIO_PIN_6
#define ESI_SCL_GPIO_Port GPIOB
#define ESI_SDA_Pin GPIO_PIN_7
#define ESI_SDA_GPIO_Port GPIOB
#endif
#define Charge_BOOT0_Pin GPIO_PIN_8
#define Charge_BOOT0_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

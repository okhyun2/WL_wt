/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    stm32l0xx_it.c
 * @brief   Interrupt Service Routines.
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

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32l0xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_system.h"
#include "app_hw.h"
#include "app_debug.h"
#include "app_selftest.h"
extern void App_FsmNfcEdIrqHandler(void);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  App_DebugConsoleOnUartRxCompleteIsr(huart);
  App_SelfTestOnUartRxCompleteIsr(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  App_DebugConsoleOnUartErrorIsr(huart);
  App_SelfTestOnUartErrorIsr(huart);
}

/* External variables --------------------------------------------------------*/
extern LPTIM_HandleTypeDef hlptim1;
extern RTC_HandleTypeDef hrtc;
/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M0+ Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
 * @brief This function handles Non maskable Interrupt.
 */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
  while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
 * @brief This function handles Hard fault interrupt.
 */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
 * @brief This function handles System service call via SWI instruction.
 */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVC_IRQn 0 */

  /* USER CODE END SVC_IRQn 0 */
  /* USER CODE BEGIN SVC_IRQn 1 */

  /* USER CODE END SVC_IRQn 1 */
}

/**
 * @brief This function handles Pendable request for system service.
 */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
 * @brief This function handles System tick timer.
 */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */

  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32L0xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32l0xx.s).                    */
/******************************************************************************/
void RTC_IRQHandler(void)
{
  uint32_t rtc_isr = RTC->ISR;

  /* 디버그용 원본 값 저장 */
  g_wakeup_ctx.raw_rtc_isr = rtc_isr;

  /* 처리할 플래그가 없으면 즉시 반환 */
  if (!(rtc_isr & (RTC_ISR_ALRAF | RTC_ISR_ALRBF | RTC_ISR_WUTF)))
  {
    return;
  }

  /* RTC 쓰기 보호 해제 */
  RTC->WPR = APP_SYSTEM_RTC_WPR_KEY1;
  RTC->WPR = APP_SYSTEM_RTC_WPR_KEY2;

  /* RTC 플래그 먼저 클리어 (순서 중요!) */
  if (rtc_isr & RTC_ISR_ALRAF)
  {
    CLEAR_BIT(RTC->ISR, RTC_ISR_ALRAF);
    g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_RTC_ALARM_A;
  }

  if (rtc_isr & RTC_ISR_ALRBF)
  {
    CLEAR_BIT(RTC->ISR, RTC_ISR_ALRBF);
    g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_RTC_ALARM_B;
  }

  if (rtc_isr & RTC_ISR_WUTF)
  {
    CLEAR_BIT(RTC->CR, RTC_CR_WUTIE | RTC_CR_WUTE);
    CLEAR_BIT(RTC->ISR, RTC_ISR_WUTF);
    g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_RTC_WUT;
  }

  /* RTC 쓰기 보호 재활성화 */
  RTC->WPR = 0xFF;

  /* EXTI 클리어: RTC ISR 클리어 완료 후 수행 */
  if (g_wakeup_ctx.pending_flags & WAKEUP_MASK_RTC_ALARM)
  {
    EXTI->PR = (1U << 17); // Alarm용 EXTI Line 17
  }
  if (g_wakeup_ctx.pending_flags & WAKEUP_FLAG_RTC_WUT)
  {
    EXTI->PR = (1U << 20); // WUT용 EXTI Line 20
  }
}

void LPTIM1_IRQHandler(void)
{
  uint32_t isr = LPTIM1->ISR;

  /* 디버그용 원본 값 저장 */
  g_wakeup_ctx.raw_lptim_isr = isr;

  /* Auto-Reload Match 처리 */
  if (isr & LPTIM_ISR_ARRM)
  {
    LPTIM1->ICR = LPTIM_ICR_ARRMCF; // LPTIM 플래그 클리어
    g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_LPTIM1_ARR;
  }

  /* Compare Match 처리 */
  if (isr & LPTIM_ISR_CMPM)
  {
    LPTIM1->ICR = LPTIM_ICR_CMPMCF; // LPTIM 플래그 클리어
    g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_LPTIM1_CMP;
  }

  /* 전역 변수 누적 및 EXTI 클리어 */
  if (g_wakeup_ctx.pending_flags)
  {
    EXTI->PR = (1U << 23); // EXTI Line 23 클리어
  }
}

void EXTI0_1_IRQHandler(void)
{

}

void EXTI2_3_IRQHandler(void)
{
#if 0	//ESI support
    uint32_t pr = EXTI->PR & 0x0000000CU;  // bit2, bit3만 마스킹

    if (pr) {
        g_wakeup_ctx.raw_exti_pr |= pr;

      /* Pin 2 ~ Pin 3 처리 */
      if (pr & ESI_Int_Pin)
      {
        g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_EXTI_PIN2;
      }

        EXTI->PR = pr;
    }
#endif
}
void EXTI4_15_IRQHandler(void)
{
  uint32_t pr = EXTI->PR & 0x0000FFF0U; // bit4 ~ bit15만 마스킹

  if (pr)
  {
    g_wakeup_ctx.raw_exti_pr |= pr;

    /* Pin 4 ~ Pin 15 처리 */
    if (pr & NFC_ED_Pin)
    {
      App_FsmNfcEdIrqHandler();
      g_wakeup_ctx.pending_flags |= WAKEUP_FLAG_EXTI_PIN4;
    }

    EXTI->PR = pr;
  }
}



void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(APP_UART_DEBUG_HANDLE);
  /* USER CODE BEGIN USART1_IRQn 1 */

  /* USER CODE END USART1_IRQn 1 */
}


void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */

  /* USER CODE END USART2_IRQn 0 */
  HAL_UART_IRQHandler(APP_UART_METER_HANDLE);
  /* USER CODE BEGIN USART2_IRQn 1 */

  /* USER CODE END USART2_IRQn 1 */
}


void RNG_LPUART1_IRQHandler(void)
{
  /* USER CODE BEGIN RNG_LPUART1_IRQn 0 */

  /* USER CODE END RNG_LPUART1_IRQn 0 */
  HAL_UART_IRQHandler(APP_UART_NBIOT_HANDLE);
  /* USER CODE BEGIN RNG_LPUART1_IRQn 1 */

  /* USER CODE END RNG_LPUART1_IRQn 1 */
}

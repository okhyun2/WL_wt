#ifndef APP_BUILD_CONFIG_H
#define APP_BUILD_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @file    app_build_config.h
 * @brief   Application-wide build configuration and constant definitions.
 */

/** @brief Application name string. */
#define APP_NAME_STRING                     "WaterLink mode d"

/** @brief Firmware semantic version major. */
#define APP_FW_VERSION_MAJOR                (0u)
/** @brief Firmware semantic version minor. */
#define APP_FW_VERSION_MINOR                (2u)
/** @brief Firmware semantic version patch. */
#define APP_FW_VERSION_PATCH                (0u)

/** @brief Project structure revision for Step 2 baseline. */
#define APP_PROJECT_LAYOUT_REV              (2u)

/** @brief Cooperative loop idle delay before scheduler is introduced. */
#define APP_SUPERLOOP_IDLE_DELAY_MS         (1u)

/** @brief Boot clock target: MSI range 5 = 2.097 MHz nominal. */
#define APP_CLOCK_MSI_RANGE_BOOT            RCC_MSIRANGE_5
/** @brief Expected SYSCLK after SystemClock_Config(). */
#define APP_CLOCK_SYSCLK_BOOT_HZ            (2097000u)
/** @brief Expected HCLK after SystemClock_Config(). */
#define APP_CLOCK_HCLK_BOOT_HZ              (APP_CLOCK_SYSCLK_BOOT_HZ)
/** @brief Expected APB1 clock after SystemClock_Config(). */
#define APP_CLOCK_PCLK1_BOOT_HZ             (APP_CLOCK_SYSCLK_BOOT_HZ)
/** @brief Expected APB2 clock after SystemClock_Config(). */
#define APP_CLOCK_PCLK2_BOOT_HZ             (APP_CLOCK_SYSCLK_BOOT_HZ)
/** @brief Expected Flash latency for Step 2 boot profile. */
#define APP_CLOCK_FLASH_LATENCY_BOOT        FLASH_LATENCY_0
/** @brief Nominal LSI frequency used by IWDG/RTC planning. */
#define APP_CLOCK_LSI_NOMINAL_HZ            (37000u)
/** @brief SysTick frequency. */
#define APP_CLOCK_SYSTICK_HZ                (1000u)

/** @brief Boolean style constants for portability. */
#define APP_FALSE                           (0u)
#define APP_TRUE                            (1u)

#if !defined(STM32L073xx)
#error "This project requires STM32L073xx device support."
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_BUILD_CONFIG_H */

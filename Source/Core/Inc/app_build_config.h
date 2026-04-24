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
#define APP_NAME_STRING                     "WaterLink WaterTerminal"

/** @brief Firmware semantic version major. */
#define APP_FW_VERSION_MAJOR                (0u)
/** @brief Firmware semantic version minor. */
#define APP_FW_VERSION_MINOR                (3u)
/** @brief Firmware semantic version patch. */
#define APP_FW_VERSION_PATCH                (0u)

/** @brief Project structure revision for Step 3 baseline. */
#define APP_PROJECT_LAYOUT_REV              (3u)

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
/** @brief Expected Flash latency for Step 2/3 boot profile. */
#define APP_CLOCK_FLASH_LATENCY_BOOT        FLASH_LATENCY_0
/** @brief Nominal LSI frequency used by IWDG/RTC planning. */
#define APP_CLOCK_LSI_NOMINAL_HZ            (37000u)
/** @brief SysTick frequency. */
#define APP_CLOCK_SYSTICK_HZ                (1000u)

/** @brief Debug UART transmit timeout. */
#define APP_DEBUG_UART_TIMEOUT_MS           (100u)
/** @brief Maximum command line length including terminator. */
#define APP_DEBUG_CONSOLE_RX_LINE_SIZE      (64u)
/** @brief Generic debug text buffer size. */
#define APP_DEBUG_CONSOLE_TX_BUFFER_SIZE    (192u)
/** @brief CLI prompt string. */
#define APP_DEBUG_CONSOLE_PROMPT            "wl> "
/** @brief Console line ending. */
#define APP_DEBUG_CONSOLE_EOL               "\r\n"
/** @brief Console banner string. */
#define APP_DEBUG_CONSOLE_BANNER            "WaterLink WaterTerminal Debug Console"
/** @brief Log formatting buffer size. */
#define APP_LOG_BUFFER_SIZE                 (192u)
/** @brief Hex dump bytes per line. */
#define APP_LOG_HEXDUMP_BYTES_PER_LINE      (16u)

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

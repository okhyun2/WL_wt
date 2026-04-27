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

/** @brief Boolean style constants for portability. */
#define APP_FALSE                                   (0u)
/** @brief Boolean style constants for portability. */
#define APP_TRUE                                    (1u)

/** @brief Application name string. */
#define APP_NAME_STRING                             "WaterLink WaterTerminal"

/** @brief Firmware semantic version major. */
#define APP_FW_VERSION_MAJOR                        (0u)
/** @brief Firmware semantic version minor. */
#define APP_FW_VERSION_MINOR                        (5u)
/** @brief Firmware semantic version patch. */
#define APP_FW_VERSION_PATCH                        (1u)

/** @brief Project structure revision for Step 4 expanded scheduler baseline. */
#define APP_PROJECT_LAYOUT_REV                      (6u)

/** @brief Cooperative loop idle fallback delay before the next scheduler pass. */
#define APP_SUPERLOOP_IDLE_DELAY_MS                 (1u)

/** @brief Boot clock target: MSI range 5 = 2.097 MHz nominal. */
#define APP_CLOCK_MSI_RANGE_BOOT                    RCC_MSIRANGE_5
/** @brief Expected SYSCLK after SystemClock_Config(). */
#define APP_CLOCK_SYSCLK_BOOT_HZ                    (2097000u)
/** @brief Expected HCLK after SystemClock_Config(). */
#define APP_CLOCK_HCLK_BOOT_HZ                      (APP_CLOCK_SYSCLK_BOOT_HZ)
/** @brief Expected APB1 clock after SystemClock_Config(). */
#define APP_CLOCK_PCLK1_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
/** @brief Expected APB2 clock after SystemClock_Config(). */
#define APP_CLOCK_PCLK2_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
/** @brief Expected Flash latency for boot profile. */
#define APP_CLOCK_FLASH_LATENCY_BOOT                FLASH_LATENCY_0
/** @brief Nominal LSI frequency used by IWDG/RTC planning. */
#define APP_CLOCK_LSI_NOMINAL_HZ                    (37000u)
/** @brief SysTick frequency. */
#define APP_CLOCK_SYSTICK_HZ                        (1000u)

/** @brief Debug UART transmit timeout. */
#define APP_DEBUG_UART_TIMEOUT_MS                   (100u)
/** @brief Maximum command line length including terminator. */
#define APP_DEBUG_CONSOLE_RX_LINE_SIZE              (64u)
/** @brief Generic debug text buffer size. */
#define APP_DEBUG_CONSOLE_TX_BUFFER_SIZE            (192u)
/** @brief CLI prompt string. */
#define APP_DEBUG_CONSOLE_PROMPT                    "wl> "
/** @brief Console line ending. */
#define APP_DEBUG_CONSOLE_EOL                       "\r\n"
/** @brief Console banner string. */
#define APP_DEBUG_CONSOLE_BANNER                    "WaterLink WaterTerminal Debug Console"
/** @brief Log formatting buffer size. */
#define APP_LOG_BUFFER_SIZE                         (192u)
/** @brief Hex dump bytes per line. */
#define APP_LOG_HEXDUMP_BYTES_PER_LINE              (16u)

/** @brief Current build selection: APP_TRUE for production image. */
#define APP_BUILD_IS_PRODUCTION                     (APP_FALSE)
/** @brief In production builds, disable SWD pins to minimize leakage. */
#define APP_GPIO_LP_DISABLE_SWD_IN_PRODUCTION       (APP_TRUE)

/** @brief Keep boot running even when one or more pseudo checks fail. */
#define APP_SELFTEST_FAIL_STOPS_BOOT                (APP_FALSE)
/** @brief Initial buzzer self-check beep count. */
#define APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT         (2u)
/** @brief Error buzzer beep count. */
#define APP_SELFTEST_BUZZER_ERROR_BEEP_COUNT        (3u)
/** @brief Buzzer ON time during boot confirmation. */
#define APP_SELFTEST_BUZZER_BEEP_ON_MS              (80u)
/** @brief Buzzer OFF time during boot confirmation. */
#define APP_SELFTEST_BUZZER_BEEP_OFF_MS             (50u)
/** @brief Buzzer ON time during error pattern. */
#define APP_SELFTEST_BUZZER_ERROR_ON_MS             (60u)
/** @brief Buzzer OFF time during error pattern. */
#define APP_SELFTEST_BUZZER_ERROR_OFF_MS            (60u)
/** @brief Generic PWM settling delay for piezo/watchdog tests. */
#define APP_SELFTEST_PWM_SETTLE_DELAY_MS            (20u)
/** @brief Piezo duty ratio used for audible confirmation. */
#define APP_SELFTEST_BUZZER_DUTY_PERCENT            (50u)
/** @brief ADC conversion timeout. */
#define APP_SELFTEST_ADC_TIMEOUT_MS                 (20u)
/** @brief I2C readiness trial count. */
#define APP_SELFTEST_I2C_READY_TRIALS               (2u)
/** @brief I2C readiness timeout. */
#define APP_SELFTEST_I2C_READY_TIMEOUT_MS           (20u)
/** @brief UART TX timeout used by real probe examples. */
#define APP_SELFTEST_UART_TIMEOUT_MS                (100u)
/** @brief UART RX timeout used by real probe examples. */
#define APP_SELFTEST_UART_REPLY_TIMEOUT_MS          (200u)
/** @brief Minimum demo RX length for pseudo real-probe examples. */
#define APP_SELFTEST_UART_EXPECTED_RX_MIN_LEN       (2u)
/** @brief Scratch RX buffer length for UART probe examples. */
#define APP_SELFTEST_UART_RX_BUFFER_SIZE            (32u)
/** @brief NB-IoT module boot stabilization delay. */
#define APP_SELFTEST_NBIOT_BOOT_DELAY_MS            (500u)
/** @brief NB-IoT reset release stabilization delay. */
#define APP_SELFTEST_NBIOT_RESET_RELEASE_DELAY_MS   (100u)
/** @brief External watchdog pulse duration example. */
#define APP_SELFTEST_EXTERNAL_WD_PULSE_MS           (50u)

/** @brief Enable actual meter UART probe. Keep FALSE until protocol is finalized. */
#define APP_SELFTEST_ENABLE_REAL_METER_PROBE        (APP_FALSE)
/** @brief Enable actual NB-IoT AT probe. Keep FALSE until module policy is finalized. */
#define APP_SELFTEST_ENABLE_REAL_NBIOT_PROBE        (APP_FALSE)
/** @brief Enable actual ESI I2C probe. */
#define APP_SELFTEST_ENABLE_REAL_ESI_PROBE          (APP_FALSE)
/** @brief Enable actual NFC I2C probe. */
#define APP_SELFTEST_ENABLE_REAL_NFC_PROBE          (APP_FALSE)
/** @brief Enable actual auxiliary I2C probe. */
#define APP_SELFTEST_ENABLE_REAL_AUX_PROBE          (APP_FALSE)
/** @brief Enable actual external watchdog pulse output. */
#define APP_SELFTEST_ENABLE_REAL_EXTERNAL_WD_PULSE  (APP_FALSE)

/** @brief ESI device 7-bit I2C address. Set non-zero when known. */
#define APP_SELFTEST_ESI_I2C_ADDRESS_7BIT           (0x00u)
/** @brief NFC device 7-bit I2C address. Set non-zero when known. */
#define APP_SELFTEST_NFC_I2C_ADDRESS_7BIT           (0x00u)
/** @brief Auxiliary device 7-bit I2C address. Set non-zero when known. */
#define APP_SELFTEST_AUX_I2C_ADDRESS_7BIT           (0x00u)

/** @brief Maximum number of cooperative tasks. */
#define APP_SCHEDULER_MAX_TASKS                     (16u)
/** @brief Allow immediate dispatch after task registration. */
#define APP_SCHEDULER_RUN_IMMEDIATE                 (APP_TRUE)
/** @brief Enable WFI during idle instead of simple delay. */
#define APP_SCHEDULER_USE_WFI_IDLE                  (APP_TRUE)
/** @brief Fallback idle delay when WFI idle is disabled. */
#define APP_SCHEDULER_IDLE_DELAY_MS                 (1u)

/** @brief Dispatch debug console polling every 1 ms. */
#define APP_SCHEDULER_TASK_DEBUG_PERIOD_MS          (1u)
/** @brief Refresh watchdog once per 1 second. */
#define APP_SCHEDULER_TASK_WATCHDOG_PERIOD_MS       (1000u)
/** @brief Run system housekeeping once per 100 ms. */
#define APP_SCHEDULER_TASK_HOUSEKEEPING_PERIOD_MS   (100u)
/** @brief Evaluate power-state policy once per 250 ms. */
#define APP_SCHEDULER_TASK_POWER_PERIOD_MS          (250u)
/** @brief Process storage state machine once per 200 ms. */
#define APP_SCHEDULER_TASK_STORAGE_PERIOD_MS        (200u)
/** @brief Poll meter communication state machine once per 500 ms. */
#define APP_SCHEDULER_TASK_METER_PERIOD_MS          (500u)
/** @brief Poll NFC interaction state machine once per 250 ms. */
#define APP_SCHEDULER_TASK_NFC_PERIOD_MS            (250u)
/** @brief Poll ESI state machine once per 500 ms. */
#define APP_SCHEDULER_TASK_ESI_PERIOD_MS            (500u)
/** @brief Poll AUX sensor state machine once per 1000 ms. */
#define APP_SCHEDULER_TASK_AUX_PERIOD_MS            (1000u)
/** @brief Poll NB-IoT state machine once per 500 ms. */
#define APP_SCHEDULER_TASK_NBIOT_PERIOD_MS          (500u)
/** @brief Poll server communication state machine once per 1000 ms. */
#define APP_SCHEDULER_TASK_SERVER_PERIOD_MS         (1000u)
/** @brief Poll RTC/timebase management once per 1000 ms. */
#define APP_SCHEDULER_TASK_RTC_PERIOD_MS            (1000u)

#if !defined(STM32L073xx)
#error "This project requires STM32L073xx device support."
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_BUILD_CONFIG_H */

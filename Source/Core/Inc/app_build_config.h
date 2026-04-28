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

#define APP_FALSE                                   (0u)
#define APP_TRUE                                    (1u)

#define APP_NAME_STRING                             "WaterLink WaterTerminal"

#define APP_FW_VERSION_MAJOR                        (0u)
#define APP_FW_VERSION_MINOR                        (6u)
#define APP_FW_VERSION_PATCH                        (0u)

#define APP_PROJECT_LAYOUT_REV                      (7u)
#define APP_SUPERLOOP_IDLE_DELAY_MS                 (1u)

#define APP_CLOCK_MSI_RANGE_BOOT                    RCC_MSIRANGE_5
#define APP_CLOCK_SYSCLK_BOOT_HZ                    (2097000u)
#define APP_CLOCK_HCLK_BOOT_HZ                      (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_PCLK1_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_PCLK2_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_FLASH_LATENCY_BOOT                FLASH_LATENCY_0
#define APP_CLOCK_LSI_NOMINAL_HZ                    (37000u)
#define APP_CLOCK_SYSTICK_HZ                        (1000u)

#define APP_DEBUG_UART_TIMEOUT_MS                   (100u)
#define APP_DEBUG_CONSOLE_RX_LINE_SIZE              (64u)
#define APP_DEBUG_CONSOLE_TX_BUFFER_SIZE            (192u)
#define APP_DEBUG_CONSOLE_PROMPT                    "wl> "
#define APP_DEBUG_CONSOLE_EOL                       "\r\n"
#define APP_DEBUG_CONSOLE_BANNER                    "WaterLink WaterTerminal Debug Console"
#define APP_LOG_BUFFER_SIZE                         (192u)
#define APP_LOG_HEXDUMP_BYTES_PER_LINE              (16u)

#define APP_BUILD_IS_PRODUCTION                     (APP_FALSE)
#ifdef DEBUG
#define APP_BUILD_DEBUG_ENABLED                     (APP_TRUE)
#else
#define APP_BUILD_DEBUG_ENABLED                     (APP_FALSE)
#endif

#define APP_GPIO_LP_DISABLE_SWD_IN_PRODUCTION       (APP_TRUE)

#define APP_SELFTEST_FAIL_STOPS_BOOT                (APP_FALSE)
#define APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT         (2u)
#define APP_SELFTEST_BUZZER_ERROR_BEEP_COUNT        (3u)
#define APP_SELFTEST_BUZZER_BEEP_ON_MS              (80u)
#define APP_SELFTEST_BUZZER_BEEP_OFF_MS             (50u)
#define APP_SELFTEST_BUZZER_ERROR_ON_MS             (60u)
#define APP_SELFTEST_BUZZER_ERROR_OFF_MS            (60u)
#define APP_SELFTEST_PWM_SETTLE_DELAY_MS            (20u)
#define APP_SELFTEST_BUZZER_DUTY_PERCENT            (50u)
#define APP_SELFTEST_ADC_TIMEOUT_MS                 (20u)
#define APP_SELFTEST_I2C_READY_TRIALS               (2u)
#define APP_SELFTEST_I2C_READY_TIMEOUT_MS           (20u)
#define APP_SELFTEST_UART_TIMEOUT_MS                (100u)
#define APP_SELFTEST_UART_REPLY_TIMEOUT_MS          (200u)
#define APP_SELFTEST_UART_EXPECTED_RX_MIN_LEN       (2u)
#define APP_SELFTEST_UART_RX_BUFFER_SIZE            (32u)
#define APP_SELFTEST_NBIOT_BOOT_DELAY_MS            (500u)
#define APP_SELFTEST_NBIOT_RESET_RELEASE_DELAY_MS   (100u)
#define APP_SELFTEST_EXTERNAL_WD_PULSE_MS           (50u)

#define APP_SELFTEST_ENABLE_REAL_METER_PROBE        (APP_FALSE)
#define APP_SELFTEST_ENABLE_REAL_NBIOT_PROBE        (APP_FALSE)
#define APP_SELFTEST_ENABLE_REAL_ESI_PROBE          (APP_FALSE)
#define APP_SELFTEST_ENABLE_REAL_NFC_PROBE          (APP_FALSE)
#define APP_SELFTEST_ENABLE_REAL_AUX_PROBE          (APP_FALSE)
#define APP_SELFTEST_ENABLE_REAL_EXTERNAL_WD_PULSE  (APP_FALSE)

#define APP_SELFTEST_ESI_I2C_ADDRESS_7BIT           (0x00u)
#define APP_SELFTEST_NFC_I2C_ADDRESS_7BIT           (0x00u)
#define APP_SELFTEST_AUX_I2C_ADDRESS_7BIT           (0x00u)

#define APP_SCHEDULER_MAX_TASKS                     (16u)
#define APP_SCHEDULER_RUN_IMMEDIATE                 (APP_TRUE)
#define APP_SCHEDULER_USE_WFI_IDLE                  (APP_TRUE)
#define APP_SCHEDULER_IDLE_DELAY_MS                 (1u)

#define APP_SCHEDULER_TASK_DEBUG_PERIOD_MS          (1u)
#define APP_SCHEDULER_TASK_WATCHDOG_PERIOD_MS       (10000u)
#define APP_SCHEDULER_TASK_HOUSEKEEPING_PERIOD_MS   (100u)
#define APP_SCHEDULER_TASK_POWER_PERIOD_MS          (250u)
#define APP_SCHEDULER_TASK_STORAGE_PERIOD_MS        (200u)
#define APP_SCHEDULER_TASK_METER_PERIOD_MS          (500u)
#define APP_SCHEDULER_TASK_NFC_PERIOD_MS            (250u)
#define APP_SCHEDULER_TASK_ESI_PERIOD_MS            (500u)
#define APP_SCHEDULER_TASK_AUX_PERIOD_MS            (1000u)
#define APP_SCHEDULER_TASK_NBIOT_PERIOD_MS          (500u)
#define APP_SCHEDULER_TASK_SERVER_PERIOD_MS         (1000u)
#define APP_SCHEDULER_TASK_RTC_PERIOD_MS            (1000u)
#define APP_SCHEDULER_TASK_MAIN_PERIOD_MS           (100u)

#define APP_RTC_LSI_ASYNC_PREDIV                    (124u)
#define APP_RTC_LSI_SYNC_PREDIV                     (295u)
#define APP_RTC_WAKEUP_PERIOD_MS                    (60 * 1000u)

#define APP_WATCHDOG_EXTERNAL_FEED_PULSE_MS         (50u)
#define APP_WATCHDOG_EXTERNAL_FEED_DUTY_PERCENT     (50u)
#define APP_WATCHDOG_EXTERNAL_FEED_BOOT_PRIME_CNT   (1u)

#define APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT          (2u)
#define APP_LP_STOP_DEBUG_DRY_RUN                   (APP_FALSE)

#define APP_MSGQ_DEPTH                              (48u)
#define APP_MSGQ_CAPACITY                           (APP_MSGQ_DEPTH)
#define APP_MSGQ_MAIN_DRAIN_PER_RUN                 (12u)
#define APP_TASK_HEARTBEAT_MIN_INTERVAL_MS          (100u)
#define APP_TASK_MAIN_STALE_FACTOR                  (3u)
#define APP_TASK_MAIN_STALE_MARGIN_MS               (100u)
#define APP_TASK_MAIN_HEARTBEAT_GRACE_MS            ((APP_TASK_MAIN_STALE_FACTOR * APP_SCHEDULER_TASK_MAIN_PERIOD_MS) + APP_TASK_MAIN_STALE_MARGIN_MS)

#define APP_STORAGE_LAYOUT_REV                      (1u)
#define APP_STORAGE_DATA_EEPROM_SLOT_COUNT          (2u)
#define APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES     (64u)
#define APP_STORAGE_FLASH_PARTITION_PAGE_COUNT      (8u)
#define APP_STORAGE_FLASH_RECORD_STRIDE_BYTES       (FLASH_PAGE_SIZE)
#define APP_STORAGE_PARAM_FLAGS_VALID               (0x00000001u)

#if !defined(STM32L073xx)
#error "This project requires STM32L073xx device support."
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_BUILD_CONFIG_H */

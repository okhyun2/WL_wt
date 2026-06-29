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

/* 128KB dual-boot flash layout */
#define APP_FLASH_TARGET_SIZE_BYTES                 (128u * 1024u)
#define APP_BOOTLOADER_BASE_ADDR                    (0x08000000u)
#define APP_BOOTLOADER_SIZE_BYTES                   (8u * 1024u)
#define APP_SLOT1_BASE_ADDR                         (0x08002000u)
#define APP_SLOT2_BASE_ADDR                         (0x08010000u)
#define APP_SLOT_SIZE_BYTES                         (56u * 1024u)
#define APP_BOOT_INFO_BASE_ADDR                     (0x0801E000u)
#define APP_DUALBOOT_TRIAL_CONFIRM_MS               (60000u)

#ifndef APP_SLOT_ID
#define APP_SLOT_ID                                 (1u)
#endif

#if (APP_SLOT_ID == 1u)
#define APP_SLOT_NAME                               "slot1"
#elif (APP_SLOT_ID == 2u)
#define APP_SLOT_NAME                               "slot2"
#else
#error "APP_SLOT_ID must be 1 or 2."
#endif

#define APP_FW_VERSION_MAJOR                        (0u)
#define APP_FW_VERSION_MINOR                        (7u)

#define APP_SUPERLOOP_IDLE_DELAY_MS                 (1u)

#define APP_DEFAULT_RTC_YEAR                        (2026)
#define APP_DEFAULT_RTC_MONTH                       (1)
#define APP_DEFAULT_RTC_DAY                         (1)
#define APP_DEFAULT_RTC_HOUR                        (0)
#define APP_DEFAULT_RTC_MIN                         (0)
#define APP_DEFAULT_RTC_SEC                         (0)

#define APP_CLOCK_MSI_RANGE_BOOT                    RCC_MSIRANGE_5
#define APP_CLOCK_SYSCLK_BOOT_HZ                    (2097000u)
#define APP_CLOCK_HCLK_BOOT_HZ                      (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_PCLK1_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_PCLK2_BOOT_HZ                     (APP_CLOCK_SYSCLK_BOOT_HZ)
#define APP_CLOCK_FLASH_LATENCY_BOOT                FLASH_LATENCY_0
#define APP_CLOCK_LSE_NOMINAL_HZ                    (32768u)
#define APP_CLOCK_LSE_DRIVE                         RCC_LSEDRIVE_LOW
#define APP_CLOCK_LSI_NOMINAL_HZ                    (APP_CLOCK_LSE_NOMINAL_HZ)
#define APP_CLOCK_SYSTICK_HZ                        (1000u)

#define APP_DEBUG_UART_TIMEOUT_MS                   (100u)
#define APP_DEBUG_CONSOLE_RX_LINE_SIZE              (64u)
#define APP_DEBUG_CONSOLE_TX_BUFFER_SIZE            (192u)
#define APP_DEBUG_CONSOLE_PROMPT                    "wl> "
#define APP_DEBUG_CONSOLE_EOL                       "\r\n"
#define APP_DEBUG_CONSOLE_BANNER                    "WaterLink WaterTerminal Debug Console"
#define APP_LOG_BUFFER_SIZE                         (192u)
#define APP_LOG_HEXDUMP_BYTES_PER_LINE              (16u)

#define APP_BUILD_IS_PRODUCTION                     (APP_FALSE) //if true. change swd&debug pin -> analog input.(=disable swd&debug).
#ifdef DEBUG
#define APP_BUILD_DEBUG_ENABLED                     (APP_TRUE) //org
#else
//#define APP_BUILD_DEBUG_ENABLED                     (APP_FALSE) //org
#define APP_BUILD_DEBUG_ENABLED                     (APP_TRUE)
#endif

#if (APP_BUILD_IS_PRODUCTION == APP_TRUE)
#define APP_BUILD_CLI_ENABLED                       (APP_FALSE)
#else
#define APP_BUILD_CLI_ENABLED                       (APP_BUILD_DEBUG_ENABLED)
#endif

#define SUPPORT_EEPROM
#undef SUPPORT_FALASH

#define APP_SELFTEST_FAIL_STOPS_BOOT                (APP_FALSE)
#define APP_SELFTEST_BUZZER_BOOT_BEEP_COUNT         (2u)
#define APP_SELFTEST_BUZZER_ERROR_BEEP_COUNT        (3u)
#define APP_SELFTEST_BUZZER_BEEP_ON_MS              (80u)
#define APP_SELFTEST_BUZZER_BEEP_OFF_MS             (50u)
#define APP_SELFTEST_BUZZER_ERROR_ON_MS             (60u)
#define APP_SELFTEST_BUZZER_ERROR_OFF_MS            (60u)
#define APP_SELFTEST_I2C_READY_TRIALS               (2u)
#define APP_SELFTEST_I2C_READY_TIMEOUT_MS           (20u)
#define APP_SELFTEST_UART_TIMEOUT_MS                (100u)
#define APP_SELFTEST_UART_REPLY_METER_NORMAL_TIMEOUT_MS          (400u)
#define APP_SELFTEST_UART_REPLY_METER_SC1xxx_TIMEOUT_MS          (800u)
#define APP_SELFTEST_UART_METER_NORMAL_EXPECTED_RX_MIN_LEN (21u)
#define APP_SELFTEST_UART_METER_SC1xxx_EXPECTED_RX_MIN_LEN (15u)
#define APP_SELFTEST_UART_RX_BUFFER_SIZE            (32u)

#define APP_SELFTEST_NFC_I2C_ADDRESS_7BIT           (0x54u)
#define APP_SELFTEST_AUX_I2C_ADDRESS_7BIT           (0x70u)

/* NFC production hardening */
#define APP_NFC_MASTER_KEY_BYTES                      { 0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6, 0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C }
#define APP_NFC_ADMIN_KEY_BYTES                       { 0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE, 0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81 }
#define APP_NFC_TEMP_THRESHOLD_MIN_X10               (-400)
#define APP_NFC_TEMP_THRESHOLD_MAX_X10               (1250)
#define APP_NFC_REPORT_INTERVAL_MIN_SEC              (10u)
#define APP_NFC_REPORT_INTERVAL_MAX_SEC              (86400u)
#define APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE      (0u) //0:disable, 1:enable
#define APP_NFC_TEST_MODE_REFRESH_MS                (100u)

#define APP_FSM_USE_WFI_IDLE                        (APP_TRUE)
#define APP_FSM_IDLE_DELAY_MS                       (1u)
#define APP_FSM_BOOT_RESET_HOLD_MS                  (1000u)
#define APP_FSM_DEBUG_POLL_PERIOD_MS                (1u)
#define APP_FSM_HOUSEKEEPING_PERIOD_MS              (500u)
#define APP_FSM_METER_PERIOD_MS                     (500u)
#if (APP_NFC_TEST_MODE_FIELD_REFRESH_ENABLE == 1u)
#define APP_FSM_NFC_PERIOD_MS                       (APP_NFC_TEST_MODE_REFRESH_MS)
#else
#define APP_FSM_NFC_PERIOD_MS                       (500u)
#endif
#define APP_FSM_AUX_PERIOD_MS                       (1000u)
#define APP_FSM_NBIOT_PERIOD_MS                     (500u)
#define APP_FSM_SERVER_PERIOD_MS                    (1000u)
#define APP_FSM_RTC_PERIOD_MS                       (1000u)
#define APP_FSM_LPTIM_PERIOD_MS                     (1000u)
#define APP_FSM_WATCHDOG_PERIOD_MS                  (1000u)
#define APP_FSM_STORAGE_PERIOD_MS                   (500u)

#define APP_RTC_LSE_ASYNC_PREDIV                    (127u)
#define APP_RTC_LSE_SYNC_PREDIV                     (255u)
#define APP_RTC_LSI_ASYNC_PREDIV                    (APP_RTC_LSE_ASYNC_PREDIV)
#define APP_RTC_LSI_SYNC_PREDIV                     (APP_RTC_LSE_SYNC_PREDIV)
//#define APP_RTC_WAKEUP_PERIOD_MS                    (60*60*1000u) //0:don't stop
//#define APP_RTC_WAKEUP_PERIOD_MS                    (10*1000u) //0:don't stop
#define APP_RTC_WAKEUP_PERIOD_MS                    (0u) //0:don't stop

#define APP_WATCHDOG_EXTERNAL_FEED_PULSE_MS         (50u)
#define APP_WATCHDOG_EXTERNAL_FEED_DUTY_PERCENT     (50u)
#define APP_WATCHDOG_EXTERNAL_FEED_BOOT_PRIME_CNT   (1u)

#define APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT          (2u)
#define APP_LP_STOP_DEBUG_DRY_RUN                   (APP_FALSE)

#define APP_MSGQ_DEPTH                              (16u)
#define APP_MSGQ_CAPACITY                           (APP_MSGQ_DEPTH)

#define APP_STORAGE_LAYOUT_REV                      (1u)
#define APP_STORAGE_DATA_EEPROM_SLOT_COUNT          (2u)
#define APP_STORAGE_DATA_EEPROM_SLOT_SIZE_BYTES     (64u)
#define APP_STORAGE_FLASH_PARTITION_PAGE_COUNT      (8u)
#define APP_STORAGE_FLASH_RECORD_STRIDE_BYTES       (FLASH_PAGE_SIZE)
#define APP_STORAGE_PARAM_FLAGS_VALID               (0x00000001u)

#define APP_STORAGE_EEPROM_TOTAL_SIZE_BYTES             (6u * 1024u)
//BANK1 size 3Kbyte(0xC00)
#define APP_STORAGE_CONFIG_EEPROM_OFFSET_BYTES          (0)
#define APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES            (1u * 1024u)
#define APP_STORAGE_METER_OPTION_EEPROM_OFFSET_BYTES    (APP_STORAGE_CONFIG_EEPROM_OFFSET_BYTES + APP_STORAGE_CONFIG_EEPROM_SIZE_BYTES)
#define APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES      (1u * 1024u)
#define APP_STORAGE_RESERVED_EEPROM_OFFSET_BYTES        (APP_STORAGE_METER_OPTION_EEPROM_OFFSET_BYTES + APP_STORAGE_METER_OPTION_EEPROM_SIZE_BYTES)
#define APP_STORAGE_RESERVED_EEPROM_SIZE_BYTES          (1u * 1024u)
//BANK2 size 3Kbyte(0xC00) - meter data save by circular buffer
#define APP_STORAGE_METER_DATA_EEPROM_OFFSET_BYTES      (3u * 1024u)
#define APP_STORAGE_METER_DATA_EEPROM_SIZE_BYTES        (3u * 1024u)

#define APP_STORAGE_CONFIG_SLOT_COUNT                           (4u)
#define APP_STORAGE_OPTION_SLOT_COUNT                           (4u)

#if !defined(STM32L073xx)
#error "This project requires STM32L073xx device support."
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_BUILD_CONFIG_H */

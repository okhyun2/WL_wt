#ifndef APP_TASK_STATE_DEFS_H
#define APP_TASK_STATE_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Legacy per-module state definitions
 * ========================================================================= */
#define APP_TASK_DEBUG_STATE_INIT                          (0u)
#define APP_TASK_DEBUG_STATE_POLL                          (1u)

#define APP_TASK_WATCHDOG_STATE_INIT                       (0u)
#define APP_TASK_WATCHDOG_STATE_FEED_EXTERNAL              (1u)

#define APP_TASK_HOUSEKEEPING_STATE_INIT                   (0u)
#define APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT               (1u)
#define APP_TASK_HOUSEKEEPING_STATE_ROTATE                 (2u)

#define APP_TASK_POWER_STATE_INIT                          (0u)
#define APP_TASK_POWER_STATE_EVALUATE                      (1u)
#define APP_TASK_POWER_STATE_DECIDE_IDLE                   (2u)
#define APP_TASK_POWER_STATE_WAIT_REQUEST                  (3u)

#define APP_TASK_STORAGE_STATE_INIT                        (0u)
#define APP_TASK_STORAGE_STATE_SCAN_QUEUE                  (1u)
#define APP_TASK_STORAGE_STATE_COMMIT_ONE                  (2u)

#define APP_TASK_METER_STATE_INIT                          (0u)
#define APP_TASK_METER_STATE_WAIT_TRIGGER                  (1u)
#define APP_TASK_METER_STATE_SEND_REQUEST                  (2u)
#define APP_TASK_METER_STATE_PARSE_REPLY                   (3u)

#define APP_TASK_NFC_STATE_INIT                            (0u)
#define APP_TASK_NFC_STATE_WAIT_EVENT                      (1u)
#define APP_TASK_NFC_STATE_EXCHANGE                        (2u)

#define APP_TASK_AUX_STATE_INIT                            (0u)
#define APP_TASK_AUX_STATE_TRIGGER_MEASURE                 (1u)
#define APP_TASK_AUX_STATE_READ_RESULT                     (2u)

#define APP_TASK_NBIOT_STATE_INIT                          (0u)
#define APP_TASK_NBIOT_STATE_DECIDE_WAKE                   (1u)
#define APP_TASK_NBIOT_STATE_POWER_ON                      (2u)
#define APP_TASK_NBIOT_STATE_EXCHANGE_AT                   (3u)

#define APP_TASK_SERVER_STATE_INIT                         (0u)
#define APP_TASK_SERVER_STATE_PREPARE_PACKET               (1u)
#define APP_TASK_SERVER_STATE_REQUEST_SEND                 (2u)

#define APP_TASK_RTC_STATE_INIT                            (0u)
#define APP_TASK_RTC_STATE_CHECK_SCHEDULE                  (1u)
#define APP_TASK_RTC_STATE_APPLY_SYNC                      (2u)

/* =========================================================================
 * Unified main state-machine states
 * ========================================================================= */
#define APP_TASK_MAIN_STATE_INIT                           (0u)
#define APP_TASK_MAIN_STATE_DISPATCH                       (1u)
#define APP_TASK_MAIN_STATE_BOOT                           (2u)
#define APP_TASK_MAIN_STATE_IDLE                           (3u)
#define APP_TASK_MAIN_STATE_DEBUG_POLL                     (4u)
#define APP_TASK_MAIN_STATE_HOUSEKEEPING_INIT              (5u)
#define APP_TASK_MAIN_STATE_HOUSEKEEPING_SNAPSHOT          (6u)
#define APP_TASK_MAIN_STATE_HOUSEKEEPING_ROTATE            (7u)
#define APP_TASK_MAIN_STATE_POWER_INIT                     (8u)
#define APP_TASK_MAIN_STATE_POWER_WAIT_REQUEST             (9u)
#define APP_TASK_MAIN_STATE_METER_INIT                     (10u)
#define APP_TASK_MAIN_STATE_METER_WAIT_TRIGGER             (11u)
#define APP_TASK_MAIN_STATE_METER_SEND_REQUEST             (12u)
#define APP_TASK_MAIN_STATE_METER_PARSE_REPLY              (13u)
#define APP_TASK_MAIN_STATE_NFC_INIT                       (14u)
#define APP_TASK_MAIN_STATE_NFC_WAIT_EVENT                 (15u)
#define APP_TASK_MAIN_STATE_NFC_EXCHANGE                   (16u)
#define APP_TASK_MAIN_STATE_AUX_INIT                       (17u)
#define APP_TASK_MAIN_STATE_AUX_TRIGGER_MEASURE            (18u)
#define APP_TASK_MAIN_STATE_AUX_READ_RESULT                (19u)
#define APP_TASK_MAIN_STATE_NBIOT_INIT                     (20u)
#define APP_TASK_MAIN_STATE_NBIOT_DECIDE_WAKE              (21u)
#define APP_TASK_MAIN_STATE_NBIOT_POWER_ON                 (22u)
#define APP_TASK_MAIN_STATE_NBIOT_EXCHANGE_AT              (23u)
#define APP_TASK_MAIN_STATE_SERVER_INIT                    (24u)
#define APP_TASK_MAIN_STATE_SERVER_PREPARE_PACKET          (25u)
#define APP_TASK_MAIN_STATE_SERVER_REQUEST_SEND            (26u)
#define APP_TASK_MAIN_STATE_RTC_INIT                       (27u)
#define APP_TASK_MAIN_STATE_RTC_CHECK_SCHEDULE             (28u)
#define APP_TASK_MAIN_STATE_RTC_APPLY_SYNC                 (29u)
#define APP_TASK_MAIN_STATE_FAULT                          (30u)

#ifdef __cplusplus
}
#endif

#endif /* APP_TASK_STATE_DEFS_H */

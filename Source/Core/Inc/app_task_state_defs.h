#ifndef APP_TASK_STATE_DEFS_H
#define APP_TASK_STATE_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Debug Task
 * ========================================================================= */
#define APP_TASK_DEBUG_STATE_INIT                 (0u)
#define APP_TASK_DEBUG_STATE_POLL                 (1u)

/* =========================================================================
 * Watchdog Task
 * ========================================================================= */
#define APP_TASK_WATCHDOG_STATE_INIT              (0u)
#define APP_TASK_WATCHDOG_STATE_FEED_EXTERNAL     (1u)

/* =========================================================================
 * Housekeeping Task
 * ========================================================================= */
#define APP_TASK_HOUSEKEEPING_STATE_INIT          (0u)
#define APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT      (1u)
#define APP_TASK_HOUSEKEEPING_STATE_ROTATE        (2u)

/* =========================================================================
 * Power Task
 * ========================================================================= */
#define APP_TASK_POWER_STATE_INIT                 (0u)
#define APP_TASK_POWER_STATE_EVALUATE             (1u)
#define APP_TASK_POWER_STATE_DECIDE_IDLE          (2u)

/* =========================================================================
 * Storage Task
 * ========================================================================= */
#define APP_TASK_STORAGE_STATE_INIT               (0u)
#define APP_TASK_STORAGE_STATE_SCAN_QUEUE         (1u)
#define APP_TASK_STORAGE_STATE_COMMIT_ONE         (2u)

/* =========================================================================
 * Meter Task
 * ========================================================================= */
#define APP_TASK_METER_STATE_INIT                 (0u)
#define APP_TASK_METER_STATE_WAIT_TRIGGER         (1u)
#define APP_TASK_METER_STATE_SEND_REQUEST         (2u)
#define APP_TASK_METER_STATE_PARSE_REPLY          (3u)

/* =========================================================================
 * NFC Task
 * ========================================================================= */
#define APP_TASK_NFC_STATE_INIT                   (0u)
#define APP_TASK_NFC_STATE_WAIT_EVENT             (1u)
#define APP_TASK_NFC_STATE_EXCHANGE               (2u)

/* =========================================================================
 * ESI Task
 * ========================================================================= */
#define APP_TASK_ESI_STATE_INIT                   (0u)
#define APP_TASK_ESI_STATE_WAIT_INTERRUPT         (1u)
#define APP_TASK_ESI_STATE_READ_COEFFICIENT       (2u)

/* =========================================================================
 * AUX Task
 * ========================================================================= */
#define APP_TASK_AUX_STATE_INIT                   (0u)
#define APP_TASK_AUX_STATE_TRIGGER_MEASURE        (1u)
#define APP_TASK_AUX_STATE_READ_RESULT            (2u)

/* =========================================================================
 * NB-IoT Task
 * ========================================================================= */
#define APP_TASK_NBIOT_STATE_INIT                 (0u)
#define APP_TASK_NBIOT_STATE_DECIDE_WAKE          (1u)
#define APP_TASK_NBIOT_STATE_POWER_ON             (2u)
#define APP_TASK_NBIOT_STATE_EXCHANGE_AT          (3u)

/* =========================================================================
 * Server Task
 * ========================================================================= */
#define APP_TASK_SERVER_STATE_INIT                (0u)
#define APP_TASK_SERVER_STATE_PREPARE_PACKET      (1u)
#define APP_TASK_SERVER_STATE_REQUEST_SEND        (2u)

/* =========================================================================
 * RTC Task
 * ========================================================================= */
#define APP_TASK_RTC_STATE_INIT                   (0u)
#define APP_TASK_RTC_STATE_CHECK_SCHEDULE         (1u)
#define APP_TASK_RTC_STATE_APPLY_SYNC             (2u)

/* =========================================================================
 * Main Task
 * ========================================================================= */
#define APP_TASK_MAIN_STATE_INIT                  (0u)
#define APP_TASK_MAIN_STATE_COLLECT               (1u)
#define APP_TASK_MAIN_STATE_EVALUATE              (2u)
#define APP_TASK_MAIN_STATE_DECIDE                (3u)

#ifdef __cplusplus
}
#endif

#endif /* APP_TASK_STATE_DEFS_H */

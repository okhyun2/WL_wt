#ifndef APP_TASK_STATE_DEFS_H
#define APP_TASK_STATE_DEFS_H

#ifdef __cplusplus
extern "C" {
#endif

    /* =========================================================================
     * Debug Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_DEBUG_STATE_INIT = 0,
        APP_TASK_DEBUG_STATE_POLL
    } AppTaskDebugCmd_t;

    /* =========================================================================
     * Watchdog Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_WATCHDOG_STATE_INIT = 0,
        APP_TASK_WATCHDOG_STATE_REFRESH
    } AppTaskWatchDogCmd_t;

    /* =========================================================================
     * Housekeeping Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_HOUSEKEEPING_STATE_INIT = 0,
        APP_TASK_HOUSEKEEPING_STATE_SNAPSHOT,
        APP_TASK_HOUSEKEEPING_STATE_ROTATE
    } AppTaskHouseKeepingCmd_t;

    /* =========================================================================
     * Power Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_POWER_STATE_INIT = 0,
        APP_TASK_POWER_STATE_EVALUATE,
        APP_TASK_POWER_STATE_DECIDE_IDLE
    } AppTaskPowerCmd_t;

    /* =========================================================================
     * Storage Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_STORAGE_STATE_INIT = 0,
        APP_TASK_STORAGE_STATE_SCAN_QUEUE,
        APP_TASK_STORAGE_STATE_COMMIT_ONE
    } AppTaskStorageCmd_t;

    /* =========================================================================
     * Meter Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_METER_STATE_INIT = 0,
        APP_TASK_METER_STATE_WAIT_TRIGGER,
        APP_TASK_METER_STATE_SEND_REQUEST,
        APP_TASK_METER_STATE_PARSE_REPLY
    } AppTaskMeterCmd_t;

    /* =========================================================================
     * NFC Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_NFC_STATE_INIT = 0,
        APP_TASK_NFC_STATE_WAIT_EVENT,
        APP_TASK_NFC_STATE_EXCHANGE
    } AppTaskNFCCmd_t;

    /* =========================================================================
     * ESI Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_ESI_STATE_INIT = 0,
        APP_TASK_ESI_STATE_WAIT_INTERRUPT,
        APP_TASK_ESI_STATE_READ_COEFFICIENT
    } AppTaskESICmd_t;

    /* =========================================================================
     * AUX Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_AUX_STATE_INIT = 0,
        APP_TASK_AUX_STATE_TRIGGER_MEASURE,
        APP_TASK_AUX_STATE_READ_RESULT
    } AppTaskAUXCmd_t;

    /* =========================================================================
     * NB-IoT Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_NBIOT_STATE_INIT = 0,
        APP_TASK_NBIOT_STATE_DECIDE_WAKE,
        APP_TASK_NBIOT_STATE_POWER_ON,
        APP_TASK_NBIOT_STATE_EXCHANGE_AT
    } AppTaskNBIOTCmd_t;

    /* =========================================================================
     * Server Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_SERVER_STATE_INIT = 0,
        APP_TASK_SERVER_STATE_PREPARE_PACKET,
        APP_TASK_SERVER_STATE_REQUEST_SEND
    } AppTaskServerCmd_t;

    /* =========================================================================
     * RTC Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_RTC_STATE_INIT = 0,
        APP_TASK_RTC_STATE_CHECK_SCHEDULE,
        APP_TASK_RTC_STATE_APPLY_SYNC
    } AppTaskRTCCmd_t;

    /* =========================================================================
     * Main Task
     * ========================================================================= */
    typedef enum
    {
        APP_TASK_MAIN_STATE_INIT = 0,
        APP_TASK_MAIN_STATE_COLLECT,
        APP_TASK_MAIN_STATE_EVALUATE,
        APP_TASK_MAIN_STATE_DECIDE
    } AppTaskMainCmd_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_TASK_STATE_DEFS_H */


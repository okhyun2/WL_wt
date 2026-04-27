#include "app_tasks.h"

#include <string.h>

#include "app_build_config.h"
#include "app_debug.h"
#include "app_hw.h"

/**
 * @file    app_tasks.c
 * @brief   Cooperative task registry and pseudo task implementations.
 */

/** @brief Global task runtime context. */
static AppTasksContext_t g_appTasksContext;

/**
 * @brief Get static name for a task id.
 *
 * @param id Task identifier.
 * @return Constant task name string.
 */
const char *App_TasksGetName(AppTaskId_t id)
{
    switch (id)
    {
        case APP_TASK_ID_DEBUG:        return "debug";
        case APP_TASK_ID_WATCHDOG:     return "watchdog";
        case APP_TASK_ID_HOUSEKEEPING: return "housekeeping";
        case APP_TASK_ID_POWER:        return "power";
        case APP_TASK_ID_STORAGE:      return "storage";
        case APP_TASK_ID_METER:        return "meter";
        case APP_TASK_ID_NFC:          return "nfc";
        case APP_TASK_ID_ESI:          return "esi";
        case APP_TASK_ID_AUX:          return "aux";
        case APP_TASK_ID_NBIOT:        return "nbiot";
        case APP_TASK_ID_SERVER:       return "server";
        case APP_TASK_ID_RTC:          return "rtc";
        default:                       return "unknown";
    }
}

/**
 * @brief Mark one task run completion.
 *
 * @param p_module Module context pointer.
 * @param status Task status.
 * @return status value for convenience.
 */
static AppStatus_t App_TasksFinishRun(AppTaskModuleContext_t *p_module, AppStatus_t status)
{
    if (p_module != NULL)
    {
        p_module->initialized = APP_TRUE;
        p_module->lastStatus = status;
        p_module->lastRunTickMs = HAL_GetTick();
        p_module->runCount++;
    }

    return status;
}

/**
 * @brief Cooperative debug task.
 */
static AppStatus_t App_TaskDebug(void *p_context)
{
    AppTaskModuleContext_t *p_module;
    AppStatus_t status;

    p_module = (AppTaskModuleContext_t *)p_context;
    status = App_DebugConsoleProcess();
    return App_TasksFinishRun(p_module, status);
}

/**
 * @brief Cooperative watchdog task.
 */
static AppStatus_t App_TaskWatchdog(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_HAL_ERROR(HAL_IWDG_Refresh(APP_IWDG_HANDLE), APP_STATUS_INIT_FAILED);
    p_module->state = 1u;
    p_module->lastActionTickMs = HAL_GetTick();
    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative housekeeping task.
 */
static AppStatus_t App_TaskHousekeeping(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    /* PSEUDO:
     * 1) Aggregate counters and error snapshots.
     * 2) Rotate deferred diagnostics.
     * 3) Prepare low-priority maintenance work for later steps.
     */
    p_module->state = 1u;
    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative power-management task.
 */
static AppStatus_t App_TaskPower(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize power policy thresholds and stop-entry guards. */
            p_module->state = 1u;
            p_module->lastActionTickMs = HAL_GetTick();
            break;

        case 1u:
            /* PSEUDO: evaluate active peripherals, pending messages, and wake sources. */
            p_module->busy = APP_FALSE;
            p_module->eventPending = APP_FALSE;
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: if system is idle long enough, request STOP entry in a later power step. */
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative storage task.
 */
static AppStatus_t App_TaskStorage(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: load parameter blocks from EEPROM / Flash and validate CRC/version. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: inspect queued save requests from NFC / server / meter modules. */
            p_module->eventPending = APP_FALSE;
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: commit one pending record per run to keep blocking time short. */
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative meter communication task.
 */
static AppStatus_t App_TaskMeter(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize UART2 meter protocol context and wake timing. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: decide whether periodic collection is due or reed-triggered read is pending. */
            p_module->eventPending = APP_FALSE;
            p_module->state = 2u;
            break;

        case 2u:
            /* PSEUDO: send request frame, arm receive timeout, and yield immediately. */
            p_module->busy = APP_TRUE;
            p_module->state = 3u;
            break;

        default:
            /* PSEUDO: parse received frame, validate checksum, then queue data for storage/upload. */
            p_module->busy = APP_FALSE;
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative NFC task.
 */
static AppStatus_t App_TaskNfc(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize NFC tag session state, interrupt flags, and config cache. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: detect field/event on NFC_ED and determine if exchange is required. */
            p_module->eventPending = (App_HwReadNfcEvent() == GPIO_PIN_SET) ? APP_TRUE : APP_FALSE;
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: read/write configuration blocks and raise storage update request. */
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative ESI task.
 */
static AppStatus_t App_TaskEsi(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize coefficient-meter access policy and cached registers. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: check interrupt/status line and decide whether coefficient read is required. */
            p_module->eventPending = (App_HwReadEsiInterrupt() == GPIO_PIN_SET) ? APP_TRUE : APP_FALSE;
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: read coefficient data over I2C1, validate range, and publish to storage/server. */
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative auxiliary sensor task.
 */
static AppStatus_t App_TaskAux(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize I2C3 auxiliary sensor bus and conversion parameters. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: trigger one auxiliary/temperature measurement. */
            p_module->busy = APP_TRUE;
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: read back conversion result and queue it for diagnostics/reporting. */
            p_module->busy = APP_FALSE;
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative NB-IoT modem task.
 */
static AppStatus_t App_TaskNbiot(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: prepare BC95 power state, UART context, and AT command queue. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: decide whether modem must wake for batch upload, config sync, or alarm report. */
            p_module->eventPending = APP_FALSE;
            p_module->state = 2u;
            break;

        case 2u:
            /* PSEUDO: power on modem, release reset, and wait for readiness without blocking. */
            p_module->busy = APP_TRUE;
            p_module->state = 3u;
            break;

        default:
            /* PSEUDO: exchange AT commands, enqueue packets for server task, and power off when idle. */
            p_module->busy = APP_FALSE;
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative server communication task.
 */
static AppStatus_t App_TaskServer(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize oneM2M / UDP session descriptors and retry counters. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: inspect queued uplink payloads and choose target server path. */
            p_module->eventPending = APP_FALSE;
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: frame packet, request modem send, and wait for response/result notification. */
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Cooperative RTC/timebase task.
 */
static AppStatus_t App_TaskRtc(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;

    switch (p_module->state)
    {
        case 0u:
            /* PSEUDO: initialize RTC service state, drift tracking, and next wake schedule. */
            p_module->state = 1u;
            break;

        case 1u:
            /* PSEUDO: evaluate whether periodic wake/event timers need to be updated. */
            p_module->state = 2u;
            break;

        default:
            /* PSEUDO: apply network/NFC time synchronization and recompute next alarm. */
            p_module->state = 1u;
            break;
    }

    return App_TasksFinishRun(p_module, APP_STATUS_OK);
}

/**
 * @brief Register one task and store the scheduler handle.
 */
static AppStatus_t App_TasksRegisterOne(AppTaskId_t id,
                                        AppSchedulerTaskHandler_t handler,
                                        uint32_t periodMs)
{
    AppStatus_t status;
    AppTaskModuleContext_t *p_module;

    APP_RETURN_IF_FALSE((uint32_t)id < (uint32_t)APP_TASK_ID_COUNT, APP_STATUS_INVALID_PARAM);
    p_module = &g_appTasksContext.modules[id];

    status = App_SchedulerRegisterTask(p_module->p_name,
                                       handler,
                                       p_module,
                                       periodMs,
                                       APP_SCHEDULER_RUN_IMMEDIATE,
                                       &p_module->schedulerHandle);
    APP_RETURN_IF_FALSE(status == APP_STATUS_OK, status);

    return APP_STATUS_OK;
}

AppStatus_t App_TasksInit(void)
{
    uint32_t index;

    (void)memset(&g_appTasksContext, 0, sizeof(g_appTasksContext));

    for (index = 0u; index < (uint32_t)APP_TASK_ID_COUNT; index++)
    {
        g_appTasksContext.modules[index].id = (AppTaskId_t)index;
        g_appTasksContext.modules[index].p_name = App_TasksGetName((AppTaskId_t)index);
        g_appTasksContext.modules[index].lastStatus = APP_STATUS_NOT_INITIALIZED;
        g_appTasksContext.modules[index].schedulerHandle = APP_SCHEDULER_TASK_HANDLE_INVALID;
    }

    g_appTasksContext.initialized = APP_TRUE;

    return APP_STATUS_OK;
}

AppStatus_t App_TasksRegisterAll(void)
{
    APP_RETURN_IF_FALSE(g_appTasksContext.initialized == APP_TRUE, APP_STATUS_NOT_INITIALIZED);

    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_DEBUG, App_TaskDebug, APP_SCHEDULER_TASK_DEBUG_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_WATCHDOG, App_TaskWatchdog, APP_SCHEDULER_TASK_WATCHDOG_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_HOUSEKEEPING, App_TaskHousekeeping, APP_SCHEDULER_TASK_HOUSEKEEPING_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_POWER, App_TaskPower, APP_SCHEDULER_TASK_POWER_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_STORAGE, App_TaskStorage, APP_SCHEDULER_TASK_STORAGE_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_METER, App_TaskMeter, APP_SCHEDULER_TASK_METER_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_NFC, App_TaskNfc, APP_SCHEDULER_TASK_NFC_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_ESI, App_TaskEsi, APP_SCHEDULER_TASK_ESI_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_AUX, App_TaskAux, APP_SCHEDULER_TASK_AUX_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_NBIOT, App_TaskNbiot, APP_SCHEDULER_TASK_NBIOT_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_SERVER, App_TaskServer, APP_SCHEDULER_TASK_SERVER_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);
    APP_RETURN_IF_FALSE(App_TasksRegisterOne(APP_TASK_ID_RTC, App_TaskRtc, APP_SCHEDULER_TASK_RTC_PERIOD_MS) == APP_STATUS_OK,
                        APP_STATUS_SCHEDULER_INIT_FAILED);

    return APP_STATUS_OK;
}

const AppTasksContext_t *App_TasksGetContext(void)
{
    return &g_appTasksContext;
}

const AppTaskModuleContext_t *App_TasksGetModuleContext(AppTaskId_t id)
{
    if ((uint32_t)id >= (uint32_t)APP_TASK_ID_COUNT)
    {
        return NULL;
    }

    return &g_appTasksContext.modules[id];
}

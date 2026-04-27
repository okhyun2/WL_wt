#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"

static AppStatus_t App_TaskMeterIf_InitProtocol(void)
{
    /* PSEUDO external interface:
     * - meter UART2 protocol context init
     * - wake/sleep timing config
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskMeterIf_SendReadFrame(void)
{
    /* PSEUDO external interface:
     * - HAL_UART_Transmit(APP_UART_METER_HANDLE, ...)
     * - meter wake pulse / request frame transmit
     * - timeout arm for receive phase
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskMeterIf_ParseReply(void)
{
    /* PSEUDO external interface:
     * - HAL_UART_Receive(APP_UART_METER_HANDLE, ...)
     * - checksum / frame parser
     * - publish read result to storage/server queues
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskMeter(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_METER_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskMeterIf_InitProtocol() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_module, APP_TASK_METER_STATE_WAIT_TRIGGER);
            break;

        case APP_TASK_METER_STATE_WAIT_TRIGGER:
            /* PSEUDO: periodic read schedule or pulse/event driven meter request. */
            p_module->eventPending = APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_METER_STATE_SEND_REQUEST);
            break;

        case APP_TASK_METER_STATE_SEND_REQUEST:
            APP_RETURN_IF_FALSE(App_TaskMeterIf_SendReadFrame() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_TRUE;
            APP_TASK_SET_STATE(p_module, APP_TASK_METER_STATE_PARSE_REPLY);
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskMeterIf_ParseReply() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_METER_STATE_WAIT_TRIGGER);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

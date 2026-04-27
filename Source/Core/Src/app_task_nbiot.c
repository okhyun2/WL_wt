#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"
#include "app_system.h"

static AppStatus_t App_TaskNbiotIf_InitContext(void)
{
    /* PSEUDO external interface:
     * - BC95 AT state machine init
     * - modem power policy init
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskNbiotIf_PowerOn(void)
{
    /* PSEUDO external interface:
     * - App_HwSetNbiotEnable(GPIO_PIN_SET)
     * - App_HwSetNbiotReset(GPIO_PIN_SET)
     * - App_SystemSetNbiotPowered(APP_TRUE)
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskNbiotIf_RunAtSession(void)
{
    /* PSEUDO external interface:
     * - HAL_UART_Transmit(APP_UART_NBIOT_HANDLE, ...)
     * - HAL_UART_Receive(APP_UART_NBIOT_HANDLE, ...)
     * - parse URC / AT responses / attach state
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskNbiot(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_NBIOT_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskNbiotIf_InitContext() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->state = APP_TASK_NBIOT_STATE_DECIDE_WAKE;
            break;

        case APP_TASK_NBIOT_STATE_DECIDE_WAKE:
            /* PSEUDO: decide upload/config/alarm requirement. */
            p_module->eventPending = APP_FALSE;
            p_module->state = APP_TASK_NBIOT_STATE_POWER_ON;
            break;

        case APP_TASK_NBIOT_STATE_POWER_ON:
            APP_RETURN_IF_FALSE(App_TaskNbiotIf_PowerOn() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_TRUE;
            p_module->state = APP_TASK_NBIOT_STATE_EXCHANGE_AT;
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskNbiotIf_RunAtSession() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->state = APP_TASK_NBIOT_STATE_DECIDE_WAKE;
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

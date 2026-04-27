#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"

static AppStatus_t App_TaskEsiIf_InitContext(void)
{
    /* PSEUDO external interface:
     * - coefficient cache init
     * - register map / scaling configuration
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskEsiIf_ReadCoefficient(void)
{
    /* PSEUDO external interface:
     * - App_HwReadEsiInterrupt()
     * - HAL_I2C_Master_Transmit(APP_I2C_ESI_HANDLE, ...)
     * - HAL_I2C_Master_Receive(APP_I2C_ESI_HANDLE, ...)
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskEsi(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_ESI_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskEsiIf_InitContext() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->state = APP_TASK_ESI_STATE_WAIT_INTERRUPT;
            break;

        case APP_TASK_ESI_STATE_WAIT_INTERRUPT:
            p_module->eventPending = (App_HwReadEsiInterrupt() == GPIO_PIN_SET) ? APP_TRUE : APP_FALSE;
            p_module->state = APP_TASK_ESI_STATE_READ_COEFFICIENT;
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskEsiIf_ReadCoefficient() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->state = APP_TASK_ESI_STATE_WAIT_INTERRUPT;
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

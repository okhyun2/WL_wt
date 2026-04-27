#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"

#define APP_TASK_AUX_STATE_INIT                 (0u)
#define APP_TASK_AUX_STATE_TRIGGER_MEASURE      (1u)
#define APP_TASK_AUX_STATE_READ_RESULT          (2u)

static AppStatus_t App_TaskAuxIf_InitBus(void)
{
    /* PSEUDO external interface:
     * - I2C3 device context init
     * - temperature/aux conversion config
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskAuxIf_TriggerMeasure(void)
{
    /* PSEUDO external interface:
     * - HAL_I2C_Master_Transmit(APP_I2C_AUX_HANDLE, ...)
     * - sensor conversion trigger command
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskAuxIf_ReadResult(void)
{
    /* PSEUDO external interface:
     * - HAL_I2C_Master_Receive(APP_I2C_AUX_HANDLE, ...)
     * - temperature/aux sample decode
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskAux(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_AUX_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskAuxIf_InitBus() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_module, APP_TASK_AUX_STATE_TRIGGER_MEASURE);
            break;

        case APP_TASK_AUX_STATE_TRIGGER_MEASURE:
            APP_RETURN_IF_FALSE(App_TaskAuxIf_TriggerMeasure() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_TRUE;
            APP_TASK_SET_STATE(p_module, APP_TASK_AUX_STATE_READ_RESULT);
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskAuxIf_ReadResult() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_AUX_STATE_TRIGGER_MEASURE);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

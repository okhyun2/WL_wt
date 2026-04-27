#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"
#include "app_hw.h"

static AppStatus_t App_TaskNfcIf_InitSession(void)
{
    /* PSEUDO external interface:
     * - NFC session/cache init
     * - tag memory map setup
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskNfcIf_ExchangeBlocks(void)
{
    /* PSEUDO external interface:
     * - App_HwReadNfcEvent()
     * - HAL_I2C_Master_Transmit(APP_I2C_NFC_HANDLE, ...)
     * - HAL_I2C_Master_Receive(APP_I2C_NFC_HANDLE, ...)
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskNfc(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_NFC_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskNfcIf_InitSession() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            APP_TASK_SET_STATE(p_module, APP_TASK_NFC_STATE_WAIT_EVENT);
            break;

        case APP_TASK_NFC_STATE_WAIT_EVENT:
            p_module->eventPending = (App_HwReadNfcEvent() == GPIO_PIN_SET) ? APP_TRUE : APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_NFC_STATE_EXCHANGE);
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskNfcIf_ExchangeBlocks() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            APP_TASK_SET_STATE(p_module, APP_TASK_NFC_STATE_WAIT_EVENT);
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

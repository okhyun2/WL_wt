#include "app_tasks.h"
#include "app_task_state_defs.h"

#include "app_build_config.h"

static AppStatus_t App_TaskServerIf_InitSession(void)
{
    /* PSEUDO external interface:
     * - oneM2M/UDP endpoint config
     * - retry/backoff initialization
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskServerIf_PreparePacket(void)
{
    /* PSEUDO external interface:
     * - pull payloads from storage/meter/esi/aux queues
     * - build oneM2M/UDP application packet
     */
    return APP_STATUS_OK;
}

static AppStatus_t App_TaskServerIf_RequestNbiotSend(void)
{
    /* PSEUDO external interface:
     * - hand off frame to NB-IoT task/session
     * - await send result / ACK / timeout
     */
    return APP_STATUS_OK;
}

AppStatus_t App_TaskServer(void *p_context)
{
    AppTaskModuleContext_t *p_module;

    p_module = (AppTaskModuleContext_t *)p_context;
    APP_RETURN_IF_FALSE((p_module != NULL), APP_STATUS_INVALID_PARAM);

    switch (p_module->state)
    {
        case APP_TASK_SERVER_STATE_INIT:
            APP_RETURN_IF_FALSE(App_TaskServerIf_InitSession() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->state = APP_TASK_SERVER_STATE_PREPARE_PACKET;
            break;

        case APP_TASK_SERVER_STATE_PREPARE_PACKET:
            APP_RETURN_IF_FALSE(App_TaskServerIf_PreparePacket() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->eventPending = APP_FALSE;
            p_module->state = APP_TASK_SERVER_STATE_REQUEST_SEND;
            break;

        default:
            APP_RETURN_IF_FALSE(App_TaskServerIf_RequestNbiotSend() == APP_STATUS_OK, APP_STATUS_INIT_FAILED);
            p_module->busy = APP_FALSE;
            p_module->state = APP_TASK_SERVER_STATE_PREPARE_PACKET;
            break;
    }

    return App_TasksCompleteRun(p_module, APP_STATUS_OK);
}

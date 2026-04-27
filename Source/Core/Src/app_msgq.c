#include "app_msgq.h"

#include <string.h>

#include "app_build_config.h"

static AppMsgqMessage_t g_appMsgqBuffer[APP_MSGQ_DEPTH];
static AppMsgqContext_t g_appMsgqContext;

AppStatus_t App_MsgqInit(void)
{
    (void)memset(g_appMsgqBuffer, 0, sizeof(g_appMsgqBuffer));
    (void)memset(&g_appMsgqContext, 0, sizeof(g_appMsgqContext));
    g_appMsgqContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPush(const AppMsgqMessage_t *p_message)
{
    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count >= APP_MSGQ_DEPTH)
    {
        g_appMsgqContext.overflowCount++;
        return APP_STATUS_MSGQ_FULL;
    }

    g_appMsgqBuffer[g_appMsgqContext.tail] = *p_message;
    g_appMsgqContext.tail = (uint8_t)((g_appMsgqContext.tail + 1u) % APP_MSGQ_DEPTH);
    g_appMsgqContext.count++;
    g_appMsgqContext.pushCount++;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPop(AppMsgqMessage_t *p_message)
{
    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count == 0u)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    *p_message = g_appMsgqBuffer[g_appMsgqContext.head];
    g_appMsgqContext.head = (uint8_t)((g_appMsgqContext.head + 1u) % APP_MSGQ_DEPTH);
    g_appMsgqContext.count--;
    g_appMsgqContext.popCount++;
    return APP_STATUS_OK;
}

const AppMsgqContext_t *App_MsgqGetContext(void)
{
    return &g_appMsgqContext;
}

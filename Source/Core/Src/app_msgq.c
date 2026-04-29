#include "app_msgq.h"

#include <string.h>

#include "app_build_config.h"

static AppMsgqMessage_t g_appMsgqBuffer[APP_MSGQ_DEPTH];
static AppMsgqContext_t g_appMsgqContext;

static uint8_t App_MsgqIncIndex(uint8_t index)
{
    return (uint8_t)((index + 1u) % APP_MSGQ_DEPTH);
}

static uint8_t App_MsgqDecIndex(uint8_t index)
{
    return (index == 0u) ? (uint8_t)(APP_MSGQ_DEPTH - 1u) : (uint8_t)(index - 1u);
}

AppStatus_t App_MsgqInit(void)
{
    (void)memset(g_appMsgqBuffer, 0, sizeof(g_appMsgqBuffer));
    (void)memset(&g_appMsgqContext, 0, sizeof(g_appMsgqContext));
    g_appMsgqContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPushFront(const AppMsgqMessage_t *p_message)
{
    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count >= APP_MSGQ_DEPTH)
    {
        g_appMsgqContext.overflowCount++;
        return APP_STATUS_MSGQ_FULL;
    }

    g_appMsgqContext.head = App_MsgqDecIndex(g_appMsgqContext.head);
    g_appMsgqBuffer[g_appMsgqContext.head] = *p_message;
    g_appMsgqContext.count++;
    g_appMsgqContext.pushFrontCount++;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPushBack(const AppMsgqMessage_t *p_message)
{
    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count >= APP_MSGQ_DEPTH)
    {
        g_appMsgqContext.overflowCount++;
        return APP_STATUS_MSGQ_FULL;
    }

    g_appMsgqBuffer[g_appMsgqContext.tail] = *p_message;
    g_appMsgqContext.tail = App_MsgqIncIndex(g_appMsgqContext.tail);
    g_appMsgqContext.count++;
    g_appMsgqContext.pushBackCount++;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPopFront(AppMsgqMessage_t *p_message)
{
    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count == 0u)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    *p_message = g_appMsgqBuffer[g_appMsgqContext.head];
    g_appMsgqContext.head = App_MsgqIncIndex(g_appMsgqContext.head);
    g_appMsgqContext.count--;
    g_appMsgqContext.popFrontCount++;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPopBack(AppMsgqMessage_t *p_message)
{
    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count == 0u)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    g_appMsgqContext.tail = App_MsgqDecIndex(g_appMsgqContext.tail);
    *p_message = g_appMsgqBuffer[g_appMsgqContext.tail];
    g_appMsgqContext.count--;
    g_appMsgqContext.popBackCount++;
    return APP_STATUS_OK;
}

AppStatus_t App_MsgqPush(const AppMsgqMessage_t *p_message)
{
    return App_MsgqPushBack(p_message);
}

AppStatus_t App_MsgqPop(AppMsgqMessage_t *p_message)
{
    return App_MsgqPopFront(p_message);
}

AppStatus_t App_MsgqTakeFirstByType(uint8_t type, AppMsgqMessage_t *p_message)
{
    AppMsgqMessage_t compacted[APP_MSGQ_DEPTH];
    uint8_t index;
    uint8_t readIndex;
    uint8_t keptCount;
    uint8_t found;

    APP_RETURN_IF_FALSE(g_appMsgqContext.initialized == APP_TRUE, APP_STATUS_MSGQ_NOT_INITIALIZED);
    APP_RETURN_IF_FALSE((p_message != NULL), APP_STATUS_INVALID_PARAM);

    if (g_appMsgqContext.count == 0u)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    keptCount = 0u;
    found = APP_FALSE;
    readIndex = g_appMsgqContext.head;

    for (index = 0u; index < g_appMsgqContext.count; index++)
    {
        if ((found == APP_FALSE) && (g_appMsgqBuffer[readIndex].type == type))
        {
            *p_message = g_appMsgqBuffer[readIndex];
            found = APP_TRUE;
        }
        else
        {
            compacted[keptCount] = g_appMsgqBuffer[readIndex];
            keptCount++;
        }
        readIndex = App_MsgqIncIndex(readIndex);
    }

    if (found == APP_FALSE)
    {
        return APP_STATUS_MSGQ_EMPTY;
    }

    (void)memset(g_appMsgqBuffer, 0, sizeof(g_appMsgqBuffer));
    for (index = 0u; index < keptCount; index++)
    {
        g_appMsgqBuffer[index] = compacted[index];
    }

    g_appMsgqContext.head = 0u;
    g_appMsgqContext.tail = keptCount;
    g_appMsgqContext.count = keptCount;
    g_appMsgqContext.popFrontCount++;
    return APP_STATUS_OK;
}

uint8_t App_MsgqIsEmpty(void)
{
    return (g_appMsgqContext.count == 0u) ? APP_TRUE : APP_FALSE;
}

uint8_t App_MsgqGetCount(void)
{
    return g_appMsgqContext.count;
}

const AppMsgqContext_t *App_MsgqGetContext(void)
{
    return &g_appMsgqContext;
}

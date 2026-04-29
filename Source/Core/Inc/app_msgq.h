#ifndef APP_MSGQ_H
#define APP_MSGQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

#define APP_MSGQ_TYPE_STATE_COMMAND       (1u)

typedef struct
{
    uint8_t type;
    uint8_t sourceId;
    uint8_t nextState;
    uint8_t pushFront;
    uint32_t tickMs;
    uint32_t eventParam;
    uint32_t param0;
    uint32_t param1;
    uint32_t param2;
} AppMsgqMessage_t;

typedef struct
{
    uint8_t initialized;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t pushFrontCount;
    uint32_t pushBackCount;
    uint32_t popFrontCount;
    uint32_t popBackCount;
    uint32_t overflowCount;
} AppMsgqContext_t;

AppStatus_t App_MsgqInit(void);
AppStatus_t App_MsgqPushFront(const AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqPushBack(const AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqPopFront(AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqPopBack(AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqPush(const AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqPop(AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqTakeFirstByType(uint8_t type, AppMsgqMessage_t *p_message);
uint8_t App_MsgqIsEmpty(void);
uint8_t App_MsgqGetCount(void);
const AppMsgqContext_t *App_MsgqGetContext(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MSGQ_H */

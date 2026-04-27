#ifndef APP_MSGQ_H
#define APP_MSGQ_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

#define APP_MSGQ_TYPE_TASK_HEARTBEAT      (1u)
#define APP_MSGQ_TYPE_TASK_EVENT          (2u)
#define APP_MSGQ_TYPE_TASK_ALERT          (3u)

#define APP_MSGQ_MSG_TASK_HEARTBEAT       APP_MSGQ_TYPE_TASK_HEARTBEAT
#define APP_MSGQ_MSG_TASK_EVENT           APP_MSGQ_TYPE_TASK_EVENT
#define APP_MSGQ_MSG_TASK_ALERT           APP_MSGQ_TYPE_TASK_ALERT

typedef struct
{
    uint8_t type;
    uint8_t sourceId;
    uint8_t reserved0;
    uint8_t reserved1;
    uint32_t tickMs;
    uint32_t param0;
    uint32_t param1;
} AppMsgqMessage_t;

typedef struct
{
    uint8_t initialized;
    uint8_t head;
    uint8_t tail;
    uint8_t count;
    uint32_t pushCount;
    uint32_t popCount;
    uint32_t overflowCount;
} AppMsgqContext_t;

AppStatus_t App_MsgqInit(void);
AppStatus_t App_MsgqPush(const AppMsgqMessage_t *p_message);
AppStatus_t App_MsgqPop(AppMsgqMessage_t *p_message);
const AppMsgqContext_t *App_MsgqGetContext(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MSGQ_H */

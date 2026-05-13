#ifndef APP_DUALBOOT_H
#define APP_DUALBOOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t activeSlot;
    uint32_t pendingSlot;
    uint32_t bootMode;
    uint32_t bootState;
    uint32_t reserved[9];
    uint32_t crc32;
} AppDualBootInfo_t;

#define APP_DUALBOOT_INFO_MAGIC                (0x44554254u)
#define APP_DUALBOOT_INFO_VERSION              (0x00010000u)
#define APP_DUALBOOT_SLOT1                     (1u)
#define APP_DUALBOOT_SLOT2                     (2u)
#define APP_DUALBOOT_MODE_NORMAL               (0u)
#define APP_DUALBOOT_MODE_ENTER_ROM_BOOT       (1u)
#define APP_DUALBOOT_MODE_TRIAL                (2u)
#define APP_DUALBOOT_STATE_IDLE                (0u)
#define APP_DUALBOOT_STATE_UPDATE_REQUESTED    (1u)
#define APP_DUALBOOT_STATE_TRIAL_PENDING       (2u)
#define APP_DUALBOOT_STATE_CONFIRMED           (3u)
#define APP_DUALBOOT_STATE_ROLLBACK            (4u)

AppStatus_t App_DualBootInit(void);
void App_DualBootService(void);
AppStatus_t App_DualBootRequestUpdateToSlot2(void);
AppStatus_t App_DualBootCancelUpdateRequest(void);
AppStatus_t App_DualBootConfirmSlot2(void);
const AppDualBootInfo_t *App_DualBootGetInfo(void);
uint32_t App_DualBootGetCurrentSlotId(void);
const char *App_DualBootGetCurrentSlotName(void);
const char *App_DualBootGetTargetSlotName(void);
uint32_t App_DualBootGetTargetSlotAddress(void);
const char *App_DualBootGetBootStateString(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_DUALBOOT_H */

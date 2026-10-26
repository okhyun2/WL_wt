#include "boot_info.h"
#include "boot_slot.h"

static void BootSaveOrResetDefaults(BootInfo_t *p_info)
{
    if (BootInfoIsValid(p_info) == 0u)
    {
        BootInfoSetDefaults(p_info);
        (void)BootInfoSave(p_info);
    }
}

int main(void)
{
    BootInfo_t info;

    HAL_Init();

    BootInfoLoad(&info);
    BootSaveOrResetDefaults(&info);

    if (info.bootState == BOOT_STATE_UPDATE_REQUESTED)
    {
        /* 요청을 소비하고 ROM 부트로더로 진입. 호스트가 app_image를 덮어쓴다 */
        info.activeSlot  = BOOT_SLOT1;
        info.pendingSlot = 0u;
        info.bootMode    = BOOT_MODE_NORMAL;
        info.bootState   = BOOT_STATE_IDLE;
        (void)BootInfoSave(&info);
        BootInfoJumpToSystemMemory();
    }

    if (BootSlotIsValid(BOOT_SLOT1_ADDR) == 0u)
    {
        /* 앱이 없거나 CRC 불일치: 멈추지 않고 ROM 부트로더로 들어가 재기록 가능하게 함 */
        BootInfoJumpToSystemMemory();
    }

    BootSlotJump(BOOT_SLOT1_ADDR);

    while (1)
    {
    }
}


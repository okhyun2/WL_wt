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

static void BootRollbackToSlot1(BootInfo_t *p_info)
{
    p_info->activeSlot = BOOT_SLOT1;
    p_info->pendingSlot = 0u;
    p_info->bootMode = BOOT_MODE_NORMAL;
    p_info->bootState = BOOT_STATE_ROLLBACK;
    (void)BootInfoSave(p_info);
}

int main(void)
{
    BootInfo_t info;
    uint32_t jumpAddress;

    HAL_Init();

    BootInfoLoad(&info);
    BootSaveOrResetDefaults(&info);

    if (info.bootState == BOOT_STATE_UPDATE_REQUESTED)
    {
            info.bootState = BOOT_STATE_TRIAL_PENDING;
            (void)BootInfoSave(&info);
            BootInfoJumpToSystemMemory();
    }
    else if (info.bootState == BOOT_STATE_TRIAL_PENDING)
    {
        if (BootSlotIsValid(BOOT_SLOT2_ADDR) != 0u)
        {
            info.activeSlot = BOOT_SLOT2;
            info.pendingSlot = BOOT_SLOT2;
            info.bootMode = BOOT_MODE_TRIAL;
            info.bootState = BOOT_STATE_TRIAL_PENDING;
            (void)BootInfoSave(&info);
        }
        else
        {
            BootRollbackToSlot1(&info);
        }
    }

    BootInfoLoad(&info);
    jumpAddress = BootSlotGetAddress(info.activeSlot);
    if (BootSlotIsValid(jumpAddress) == 0u)
    {
        BootRollbackToSlot1(&info);
        jumpAddress = BOOT_SLOT1_ADDR;
    }

    BootSlotJump(jumpAddress);

    while (1)
    {
    }
}

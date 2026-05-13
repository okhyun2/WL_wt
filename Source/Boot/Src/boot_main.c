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
    uint32_t jumpAddress;

    HAL_Init();

    BootInfoLoad(&info);
    BootSaveOrResetDefaults(&info);
    info.bootCounter++;
    (void)BootInfoSave(&info);

    if (info.bootState == BOOT_STATE_UPDATE_REQUESTED)
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
            info.activeSlot = BOOT_SLOT1;
            info.pendingSlot = 0u;
            info.bootMode = BOOT_MODE_NORMAL;
            info.bootState = BOOT_STATE_IDLE;
            (void)BootInfoSave(&info);
        }
    }
    else if (info.bootState == BOOT_STATE_TRIAL_PENDING)
    {
        info.activeSlot = BOOT_SLOT1;
        info.pendingSlot = 0u;
        info.bootMode = BOOT_MODE_NORMAL;
        info.bootState = BOOT_STATE_ROLLBACK;
        (void)BootInfoSave(&info);
    }

    BootInfoLoad(&info);
    jumpAddress = BootSlotGetAddress(info.activeSlot);
    if (BootSlotIsValid(jumpAddress) == 0u)
    {
        jumpAddress = BOOT_SLOT1_ADDR;
    }

    BootSlotJump(jumpAddress);

    while (1)
    {
    }
}

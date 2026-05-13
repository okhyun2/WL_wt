#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l0xx_hal.h"
#include <stdint.h>

#define BOOT_INFO_ADDR                         (0x0801E000u)
#define BOOT_INFO_MAGIC                        (0x44554254u)
#define BOOT_INFO_VERSION                      (0x00010000u)
#define BOOT_SLOT1                             (1u)
#define BOOT_SLOT2                             (2u)
#define BOOT_MODE_NORMAL                       (0u)
#define BOOT_MODE_ENTER_ROM_BOOT               (1u)
#define BOOT_MODE_TRIAL                        (2u)
#define BOOT_STATE_IDLE                        (0u)
#define BOOT_STATE_UPDATE_REQUESTED            (1u)
#define BOOT_STATE_TRIAL_PENDING               (2u)
#define BOOT_STATE_CONFIRMED                   (3u)
#define BOOT_STATE_ROLLBACK                    (4u)

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t activeSlot;
    uint32_t pendingSlot;
    uint32_t bootMode;
    uint32_t bootState;
    uint32_t bootCounter;
    uint32_t reserved[8];
    uint32_t crc32;
} BootInfo_t;

void BootInfoLoad(BootInfo_t *p_info);
uint8_t BootInfoIsValid(const BootInfo_t *p_info);
void BootInfoSetDefaults(BootInfo_t *p_info);
HAL_StatusTypeDef BootInfoSave(const BootInfo_t *p_info);

#endif

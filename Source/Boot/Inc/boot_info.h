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

#define BOOT_SYSTEM_MEMORY_ADDR                (0x1FF00000u)

/* -------------------------------------------------------
 * Image header
 * Located after vector table
 * Offset is determined by actual vector table size
 * Check .map file: size of .isr_vector section
 * ------------------------------------------------------- */
#define BOOT_IMAGE_HEADER_OFFSET     (0x000000C0u)  /* Adjust after checking .map */

#define BOOT_MAGIC_NUMBER            (0xDEADC0DEu)

/* -------------------------------------------------------
 * Image header structure
 * Placed immediately after vector table in flash
 * ------------------------------------------------------- */
typedef struct
{
    uint32_t magicNumber;     /* 0xDEADC0DE                        */
    uint32_t firmwareSize;    /* CRC calculation target size(bytes) */
    uint32_t firmwareCRC;     /* CRC32 value                        */
    uint32_t versionMajor;    /* Firmware version Major             */
    uint32_t versionMinor;    /* Firmware version Minor             */
    uint32_t versionPatch;    /* Firmware version Patch             */
    uint32_t reserved[2u];    /* Reserved                           */
} BootImageHeader_t;          /* Total: 32 bytes (0x20)             */

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
} BootInfo_t;

void BootInfoLoad(BootInfo_t *p_info);
uint8_t BootInfoIsValid(const BootInfo_t *p_info);
void BootInfoSetDefaults(BootInfo_t *p_info);
HAL_StatusTypeDef BootInfoSave(const BootInfo_t *p_info);
void BootInfoJumpToSystemMemory(void);

#ifdef __cplusplus
}
#endif

#endif

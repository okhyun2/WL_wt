#ifndef BOOT_SLOT_H
#define BOOT_SLOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define BOOT_SLOT1_ADDR                        (0x08002000u)
#define BOOT_SLOT2_ADDR                        (0x08010000u)
#define BOOT_SLOT_SIZE_BYTES                   (56u * 1024u)
#define BOOT_SRAM_START                        (0x20000000u)
#define BOOT_SRAM_END                          (0x20004FFFu)

uint8_t BootSlotIsValid(uint32_t baseAddress);
uint32_t BootSlotGetAddress(uint32_t slotId);
void BootSlotJump(uint32_t baseAddress);

#ifdef __cplusplus
}
#endif

#endif

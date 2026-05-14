#ifndef BOOT_CRC_H
#define BOOT_CRC_H

#include <stdint.h>

void BootCRC_Init(void);
void BootCRC_DeInit(void);
uint32_t BootCRC_CalculateWithPatch(uint32_t baseAddress,
                                    uint32_t sizeBytes,
                                    uint32_t patchOffset);


#endif /* BOOT_CRC_H */

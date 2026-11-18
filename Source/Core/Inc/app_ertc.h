#ifndef APP_ERTC_H
#define APP_ERTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "app_error.h"

/**
 * @file    app_ertc.h
 * @brief   RV-3032-C7 external RTC (eRTC) minimal driver: CLKOUT 32.768kHz verify.
 *
 *          V0.4 board routes RV-3032 CLKOUT into the STM32 LSE input
 *          (APP_CLOCK_LSE_BYPASS_ENABLE=TRUE). Per RV-3032 App-Manual Rev 1.3
 *          sec 4.4: after POR the 32.768kHz square wave is output on CLKOUT
 *          with delivery defaults (FD=00, OS=0 in EEPROM C3h, NCLKE=0 in C0h).
 *          This driver reads back those registers to confirm the default is
 *          intact; if a previous customization disabled it, NCLKE is corrected
 *          via the EEPROM update flow (PW -> EERD=1 -> RAM edit -> EECMD=11h).
 *
 *          On V0.3 boards (no RV-3032) probe NACKs and the caller silently
 *          skips, keeping the existing LSE-crystal/LSI fallback behavior.
 */

/** @brief RV-3032 7-bit I2C address (fixed, no ADDR pin on C7 package). */
#define APP_ERTC_I2C_ADDR_7BIT          (0x51u)

/** @brief RV-3032 register map (App-Manual Rev 1.3, section 3). */
#define APP_ERTC_REG_EEPROM_PMU         (0xC0u) /* NCLKE is bit7; 0 = CLKOUT enabled */
#define APP_ERTC_REG_EEPROM_CLKOUT1     (0xC2u) /* HFD[7:0], unused in XTAL mode */
#define APP_ERTC_REG_EEPROM_CLKOUT2     (0xC3u) /* OS(bit7)=0, HFD[12:8], FD[1:0]=00 -> 32.768kHz */
#define APP_ERTC_REG_EEADDR             (0x3Du) /* EEPROM address index */
#define APP_ERTC_REG_EEDATA             (0x3Eu) /* EEPROM data */
#define APP_ERTC_REG_EECMD              (0x3Fu) /* EEPROM command (11h = update all RAM->EEPROM) */

/**
 * @brief Probe eRTC presence on the I2C bus (address-only access).
 *
 * @return APP_STATUS_OK when the device ACKs, non-OK otherwise (V0.3 board).
 */
AppStatus_t App_ErtcProbe(void);

/**
 * @brief Confirm CLKOUT outputs 32.768kHz (delivery default FD=00/OS=0/NCLKE=0).
 *
 *        Read-back of EEPROM PMU (C0h) and EEPROM Clkout 2 (C3h) RAM mirrors.
 *        If defaults were customized away (NCLKE=1 or FD!=00), attempts the
 *        persistent EEPROM correction flow; write-protected parts report
 *        failure and the LSE bypass will time out to the LSI fallback.
 *
 * @return APP_STATUS_OK when 32.768kHz output is confirmed.
 */
AppStatus_t App_ErtcEnsureClkout32k(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ERTC_H */

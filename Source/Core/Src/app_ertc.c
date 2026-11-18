#include "app_ertc.h"
#include "app_hw.h"
#include "app_build_config.h"
#include "app_log.h"

/* [V0.4 eRTC] RV-3032-C7 CLKOUT verify/correct.
 * Source: RV-3032-C7 App-Manual Rev 1.3 (May 2023):
 *  - sec 4.4  : POR defaults FD=00, OS=0 (C3h), NCLKE=0 (C0h) -> 32.768kHz on CLKOUT
 *  - sec 3.20 : C0h EEPROM PMU (NCLKE=bit7), C3h EEPROM Clkout 2 (OS=bit7, FD=bits[1:0])
 *  - sec 4.6  : EEPROM edit = PW unlock -> EERD=1 -> RAM edit -> EECMD=11h -> EERD=0
 * Note: registers C0h..C3h are the RAM mirrors (active zone) and are read/written
 * directly at addresses C0h..C3h. */

#define ERTC_I2C_ADDR_8BIT              ((uint16_t)(APP_ERTC_I2C_ADDR_7BIT << 1))
#define ERTC_I2C_TIMEOUT_MS             (100u)

/* expected values for 32.768kHz XTAL output */
#define ERTC_PMU_NCLKE_MASK             (0x80u) /* bit7: 0 = normal clock output enabled */
#define ERTC_CLKOUT2_OS_MASK            (0x80u) /* bit7: 0 = XTAL mode */
#define ERTC_CLKOUT2_FD_MASK            (0x03u) /* bits[1:0]: 00 = 32.768kHz */
#define ERTC_EECMD_UPDATE               (0x11u)

static AppStatus_t ErtcReadReg(uint8_t reg, uint8_t *p_out)
{
    return (HAL_I2C_Mem_Read(APP_I2C_RTC_HANDLE, ERTC_I2C_ADDR_8BIT,
                             reg, I2C_MEMADD_SIZE_8BIT,
                             p_out, 1u, ERTC_I2C_TIMEOUT_MS) == HAL_OK)
           ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
}

static AppStatus_t ErtcWriteReg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return (HAL_I2C_Master_Transmit(APP_I2C_RTC_HANDLE, ERTC_I2C_ADDR_8BIT,
                                    buf, 2u, ERTC_I2C_TIMEOUT_MS) == HAL_OK)
           ? APP_STATUS_OK : APP_STATUS_UART_TIMEOUT;
}

AppStatus_t App_ErtcProbe(void)
{
    uint8_t reg = 0u;
    return ErtcReadReg(APP_ERTC_REG_EEPROM_CLKOUT2, &reg);
}

AppStatus_t App_ErtcEnsureClkout32k(void)
{
    AppStatus_t status;
    uint8_t pmu = 0u;
    uint8_t clkout2 = 0u;

    status = ErtcReadReg(APP_ERTC_REG_EEPROM_PMU, &pmu);
    if (status != APP_STATUS_OK) { return status; }
    status = ErtcReadReg(APP_ERTC_REG_EEPROM_CLKOUT2, &clkout2);
    if (status != APP_STATUS_OK) { return status; }

    if (((pmu & ERTC_PMU_NCLKE_MASK) == 0u) &&
        ((clkout2 & (ERTC_CLKOUT2_OS_MASK | ERTC_CLKOUT2_FD_MASK)) == 0u))
    {
        APP_LOGD("ERTC", "CLKOUT 32.768kHz default ok (PMU=0x%02X C3=0x%02X)", pmu, clkout2);
        return APP_STATUS_OK;
    }

    APP_LOGW("ERTC", "CLKOUT not 32.768kHz default (PMU=0x%02X C3=0x%02X) -> EEPROM correction", pmu, clkout2);

    /* EEPROM correction flow (sec 4.6.10): RAM edit already affects the active
     * zone; persist via EECMD=11h (password-protected parts will NACK the
     * command effect - reported as failure, LSE falls back to LSI). */
    if ((clkout2 & (ERTC_CLKOUT2_OS_MASK | ERTC_CLKOUT2_FD_MASK)) != 0u)
    {
        status = ErtcWriteReg(APP_ERTC_REG_EEPROM_CLKOUT2,
                              (uint8_t)(clkout2 & ~(ERTC_CLKOUT2_OS_MASK | ERTC_CLKOUT2_FD_MASK)));
        if (status != APP_STATUS_OK) { return status; }
    }
    if ((pmu & ERTC_PMU_NCLKE_MASK) != 0u)
    {
        status = ErtcWriteReg(APP_ERTC_REG_EEPROM_PMU,
                              (uint8_t)(pmu & (uint8_t)~ERTC_PMU_NCLKE_MASK));
        if (status != APP_STATUS_OK) { return status; }
    }
    status = ErtcWriteReg(APP_ERTC_REG_EECMD, ERTC_EECMD_UPDATE);
    if (status != APP_STATUS_OK) { return status; }

    APP_LOGN("ERTC", "CLKOUT corrected to 32.768kHz (EEPROM updated)");
    return APP_STATUS_OK;
}

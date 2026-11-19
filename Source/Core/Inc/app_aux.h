#ifndef APP_AUX_H
#define APP_AUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l0xx_hal.h"
#include <stdint.h>

/* STM32L073 VREFINT 공장 캘리브레이션 값 주소 (3.0V, 30°C에서 측정된 raw) */
/* MX_ADC_Init()에서 
    SamplingTime은 ADC_SAMPLETIME_160CYCLES_5, 
    ContinuousConvMode = DISABLE, 
    EOCSelection = ADC_EOC_SINGLE_CONV 로 두는 것을 권장
*/
#define VREFINT_CAL_ADDR    ((uint16_t*)((uint32_t)0x1FF80078))
#define VREFINT_CAL_VREF_MV (3000U)   /* 캘리브레이션 시 VDDA 기준 전압 (mV) */
#define ADC_FULL_SCALE      (4095U)   /* 12-bit */
#define VDIV_RATIO          (2U)      /* 2MΩ:2MΩ → 1:2 분압 */


HAL_StatusTypeDef Battery_ReadVoltage_mV(uint32_t *vbat_mv, uint32_t *vdda_mv);
HAL_StatusTypeDef Battery_ReadVoltage_Averaged_mV(uint32_t *adc_vref, uint32_t *adc_vbat, uint32_t *vdda_mv, uint32_t *vbat_mv);
uint32_t Battery_ReadAdcNoiseSeed(void); /* 부팅 랜덤화용 ADC 열노이즈 시드 */
void App_UpdateBatteryToOptions(uint8_t voltX10, uint8_t alarm);

/* [SHT40] Sensirion SHT40-AD1B (V0.4 보드 교체: SHTC3 -> SHT40)
 * - I2C 주소 0x44 (ADDR=GND), Wake/Sleep 명령 없음(상시 활성)
 * - 측정: 0xFD(high repeatability, T first) -> 6B 응답(T_CRC, RH_CRC)
 * - 소프트 리셋: 0x94 / 시리얼 번호: 0x89(6B+2CRC)
 * - CRC: poly 0x31, init 0xFF (Datasheet v7.3 Table 7, CRC(0xBEEF)=0x92) */
#define SHT40_I2C_ADDR        (0x44 << 1)
 
#define SHT40_CMD_SOFT_RESET  0x94
#define SHT40_CMD_READ_SERIAL 0x89
#define SHT40_CMD_MEAS_HIGH_T 0xFD   /* high repeatability, T first */

#define SHT40_MEAS_DELAY_MS   10U    /* high rep. max 8.3ms + margin */

typedef struct {
    float temperature;
    float humidity;
} SHT40_Data_t;

HAL_StatusTypeDef SHT40_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef SHT40_SoftReset(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef SHT40_ReadSerial(I2C_HandleTypeDef *hi2c, uint32_t *serial);
HAL_StatusTypeDef SHT40_ReadTempHumidity(I2C_HandleTypeDef *hi2c, SHT40_Data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* APP_AUX_H */

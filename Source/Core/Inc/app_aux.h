#ifndef APP_AUX_H
#define APP_AUX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l0xx_hal.h"

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


#ifdef __cplusplus
}
#endif

#endif /* APP_AUX_H */

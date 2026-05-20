#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_aux.h"

/**
 * @brief  지정한 채널 1개를 단일 변환으로 측정
 * @param  channel : ADC_CHANNEL_1, ADC_CHANNEL_VREFINT 등
 * @retval ADC raw 값 (12-bit). 실패 시 0xFFFF 반환
 */
static uint16_t ADC_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t raw = 0xFFFF;

    /* 이전 채널 선택을 모두 해제하기 위해 채널 레지스터 클리어 */
    hadc.Instance->CHSELR = 0;

    sConfig.Channel = channel;
    sConfig.Rank    = ADC_RANK_CHANNEL_NUMBER;
    if (HAL_ADC_ConfigChannel(APP_ADC_BATTERY_HANDLE, &sConfig) != HAL_OK) {
        return raw;
    }

    if (HAL_ADC_Start(APP_ADC_BATTERY_HANDLE) != HAL_OK) {
        return raw;
    }

    if (HAL_ADC_PollForConversion(APP_ADC_BATTERY_HANDLE, 10) == HAL_OK) {
        raw = (uint16_t)HAL_ADC_GetValue(APP_ADC_BATTERY_HANDLE);
    }

    HAL_ADC_Stop(APP_ADC_BATTERY_HANDLE);
    return raw;
}

/**
 * @brief  분압된 PA1 입력으로부터 실제 배터리 전압을 mV 단위로 측정
 * @param  vbat_mv  : [out] 배터리 전압 (mV)
 * @param  vdda_mv  : [out] 측정된 VDDA 전압 (mV), NULL 허용
 * @retval HAL_StatusTypeDef
 */
HAL_StatusTypeDef Battery_ReadVoltage_mV(uint32_t *vbat_mv, uint32_t *vdda_mv)
{
    uint16_t adc_pa1_raw;
    uint16_t adc_vrefint_raw;
    uint32_t vdda_calc_mv;
    uint32_t vpin_mv;

    if (vbat_mv == NULL) {
        return HAL_ERROR;
    }

    /* 1) VREFINT 내부 채널 활성화 및 안정화 대기 (T_START_VREFINT ≈ 3ms) */
    ADC->CCR |= ADC_CCR_VREFEN;
    HAL_Delay(3);

    /* 2) VREFINT raw 측정 */
    adc_vrefint_raw = ADC_ReadChannel(ADC_CHANNEL_VREFINT);
    if (adc_vrefint_raw == 0xFFFF || adc_vrefint_raw == 0) {
        ADC->CCR &= ~ADC_CCR_VREFEN;
        return HAL_ERROR;
    }

    /* 3) PA1 raw 측정 */
    adc_pa1_raw = ADC_ReadChannel(ADC_CHANNEL_1);
    if (adc_pa1_raw == 0xFFFF) {
        ADC->CCR &= ~ADC_CCR_VREFEN;
        return HAL_ERROR;
    }

    /* 4) VREFINT 공장 캘리브레이션으로 실제 VDDA 역산
     *    VDDA = 3000mV * VREFINT_CAL / VREFINT_DATA            */
    vdda_calc_mv = ((uint32_t)VREFINT_CAL_VREF_MV
                    * (uint32_t)(*VREFINT_CAL_ADDR))
                    / (uint32_t)adc_vrefint_raw;

    /* 5) PA1 핀 전압 계산 (mV)
     *    Vpin = VDDA * ADC_PA1 / 4095                          */
    vpin_mv = ((uint32_t)adc_pa1_raw * vdda_calc_mv) / ADC_FULL_SCALE;

    /* 6) 1:1 분압이므로 ×2 하여 실제 배터리 전압 복원 */
    *vbat_mv = vpin_mv * VDIV_RATIO;

    if (vdda_mv != NULL) {
        *vdda_mv = vdda_calc_mv;
    }

    /* 7) 전력 절약을 위해 VREFINT 비활성화 (선택) */
    ADC->CCR &= ~ADC_CCR_VREFEN;

    return HAL_OK;
}

#define BATT_SAMPLES        8U   /* 2의 거듭제곱 권장 (시프트 연산) */

HAL_StatusTypeDef Battery_ReadVoltage_Averaged_mV(uint32_t *adc_vref, uint32_t *adc_vbat, uint32_t *vdda_mv, uint32_t *vbat_mv)
{
    uint32_t sum_pa1 = 0;
    uint32_t sum_vref = 0;
    uint16_t r_pa1, r_vref;
    uint32_t vdda_calc_mv, vpin_mv;

    if (vbat_mv == NULL) return HAL_ERROR;

    ADC->CCR |= ADC_CCR_VREFEN;
    HAL_Delay(3);

    for (uint32_t i = 0; i < BATT_SAMPLES; i++) {
        r_vref = ADC_ReadChannel(ADC_CHANNEL_VREFINT);
        r_pa1  = ADC_ReadChannel(ADC_CHANNEL_1);
        if (r_vref == 0xFFFF || r_pa1 == 0xFFFF || r_vref == 0) {
            ADC->CCR &= ~ADC_CCR_VREFEN;
            return HAL_ERROR;
        }
        sum_vref += r_vref;
        sum_pa1  += r_pa1;
    }

    uint32_t avg_vref = sum_vref / BATT_SAMPLES;
    uint32_t avg_pa1  = sum_pa1  / BATT_SAMPLES;

    vdda_calc_mv = (VREFINT_CAL_VREF_MV * (uint32_t)(*VREFINT_CAL_ADDR))
                   / avg_vref;
    vpin_mv      = (avg_pa1 * vdda_calc_mv) / ADC_FULL_SCALE;
    *vbat_mv     = vpin_mv * VDIV_RATIO;

    if (vdda_mv) *vdda_mv = vdda_calc_mv;
    if(adc_vref) *adc_vref = avg_vref;
    if(adc_vbat) *adc_vbat = avg_pa1;

    ADC->CCR &= ~ADC_CCR_VREFEN;
    return HAL_OK;
}

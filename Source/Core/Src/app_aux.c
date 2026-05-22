#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_aux.h"
#include "app_meter_server_format.h"

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

void App_UpdateBatteryToOptions(uint8_t voltX10, uint8_t alarm)
{
    AppMeterServerFormatOptions_t opt;
    (void)App_MeterServerOptionsLoad(&opt);
    App_MeterServerOptionsSetBattery(&opt, voltX10, alarm);
    (void)App_MeterServerOptionsUpdate(&opt);
}


/////////////////////////////////////////////////////////////////////////////////////////

/* CRC-8: Polynomial 0x31, Init 0xFF (Sensirion 표준) */
static uint8_t SHTC3_CRC8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

/* 16비트 명령 전송 */
static HAL_StatusTypeDef SHTC3_SendCmd(I2C_HandleTypeDef *hi2c, uint16_t cmd)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(cmd >> 8);
    buf[1] = (uint8_t)(cmd & 0xFF);
    return HAL_I2C_Master_Transmit(hi2c, SHTC3_I2C_ADDR, buf, 2, 100);
}

HAL_StatusTypeDef SHTC3_Wakeup(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef st = SHTC3_SendCmd(hi2c, SHTC3_CMD_WAKEUP);
    HAL_Delay(1);   /* tWAKEUP 최대 240us, 안전하게 1ms */
    return st;
}

HAL_StatusTypeDef SHTC3_Sleep(I2C_HandleTypeDef *hi2c)
{
    return SHTC3_SendCmd(hi2c, SHTC3_CMD_SLEEP);
}

HAL_StatusTypeDef SHTC3_SoftReset(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef st = SHTC3_SendCmd(hi2c, SHTC3_CMD_SOFT_RESET);
    HAL_Delay(1);
    return st;
}

HAL_StatusTypeDef SHTC3_ReadID(I2C_HandleTypeDef *hi2c, uint16_t *id)
{
    uint8_t rx[3];
    HAL_StatusTypeDef st;

    st = SHTC3_Wakeup(hi2c);
    if (st != HAL_OK) return st;

    st = SHTC3_SendCmd(hi2c, SHTC3_CMD_READ_ID);
    if (st != HAL_OK) return st;

    st = HAL_I2C_Master_Receive(hi2c, SHTC3_I2C_ADDR, rx, 3, 100);
    if (st != HAL_OK) return st;

    if (SHTC3_CRC8(rx, 2) != rx[2]) return HAL_ERROR;

    *id = ((uint16_t)rx[0] << 8) | rx[1];
    return HAL_OK;
}

HAL_StatusTypeDef SHTC3_ReadTempHumidity(I2C_HandleTypeDef *hi2c, SHTC3_Data_t *data)
{
    uint8_t rx[6];
    HAL_StatusTypeDef st;

    /* 1) Wake-up */
    st = SHTC3_Wakeup(hi2c);
    if (st != HAL_OK) return st;

    /* 2) 측정 명령 (Normal mode, T first, clock stretching disabled) */
    st = SHTC3_SendCmd(hi2c, SHTC3_CMD_MEAS_TFIRST);
    if (st != HAL_OK) return st;

    /* 3) 측정 대기: Normal mode 최대 12.1ms */
    HAL_Delay(15);

    /* 4) 6바이트 읽기 (T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC) */
    st = HAL_I2C_Master_Receive(hi2c, SHTC3_I2C_ADDR, rx, 6, 100);
    if (st != HAL_OK) return st;

    /* 5) CRC 검증 */
    if (SHTC3_CRC8(&rx[0], 2) != rx[2]) return HAL_ERROR;
    if (SHTC3_CRC8(&rx[3], 2) != rx[5]) return HAL_ERROR;

    /* 6) Raw 값 → 물리값 변환
     *   T  = -45 + 175 * (raw / 65535)
     *   RH = 100 * (raw / 65535)
     */
    uint16_t rawT  = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t rawRH = ((uint16_t)rx[3] << 8) | rx[4];

    data->temperature = -45.0f + 175.0f * ((float)rawT  / 65535.0f);
    data->humidity    =          100.0f * ((float)rawRH / 65535.0f);

    /* 7) 다시 Sleep 진입 (저전력) */
    SHTC3_Sleep(hi2c);

    return HAL_OK;
}

HAL_StatusTypeDef SHTC3_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef st;
    uint16_t id = 0;

    st = SHTC3_Wakeup(hi2c);
    if (st != HAL_OK) return st;

    st = SHTC3_SoftReset(hi2c);
    if (st != HAL_OK) return st;

    HAL_Delay(1);

    st = SHTC3_ReadID(hi2c, &id);
    if (st != HAL_OK) return st;

    /* SHTC3 ID 확인: bits [11:6] = 000111, bits [2:0] = 111
     *  => (id & 0x083F) == 0x0807
     */
    if ((id & 0x083F) != 0x0807) return HAL_ERROR;

    SHTC3_Sleep(hi2c);
    return HAL_OK;
}

void App_UpdateSHTC3ToConfigs(float temperature, float humidity)
{
    //;; TODO
}




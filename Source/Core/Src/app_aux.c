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

/* 부팅 랜덤화용 ADC 열노이즈 시드.
 * 평균화 리딩은 노이즈가 상쇄되어 부팅 간 값이 고정되는 반면, raw 단일 샘플은
 * 매번 LSB 단위로 흔들린다. VREFINT/PA1을 교대로 32회 단발 읽어 혼합하고,
 * 샘플 사이 시간 지터(SysTick->VAL, 코어 클럭마다 변함)도 함께 섞는다. */
uint32_t Battery_ReadAdcNoiseSeed(void)
{
    uint32_t seed = 0x9E3779B9u;
    uint32_t i;
    uint16_t r;

    ADC->CCR |= ADC_CCR_VREFEN;
    HAL_Delay(3);

    for (i = 0u; i < 32u; i++)
    {
        r = ADC_ReadChannel((i & 1u) ? ADC_CHANNEL_1 : ADC_CHANNEL_VREFINT);
        seed = (seed * 2654435761u) ^ (uint32_t)r ^ (uint32_t)SysTick->VAL;
    }

    ADC->CCR &= ~ADC_CCR_VREFEN;
    return seed;
}

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

    //invalid value
    if(*vbat_mv <= 2000) return(HAL_ERROR);

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

/* CRC-8: Polynomial 0x31, Init 0xFF (Sensirion 표준, SHT40 Datasheet Table 7) */
static uint8_t SHT40_CRC8(const uint8_t *data, uint8_t len)
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

/* 8비트 명령 전송 (SHT40 명령은 단일 바이트) */
static HAL_StatusTypeDef SHT40_SendCmd(I2C_HandleTypeDef *hi2c, uint8_t cmd)
{
    return HAL_I2C_Master_Transmit(hi2c, SHT40_I2C_ADDR, &cmd, 1, 100);
}

HAL_StatusTypeDef SHT40_SoftReset(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef st = SHT40_SendCmd(hi2c, SHT40_CMD_SOFT_RESET);
    HAL_Delay(1);   /* 리셋 후 최대 1ms 대기 */
    return st;
}

HAL_StatusTypeDef SHT40_ReadSerial(I2C_HandleTypeDef *hi2c, uint32_t *serial)
{
    uint8_t rx[6];
    HAL_StatusTypeDef st;

    st = SHT40_SendCmd(hi2c, SHT40_CMD_READ_SERIAL);
    if (st != HAL_OK) return st;

    st = HAL_I2C_Master_Receive(hi2c, SHT40_I2C_ADDR, rx, sizeof(rx), 100);
    if (st != HAL_OK) return st;

    if (SHT40_CRC8(&rx[0], 2) != rx[2]) return HAL_ERROR;
    if (SHT40_CRC8(&rx[3], 2) != rx[5]) return HAL_ERROR;

    if (serial != NULL)
    {
        *serial = ((uint32_t)rx[0] << 24) | ((uint32_t)rx[1] << 16) |
                  ((uint32_t)rx[3] << 8)  | (uint32_t)rx[4];
    }

    return HAL_OK;
}

HAL_StatusTypeDef SHT40_ReadTempHumidity(I2C_HandleTypeDef *hi2c, SHT40_Data_t *data)
{
    uint8_t rx[6];
    HAL_StatusTypeDef st;

    /* SHT40은 Wake/Sleep이 없으므로 측정 명령 1회로 완결된다. */
    st = SHT40_SendCmd(hi2c, SHT40_CMD_MEAS_HIGH_T);
    if (st != HAL_OK) return st;

    /* 측정 대기: high repeatability 최대 8.3ms */
    HAL_Delay(SHT40_MEAS_DELAY_MS);

    /* 6바이트 읽기 (T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC) */
    st = HAL_I2C_Master_Receive(hi2c, SHT40_I2C_ADDR, rx, 6, 100);
    if (st != HAL_OK) return st;

    /* CRC 검증 */
    if (SHT40_CRC8(&rx[0], 2) != rx[2]) return HAL_ERROR;
    if (SHT40_CRC8(&rx[3], 2) != rx[5]) return HAL_ERROR;

    /* Raw 값 -> 물리값 변환 (Datasheet eq. (1),(2))
     *   T  = -45 + 175 * (raw / 65535)
     *   RH = 100 * (raw / 65535)  (0~100 클램프) */
    uint16_t rawT  = ((uint16_t)rx[0] << 8) | rx[1];
    uint16_t rawRH = ((uint16_t)rx[3] << 8) | rx[4];

    data->temperature = -45.0f + 175.0f * ((float)rawT  / 65535.0f);
    data->humidity    =          100.0f * ((float)rawRH / 65535.0f);

    if (data->humidity < 0.0f)   data->humidity = 0.0f;
    if (data->humidity > 100.0f) data->humidity = 100.0f;

    return HAL_OK;
}

/* SHT40 존재/응답 확인: 소프트 리셋 후 시리얼 번호 판독 */
HAL_StatusTypeDef SHT40_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef st;
    uint32_t serial = 0;

    st = SHT40_SoftReset(hi2c);
    if (st != HAL_OK) return st;

    return SHT40_ReadSerial(hi2c, &serial);
}




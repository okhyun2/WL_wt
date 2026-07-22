#include "app_clock.h"
#include "app_hw.h"

#include <string.h>
#include <stdio.h>

#include "app_build_config.h"
#include "app_log.h"
#include "app_nbiot.h"

/**
 * @file    app_clock.c
 * @brief   Boot-time clock validation and runtime clock context.
 */

static AppClockContext_t g_appClockContext;

static AppClockSource_t App_ClockDecodeSystemSource(uint32_t halSource)
{
    switch (halSource)
    {
        case RCC_SYSCLKSOURCE_STATUS_MSI:
            return APP_CLOCK_SOURCE_MSI;

        case RCC_SYSCLKSOURCE_STATUS_HSI:
            return APP_CLOCK_SOURCE_HSI16;

        case RCC_SYSCLKSOURCE_STATUS_HSE:
            return APP_CLOCK_SOURCE_HSE;

        case RCC_SYSCLKSOURCE_STATUS_PLLCLK:
            return APP_CLOCK_SOURCE_PLL;

        default:
            return APP_CLOCK_SOURCE_UNKNOWN;
    }
}

static AppStatus_t App_ClockCaptureContext(void)
{
    RCC_OscInitTypeDef oscConfig;
    uint32_t halSysClkSource;

    (void)memset(&g_appClockContext, 0, sizeof(g_appClockContext));
    (void)memset(&oscConfig, 0, sizeof(oscConfig));

    halSysClkSource = __HAL_RCC_GET_SYSCLK_SOURCE();
    g_appClockContext.sysclkSource = App_ClockDecodeSystemSource(halSysClkSource);
    g_appClockContext.lseReady = (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET) ? APP_TRUE : APP_FALSE;
    g_appClockContext.flashLatency = __HAL_FLASH_GET_LATENCY();

    HAL_RCC_GetOscConfig(&oscConfig);
    SystemCoreClockUpdate();

    g_appClockContext.msiRange = oscConfig.MSIClockRange;
    g_appClockContext.sysclkHz = HAL_RCC_GetSysClockFreq();
    g_appClockContext.hclkHz = HAL_RCC_GetHCLKFreq();
    g_appClockContext.pclk1Hz = HAL_RCC_GetPCLK1Freq();
    g_appClockContext.pclk2Hz = HAL_RCC_GetPCLK2Freq();

    APP_RETURN_IF_FALSE(g_appClockContext.sysclkSource == APP_CLOCK_SOURCE_MSI, APP_STATUS_CLOCK_SOURCE_INVALID);
    APP_RETURN_IF_FALSE((__HAL_RCC_GET_FLAG(RCC_FLAG_MSIRDY) != RESET), APP_STATUS_CLOCK_VERIFY_FAILED);

    APP_RETURN_IF_FALSE(g_appClockContext.lseReady == APP_TRUE, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(oscConfig.MSIState == RCC_MSI_ON, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(oscConfig.MSIClockRange == APP_CLOCK_MSI_RANGE_BOOT, APP_STATUS_CLOCK_VERIFY_FAILED);

    g_appClockContext.initialized = APP_TRUE;
    return APP_STATUS_OK;
}

AppStatus_t App_ClockInit(void)
{
    (void)memset(&g_appClockContext, 0, sizeof(g_appClockContext));
    return App_ClockCaptureContext();
}

AppStatus_t App_ClockRecoverAfterStop(void)
{
    RCC_OscInitTypeDef oscConfig;
    RCC_ClkInitTypeDef clkConfig;

    (void)memset(&oscConfig, 0, sizeof(oscConfig));
    (void)memset(&clkConfig, 0, sizeof(clkConfig));

    oscConfig.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    oscConfig.MSIState = RCC_MSI_ON;
    oscConfig.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    oscConfig.MSIClockRange = APP_CLOCK_MSI_RANGE_BOOT;
    oscConfig.PLL.PLLState = RCC_PLL_NONE;
    APP_RETURN_IF_HAL_ERROR(HAL_RCC_OscConfig(&oscConfig), APP_STATUS_CLOCK_VERIFY_FAILED);

    clkConfig.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    clkConfig.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clkConfig.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clkConfig.APB1CLKDivider = RCC_HCLK_DIV1;
    clkConfig.APB2CLKDivider = RCC_HCLK_DIV1;
    APP_RETURN_IF_HAL_ERROR(HAL_RCC_ClockConfig(&clkConfig, APP_CLOCK_FLASH_LATENCY_BOOT), APP_STATUS_CLOCK_VERIFY_FAILED);

    return APP_STATUS_OK;
}

uint8_t App_ClockIsInitialized(void)
{
    return g_appClockContext.initialized;
}

const AppClockContext_t *App_ClockGetContext(void)
{
    return &g_appClockContext;
}

uint32_t CalcElapsedMs(uint64_t before_ms, uint64_t after_ms)
{
    int64_t elapsed = (int64_t)after_ms - (int64_t)before_ms;

    // 자정을 넘긴 경우 보정 (24시간 = 86,400,000ms)
    if (elapsed < 0) {
        elapsed += 24ULL * 3600ULL * 1000ULL;
    }

    return (uint32_t)elapsed;
}

// 전역 보정값 (ms 단위)
uint32_t g_tick_offset = 0;

// 보정된 Tick 반환 함수
uint32_t GetCorrectedTick(void)
{
    return HAL_GetTick() + g_tick_offset;
}

void RTC_SetTime(int year, int month, int date, int hour, int min, int sec)
{
  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  sTime.Hours = hour;
  sTime.Minutes = min;
  sTime.Seconds = sec;
  sDate.Date = date;
  sDate.Month = month;
  sDate.Year = year%100;

  HAL_RTC_SetTime(APP_RTC_HANDLE, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(APP_RTC_HANDLE, &sDate, RTC_FORMAT_BIN);
}

AppStatus_t RTC_GetTime(AppDateTime_t *pDateTime)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;
    HAL_StatusTypeDef halStatus;

    APP_RETURN_IF_FALSE((pDateTime != NULL), APP_STATUS_INVALID_PARAM);
    (void)memset(pDateTime, 0, sizeof(*pDateTime));

    (void)memset(&sTime, 0, sizeof(sTime));
    (void)memset(&sDate, 0, sizeof(sDate));

    /* 반드시 Time -> Date 순서로 호출 (shadow register unlock) */
    halStatus = HAL_RTC_GetTime(APP_RTC_HANDLE, &sTime, RTC_FORMAT_BIN);
    if (halStatus != HAL_OK)
    {
        APP_LOGE("RTC", "GetTime failed (hal=%d)", (int)halStatus);
        return APP_STATUS_FATAL;
    }

    halStatus = HAL_RTC_GetDate(APP_RTC_HANDLE, &sDate, RTC_FORMAT_BIN);
    if (halStatus != HAL_OK)
    {
        APP_LOGE("RTC", "GetDate failed (hal=%d)", (int)halStatus);
        return APP_STATUS_FATAL;
    }

    pDateTime->year   = (uint16_t)(2000u + (uint16_t)sDate.Year);
    pDateTime->month  = sDate.Month;
    pDateTime->day    = sDate.Date;
    pDateTime->hour   = sTime.Hours;
    pDateTime->minute = sTime.Minutes;
    pDateTime->second = sTime.Seconds;

    return APP_STATUS_OK;
}

AppStatus_t RTC_PrintTime(void)
{
    AppStatus_t status;
    AppDateTime_t timeInfo;
    int written;
    char timeStr[APP_RTC_TIME_STR_LEN];

    timeStr[0] = '\0';

    status = RTC_GetTime(&timeInfo);
    if (status != APP_STATUS_OK) return status;

    written = snprintf(timeStr, APP_RTC_TIME_STR_LEN,
                       "%04u-%02u-%02u %02u:%02u:%02u",
                       (unsigned)timeInfo.year,
                       (unsigned)timeInfo.month,
                       (unsigned)timeInfo.day,
                       (unsigned)timeInfo.hour,
                       (unsigned)timeInfo.minute,
                       (unsigned)timeInfo.second);

    if ((written <= 0) || ((uint32_t)written >= APP_RTC_TIME_STR_LEN))
    {
        APP_LOGE("RTC", "time string format fail");
        timeStr[0] = '\0';
        return APP_STATUS_FATAL;
    }
    APP_LOGI("RTC", "Current time:%s", timeStr);
    return APP_STATUS_OK;
}

uint64_t RTC_GetTimeMs(void)
{
    RTC_TimeTypeDef sTime;
    RTC_DateTypeDef sDate;

    HAL_RTC_GetTime(APP_RTC_HANDLE, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(APP_RTC_HANDLE, &sDate, RTC_FORMAT_BIN);   // Time 읽은 후 Date도 반드시 읽어야 함

    // 시/분/초를 밀리초로 변환
    uint64_t ms = 0;
    ms += (uint64_t)sTime.Hours   * 3600000ULL;
    ms += (uint64_t)sTime.Minutes * 60000ULL;
    ms += (uint64_t)sTime.Seconds * 1000ULL;

    // SubSeconds를 이용한 밀리초 정밀도 향상
    if (hrtc.Init.SynchPrediv > 0) {
        ms += ((hrtc.Init.SynchPrediv - sTime.SubSeconds) * 1000) 
              / (hrtc.Init.SynchPrediv + 1);
    }

    return ms;
}

extern CRC_HandleTypeDef hcrc;

#define APP_CLOCK_UID_BASE_ADDR   (0x1FF80050UL)

/**
 * @brief 32비트 정수에 대한 비선형 애벌런치 믹서 (Murmur3 finalizer 변형).
 *        입력값의 구조화된 패턴(웨이퍼 좌표 등)을 무작위에 가깝게 확산시킨다.
 */
static uint32_t App_ClockAvalancheMix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

/**
 * @brief STM32 96비트 UID를 읽어 비선형 믹싱으로 워드 간 상관관계를 제거한
 *        중간값 3개를 만든 뒤, 하드웨어 CRC로 최종 확산하여 반환한다.
 *
 * @note  동일 생산 로트/웨이퍼에서 나온 장치들은 UID 워드가 부분적으로
 *        동일하거나 유사한 값을 가질 수 있으므로, CRC(선형 연산)만 단독으로
 *        사용할 경우 해시값이 편향될 위험이 있다. 이를 막기 위해 CRC 이전에
 *        각 워드를 독립적으로 애벌런치 믹싱하고, 워드 간에도 서로 다른
 *        상수를 섞어 넣어 입력 구조를 완전히 흐트러뜨린다.
 */
uint32_t App_ClockGetDeviceUidHash(void)
{
    uint32_t rawWords[3];
    uint32_t mixedWords[3];
    uint32_t combined;

    rawWords[0] = *(uint32_t *)(APP_CLOCK_UID_BASE_ADDR + 0x00);
    rawWords[1] = *(uint32_t *)(APP_CLOCK_UID_BASE_ADDR + 0x04);
    rawWords[2] = *(uint32_t *)(APP_CLOCK_UID_BASE_ADDR + 0x14);

    /* 1단계: 각 워드를 독립적으로 애벌런치 믹싱 (워드별로 다른 상수 가산) */
    mixedWords[0] = App_ClockAvalancheMix32(rawWords[0] ^ 0x9E3779B9u);
    mixedWords[1] = App_ClockAvalancheMix32(rawWords[1] + 0x9E3779B9u);
    mixedWords[2] = App_ClockAvalancheMix32(rawWords[2] + 0x9E3779B9u * 2u);

    /* 2단계: 세 워드를 서로 결합하여 워드 간 상관관계까지 제거 */
    combined = mixedWords[0];
    combined = App_ClockAvalancheMix32(combined ^ mixedWords[1]);
    combined = App_ClockAvalancheMix32(combined ^ mixedWords[2]);
    mixedWords[0] = combined;
    mixedWords[1] = App_ClockAvalancheMix32(combined + rawWords[0]);
    mixedWords[2] = App_ClockAvalancheMix32(combined + rawWords[1] + rawWords[2]);

    /* 3단계: 이미 초기화되어 있는 하드웨어 CRC로 최종 확산 (기존 구조 재사용) */
    return HAL_CRC_Calculate(&hcrc, mixedWords, 3u);
}

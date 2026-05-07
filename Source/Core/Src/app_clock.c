#include "app_clock.h"
#include "app_hw.h"

#include <string.h>

#include "app_build_config.h"

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
  sDate.Year = year;

  HAL_RTC_SetTime(APP_RTC_HANDLE, &sTime, RTC_FORMAT_BIN);
  HAL_RTC_SetDate(APP_RTC_HANDLE, &sDate, RTC_FORMAT_BIN);
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

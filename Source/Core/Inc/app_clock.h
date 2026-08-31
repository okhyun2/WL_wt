#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "app_error.h"

/**
 * @file    app_clock.h
 * @brief   Boot-time clock validation and runtime clock context.
 */

/**
 * @brief Supported system clock source type.
 */
typedef enum
{
    APP_CLOCK_SOURCE_UNKNOWN = 0,
    APP_CLOCK_SOURCE_MSI,
    APP_CLOCK_SOURCE_HSI16,
    APP_CLOCK_SOURCE_HSE,
    APP_CLOCK_SOURCE_PLL
} AppClockSource_t;

/**
 * @brief Captured clock tree information.
 */
typedef struct
{
    uint8_t initialized;
    uint8_t lseReady;
    uint32_t msiRange;
    uint32_t flashLatency;
    uint32_t sysclkHz;
    uint32_t hclkHz;
    uint32_t pclk1Hz;
    uint32_t pclk2Hz;
    AppClockSource_t sysclkSource;
    AppClockSource_t rtcSourceAtBoot;   /* SystemClock_Config() 직후 확정된 소스 */
    AppClockSource_t rtcSourceCurrent;  /* App_ClockInit() 호출 시점의 소스 */
} AppClockContext_t;

#define APP_RTC_TIME_STR_LEN     (20u)   /* "yyyy-mm-dd hh:mm:ss" + NUL = 20 */

typedef struct
{
    uint16_t year;          /* 2000~2099 */
    uint8_t  month;         /* 1~12 */
    uint8_t  day;           /* 1~31 */
    uint8_t  hour;          /* 0~23 */
    uint8_t  minute;        /* 0~59 */
    uint8_t  second;        /* 0~59 */
} AppDateTime_t;

typedef enum
{
    APP_RTC_CLOCK_SOURCE_UNKNOWN = 0,
    APP_RTC_CLOCK_SOURCE_LSI,
    APP_RTC_CLOCK_SOURCE_LSE
} AppRtcClockSource_t;

const char *App_ClockRtcSourceToString(AppRtcClockSource_t source);

/* 부팅 시 LSI로 즉시 RTC 클럭을 확정하고 LSE는 백그라운드로 기동 시작 */
AppStatus_t App_ClockBootStartLsiFirst(void);

/* main loop/FSM housekeeping에서 주기적으로 호출: LSE 준비되면 시각 보존 후 전환 */
void App_ClockPollLseAndSwitchIfReady(void);

/* RCC->CSR 기반으로 현재 RTC가 어떤 소스로 구동 중인지 조회 (별도 상태 저장 불필요) */
AppRtcClockSource_t App_ClockGetActiveRtcSource(void);

/**
 * @brief Validate SystemClock_Config() result and capture clock context.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_ClockInit(void);

/**
 * @brief Restore boot clock tree after STOP wake-up and refresh clock context.
 *
 * @return APP_STATUS_OK on success, error code otherwise.
 */
AppStatus_t App_ClockRecoverAfterStop(void);

/**
 * @brief Check whether clock context was initialized.
 *
 * @return APP_TRUE when initialized, otherwise APP_FALSE.
 */
uint8_t App_ClockIsInitialized(void);

/**
 * @brief Get immutable clock context.
 *
 * @return Pointer to internal clock context.
 */
const AppClockContext_t *App_ClockGetContext(void);

// 전역 보정값 (ms 단위)
extern uint32_t g_tick_offset;
uint32_t CalcElapsedMs(uint64_t before_ms, uint64_t after_ms);
uint32_t GetCorrectedTick(void);
void RTC_SetTime(int year, int month, int date, int hour, int min, int sec);
AppStatus_t RTC_GetTime(AppDateTime_t *pDateTime);
AppStatus_t RTC_PrintTime(void);
uint64_t RTC_GetTimeMs(void);
static inline uint8_t IsUpdatedRTC(void)
{
    return (RTC->ISR & RTC_ISR_INITS) != 0u;
}
uint32_t App_ClockGetDeviceUidHash(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOCK_H */

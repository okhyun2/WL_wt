#include "app_clock.h"

#include <string.h>

#include "app_build_config.h"

/**
 * @file    app_clock.c
 * @brief   Boot-time clock validation and clock tree capture.
 */

/** @brief Internal clock context. */
static AppClockContext_t g_appClockContext;

/**
 * @brief Convert HAL SYSCLK source status value to application enum.
 *
 * @param halSource HAL RCC SYSCLK status value.
 * @return Converted application clock source.
 */
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

AppStatus_t App_ClockInit(void)
{
    RCC_OscInitTypeDef oscConfig;
    uint32_t halSysClkSource;

    (void)memset(&g_appClockContext, 0, sizeof(g_appClockContext));
    (void)memset(&oscConfig, 0, sizeof(oscConfig));

    halSysClkSource = __HAL_RCC_GET_SYSCLK_SOURCE();
    g_appClockContext.sysclkSource = App_ClockDecodeSystemSource(halSysClkSource);
    g_appClockContext.lsiReady = (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) != RESET) ? APP_TRUE : APP_FALSE;
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
    //Check More depth
#if 0
    APP_RETURN_IF_FALSE(g_appClockContext.lsiReady == APP_TRUE, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(oscConfig.MSIState == RCC_MSI_ON, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(oscConfig.MSIClockRange == APP_CLOCK_MSI_RANGE_BOOT, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(g_appClockContext.sysclkHz == APP_CLOCK_SYSCLK_BOOT_HZ, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(g_appClockContext.hclkHz == APP_CLOCK_HCLK_BOOT_HZ, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(g_appClockContext.pclk1Hz == APP_CLOCK_PCLK1_BOOT_HZ, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(g_appClockContext.pclk2Hz == APP_CLOCK_PCLK2_BOOT_HZ, APP_STATUS_CLOCK_VERIFY_FAILED);
    APP_RETURN_IF_FALSE(g_appClockContext.flashLatency == APP_CLOCK_FLASH_LATENCY_BOOT, APP_STATUS_CLOCK_VERIFY_FAILED);
#endif

    g_appClockContext.initialized = APP_TRUE;

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

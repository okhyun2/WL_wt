#include "app_clock.h"

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
    //TODO Check More depth
#if 0
    APP_RETURN_IF_FALSE(g_appClockContext.lseReady == APP_TRUE, APP_STATUS_CLOCK_VERIFY_FAILED);
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

AppStatus_t App_ClockInit(void)
{
    (void)memset(&g_appClockContext, 0, sizeof(g_appClockContext));
    return App_ClockCaptureContext();
}

AppStatus_t App_ClockRecoverAfterStop(void)
{
    RCC_OscInitTypeDef oscConfig;
    RCC_ClkInitTypeDef clkConfig;
    RCC_PeriphCLKInitTypeDef periphClkInit;

    (void)memset(&oscConfig, 0, sizeof(oscConfig));
    (void)memset(&clkConfig, 0, sizeof(clkConfig));
    (void)memset(&periphClkInit, 0, sizeof(periphClkInit));

    oscConfig.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    oscConfig.MSIState = RCC_MSI_ON;
    oscConfig.MSICalibrationValue = 0u;
    oscConfig.MSIClockRange = APP_CLOCK_MSI_RANGE_BOOT;
    oscConfig.PLL.PLLState = RCC_PLL_NONE;
    APP_RETURN_IF_HAL_ERROR(HAL_RCC_OscConfig(&oscConfig), APP_STATUS_CLOCK_VERIFY_FAILED);

    clkConfig.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clkConfig.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
    clkConfig.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clkConfig.APB1CLKDivider = RCC_HCLK_DIV1;
    clkConfig.APB2CLKDivider = RCC_HCLK_DIV1;
    APP_RETURN_IF_HAL_ERROR(HAL_RCC_ClockConfig(&clkConfig, APP_CLOCK_FLASH_LATENCY_BOOT), APP_STATUS_CLOCK_VERIFY_FAILED);

    periphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1 | RCC_PERIPHCLK_USART2 | RCC_PERIPHCLK_LPUART1 | RCC_PERIPHCLK_I2C1 | RCC_PERIPHCLK_I2C3;
    periphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
    periphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
    periphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_PCLK1;
    periphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_PCLK1;
    periphClkInit.I2c3ClockSelection = RCC_I2C3CLKSOURCE_PCLK1;
    APP_RETURN_IF_HAL_ERROR(HAL_RCCEx_PeriphCLKConfig(&periphClkInit), APP_STATUS_CLOCK_VERIFY_FAILED);

    return App_ClockCaptureContext();
}

uint8_t App_ClockIsInitialized(void)
{
    return g_appClockContext.initialized;
}

const AppClockContext_t *App_ClockGetContext(void)
{
    return &g_appClockContext;
}

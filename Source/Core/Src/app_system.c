#include "app_system.h"
#include "main.h"

#include <string.h>

#include "app_build_config.h"
#include "app_clock.h"
#include "app_debug.h"
#include "app_gpio_lp.h"
#include "app_hw.h"
#include "app_log.h"
#include "app_fsm.h"
#include "app_msgq.h"
#include "app_selftest.h"

wakeup_context_t g_wakeup_ctx = {0};

static void handle_lptim1_wakeup(uint32_t flags)
{
    if (flags & WAKEUP_FLAG_LPTIM1_ARR) {
        /* Auto-Reload Match: 주기적 타이머 처리 */
        App_SystemHandleLptim1AutoReloadMatchCallback();
    }
    
    if (flags & WAKEUP_FLAG_LPTIM1_CMP) {
        /* Compare Match: 스케줄된 이벤트 처리 */
        // 예: scheduled_event_execute();
    }
}

static void handle_rtc_alarm_wakeup(uint32_t flags)
{
    if (flags & WAKEUP_FLAG_RTC_ALARM_A) {
        /* RTC Alarm A 처리 */
        // 예: alarm_a_callback();
    }
    
    if (flags & WAKEUP_FLAG_RTC_ALARM_B) {
        /* RTC Alarm B 처리 */
        // 예: alarm_b_callback();
    }
}

static void handle_rtc_wut_wakeup(void)
{
  App_SystemHandleRtcCallBack();
}

static void handle_exti_wakeup(uint32_t flags)
{
    /* 각 핀을 개별적으로 처리 */
    for (uint8_t pin = 0; pin <= 15; pin++) {
        uint32_t pin_flag = (WAKEUP_FLAG_EXTI_PIN0 << pin);
        if (flags & pin_flag) {
            /* 해당 핀 인터럽트 처리 */
            App_SystemHandleExtiCallBack(1u<<pin);
        }
    }
}

void debug_print_wakeup_info(void)
{
    APP_LOGD("SYS", "[DEBUG] source cnt: %d", g_wakeup_ctx.source_count);
    if (g_wakeup_ctx.source_count > 0)
    {
        APP_LOGD("SYS", "[DEBUG] Multiple wakeup sources detected: %d", g_wakeup_ctx.source_count);
        APP_LOGD("SYS", "  Processed flags: 0x%08lX", g_wakeup_ctx.processed_flags);
        APP_LOGD("SYS", "  Raw registers - LPTIM: 0x%08lX, RTC: 0x%08lX, EXTI: 0x%08lX",
                 g_wakeup_ctx.raw_lptim_isr, g_wakeup_ctx.raw_rtc_isr, g_wakeup_ctx.raw_exti_pr);
    }
}

void wakeup_process_all_pending(void)
{
    __DSB();    // Data Synchronization Barrier
    __ISB();    // Instruction Synchronization Barrier

    /* Race Condition 방지: 플래그를 로컬 변수로 안전하게 복사 */
    __disable_irq();
    uint32_t pending_flags = g_wakeup_ctx.pending_flags;
    g_wakeup_ctx.pending_flags = WAKEUP_FLAG_NONE;
    __enable_irq();

    /* 처리할 플래그가 없으면 즉시 반환 */
    if (pending_flags == WAKEUP_FLAG_NONE) {
        return;
    }

    /* 소스 개수 계산 */
    uint32_t temp = pending_flags;
    g_wakeup_ctx.source_count = 0;
    while (temp) {
        g_wakeup_ctx.source_count += (temp & 1U);
        temp >>= 1;
    }

    /* 모든 소스를 순회하며 처리 (중간에 return 없이 전체 검사) */

    /* 1. LPTIM1 처리 */
    if (pending_flags & WAKEUP_MASK_LPTIM1) {
        handle_lptim1_wakeup(pending_flags & WAKEUP_MASK_LPTIM1);
        g_wakeup_ctx.processed_flags |= (pending_flags & WAKEUP_MASK_LPTIM1);
    }

    /* 2. RTC Alarm 처리 */
    if (pending_flags & WAKEUP_MASK_RTC_ALARM) {
        handle_rtc_alarm_wakeup(pending_flags & WAKEUP_MASK_RTC_ALARM);
        g_wakeup_ctx.processed_flags |= (pending_flags & WAKEUP_MASK_RTC_ALARM);
    }

    /* 3. RTC Wakeup Timer 처리 */
    if (pending_flags & WAKEUP_FLAG_RTC_WUT) {
        handle_rtc_wut_wakeup();
        g_wakeup_ctx.processed_flags |= WAKEUP_FLAG_RTC_WUT;
    }

    /* 4. 모든 EXTI 핀 처리 */
    uint32_t exti_flags = pending_flags & (WAKEUP_MASK_EXTI0_1 | WAKEUP_MASK_EXTI2_3 | WAKEUP_MASK_EXTI4_15);
    if (exti_flags) {
        handle_exti_wakeup(exti_flags);
        g_wakeup_ctx.processed_flags |= exti_flags;
    }

    /* 미처리 플래그 확인 */
    uint32_t unhandled = pending_flags & ~g_wakeup_ctx.processed_flags;
    if (unhandled) {
        /* 예상치 못한 wakeup 소스 처리 또는 오류 처리 */
        Error_Handler();
    }
}

///////////////////////////////////////////////////////

static const char *App_SystemBuildWakeSourceString(uint32_t wakeMask)
{
    uint8_t first;

    if (wakeMask == APP_SYSTEM_WAKE_SRC_NONE)
    {
        return "NONE";
    }

    g_appSystemWakeString[0] = '\0';
    first = APP_TRUE;

#define APP_SYSTEM_APPEND_WAKE(name_literal)                     \
    do                                                           \
    {                                                            \
        if (first == APP_FALSE)                                  \
        {                                                        \
            (void)strncat(g_appSystemWakeString, "|", sizeof(g_appSystemWakeString) - strlen(g_appSystemWakeString) - 1u); \
        }                                                        \
        (void)strncat(g_appSystemWakeString, (name_literal), sizeof(g_appSystemWakeString) - strlen(g_appSystemWakeString) - 1u); \
        first = APP_FALSE;                                       \
    } while (0)

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_NBIOT_RI) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("NBIOT_RI");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_NFC_ED) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("NFC_ED");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_REED) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("REED");
    }
#if 0	//ESI support
    if ((wakeMask & APP_SYSTEM_WAKE_SRC_ESI_INT) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("ESI_INT");
    }
#endif
    if ((wakeMask & APP_SYSTEM_WAKE_SRC_RTC) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("RTC");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_LPTIM) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("LPTIM");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_DEBUG_DRYRUN) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("DEBUG_DRYRUN");
    }

    if ((wakeMask & APP_SYSTEM_WAKE_SRC_UNKNOWN) != 0u)
    {
        APP_SYSTEM_APPEND_WAKE("UNKNOWN");
    }

#undef APP_SYSTEM_APPEND_WAKE

    return g_appSystemWakeString;
}

static uint8_t App_SystemCanDebugLog(void)
{
    const AppLogContext_t *p_logContext;

    p_logContext = App_LogGetContext();
    return ((g_appSystemContext.logReady == APP_TRUE) &&
            (p_logContext != NULL) &&
            (p_logContext->initialized == APP_TRUE)) ? APP_TRUE : APP_FALSE;
}

static void App_SystemResetStopQualification(void)
{
    g_appSystemContext.stopQualificationCount = 0u;
}

static AppStatus_t App_SystemRtcOpenBackupDomain(void)
{
    uint32_t timeoutLoops;

    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR, PWR_CR_DBP);

    timeoutLoops = APP_SYSTEM_RTC_TIMEOUT_LOOPS;
    while (((PWR->CR & PWR_CR_DBP) == 0u) && (timeoutLoops > 0u))
    {
        timeoutLoops--;
    }

    APP_RETURN_IF_FALSE((PWR->CR & PWR_CR_DBP) != 0u, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static void App_SystemRtcWriteProtectionDisable(void)
{
    RTC->WPR = APP_SYSTEM_RTC_WPR_KEY1;
    RTC->WPR = APP_SYSTEM_RTC_WPR_KEY2;
}

static void App_SystemRtcWriteProtectionEnable(void)
{
    RTC->WPR = 0xFFu;
}

static AppStatus_t App_SystemRtcWaitFlagSet(uint32_t flagMask)
{
    uint32_t timeoutLoops;

    timeoutLoops = APP_SYSTEM_RTC_TIMEOUT_LOOPS;
    while (((RTC->ISR & flagMask) == 0u) && (timeoutLoops > 0u))
    {
        timeoutLoops--;
    }

    APP_RETURN_IF_FALSE((RTC->ISR & flagMask) != 0u, APP_STATUS_INIT_FAILED);
    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRtcInitBase(void)
{
    AppStatus_t status;

    status = App_SystemRtcOpenBackupDomain();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    SET_BIT(RCC->CSR, RCC_CSR_LSEON);
    APP_RETURN_IF_FALSE((__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) != RESET), APP_STATUS_INIT_FAILED);

    if (((RCC->CSR & RCC_CSR_RTCSEL) != RCC_CSR_RTCSEL_LSE) || ((RCC->CSR & RCC_CSR_RTCEN) == 0u))
    {
        SET_BIT(RCC->CSR, RCC_CSR_RTCRST);
        CLEAR_BIT(RCC->CSR, RCC_CSR_RTCRST);
        MODIFY_REG(RCC->CSR, RCC_CSR_RTCSEL, RCC_CSR_RTCSEL_LSE);
        SET_BIT(RCC->CSR, RCC_CSR_RTCEN);
    }

    if ((RTC->ISR & RTC_ISR_INITS) == 0u)
    {
        App_SystemRtcWriteProtectionDisable();
        SET_BIT(RTC->ISR, RTC_ISR_INIT);
        status = App_SystemRtcWaitFlagSet(RTC_ISR_INITF);
        if (status != APP_STATUS_OK)
        {
            App_SystemRtcWriteProtectionEnable();
            return status;
        }

        CLEAR_BIT(RTC->CR, RTC_CR_FMT | RTC_CR_WUTE | RTC_CR_WUTIE);
        RTC->PRER = ((APP_RTC_LSE_ASYNC_PREDIV << RTC_PRER_PREDIV_A_Pos) & RTC_PRER_PREDIV_A) |
                    ((APP_RTC_LSE_SYNC_PREDIV << RTC_PRER_PREDIV_S_Pos) & RTC_PRER_PREDIV_S);
        RTC->TR = 0u;
        RTC->DR = (1u << RTC_DR_WDU_Pos) | (1u << RTC_DR_MU_Pos) | (1u << RTC_DR_DU_Pos);
        CLEAR_BIT(RTC->ISR, RTC_ISR_INIT);
        App_SystemRtcWriteProtectionEnable();
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRtcConfigureWakeupTimer(uint32_t *pWakeupSeconds)
{
    AppStatus_t status;
    uint32_t wakeupSeconds;

    wakeupSeconds = (APP_RTC_WAKEUP_PERIOD_MS + 999u) / 1000u;
    if (wakeupSeconds == 0u)
    {
        wakeupSeconds = 1u;
    }

    APP_RETURN_IF_FALSE(wakeupSeconds <= 0x10000u, APP_STATUS_INVALID_PARAM);

    status = App_SystemRtcInitBase();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    App_SystemRtcWriteProtectionDisable();
    CLEAR_BIT(RTC->CR, RTC_CR_WUTE | RTC_CR_WUTIE);
    status = App_SystemRtcWaitFlagSet(RTC_ISR_WUTWF);
    if (status != APP_STATUS_OK)
    {
        App_SystemRtcWriteProtectionEnable();
        return status;
    }

    CLEAR_BIT(RTC->ISR, RTC_ISR_WUTF);
    EXTI->PR = EXTI_PR_PIF20;
    RTC->WUTR = (wakeupSeconds - 1u) & RTC_WUTR_WUT;
    MODIFY_REG(RTC->CR, RTC_CR_WUCKSEL, RTC_CR_WUCKSEL_2);
    SET_BIT(RTC->CR, RTC_CR_WUTIE | RTC_CR_WUTE);
    App_SystemRtcWriteProtectionEnable();

    *pWakeupSeconds = wakeupSeconds;
    return APP_STATUS_OK;
}

static AppStatus_t App_SystemInitRtcWakeup(void)
{
    AppStatus_t status;

    status = App_SystemRtcInitBase();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    EXTI->IMR |= EXTI_IMR_IM20;
    EXTI->RTSR |= EXTI_RTSR_RT20;
    EXTI->FTSR &= ~EXTI_FTSR_FT20;
    EXTI->PR = EXTI_PR_PIF20;

    HAL_NVIC_SetPriority(RTC_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(RTC_IRQn);

    return APP_STATUS_OK;
}

const char *App_SystemGetLowPowerModeString(void)
{
    switch (g_appSystemContext.lastLowPowerMode)
    {
        case APP_SYSTEM_LP_MODE_SLEEP: return "SLEEP";
        case APP_SYSTEM_LP_MODE_STOP:  return "STOP";
        case APP_SYSTEM_LP_MODE_RUN:
        default:                       return "RUN";
    }
}

const char *App_SystemGetWakeSourceString(void)
{
    return App_SystemBuildWakeSourceString(g_appSystemContext.wakeSourceMask);
}

uint32_t App_SystemGetWakeSourceMask(void)
{
    return g_appSystemContext.wakeSourceMask;
}

void App_SystemNotifyWakeSource(uint32_t sourceMask)
{
    if (sourceMask == 0u)
    {
        return;
    }

    g_appSystemContext.wakeSourceMask |= sourceMask;
    g_appSystemContext.lastWakeTickMs = HAL_GetTick();
    g_appSystemContext.stopRequested = APP_FALSE;
    App_SystemResetStopQualification();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("WAKE",
                       "source=%s mask=0x%08lX tick=%lu",
                       App_SystemBuildWakeSourceString(g_appSystemContext.wakeSourceMask),
                       (unsigned long)g_appSystemContext.wakeSourceMask,
                       (unsigned long)g_appSystemContext.lastWakeTickMs);
    }
#endif
}

static void App_SystemQueueStateCommand(uint8_t nextState)
{
    (void)App_FsmQueueStateFront(nextState, 1u, 0u);
}

static void App_SystemConfigureWakeupInterrupts(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_LPTIM1_CLK_ENABLE();

    HAL_NVIC_SetPriority(LPTIM1_IRQn, 1u, 0);
    HAL_NVIC_EnableIRQ(LPTIM1_IRQn);
    HAL_NVIC_SetPriority(RTC_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(RTC_IRQn);
    HAL_NVIC_SetPriority(EXTI0_1_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI0_1_IRQn);
    HAL_NVIC_SetPriority(EXTI2_3_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI2_3_IRQn);
    HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2u, 0u);
    HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);
}

static AppStatus_t App_SystemValidateHandles(void)
{
    APP_RETURN_IF_FALSE(APP_UART_DEBUG_HANDLE->Instance == USART1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_UART_METER_HANDLE->Instance == USART2, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_UART_NBIOT_HANDLE->Instance == LPUART1, APP_STATUS_HW_HANDLE_INVALID);
    #if 0	//ESI support
    APP_RETURN_IF_FALSE(APP_I2C_ESI_HANDLE->Instance == I2C1, APP_STATUS_HW_HANDLE_INVALID);
    #endif
    APP_RETURN_IF_FALSE(APP_I2C_NFC_HANDLE->Instance == I2C2, APP_STATUS_HW_HANDLE_INVALID);
#if 0	//Temp support
    APP_RETURN_IF_FALSE(APP_I2C_AUX_HANDLE->Instance == I2C3, APP_STATUS_HW_HANDLE_INVALID);
#endif
    APP_RETURN_IF_FALSE(APP_ADC_BATTERY_HANDLE->Instance == ADC1, APP_STATUS_HW_HANDLE_INVALID);
    APP_RETURN_IF_FALSE(APP_CRC_HANDLE->Instance == CRC, APP_STATUS_HW_HANDLE_INVALID);
    return APP_STATUS_OK;
}

static void App_SystemApplySafeOutputs(void)
{
    App_HwSetNbiotEnable(GPIO_PIN_RESET);
    App_HwSetNbiotReset(GPIO_PIN_SET);
    App_HwSetChargeBoot0(GPIO_PIN_RESET);
}

static AppStatus_t App_SystemInitLowPowerGpio(void)
{
    AppStatus_t status;

    App_GpioLpGetDefaultConfig(&g_appGpioLpConfig);

    if ((APP_BUILD_IS_PRODUCTION == APP_TRUE) &&
        (APP_GPIO_LP_DISABLE_SWD_IN_PRODUCTION == APP_TRUE))
    {
        g_appGpioLpConfig.swdPolicy = APP_GPIO_LP_SWD_DISABLE_IN_PRODUCTION;
    }
    else
    {
        g_appGpioLpConfig.swdPolicy = APP_GPIO_LP_SWD_KEEP;
    }

    g_appGpioLpConfig.keepDebugUartPinsInStop = APP_FALSE;
    g_appGpioLpConfig.keepMeterUartPinsInStop = APP_FALSE;
    #if 0	//ESI support
    g_appGpioLpConfig.keepEsiI2cPinsInStop = APP_FALSE;
	#endif
    g_appGpioLpConfig.keepNfcI2cPinsInStop = APP_FALSE;
#if 0	//Temp support
    g_appGpioLpConfig.keepTempI2cPinsInStop = APP_FALSE;
#endif
    g_appGpioLpConfig.keepPiezoPinInStop = APP_FALSE;
    g_appGpioLpConfig.keepExternalWatchdogPinInStop = APP_FALSE;
    g_appGpioLpConfig.stopClockDisableMask =
        APP_GPIO_LP_CLK_ADC1 |
        APP_GPIO_LP_CLK_CRC |
        APP_GPIO_LP_CLK_USART1 |
        APP_GPIO_LP_CLK_USART2 |
        APP_GPIO_LP_CLK_LPUART1 |
#if 0	//ESI support
        APP_GPIO_LP_CLK_I2C1 |
#endif
        APP_GPIO_LP_CLK_I2C2;
#if 0	//Temp support
        APP_GPIO_LP_CLK_I2C3;
#endif

    status = App_GpioLpInit(&g_appGpioLpConfig);
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_GPIO_LP_INIT_FAILED;
    }

    status = App_GpioLpSetNbiotPowered(APP_FALSE);
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_GpioLpApplyRunBaseState();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemPrintBootLogs(void)
{
    const AppClockContext_t *p_clockContext;

    p_clockContext = App_ClockGetContext();

    APP_RETURN_IF_FALSE(App_DebugConsoleWriteString(APP_DEBUG_CONSOLE_BANNER APP_DEBUG_CONSOLE_EOL) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_LogInit() == APP_STATUS_OK, APP_STATUS_LOG_INIT_FAILED);

    g_appSystemContext.logReady = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_LOG_READY;

    APP_RETURN_IF_FALSE(APP_LOGI("SYS", "Boot complete: %s v%s", APP_NAME_STRING, App_SystemGetVersionString()) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("CLK", "SYS=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu MSI=%lu LSE=%u",
                                 (unsigned long)p_clockContext->sysclkHz,
                                 (unsigned long)p_clockContext->hclkHz,
                                 (unsigned long)p_clockContext->pclk1Hz,
                                 (unsigned long)p_clockContext->pclk2Hz,
                                 (unsigned long)p_clockContext->msiRange,
                                 (unsigned int)p_clockContext->lseReady) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("GPIO", "LP policy ready: SWD=%lu",
                                 (unsigned long)g_appGpioLpConfig.swdPolicy) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("RTC", "STOP wake period=%lu ms (%s)",
                                 (unsigned long)APP_RTC_WAKEUP_PERIOD_MS,
                                 (APP_RTC_WAKEUP_PERIOD_MS == 0) ? "Don't work stop":"LSE rtc wakeup") == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("DBG", "USART1 debug console ready at %lu baud",
                                 (unsigned long)APP_UART_DEBUG_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("METER", "USART2 meter ready at %lu baud",
                                 (unsigned long)APP_UART_METER_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("NBIoT", "LPUART1 NBIoT ready at %lu baud",
                                 (unsigned long)APP_UART_NBIOT_HANDLE->Init.BaudRate) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(App_DebugConsolePrintPrompt() == APP_STATUS_OK, APP_STATUS_UART_TX_FAILED);
    APP_RETURN_IF_FALSE(APP_LOGI("SYS", "Boot path complete: clock/log/debug ready") == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemRunBootSelfTest(void)
{
    AppStatus_t status;

    status = App_SelfTestInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_SELFTEST_INIT_FAILED;
    }

    status = App_SelfTestRunBootSequence();
    g_appSystemContext.selfTestCompleted = APP_TRUE;
    g_appSystemContext.selfTestStatus = status;
    g_appSystemContext.selfTestFailed = (status == APP_STATUS_OK) ? APP_FALSE : APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_SELFTEST_DONE;

    if (status == APP_STATUS_OK)
    {
        APP_RETURN_IF_FALSE(APP_LOGI("SELF", "Boot self-test finished without failures") == APP_STATUS_OK,
                            APP_STATUS_UART_TX_FAILED);
        return APP_STATUS_OK;
    }

    APP_RETURN_IF_FALSE(APP_LOGW("SELF", "Boot self-test completed with one or more failures") == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    if (APP_SELFTEST_FAIL_STOPS_BOOT == APP_TRUE)
    {
        return status;
    }

    return APP_STATUS_OK;
}

static AppStatus_t App_SystemInitFsm(void)
{
    AppStatus_t status;

    status = App_MsgqInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_FsmInit();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.fsmReady = APP_TRUE;
    g_appSystemContext.fsmStatus = APP_STATUS_OK;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_FSM_READY;

    APP_RETURN_IF_FALSE(APP_LOGI("FSM", "FSM ready: queue=%u state=%s",
                                 (unsigned int)APP_MSGQ_CAPACITY,
                                 App_FsmGetCurrentStateString()) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);

    return APP_STATUS_OK;
}

static uint64_t rtc_time_before_stop = 0;

static AppStatus_t App_SystemEnterStopMode(void)
{
    AppStatus_t status;

    (void)APP_LOGI("LP", "Enter STOP mode");

    g_appSystemContext.stopCandidateCount++;

    if( (g_appSystemContext.oldWakeSourceMask == APP_SYSTEM_WAKE_SRC_NONE) ||
        (g_appSystemContext.oldWakeSourceMask & APP_SYSTEM_WAKE_SRC_RTC) )
    {
        uint32_t wakeupSeconds = 0;
        (void)APP_LOGD("LP", "RTCConfigureWakeupTimer");
        g_appSystemContext.oldWakeSourceMask &= ~APP_SYSTEM_WAKE_SRC_RTC;
        status = App_SystemRtcConfigureWakeupTimer(&wakeupSeconds);
        if (status != APP_STATUS_OK)
        {
            (void)APP_LOGE("LP", "RtcConfigureWakeupTimer");
            return status;
        }

        (void)APP_LOGI("RTC", "STOP periodic:%ds", wakeupSeconds);
    }
    else if(g_appSystemContext.oldWakeSourceMask & APP_SYSTEM_WAKE_SRC_LPTIM)
    {
        float fTemp = 0.0;
        uint32_t wakeupSeconds = 0;
        uint32_t lptim1_prescaler = (APP_SYSTEM_LPTIM1_PRESCALER == LPTIM_PRESCALER_DIV32) ? 32:
                                    ((APP_SYSTEM_LPTIM1_PRESCALER == LPTIM_PRESCALER_DIV64) ? 64: 128);
        fTemp = (((APP_SYSTEM_LPTIM1_ARR+1) * lptim1_prescaler)/32768) + 0.5;
        wakeupSeconds = (uint32_t)fTemp;
        g_appSystemContext.oldWakeSourceMask &= ~APP_SYSTEM_WAKE_SRC_LPTIM;
        (void)APP_LOGI("LPTIM", "STOP periodic:%ds", wakeupSeconds);
    }

    status = App_SystemPrepareForStop();
    if (status != APP_STATUS_OK)
    {
        (void)APP_LOGE("LP", "systemPrepareForStop");
        return status;
    }

    g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_STOP;
    g_appSystemContext.lastStopEntryTickMs = HAL_GetTick();
    g_appSystemContext.wakeSourceMask = APP_SYSTEM_WAKE_SRC_NONE;

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        // this don't print because stopped external interface.
        (void)APP_LOGI("LP", "STOP candidate=%lu qual=%u idle=%lu sleep=%lu stop=%lu dryrun=%u",
                       (unsigned long)g_appSystemContext.stopCandidateCount,
                       (unsigned int)g_appSystemContext.stopQualificationCount,
                       (unsigned long)g_appSystemContext.idleCounter,
                       (unsigned long)g_appSystemContext.sleepEntryCount,
                       (unsigned long)g_appSystemContext.stopEntryCount,
                       (unsigned int)APP_LP_STOP_DEBUG_DRY_RUN);
    }
#endif

#if (APP_LP_STOP_DEBUG_DRY_RUN == APP_TRUE)
    g_appSystemContext.stopDryRunCount++;
    App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_DEBUG_DRYRUN);
    status = App_SystemRecoverFromStop();
    if (status != APP_STATUS_OK)
    {
        return status;
    }
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        // this don't print because stopped external interface.
        (void)APP_LOGW("LP", "STOP dry-run only: candidate=%lu dryrun=%lu wake=%s",
                       (unsigned long)g_appSystemContext.stopCandidateCount,
                       (unsigned long)g_appSystemContext.stopDryRunCount,
                       App_SystemGetWakeSourceString());
    }
#endif
    return APP_STATUS_OK;
#else

    // Stop 진입 전 RTC 시간 저장
    rtc_time_before_stop = RTC_GetTimeMs();

    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    /*
    * STOP mode
    */

    status = App_ClockRecoverAfterStop();
    HAL_ResumeTick();
    if (status != APP_STATUS_OK)
    {
        return status;
    }
    status = App_SystemRecoverFromStop();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    // Wake-up 후 경과 시간 계산 및 보정
    uint64_t rtc_time_after = RTC_GetTimeMs() + 100; //100msec tunnning
    uint32_t elapsed_ms = CalcElapsedMs(rtc_time_before_stop, rtc_time_after);
    
    // 전역 offset에 누락된 시간 누적
    g_tick_offset += elapsed_ms;

    wakeup_process_all_pending();
#ifdef DEBUG
    debug_print_wakeup_info();
#endif
    g_appSystemContext.oldWakeSourceMask |= g_appSystemContext.wakeSourceMask;

    if (g_appSystemContext.wakeSourceMask == APP_SYSTEM_WAKE_SRC_NONE)
    {
        App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_UNKNOWN);
    }

    g_appSystemContext.stopEntryCount++;

    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGI("LP", "STOP exit wake=%s, mask=%x count=%lu tick=%lu",
                       App_SystemGetWakeSourceString(),
                       g_appSystemContext.wakeSourceMask,
                       (unsigned long)g_appSystemContext.stopEntryCount,
                       (unsigned long)g_appSystemContext.lastWakeTickMs);
    }

    return APP_STATUS_OK;
#endif
}

static void App_SystemHandleIdle(void)
{
    AppStatus_t status;
    const AppFsmSummary_t *p_fsmSummary;

    p_fsmSummary = App_FsmGetSummary();
    g_appSystemContext.idleCounter++;

    if (g_appSystemContext.stopRequested == APP_TRUE)
    {
        if (g_appSystemContext.stopQualificationCount < 0xFFu)
        {
            if (APP_RTC_WAKEUP_PERIOD_MS > 0)
            {
                g_appSystemContext.stopQualificationCount++;
            }
        }

#ifdef DEBUG
        if (App_SystemCanDebugLog() == APP_TRUE)
        {
            (void)APP_LOGD("LP",
                           "STOP qualify: step=%u/%u decision=%s idle=%lu dispatch=%lu",
                           (unsigned int)g_appSystemContext.stopQualificationCount,
                           (unsigned int)APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT,
                           App_FsmGetDecisionString(),
                           (unsigned long)g_appSystemContext.idleCounter,
                           (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u));
        }
#endif

        if (g_appSystemContext.stopQualificationCount >= APP_LP_STOP_MIN_IDLE_QUALIFY_COUNT)
        {
            g_appSystemContext.stopRequested = APP_FALSE;
            App_SystemResetStopQualification();
            status = App_SystemEnterStopMode();
            if (status != APP_STATUS_OK)
            {
                g_appSystemContext.fsmStatus = status;
                App_ErrorRecord(status, __FILE__, __LINE__);
                (void)APP_LOGE("LP", "STOP entry/exit failed: status=%lu", (unsigned long)status);
            }
            return;
        }
    }
    else
    {
        App_SystemResetStopQualification();
    }

#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("SYS",
                       "Entering idle path: mode=%s stop_req=%u idle=%lu dispatch=%lu",
                       (APP_FSM_USE_WFI_IDLE == APP_TRUE) ? "WFI" : "delay",
                       (unsigned int)g_appSystemContext.stopRequested,
                       (unsigned long)g_appSystemContext.idleCounter,
                       (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u));
    }
#endif

    if (APP_FSM_USE_WFI_IDLE == APP_TRUE)
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_SLEEP;
        g_appSystemContext.lastSleepEntryTickMs = HAL_GetTick();
        g_appSystemContext.sleepEntryCount++;
        __WFI();
    }
    else
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;
        HAL_Delay(APP_FSM_IDLE_DELAY_MS);
    }
}

void App_SystemHandleLptim1AutoReloadMatchCallback(void)
{
    g_appSystemContext.lptimWakeEventCount++;
    App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_LPTIM);
    App_SystemQueueStateCommand(APP_FSM_STATE_LPTIM_WAKE_SERVICE);
}

void App_SystemHandleRtcCallBack(void)
{
    g_appSystemContext.rtcWakeEventCount++;
    App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_RTC);
    App_SystemQueueStateCommand(APP_FSM_STATE_RTC_WAKE_SERVICE);
    App_SystemQueueStateCommand(APP_FSM_STATE_WATCHDOG_FEED);
}

void App_SystemHandleExtiCallBack(uint16_t GPIO_Pin)
{
    switch (GPIO_Pin)
    {
        case NFC_ED_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_NFC_ED);
            App_SystemQueueStateCommand(APP_FSM_STATE_NFC_WAIT_EVENT);
            break;

        case REED_IN_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_REED);
            App_SystemQueueStateCommand(APP_FSM_STATE_METER_WAIT_TRIGGER);
            break;
#if 0	//ESI support
        case ESI_Int_Pin:
            App_SystemNotifyWakeSource(APP_SYSTEM_WAKE_SRC_ESI_INT);
            App_SystemQueueStateCommand(APP_FSM_STATE_HOUSEKEEPING_SNAPSHOT);
            break;
#endif
        default:
            break;
    }
}

AppStatus_t App_SystemInit(void)
{
    AppStatus_t status;
    const AppClockContext_t *clockContext;

    (void)memset(&g_appSystemContext, 0, sizeof(g_appSystemContext));
    App_ErrorInit();

    g_appSystemContext.bootStage = APP_BOOT_STAGE_HAL_READY;
    g_appSystemContext.selfTestStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSystemContext.fsmStatus = APP_STATUS_NOT_INITIALIZED;
    g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;

    APP_RETURN_IF_FALSE(App_ClockIsInitialized() == APP_TRUE, APP_STATUS_CLOCK_NOT_INITIALIZED);

    clockContext = App_ClockGetContext();
    APP_RETURN_IF_FALSE(clockContext->initialized == APP_TRUE, APP_STATUS_CLOCK_NOT_INITIALIZED);

    g_appSystemContext.bootSysClockHz = clockContext->sysclkHz;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_CLOCK_READY;

    status = App_SystemValidateHandles();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.bootStage = APP_BOOT_STAGE_PERIPH_READY;
    App_SystemApplySafeOutputs();
    App_SystemConfigureWakeupInterrupts();

    status = App_SystemInitRtcWakeup();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemInitLowPowerGpio();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    g_appSystemContext.bootStage = APP_BOOT_STAGE_GPIO_LP_READY;

    status = App_DebugConsoleInit();
    if (status != APP_STATUS_OK)
    {
        return APP_STATUS_DEBUG_INIT_FAILED;
    }

    g_appSystemContext.debugReady = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_DEBUG_READY;

    status = App_SystemPrintBootLogs();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemRunBootSelfTest();
    if (status != APP_STATUS_OK)
    {
        return status;
    }

    status = App_SystemInitFsm();
    if (status != APP_STATUS_OK)
    {
        g_appSystemContext.fsmStatus = status;
        return status;
    }

    g_appSystemContext.initialized = APP_TRUE;
    g_appSystemContext.bootStage = APP_BOOT_STAGE_APP_READY;

#ifdef DEBUG
    APP_RETURN_IF_FALSE(APP_LOGD("SYS", "Application ready: boot=%lu/%lu stop_req=%u",
                                 (unsigned long)g_appSystemContext.bootStage,
                                 (unsigned long)APP_BOOT_STAGE_APP_READY,
                                 (unsigned int)g_appSystemContext.stopRequested) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
#endif

    APP_RETURN_IF_FALSE(APP_LOGI("SYS", "RTC Set(%04d-%02d-%02d %02d:%02d:%02d)",
                        APP_DEFAULT_RTC_YEAR, APP_DEFAULT_RTC_MONTH, APP_DEFAULT_RTC_DAY, APP_DEFAULT_RTC_HOUR, APP_DEFAULT_RTC_MIN, APP_DEFAULT_RTC_SEC) == APP_STATUS_OK,
                        APP_STATUS_UART_TX_FAILED);
    RTC_SetTime(APP_DEFAULT_RTC_YEAR, APP_DEFAULT_RTC_MONTH, APP_DEFAULT_RTC_DAY, APP_DEFAULT_RTC_HOUR, APP_DEFAULT_RTC_MIN, APP_DEFAULT_RTC_SEC);

    return APP_STATUS_OK;
}

void App_SystemProcess(void)
{
    AppStatus_t status;
    const AppFsmSummary_t *p_fsmSummary;

    if (g_appSystemContext.initialized != APP_TRUE)
    {
        App_ErrorRecord(APP_STATUS_NOT_INITIALIZED, __FILE__, __LINE__);
        App_ErrorTrap();
    }

    status = App_FsmRun();
    g_appSystemContext.fsmStatus = status;
    if (status != APP_STATUS_OK)
    {
        App_ErrorRecord(status, __FILE__, __LINE__);
#ifdef DEBUG
        (void)APP_LOGE("SYS", "FSM run failed: status=%lu", (unsigned long)status);
#endif
    }

    p_fsmSummary = App_FsmGetSummary();
    if (((p_fsmSummary != NULL) && (p_fsmSummary->lastLoopDispatchCount == 0u)) ||
        (g_appSystemContext.stopRequested == APP_TRUE))
    {
#ifdef DEBUG
        if ((p_fsmSummary != NULL) &&
            (p_fsmSummary->lastLoopDispatchCount != 0u) &&
            (g_appSystemContext.stopRequested == APP_TRUE) &&
            (App_SystemCanDebugLog() == APP_TRUE))
        {
            (void)APP_LOGD("LP",
                           "idle gate forced: dispatch=%lu decision=%s stop_req=%u",
                           (unsigned long)p_fsmSummary->lastLoopDispatchCount,
                           App_FsmGetDecisionString(),
                           (unsigned int)g_appSystemContext.stopRequested);
        }
#endif
        App_SystemHandleIdle();
    }
    else
    {
        g_appSystemContext.lastLowPowerMode = APP_SYSTEM_LP_MODE_RUN;
        App_SystemResetStopQualification();
    }

    g_appSystemContext.loopCounter++;
}

AppStatus_t App_SystemOnBeforeStopEnter(void)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    status = App_GpioLpOnBeforeStopEnter();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("LP", "before STOP status=%lu", (unsigned long)status);
    }
#endif
    return status;
}

AppStatus_t App_SystemOnAfterStopExit(void)
{
    AppStatus_t status;

    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    status = App_GpioLpOnAfterStopExit();
#ifdef DEBUG
    if (App_SystemCanDebugLog() == APP_TRUE)
    {
        (void)APP_LOGD("LP", "after STOP status=%lu wake=%s", (unsigned long)status, App_SystemGetWakeSourceString());
    }
#endif
    return status;
}

AppStatus_t App_SystemPrepareForStop(void)
{
    return App_SystemOnBeforeStopEnter();
}

AppStatus_t App_SystemRecoverFromStop(void)
{
    return App_SystemOnAfterStopExit();
}

AppStatus_t App_SystemSetNbiotPowered(uint8_t powered)
{
    APP_RETURN_IF_FALSE((g_appSystemContext.bootStage >= APP_BOOT_STAGE_GPIO_LP_READY), APP_STATUS_NOT_INITIALIZED);
    return App_GpioLpSetNbiotPowered(powered);
}

AppStatus_t App_SystemRequestLowPower(uint8_t allowStop)
{
    uint8_t previousRequest;
#ifdef DEBUG
    const AppFsmSummary_t *p_fsmSummary;
#endif

    APP_RETURN_IF_FALSE((allowStop == APP_FALSE) || (allowStop == APP_TRUE), APP_STATUS_INVALID_PARAM);
    previousRequest = g_appSystemContext.stopRequested;
    g_appSystemContext.stopRequested = allowStop;
    if (allowStop == APP_TRUE)
    {
        g_appSystemContext.lastStopRequestTickMs = HAL_GetTick();
    }
    else
    {
        App_SystemResetStopQualification();
    }
#ifdef DEBUG
    p_fsmSummary = App_FsmGetSummary();
    if ((previousRequest != allowStop) && (App_SystemCanDebugLog() == APP_TRUE))
    {
        (void)APP_LOGD("LP",
                       "stop_request=%u decision=%s dispatch=%lu qualify=%u",
                       (unsigned int)allowStop,
                       App_FsmGetDecisionString(),
                       (unsigned long)((p_fsmSummary != NULL) ? p_fsmSummary->lastLoopDispatchCount : 0u),
                       (unsigned int)g_appSystemContext.stopQualificationCount);
    }
#endif
    return APP_STATUS_OK;
}

const AppSystemContext_t *App_SystemGetContext(void)
{
    return &g_appSystemContext;
}

const char *App_SystemGetVersionString(void)
{
    return g_appVersionString;
}

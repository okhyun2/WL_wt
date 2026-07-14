/**
 * @file    nfc_lowpower.c
 * @brief   NFC Low Power Manager  [v2.2.0]
 *
 * Changes from v2.1:
 *  - Debounce logic moved HERE from IRQ handler (H5 fix)
 *  - total_active_ms measured around callback only (M3 fix)
 */

#include "nfc_lowpower.h"
#include <stdio.h>
#include <string.h>
#include "app_log.h"

/* ============================================================
 * Init
 * ============================================================ */
NFC_Result_t NFC_LP_Init(NFC_LP_Handle_t *hlp, NFC_NTP53321_Handle_t *hntag)
{
    if (hlp == NULL || hntag == NULL)
        return NFC_RESULT_ERROR_INVALID_PARAM;

    memset(hlp, 0, sizeof(NFC_LP_Handle_t));
    hlp->hntag       = hntag;
    hlp->initialized = true;

    APP_LOGI("NFC", "Low power manager initialized");
    return NFC_RESULT_OK;
}

/* ============================================================
 * Callback Registration
 * ============================================================ */
void NFC_LP_RegisterCallbacks(NFC_LP_Handle_t *hlp,
                               void (*OnWakeup)(NFC_WakeupEvent_t),
                               void (*OnPreSleep)(void))
{
    if (hlp == NULL) return;
    hlp->OnWakeup_Callback   = OnWakeup;
    hlp->OnPreSleep_Callback = OnPreSleep;
    APP_LOGI("NFC", "Callbacks registered");
}

/* ============================================================
 * Enter Stop
 * ============================================================ */
NFC_Result_t NFC_LP_EnterStop(NFC_LP_Handle_t *hlp)
{
    uint32_t     sleep_start;
    NFC_Result_t ret;

    if (hlp == NULL || !hlp->initialized)
        return NFC_RESULT_ERROR_NOT_INIT;

    if (hlp->OnPreSleep_Callback != NULL)
        hlp->OnPreSleep_Callback();

    sleep_start = HAL_GetTick();
    ret         = NFC_NTP53321_EnterStandby(hlp->hntag);

    hlp->stats.total_sleep_ms += (HAL_GetTick() - sleep_start);
    return ret;
}

/* ============================================================
 * Handle Wakeup
 *
 * [FIX v2.2 - H5] Debounce moved from IRQ to here.
 *   HAL_GetTick() is unreliable inside IRQ just after Stop wakeup
 *   because SysTick is suspended; check here instead.
 *
 * [FIX v2.2 - M3] active_ms measures only callback processing time,
 *   not LED delay time.
 * ============================================================ */
NFC_Result_t NFC_LP_HandleWakeup(NFC_LP_Handle_t *hlp)
{
    static uint32_t   last_wakeup_tick = 0U;
    uint32_t          now;
    uint32_t          active_start;
    NFC_WakeupEvent_t event = NFC_WAKEUP_EVENT_UNKNOWN;

    if (hlp == NULL || !hlp->initialized)
        return NFC_RESULT_ERROR_NOT_INIT;

    now = HAL_GetTick();

    /* Debounce: ignore wakeup events within NFC_ED_DEBOUNCE_MS */
    if ((now - last_wakeup_tick) < NFC_ED_DEBOUNCE_MS) {
        APP_LOGD("NFC", "Debounce filtered (%lu ms elapsed)",
               (unsigned long)(now - last_wakeup_tick));
        NFC_NTP53321_ClearEDFlag(hlp->hntag);
        return NFC_RESULT_OK;
    }
    last_wakeup_tick = now;

    hlp->stats.total_wakeups++;

    if (NFC_NTP53321_IsEDTriggered(hlp->hntag)) {
        event = NFC_WAKEUP_EVENT_ED_PIN;
        NFC_NTP53321_ClearEDFlag(hlp->hntag);
        APP_LOGD("NFC", "Wakeup: ED pin (total=%lu)",
               (unsigned long)hlp->stats.total_wakeups);
    } else {
        APP_LOGW("NFC", "Wakeup: unknown event");
    }

    /* Measure processing time (callback only, excludes LED delays) */
    active_start = HAL_GetTick();
    NFC_NTP53321_ExitStandby(hlp->hntag);

    if (hlp->OnWakeup_Callback != NULL)
        hlp->OnWakeup_Callback(event);

    hlp->stats.total_active_ms   += (HAL_GetTick() - active_start);
    hlp->stats.avg_current_x100   = NFC_LP_GetAvgCurrent_x100(hlp);
    hlp->stats.last_active_start_ms = active_start;

    return NFC_RESULT_OK;
}

/* ============================================================
 * Average Current (fixed-point, no FPU)
 * Unit: 0.01 µA  (divide result by 100 to get µA)
 * ============================================================ */
uint32_t NFC_LP_GetAvgCurrent_x100(NFC_LP_Handle_t *hlp)
{
    uint32_t total_ms;
    uint64_t numerator;

    if (hlp == NULL) return 0U;

    total_ms = hlp->stats.total_active_ms + hlp->stats.total_sleep_ms;
    if (total_ms == 0U) return 0U;

    numerator = ((uint64_t)hlp->stats.total_active_ms *
                 (uint64_t)NFC_LP_ACTIVE_CURRENT_X100) +
                ((uint64_t)hlp->stats.total_sleep_ms  *
                 (uint64_t)NFC_LP_STOP_CURRENT_X100);

    return (uint32_t)(numerator / (uint64_t)total_ms);
}

/* ============================================================
 * Statistics
 * ============================================================ */
void NFC_LP_GetStats(NFC_LP_Handle_t *hlp, NFC_LP_Stats_t *stats)
{
    if (hlp == NULL || stats == NULL) return;
    memcpy(stats, &hlp->stats, sizeof(NFC_LP_Stats_t));
}

void NFC_LP_PrintStats(NFC_LP_Handle_t *hlp)
{
    uint32_t avg_x100;
    uint32_t total_ms;
    uint32_t duty_x100;

    if (hlp == NULL) return;

    avg_x100 = NFC_LP_GetAvgCurrent_x100(hlp);
    APP_LOGI("NFC", "=== Low Power Stats ===");
    APP_LOGI("NFC", "Wakeups     : %lu",
           (unsigned long)hlp->stats.total_wakeups);
    APP_LOGI("NFC", "Active ms   : %lu",
           (unsigned long)hlp->stats.total_active_ms);
    APP_LOGI("NFC", "Sleep ms    : %lu",
           (unsigned long)hlp->stats.total_sleep_ms);
    APP_LOGI("NFC", "Avg current : %lu.%02lu uA",
           (unsigned long)(avg_x100 / 100U),
           (unsigned long)(avg_x100 % 100U));

    total_ms = hlp->stats.total_active_ms + hlp->stats.total_sleep_ms;
    if (total_ms > 0U) {
        duty_x100 = (hlp->stats.total_active_ms * 10000U) / total_ms;
        APP_LOGI("NFC", "Duty cycle  : %lu.%02lu%%",
               (unsigned long)(duty_x100 / 100U),
               (unsigned long)(duty_x100 % 100U));
    }
}


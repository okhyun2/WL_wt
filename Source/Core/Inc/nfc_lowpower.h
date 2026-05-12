/**
 * @file    nfc_lowpower.h
 * @brief   NFC Low Power Manager  [v2.2.0]
 */

#ifndef NFC_LOWPOWER_H
#define NFC_LOWPOWER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "nfc_ntag5_ntp53321.h"
#include <stdint.h>
#include <stdbool.h>

/* Fixed-point x100 (unit: 0.01 µA) */
#define NFC_LP_STOP_CURRENT_X100    950U    /*   9.50 µA */
#define NFC_LP_ACTIVE_CURRENT_X100  43800U  /* 438.00 µA */
#define NFC_LP_ACTIVE_DURATION_MS   50U

typedef struct {
    uint32_t total_wakeups;
    uint32_t total_active_ms;
    uint32_t total_sleep_ms;
    uint32_t avg_current_x100;
    uint32_t last_active_start_ms;
} NFC_LP_Stats_t;

typedef struct {
    NFC_NTP53321_Handle_t *hntag;
    NFC_LP_Stats_t         stats;
    bool                   initialized;
    void (*OnWakeup_Callback)(NFC_WakeupEvent_t event);
    void (*OnPreSleep_Callback)(void);
} NFC_LP_Handle_t;

NFC_Result_t NFC_LP_Init(NFC_LP_Handle_t *hlp,
                          NFC_NTP53321_Handle_t *hntag);
NFC_Result_t NFC_LP_EnterStop(NFC_LP_Handle_t *hlp);
NFC_Result_t NFC_LP_HandleWakeup(NFC_LP_Handle_t *hlp);
void         NFC_LP_RegisterCallbacks(NFC_LP_Handle_t *hlp,
                                       void (*OnWakeup)(NFC_WakeupEvent_t),
                                       void (*OnPreSleep)(void));
void         NFC_LP_GetStats(NFC_LP_Handle_t *hlp, NFC_LP_Stats_t *stats);
void         NFC_LP_PrintStats(NFC_LP_Handle_t *hlp);
uint32_t     NFC_LP_GetAvgCurrent_x100(NFC_LP_Handle_t *hlp);

#ifdef __cplusplus
}
#endif
#endif /* NFC_LOWPOWER_H */


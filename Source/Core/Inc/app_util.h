#ifndef APP_UTIL_H
#define APP_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l0xx_hal.h"
#include <stdint.h>
#include "app_error.h"
#include "app_meter_server_format.h"

/* ----------------------------------------------------------------
 * Shared tiny binary helpers for NFC / packet formatting
 * ---------------------------------------------------------------- */
void App_UtilPutU16Le(uint8_t out[2], uint16_t value);
void App_UtilPutU32Le(uint8_t out[4], uint32_t value);
void App_UtilPutU16Be(uint8_t out[2], uint16_t value);
void App_UtilPutU32Be(uint8_t out[4], uint32_t value);
uint16_t App_UtilReadU16Le(const uint8_t *p_buf);
uint16_t App_UtilReadU16Be(const uint8_t *p_buf);
uint32_t App_UtilReadU32Be(const uint8_t *p_buf);
uint8_t App_UtilIsAllZero(const uint8_t *p_data, uint32_t len);
AppStatus_t App_UtilLoadMeterServerOptionsWithDefaults(AppMeterServerFormatOptions_t *p_options);

#ifdef __cplusplus
}
#endif

#endif /* APP_UTIL_H */

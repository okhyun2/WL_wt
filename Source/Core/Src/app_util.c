#include <string.h>

#include "app_build_config.h"
#include "app_hw.h"
#include "app_util.h"
#include "app_meter_server_format.h"

void App_UtilPutU16Le(uint8_t out[2], uint16_t value)
{
    if (out == NULL) { return; }
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
}

void App_UtilPutU32Le(uint8_t out[4], uint32_t value)
{
    if (out == NULL) { return; }
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)((value >> 8) & 0xFFu);
    out[2] = (uint8_t)((value >> 16) & 0xFFu);
    out[3] = (uint8_t)((value >> 24) & 0xFFu);
}

void App_UtilPutU16Be(uint8_t out[2], uint16_t value)
{
    if (out == NULL) { return; }
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

void App_UtilPutU32Be(uint8_t out[4], uint32_t value)
{
    if (out == NULL) { return; }
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

uint16_t App_UtilReadU16Le(const uint8_t *p_buf)
{
    if (p_buf == NULL) { return 0u; }
    return (uint16_t)((uint16_t)p_buf[0] | ((uint16_t)p_buf[1] << 8));
}

uint16_t App_UtilReadU16Be(const uint8_t *p_buf)
{
    if (p_buf == NULL) { return 0u; }
    return (uint16_t)(((uint16_t)p_buf[0] << 8) | (uint16_t)p_buf[1]);
}

uint32_t App_UtilReadU32Be(const uint8_t *p_buf)
{
    if (p_buf == NULL) { return 0u; }
    return ((uint32_t)p_buf[0] << 24) |
           ((uint32_t)p_buf[1] << 16) |
           ((uint32_t)p_buf[2] << 8)  |
           ((uint32_t)p_buf[3]);
}

uint8_t App_UtilIsAllZero(const uint8_t *p_data, uint32_t len)
{
    uint32_t i;
    if (p_data == NULL) { return APP_TRUE; }
    for (i = 0u; i < len; ++i)
    {
        if (p_data[i] != 0u)
        {
            return APP_FALSE;
        }
    }
    return APP_TRUE;
}

AppStatus_t App_UtilLoadMeterServerOptionsWithDefaults(AppMeterServerFormatOptions_t *p_options)
{
    AppStatus_t status;

    if (p_options == NULL)
    {
        return APP_STATUS_INVALID_PARAM;
    }

    App_MeterServerOptionsSetDefaults(p_options);
    status = App_MeterServerOptionsLoad(p_options);
    if ((status == APP_STATUS_OK) || (status == APP_STATUS_NOT_INITIALIZED))
    {
        return APP_STATUS_OK;
    }

    return status;
}



/* app_comm_param.h */
#ifndef APP_COMM_PARAM_H
#define APP_COMM_PARAM_H

#include "app_nbiot.h"

typedef enum { APP_COMM_SIGNAL_STRONG = 0, APP_COMM_SIGNAL_WEAK } AppCommSignalState_t;
typedef enum { APP_COMM_SUCCESS_HIGH = 0, APP_COMM_SUCCESS_LOW }  AppCommSuccessState_t;

void App_CommSignalMeasureAndUpdate(const AppBc95Quality_t *p_quality);
void App_CommSuccessUpdate(uint8_t attemptUsedIdx, uint8_t allFailed);
AppCommSignalState_t  App_CommGetSignalState(void);
AppCommSuccessState_t App_CommGetSuccessState(void);
void App_CommParamRecompose(void);

#endif

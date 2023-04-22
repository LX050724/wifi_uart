#pragma once

#include "freertos/portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif


int wifi_init();
bool wifi_wait_connect(TickType_t xTicksToWait);
bool wifi_wait_sta_start(TickType_t xTicksToWait);
bool wifi_is_password_error(TickType_t xTicksToWait);

#ifdef __cplusplus
}
#endif
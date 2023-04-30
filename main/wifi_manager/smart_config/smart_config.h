#pragma once

#include "freertos/portmacro.h"
#ifdef __cplusplus
extern "C" {
#endif

esp_err_t smart_config_start(int timeout_time);

/**
 * @brief
 *
 * @param xTicksToWait
 * @return esp_err_t
 */
esp_err_t smart_config_wait(TickType_t xTicksToWait);

int smart_config_get_timeout_time();

#ifdef __cplusplus
}
#endif
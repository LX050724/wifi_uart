#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t usr_uart_init();
QueueHandle_t uart_get_event_queue();

#ifdef __cplusplus
}
#endif
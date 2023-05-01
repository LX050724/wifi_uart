#pragma once

#include "esp_event.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(APP_EVENTS);

typedef enum AppEventID
{
    APP_EVENT_DEV_NAME_CHANGED,
    APP_EVENT_POWER_DOWN,
    APP_EVENT_POWER_ON,
    APP_EVENT_POWER_LOW,
} AppEventID;

int app_event_post(AppEventID event, void *event_data, size_t event_data_size, TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif
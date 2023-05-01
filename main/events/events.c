
#include "events.h"

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

int app_event_post(AppEventID event, void *event_data, size_t event_data_size, TickType_t ticks_to_wait)
{
    return esp_event_post(APP_EVENTS, event, event_data, event_data_size, ticks_to_wait);
}
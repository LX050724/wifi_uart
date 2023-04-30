#pragma once

#include "esp_err.h"
#include "freertos/portmacro.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    KEY_UP_SHORT_PRESSED = 1,
    KEY_UP_LONG_PRESSED = 2,
    KEY_DOWN_SHORT_PRESSED = 4,
    KEY_DOWN_LONG_PRESSED = 8,
} KeyEvent;

esp_err_t key_init();
KeyEvent key_wait_event(KeyEvent event, TickType_t xTicksToWait);
bool key_up_button_pressed();
bool key_down_button_pressed();

#ifdef __cplusplus
}
#endif

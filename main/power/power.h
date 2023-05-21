#pragma once

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

void power_manager_init();
void power_manager_shutdown(bool power_wakeup);
void power_oled_power_ctl(bool power);

#ifdef __cplusplus
}
#endif
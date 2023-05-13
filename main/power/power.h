#pragma once

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

void power_manager_init();
void power_manager_shutdown(bool power_wakeup);

#ifdef __cplusplus
}
#endif
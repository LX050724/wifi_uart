#pragma once

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

int adc_init();
int adc_read_bat_voltage_mv();

#ifdef __cplusplus
}
#endif
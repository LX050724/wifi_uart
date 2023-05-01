#pragma once

#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

int adc_init();
int adc_read_bat_voltage_mv();
float adc_read_bat_capacity();
bool adc_bat_is_charging();

#ifdef __cplusplus
}
#endif
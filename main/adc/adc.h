#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int adc_init();
int adc_read_voltage_mv();

#ifdef __cplusplus
}
#endif
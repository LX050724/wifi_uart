#pragma once

#include "hal/uart_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int conf_get_dev_name(char *name, size_t len);
int conf_set_dev_name(const char *name);

int conf_get_uart_param(uart_config_t *uart_config);
int conf_set_uart_param(uart_config_t *uart_config);

int conf_set_wifi_ssid(const char *ssid);
int conf_get_wifi_ssid(char *ssid, size_t len);

int conf_set_wifi_passwd(const char *passwd);
int conf_get_wifi_passwd(char *passwd, size_t len);

#ifdef __cplusplus
}
#endif
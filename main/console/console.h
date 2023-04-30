#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

int console_repl_init();
int console_printf(const char *fmt, ...);
int console_register_redirection(TaskHandle_t task_hdl, int (*write)(const char *, uint32_t));
int console_run_command(const char *cmd);

#ifdef __cplusplus
}
#endif
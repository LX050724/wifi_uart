#include "esp_console.h"
#include "esp_sleep.h"
#include <stdio.h>
#include <sys/unistd.h>
#include "power/power.h"
#include "console.h"

static int shutdown_cmd_cb(int argc, char **argv)
{
    console_printf("30s 后进入休眠，请断开供电\n");
    sleep(30);
    power_manager_shutdown();
    return 0;
}

void register_shutdown()
{
    const esp_console_cmd_t cmd = {
        .command = "shutdown",
        .help = "关机",
        .hint = NULL,
        .func = shutdown_cmd_cb,
        .argtable = NULL,
    };

    esp_console_cmd_register(&cmd);
}
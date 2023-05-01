#include "adc/adc.h"
#include "console.h"
#include "esp_console.h"

static int battery_cmd_cb(int argc, char **argv)
{
    console_printf("电池电量: %.1f%%%s\n", adc_read_bat_capacity(), adc_bat_is_charging() ? " 正在充电" : "");
    return ESP_OK;
}

void register_battery_cmd()
{
    const esp_console_cmd_t cmd = {
        .command = "battery",
        .help = "获取电池状态",
        .hint = NULL,
        .func = battery_cmd_cb,
    };

    esp_console_cmd_register(&cmd);
}
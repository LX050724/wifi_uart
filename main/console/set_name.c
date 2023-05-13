#include "argtable3/argtable3.h"
#include "config/config.h"
#include "console.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "events/events.h"
#include "freertos/portmacro.h"

static struct
{
    struct arg_str *name;
    struct arg_end *end;
} name_cmd_args;

static int set_name_cmd_cb(int argc, char **argv)
{
    arg_parse(argc, argv, (void **)&name_cmd_args);
    const char *p = name_cmd_args.name->sval[0];
    do {
        if (*p < 0x20 || *p > 0x7e)
        {
            console_printf("名称包含非法字符 \\x%02x\n", *p);
            return ESP_OK;
        }
    } while (*++p != 0);

    esp_err_t err = conf_set_dev_name(name_cmd_args.name->sval[0]);
    if (err != ESP_OK)
    {
        console_printf("%s\n", esp_err_to_name(err));
    }
    return ESP_OK;
}

void register_set_name()
{
    name_cmd_args.name = arg_str1(NULL, NULL, NULL, NULL);
    name_cmd_args.end = arg_end(1);

    const esp_console_cmd_t cmd = {
        .command = "set-name",
        .help = "设置设备名称，包括蓝牙和WIFI设备名",
        .hint = NULL,
        .func = set_name_cmd_cb,
        .argtable = &name_cmd_args,
    };

    esp_console_cmd_register(&cmd);
}
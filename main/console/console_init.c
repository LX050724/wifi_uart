
#include "argtable3/argtable3.h"
#include "console/console.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/uart_types.h"
#include "nvs.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define REDIRECTION_LIST_LEN 2

void register_set_uart();
void register_shutdown();
void register_set_name();
void register_system();
void register_battery_cmd();

typedef struct
{
    esp_console_repl_t repl_core; // base class
    char prompt[32];              // Prompt to be printed before each line
    int state;
    const char *history_save_path;
    TaskHandle_t task_hdl;     // REPL task handle
    size_t max_cmdline_length; // Maximum length of a command line. If 0, default value will be used.
} esp_console_repl_com_t;

static struct
{
    TaskHandle_t task_hdl;
    int (*write)(const char *, uint32_t);
} redirection_list[REDIRECTION_LIST_LEN];

static int console_witre(const char *buf, uint32_t len)
{
    return usb_serial_jtag_write_bytes(buf, len, 0);
}

static int console_log_output(const char *fmt, va_list ap)
{
    static char buf[512];
    int num = vsnprintf(buf, sizeof(buf), fmt, ap);
    console_witre(buf, num);
    return num;
}

int console_repl_init()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = "#";
    repl_config.max_cmdline_length = 128;

    esp_console_register_help_command();
    register_set_uart();
    register_shutdown();
    register_set_name();
    register_system();
    register_battery_cmd();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
    /* 重设日志输出接口为非阻塞型 */
    esp_log_set_vprintf(console_log_output);
#else
#error Unsupported console type
#endif
    esp_console_repl_com_t *repl_com = __containerof(repl, esp_console_repl_com_t, repl_core);
    console_register_redirection(repl_com->task_hdl, console_witre);
    return esp_console_start_repl(repl);
}

int console_printf(const char *fmt, ...)
{
    static char buf[512];
    static SemaphoreHandle_t mutex = NULL;
    if (mutex == NULL)
        mutex = xSemaphoreCreateMutex();

    xSemaphoreTake(mutex, portMAX_DELAY);
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    TaskHandle_t handle = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < REDIRECTION_LIST_LEN; i++)
    {
        if (redirection_list[i].task_hdl == handle && redirection_list[i].write)
        {
            r = redirection_list[i].write(buf, r);
            break;
        }
    }
    xSemaphoreGive(mutex);
    return r;
}

int console_register_redirection(TaskHandle_t task_hdl, int (*write)(const char *, uint32_t))
{
    if (task_hdl == NULL || write == NULL)
    {
        return -ESP_ERR_INVALID_ARG;
    }
    for (int i = 0; i < REDIRECTION_LIST_LEN; i++)
    {
        if (redirection_list[i].task_hdl == NULL)
        {
            redirection_list[i].task_hdl = task_hdl;
            redirection_list[i].write = write;
            return i;
        }
    }
    return -ESP_ERR_NO_MEM;
}

int console_run_command(const char *cmd)
{
    int ret;
    esp_err_t err = esp_console_run(cmd, &ret);
    if (err == ESP_ERR_NOT_FOUND)
    {
        console_printf("Unrecognized command\n");
    }
    else if (err == ESP_ERR_INVALID_ARG)
    {
        // command was empty
    }
    else if (err == ESP_OK && ret != ESP_OK)
    {
        console_printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
    }
    else if (err != ESP_OK)
    {
        console_printf("Internal error: %s\n", esp_err_to_name(err));
    }
    return ret;
}
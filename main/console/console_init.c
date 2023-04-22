
#include "argtable3/argtable3.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/uart_types.h"
#include "nvs.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void register_uart_cmd();

int console_repl_init()
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    repl_config.prompt = "ESP >";
    repl_config.max_cmdline_length = 128;

    esp_console_register_help_command();
    register_uart_cmd();

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &repl));
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl));
#else
#error Unsupported console type
#endif
    return esp_console_start_repl(repl);
}

static struct
{
    struct arg_int *baud;
    struct arg_int *word_length;
    struct arg_dbl *stop_bits;
    struct arg_str *parity;
    struct arg_end *end;
} uart_cmd_args;

static int set_uart_cmd_cb(int argc, char **argv)
{
    arg_parse(argc, argv, (void **)&uart_cmd_args);
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "NVS open field %s", esp_err_to_name(err));
        return 1;
    }
    uart_config_t uart_nvs_config = {};
    size_t read_size = sizeof(uart_config_t);
    nvs_get_blob(nvs_handle, "uart_conf", &uart_nvs_config, &read_size);

    if (uart_cmd_args.baud->count)
    {
        int baud = 0, baud_old = 0;
        uart_get_baudrate(UART_NUM_1, (uint32_t *)&baud_old);
        uart_set_baudrate(UART_NUM_1, uart_cmd_args.baud->ival[0]);
        uart_get_baudrate(UART_NUM_1, (uint32_t *)&baud);
        if (abs(baud - uart_cmd_args.baud->ival[0]) < uart_cmd_args.baud->ival[0] * 0.0005)
        {
            printf("设置 波特率：%d ", baud);
            uart_nvs_config.baud_rate = baud;
        }
        else
        {
            printf("\n错误：无法设置波特率%d，实际波特率%d\n", uart_cmd_args.baud->ival[0], baud);
            uart_set_baudrate(UART_NUM_1, baud_old);
            return 1;
        }
    }

    if (uart_cmd_args.word_length->count)
    {
        uart_word_length_t word_length = uart_cmd_args.word_length->ival[0] - 5;
        if (word_length < 0 || word_length >= UART_DATA_BITS_MAX)
        {
            printf("\n错误：字长仅支持5到8比特\n");
            return 1;
        }
        uart_set_word_length(UART_NUM_1, word_length);
        printf("，字长：%d ", uart_cmd_args.word_length->ival[0]);
        uart_nvs_config.data_bits = word_length;
    }
    if (uart_cmd_args.stop_bits->count)
    {

        uart_stop_bits_t stop_bits;

        if (uart_cmd_args.stop_bits->dval[0] == 1)
        {
            stop_bits = UART_STOP_BITS_1;
        }
        else if (uart_cmd_args.stop_bits->dval[0] == 1.5)
        {
            stop_bits = UART_STOP_BITS_1_5;
        }
        else if (uart_cmd_args.stop_bits->dval[0] == 2)
        {
            stop_bits = UART_STOP_BITS_2;
        }
        else
        {
            printf("\n错误：停止位仅支持1，1.5，2比特\n");
            return 1;
        }
        uart_set_stop_bits(UART_NUM_1, stop_bits);
        printf("，停止位：%.1lf ", uart_cmd_args.stop_bits->dval[0]);
        uart_nvs_config.stop_bits = stop_bits;
    }
    if (uart_cmd_args.parity->count)
    {
        uart_parity_t parity;
        if (strcmp("DIS", uart_cmd_args.parity->sval[0]) == 0)
        {
            parity = UART_PARITY_DISABLE;
        }
        else if (strcmp("EVEN", uart_cmd_args.parity->sval[0]) == 0)
        {
            parity = UART_PARITY_EVEN;
        }
        else if (strcmp("ODD", uart_cmd_args.parity->sval[0]) == 0)
        {
            parity = UART_PARITY_ODD;
        }
        else
        {
            printf("\n错误：校验方式仅支持无校验DIS，奇校验ODD，偶校验EVEN\n");
            return 1;
        }
        printf("，校验方式：%s ", uart_cmd_args.parity->sval[0]);
        uart_set_parity(UART_NUM_1, parity);
        uart_nvs_config.parity = parity;
    }

    printf("\n");
    err = nvs_set_blob(nvs_handle, "uart_conf", &uart_nvs_config, sizeof(uart_config_t));
    if (err != ESP_OK)
    {
        printf("错误：无法保存配置 %s\n", esp_err_to_name(err));
        return 1;
    }
    nvs_close(nvs_handle);
    return 0;
}

void register_uart_cmd()
{
    uart_cmd_args.baud = arg_int1("b", "baud", "<baud>", "波特率");
    uart_cmd_args.word_length = arg_int1("w", "word_length", "<word_length>", "字长5-8");
    uart_cmd_args.stop_bits = arg_dbl1("s", "stop_bits", "<stop_bits>", "停止位：1，1.5，2");
    uart_cmd_args.parity = arg_str1("p", "parity", "<parity>", "校验方式：无校验DIS，奇校验ODD，偶校验EVEN");
    uart_cmd_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "set_uart",
        .help = "设置远程串口波特率等配置",
        .hint = NULL,
        .func = set_uart_cmd_cb,
        .argtable = &uart_cmd_args,
    };

    esp_console_cmd_register(&cmd);
}
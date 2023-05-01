#include "argtable3/argtable3.h"
#include "console.h"
#include "driver/uart.h"
#include "esp_console.h"
#include "esp_log.h"
#include "hal/uart_types.h"
#include "nvs.h"
#include <string.h>

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
        return ESP_OK;
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
            console_printf("设置 波特率：%d ", baud);
            uart_nvs_config.baud_rate = baud;
        }
        else
        {
            console_printf("\n错误：无法设置波特率%d，实际波特率%d\n", uart_cmd_args.baud->ival[0], baud);
            uart_set_baudrate(UART_NUM_1, baud_old);
            goto exit;
        }
    }

    if (uart_cmd_args.word_length->count)
    {
        uart_word_length_t word_length = uart_cmd_args.word_length->ival[0] - 5;
        if (word_length < 0 || word_length >= UART_DATA_BITS_MAX)
        {
            console_printf("\n错误：字长仅支持5到8比特\n");
            goto exit;
        }
        uart_set_word_length(UART_NUM_1, word_length);
        console_printf("，字长：%d ", uart_cmd_args.word_length->ival[0]);
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
            console_printf("\n错误：停止位仅支持1，1.5，2比特\n");
            goto exit;
        }
        uart_set_stop_bits(UART_NUM_1, stop_bits);
        console_printf("，停止位：%.1lf ", uart_cmd_args.stop_bits->dval[0]);
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
            console_printf("\n错误：校验方式仅支持无校验DIS，奇校验ODD，偶校验EVEN\n");
            goto exit;
        }
        console_printf("，校验方式：%s ", uart_cmd_args.parity->sval[0]);
        uart_set_parity(UART_NUM_1, parity);
        uart_nvs_config.parity = parity;
    }

    console_printf("\n");
    err = nvs_set_blob(nvs_handle, "uart_conf", &uart_nvs_config, sizeof(uart_config_t));
    if (err != ESP_OK)
    {
        console_printf("错误：无法保存配置 %s\n", esp_err_to_name(err));
        goto exit;
    }

exit:
    nvs_close(nvs_handle);
    return ESP_OK;
}

void register_set_uart()
{
    uart_cmd_args.baud = arg_int1("b", "baud", "<baud>", "波特率");
    uart_cmd_args.word_length = arg_int1("w", "word_length", "<word_length>", "字长5-8");
    uart_cmd_args.stop_bits = arg_dbl1("s", "stop_bits", "<stop_bits>", "停止位：1，1.5，2");
    uart_cmd_args.parity = arg_str1("p", "parity", "<parity>", "校验方式：无校验DIS，奇校验ODD，偶校验EVEN");
    uart_cmd_args.end = arg_end(4);

    const esp_console_cmd_t cmd = {
        .command = "set-uart",
        .help = "设置远程串口波特率等配置",
        .hint = NULL,
        .func = set_uart_cmd_cb,
        .argtable = &uart_cmd_args,
    };

    esp_console_cmd_register(&cmd);
}
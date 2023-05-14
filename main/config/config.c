#include "config.h"
#include "esp_err.h"
#include "esp_log.h"
#include "events/events.h"
#include "nvs.h"
#include <string.h>

int conf_get_dev_name(char *name, size_t len)
{
    if (len < 20)
    {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "NVS open field %s", esp_err_to_name(err));
        return err;
    }

    size_t _len = len;
    err = nvs_get_str(nvs_handle, "dev_name", name, &_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        strcpy(name, "DEVICE");
        err = nvs_set_str(nvs_handle, "dev_name", name);
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "NVS set dev_name default config field %s", esp_err_to_name(err));
        }
    }
    nvs_close(nvs_handle);
    return err;
}

int conf_set_dev_name(const char *name)
{
    if (strlen(name) > 20)
        return ESP_ERR_INVALID_SIZE;

    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "NVS open field %s", esp_err_to_name(err));
        return err;
    }

    char old_name[32];
    size_t len = sizeof(old_name);
    err = nvs_get_str(nvs_handle, "dev_name", old_name, &len);
    if (err == ESP_OK && len > 0)
    {
        if (strcmp(old_name, name) != 0)
        {
            err = nvs_set_str(nvs_handle, "dev_name", name);
            if (err == ESP_OK)
                app_event_post(APP_EVENT_DEV_NAME_CHANGED, NULL, 0, portMAX_DELAY);
        }
    }

    nvs_close(nvs_handle);
    return err;
}

int conf_get_uart_param(uart_config_t *uart_config)
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "NVS open field %s", esp_err_to_name(err));
        return err;
    }
    size_t length = sizeof(uart_config_t);
    err = nvs_get_blob(nvs_handle, "uart_conf", uart_config, &length);
    if (err != ESP_OK || length != sizeof(uart_config_t))
    {
        uart_config->baud_rate = 115200;
        uart_config->data_bits = UART_DATA_8_BITS;
        uart_config->stop_bits = UART_STOP_BITS_1;
        uart_config->flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        uart_config->parity = UART_PARITY_DISABLE;
        uart_config->source_clk = UART_SCLK_DEFAULT;
        uart_config->rx_flow_ctrl_thresh = 122;
        err = nvs_set_blob(nvs_handle, "uart_conf", uart_config, sizeof(uart_config_t));
        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "NVS set uart default config field %s", esp_err_to_name(err));
        }
    }
    nvs_close(nvs_handle);
    return err;
}

int conf_set_uart_param(uart_config_t *uart_config)
{
    nvs_handle_t nvs_handle = 0;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "NVS open field %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_blob(nvs_handle, "uart_conf", &uart_config, sizeof(uart_config_t));
    nvs_close(nvs_handle);
    return err;
}
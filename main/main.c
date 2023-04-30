#include "adc/adc.h"
#include "console/console.h"
#include "display/display.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "hal/gpio_types.h"
#include "key/key.h"
#include "nvs_flash.h"
#include "power/power.h"
#include "wifi_manager/blufi/blufi.h"
#include "wifi_manager/wifi_manager.h"

static void nvs_init();

void app_main(void)
{
    esp_event_loop_create_default();
    key_init();
    adc_init();
    power_manager_init();

    /* 默认参数初始化串口 */
    uart_config_t uart_config = {};
    uart_config.baud_rate = 115200;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;
    uart_config.rx_flow_ctrl_thresh = 122;

    uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, GPIO_NUM_4, GPIO_NUM_5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    nvs_init();
    console_repl_init();
    display_init();
    wifi_init();
    blufi_init();
}

static void nvs_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    else if (err == ESP_OK)
    {
        nvs_handle_t nvs_handle = 0;
        err = nvs_open("storage", NVS_READWRITE, &nvs_handle);

        if (err != ESP_OK)
        {
            ESP_LOGE("NVS", "NVS open field %s", esp_err_to_name(err));
        }
        else
        {
            uart_config_t uart_nvs_config = {};
            uart_config_t uart_config = {};
            size_t read_size = sizeof(uart_config_t);
            err = nvs_get_blob(nvs_handle, "uart_conf", &uart_nvs_config, &read_size);
            if (err == ESP_OK && read_size == sizeof(uart_config_t))
            {
                uart_set_baudrate(UART_NUM_1, uart_nvs_config.baud_rate);
                uart_set_word_length(UART_NUM_1, uart_config.data_bits);
                uart_set_stop_bits(UART_NUM_1, uart_config.stop_bits);
                uart_set_parity(UART_NUM_1, uart_config.parity);
            }
            else if ((err = nvs_set_blob(nvs_handle, "uart_conf", &uart_config, sizeof(uart_config_t))) != ESP_OK)
            {
                ESP_LOGE("NVS", "NVS set uart default config field %s, length %u", esp_err_to_name(err), read_size);
            }

            nvs_close(nvs_handle);
        }
    }
}
#include "adc/adc.h"
#include "console/console.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_wifi_types.h"
#include "hal/gpio_types.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "soc/clk_tree_defs.h"
#include <esp_wifi.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <oled/oled.h>

void app_main(void)
{
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

    gpio_config_t gpio_conf = {};
    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_1;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&gpio_conf);

    esp_sleep_enable_gpio_wakeup();
    gpio_hold_en(GPIO_NUM_1);
    esp_deep_sleep_enable_gpio_wakeup(0x02, ESP_GPIO_WAKEUP_GPIO_LOW);
    // gpio_wakeup_enable(GPIO_NUM_1, GPIO_INTR_LOW_LEVEL);

    adc_init();
    oled_init();

    /* 初始化NVS */
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

    init_console_repl();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_config_t scan_config = {};
    scan_config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    wifi_ap_record_t ap_records[16];
    uint16_t number = 16;
    esp_wifi_scan_get_ap_records(&number, ap_records);
    for (int i = 0; i < number; i++)
    {
        ESP_LOGI("SCAN", "%d \"%s\": %d %d", i + 1, ap_records[i].ssid, ap_records[i].rssi, ap_records[i].authmode);
    }

    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.sta.ssid, "PDCN");
    strcpy((char *)wifi_config.sta.password, "zhengou123");
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_connect();
}


#include "adc/adc.h"
#include "config/config.h"
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
#include "telnet/telnet_server.h"

static int nvs_init();

void app_main(void)
{
    key_init();
    adc_init();
    power_manager_init();
    esp_event_loop_create_default();
    nvs_init();

    /* 默认参数初始化串口 */
    uart_config_t uart_config = {};
    conf_get_uart_param(&uart_config);
    uart_driver_install(UART_NUM_1, 1024, 1024, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, GPIO_NUM_4, GPIO_NUM_5, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    console_repl_init();
    display_init();
    wifi_init();
    blufi_init();
    telnet_init();
}

static int nvs_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}
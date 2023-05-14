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
#include "usr_uart/usr_uart.h"

static int nvs_init();
void app_main(void)
{
    key_init();
    adc_init();
    power_manager_init();
    esp_event_loop_create_default();
    nvs_init();

    usr_uart_init();
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
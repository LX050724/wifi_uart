#include "power.h"
#include "adc/adc.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "events/events.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "hal/gpio_ll.h"
#include "hal/gpio_types.h"
#include "key/key.h"
#include "soc/gpio_struct.h"
#include "wifi_manager/blufi/blufi_private.h"
#include <stdbool.h>
#include <sys/unistd.h>

static void power_manager_deep_sleep(bool power_wakeup)
{
    /* 重置GPIO0 */
    gpio_reset_pin(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_1);

    gpio_deep_sleep_hold_dis();
    esp_sleep_enable_gpio_wakeup();

    gpio_config_t gpio_conf = {};
    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_0;
    if (power_wakeup)
    {
        gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_1;
    }
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&gpio_conf);

    esp_deep_sleep_enable_gpio_wakeup(gpio_conf.pin_bit_mask, ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_deep_sleep_start();
}

static TaskHandle_t power_monitor_task_handle;
static void power_monitor_task(void *arg)
{
    int last_power_status = 0;
    while (true)
    {
        int charging = adc_bat_is_charging();
        if (last_power_status != charging)
        {
            app_event_post(charging ? APP_EVENT_POWER_ON : APP_EVENT_POWER_DOWN, NULL, 0, portMAX_DELAY);
        }
        last_power_status = charging;
        float bat_capacity = adc_read_bat_capacity();

        if (!charging && bat_capacity <= 0)
        {
            power_manager_shutdown(false);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void power_manager_init()
{
    /* 如果系统是因为电源不稳定复位则立刻进入休眠，并禁用上电唤醒，只能按键唤醒 */
    soc_reset_reason_t reset_reason = esp_rom_get_reset_reason(0);
    if (reset_reason == RESET_REASON_CHIP_BROWN_OUT || reset_reason == RESET_REASON_SYS_BROWN_OUT)
    {
        power_manager_deep_sleep(false);
    }

    gpio_config_t gpio_conf = {};
    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_2;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&gpio_conf);

    power_oled_power_ctl(true);
    xTaskCreate(power_monitor_task, "power_monitor", 2048, NULL, 1, &power_monitor_task_handle);
}

void power_manager_shutdown(bool power_wakeup)
{
    esp_wifi_stop();
    esp_blufi_host_deinit();
    power_oled_power_ctl(false);
    power_manager_deep_sleep(power_wakeup);
}

void power_oled_power_ctl(bool power)
{
    gpio_set_level(GPIO_NUM_2, power ? 0 : 1);
}
#include "adc/adc.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "esp_event.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "hal/gpio_ll.h"
#include "hal/gpio_types.h"
#include "key/key.h"
#include "soc/gpio_struct.h"
#include "wifi_manager/blufi/blufi_private.h"
#include <sys/unistd.h>

#define GPIO (*(gpio_dev_t *)0x60004000)
#define gpio_read_pin(NUM) ((GPIO.in.data >> (NUM)) & 0x1)


static void power_manager_deep_sleep()
{
    /* 重置GPIO0 */
    gpio_reset_pin(GPIO_NUM_0);

    gpio_deep_sleep_hold_dis();
    esp_sleep_enable_gpio_wakeup();

    gpio_config_t gpio_conf = {};
    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_0;
    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_1;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&gpio_conf);

    esp_deep_sleep_enable_gpio_wakeup(gpio_conf.pin_bit_mask, ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_deep_sleep_start();
}

void power_manager_init()
{
}

void power_manager_shutdown()
{
    esp_wifi_stop();
    esp_blufi_host_deinit();
    power_manager_deep_sleep();
}
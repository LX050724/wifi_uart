
#include "key.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/gpio_hal.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

esp_err_t key_init()
{
    gpio_config_t gpio_conf = {};
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.intr_type = GPIO_INTR_ANYEDGE;

    gpio_conf.pin_bit_mask = 1 << GPIO_NUM_0;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&gpio_conf);

    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_9;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLUP_DISABLE;
    gpio_config(&gpio_conf);

    REG_SET_BIT(IO_MUX_GPIO0_REG, BIT(15));
    REG_SET_BIT(IO_MUX_GPIO9_REG, BIT(15));

    return ESP_OK;
}

bool key_up_button_pressed()
{
    return !gpio_get_level(GPIO_NUM_9);
}

bool key_down_button_pressed()
{
    return gpio_get_level(GPIO_NUM_0);
}

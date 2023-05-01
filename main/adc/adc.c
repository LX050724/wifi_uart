#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_private/adc_share_hw_ctrl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "hal/gpio_types.h"
#include <stdbool.h>

#define TAG "ADC"

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_handle;
static const float capacity_table[12][2] = {
    {0, 3.5},   {9, 3.68},  {18, 3.7},  {27, 3.73}, {36, 3.77}, {45, 3.79},
    {55, 3.82}, {64, 3.87}, {73, 3.93}, {82, 4},    {91, 4.08}, {100, 4.2},
};

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);

int adc_init()
{
    gpio_config_t gpio_conf = {};
    gpio_conf.pin_bit_mask = 1 << GPIO_NUM_10;
    gpio_conf.mode = GPIO_MODE_OUTPUT;
    gpio_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&gpio_conf);
    gpio_set_level(GPIO_NUM_10, 1);

    gpio_conf.pin_bit_mask = 1 << GPIO_NUM_1;
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_config(&gpio_conf);

    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    int ret = ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_new_unit(&init_config1, &adc1_handle));
    if (ret != ESP_OK)
        return ret;
    adc_oneshot_chan_cfg_t config = {};
    config.atten = ADC_ATTEN_DB_11;
    config.bitwidth = ADC_BITWIDTH_DEFAULT;

    ret = ESP_ERROR_CHECK_WITHOUT_ABORT(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_3, &config));
    if (ret != ESP_OK)
    {
        adc_oneshot_del_unit(adc1_handle);
        return ret;
    }

    return adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_11, &adc1_cali_handle) ? ESP_OK : ESP_FAIL;
}

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated)
    {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK)
        {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Calibration Success");
    }
    else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated)
    {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    }
    else
    {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

int adc_read_bat_voltage_mv()
{
    int vlotage = 0;
    int ret = adc_oneshot_read(adc1_handle, ADC_CHANNEL_3, &vlotage);
    if (ret == ESP_OK && adc1_cali_handle)
    {
        ret = adc_cali_raw_to_voltage(adc1_cali_handle, vlotage, &vlotage);
    }

    return ret == ESP_OK ? vlotage * 2 : -1;
}

float adc_read_bat_capacity()
{
    float vlotage = 0;
    for (int i = 0; i < 5; i++)
    {
        vlotage += adc_read_bat_voltage_mv();
        vTaskDelay(1);
    }
    vlotage /= 5000;

    if (vlotage < 3.5)
        return 0;
    if (vlotage > 4.2)
        return 100;

    for (int i = 0; i < 11; i++)
    {
        if (vlotage >= capacity_table[i][1] && vlotage < capacity_table[i + 1][1])
        {
            return (vlotage - capacity_table[i][1]) / (capacity_table[i + 1][1] - capacity_table[i][1]) *
                       (capacity_table[i + 1][0] - capacity_table[i][0]) +
                   capacity_table[i][0];
        }
    }
    return 0;
}

bool adc_bat_is_charging()
{
    return gpio_get_level(GPIO_NUM_1);
}
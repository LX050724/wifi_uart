#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "wifi_manager/wifi_manager.h"
#include <stdint.h>
#include <string.h>
#include <strings.h>

// in flag
#define ESPTOUCH_IN_FLAG_MASK 0x00ff0000
#define ESPTOUCH_DONE_BIT BIT16
#define ESPTOUCH_CANCEL0_BIT BIT17

// out flag
#define ESPTOUCH_OUT_FLAG_MASK 0x0000ffff
#define ESPTOUCH_CANCEL_BIT BIT0
#define ESPTOUCH_SUCCESS_BIT BIT1
#define ESPTOUCH_TIMEOUT_BIT BIT2
#define ESPTOUCH_PWD_ERR_BIT BIT3

static const char *TAG = "smart_config";

static TaskHandle_t smart_config_task_handle;
static void smart_config_task(void *arg);

static EventGroupHandle_t smart_config_event;
static int s_timeout_time;

esp_err_t smart_config_start(int timeout_time)
{
    if (!wifi_wait_sta_start(0))
    {
        return ESP_ERR_WIFI_NOT_INIT;
    }

    if (!smart_config_event)
    {
        smart_config_event = xEventGroupCreate();
        if (smart_config_event == NULL)
        {
            ESP_LOGE(TAG, "xEventGroupCreate failed");
            return ESP_FAIL;
        }
    }

    xEventGroupClearBits(smart_config_event, 0x00ffffff);
    s_timeout_time = timeout_time * 2;
    int ret = xTaskCreate(smart_config_task, "smart_config", 2048, NULL, 5, &smart_config_task_handle);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "create smart_config task failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "Scan done");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(TAG, "Found channel");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = {0};
        uint8_t password[65] = {0};

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true)
        {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        uint8_t rvd_data[33] = {0};
        if (evt->type == SC_TYPE_ESPTOUCH_V2)
        {
            ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i = 0; i < 33; i++)
            {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        esp_wifi_connect();
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(smart_config_event, ESPTOUCH_DONE_BIT);
    }
}

static void smart_config_task(void *arg)
{
    EventBits_t uxBits;

    esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);

    esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_V2);
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    esp_smartconfig_start(&cfg);

    for (; s_timeout_time > 0; s_timeout_time--)
    {
        uxBits = xEventGroupWaitBits(smart_config_event, ESPTOUCH_IN_FLAG_MASK, true, false, pdMS_TO_TICKS(500));
        if (uxBits & ESPTOUCH_DONE_BIT)
        {
            ESP_LOGI(TAG, "smartconfig success");
            xEventGroupSetBits(smart_config_event, ESPTOUCH_SUCCESS_BIT);
            break;
        }

        if (uxBits & ESPTOUCH_CANCEL0_BIT)
        {
            ESP_LOGI(TAG, "smartconfig cancel");
            xEventGroupSetBits(smart_config_event, ESPTOUCH_CANCEL_BIT);
            break;
        }

        if (wifi_is_password_error(0))
        {
            ESP_LOGE(TAG, "password error");
            xEventGroupSetBits(smart_config_event, ESPTOUCH_PWD_ERR_BIT);
            break;
        }
    }

    if (s_timeout_time == 0)
    {
        xEventGroupSetBits(smart_config_event, ESPTOUCH_TIMEOUT_BIT);
    }

    ESP_LOGI(TAG, "smartconfig over");
    esp_event_handler_unregister(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler);
    esp_smartconfig_stop();

    vTaskDelete(NULL);
}

int smart_config_get_timeout_time()
{
    return s_timeout_time / 2;
}

esp_err_t smart_config_cancel()
{
    if (smart_config_event == NULL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }
    xEventGroupSetBits(smart_config_event, ESPTOUCH_CANCEL0_BIT);
    return ESP_OK;
}

esp_err_t smart_config_wait(TickType_t xTicksToWait)
{
    EventBits_t uxBits;
    if (smart_config_event == NULL)
    {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uxBits = xEventGroupWaitBits(smart_config_event, ESPTOUCH_OUT_FLAG_MASK, true, false, xTicksToWait);

    if (uxBits & ESPTOUCH_SUCCESS_BIT)
    {
        return ESP_OK;
    }
    else if (uxBits & ESPTOUCH_TIMEOUT_BIT)
    {
        return ESP_ERR_TIMEOUT;
    }
    else if (uxBits & ESPTOUCH_PWD_ERR_BIT)
    {
        return ESP_ERR_WIFI_PASSWORD;
    }
    else if (uxBits & ESPTOUCH_CANCEL_BIT)
    {
        return ESP_ERR_NOT_FINISHED;
    }

    return ESP_ERR_TIMEOUT;
}
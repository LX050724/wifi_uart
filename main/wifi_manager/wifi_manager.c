#include "wifi_manager/wifi_manager.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_wifi_event_group;

#define STA_START_BIT BIT0
#define CONNECTED_BIT BIT1
#define PASSWORD_ERROR BIT2

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "wifi STA started");
        xEventGroupSetBits(s_wifi_event_group, STA_START_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP)
    {
        ESP_LOGW(TAG, "wifi STA stoped");
        xEventGroupClearBits(s_wifi_event_group, STA_START_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event = event_data;
        if (event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT)
        {
            ESP_LOGE(TAG, "4WAY_HANDSHAKE_TIMEOUT, stop connect");
            xEventGroupSetBits(s_wifi_event_group, PASSWORD_ERROR);
        }
        else
        {
            ESP_LOGW(TAG, "wifi disconnted reason %d", event->reason);
            esp_wifi_connect();
        }
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        // ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "wifi connected");
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, PASSWORD_ERROR);
    }
}

int wifi_init()
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    return ESP_OK;
}

bool wifi_wait_connect(TickType_t xTicksToWait)
{
    return (CONNECTED_BIT & xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, false, false, xTicksToWait)) ==
           CONNECTED_BIT;
}

bool wifi_wait_sta_start(TickType_t xTicksToWait)
{
    return (STA_START_BIT & xEventGroupWaitBits(s_wifi_event_group, STA_START_BIT, false, false, xTicksToWait)) ==
           STA_START_BIT;
}

bool wifi_is_password_error(TickType_t xTicksToWait)
{
    return (PASSWORD_ERROR & xEventGroupWaitBits(s_wifi_event_group, PASSWORD_ERROR, false, false, xTicksToWait)) ==
           PASSWORD_ERROR;
}
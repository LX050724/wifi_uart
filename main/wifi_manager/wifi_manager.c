#include "wifi_manager/wifi_manager.h"
#include "esp_blufi_api.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_smartconfig.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_manager";

#define WIFI_CONNECTION_MAXIMUM_RETRY 5
#define INVALID_REASON 255
#define INVALID_RSSI -128

static wifi_config_t sta_config;
static wifi_config_t ap_config;

static EventGroupHandle_t s_wifi_event_group;
static esp_blufi_extra_info_t gl_sta_conn_info;
static uint8_t wifi_retry_count = 0;

static uint8_t gl_sta_bssid[6];
static uint8_t gl_sta_ssid[32];
static int gl_sta_ssid_len;
static esp_netif_ip_info_t ip_info;

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case IP_EVENT_STA_GOT_IP: {
        ip_event_got_ip_t *event = event_data;
        char ip[16], gw[16], mask[16];
        inet_ntoa_r(event->ip_info.ip.addr, ip, sizeof(ip));
        inet_ntoa_r(event->ip_info.gw.addr, gw, sizeof(gw));
        inet_ntoa_r(event->ip_info.netmask.addr, mask, sizeof(mask));
        ip_info = event->ip_info;
        ESP_LOGI(TAG, "got ip:%s gw:%s mask:%s", ip, gw, mask);
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT | GOT_IP_BIT);
        break;
    }
    case IP_EVENT_GOT_IP6: {
        ip_event_got_ip6_t *event = event_data;
        // ESP_LOGI(TAG, "got ip:%s gw:%s mask:%s", ip, gw, mask);
        break;
    }
    case IP_EVENT_STA_LOST_IP: {
        memset(&ip_info, 0, sizeof(esp_netif_ip_info_t));
        break;
    }
    case IP_EVENT_AP_STAIPASSIGNED:
    case IP_EVENT_ETH_GOT_IP:
    case IP_EVENT_ETH_LOST_IP:
    case IP_EVENT_PPP_GOT_IP:
    case IP_EVENT_PPP_LOST_IP:
        break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case WIFI_EVENT_STA_START: {
        ESP_LOGI(TAG, "wifi STA started");
        xEventGroupSetBits(s_wifi_event_group, STA_START_BIT);
        break;
    }
    case WIFI_EVENT_STA_STOP: {
        ESP_LOGW(TAG, "wifi STA stoped");
        xEventGroupClearBits(s_wifi_event_group, STA_START_BIT);
        break;
    }
    case WIFI_EVENT_STA_CONNECTED: {
        wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
        memcpy(gl_sta_bssid, event->bssid, 6);
        memcpy(gl_sta_ssid, event->ssid, event->ssid_len);
        gl_sta_ssid_len = event->ssid_len;
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        xEventGroupClearBits(s_wifi_event_group, PASSWORD_ERROR | CONNECTING_BIT);
        break;
    }
    case WIFI_EVENT_STA_DISCONNECTED: {
        /* Only handle reconnection during connecting */
        if (!wifi_wait_connect(0) && wifi_reconnect() == false)
        {
            wifi_event_sta_disconnected_t *disconnected_event = (wifi_event_sta_disconnected_t *)event_data;
            record_wifi_conn_info(disconnected_event->rssi, disconnected_event->reason);
            xEventGroupClearBits(s_wifi_event_group, CONNECTING_BIT);
        }
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        memset(gl_sta_ssid, 0, sizeof(gl_sta_ssid));
        memset(gl_sta_bssid, 0, sizeof(gl_sta_bssid));
        gl_sta_ssid_len = 0;
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT | GOT_IP_BIT);
        break;
    }
    }
}

bool wifi_wait_event(uint32_t event, TickType_t xTicksToWait)
{
    return (event & xEventGroupWaitBits(s_wifi_event_group, event, false, false, xTicksToWait)) == event;
}

void record_wifi_conn_info(int rssi, uint8_t reason)
{
    memset(&gl_sta_conn_info, 0, sizeof(esp_blufi_extra_info_t));
    if (wifi_is_connecting())
    {
        gl_sta_conn_info.sta_max_conn_retry_set = true;
        gl_sta_conn_info.sta_max_conn_retry = WIFI_CONNECTION_MAXIMUM_RETRY;
    }
    else
    {
        gl_sta_conn_info.sta_conn_rssi_set = true;
        gl_sta_conn_info.sta_conn_rssi = rssi;
        gl_sta_conn_info.sta_conn_end_reason_set = true;
        gl_sta_conn_info.sta_conn_end_reason = reason;
    }
}

void wifi_connect(void)
{
    wifi_retry_count = 0;
    if (esp_wifi_connect() == ESP_OK)
        xEventGroupSetBits(s_wifi_event_group, CONNECTING_BIT);
    record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
}

bool wifi_reconnect(void)
{
    bool ret;
    if (wifi_is_connecting() && wifi_retry_count++ < WIFI_CONNECTION_MAXIMUM_RETRY)
    {
        ESP_LOGI(TAG, "WiFi starts reconnection\n");
        if (esp_wifi_connect() == ESP_OK)
            xEventGroupSetBits(s_wifi_event_group, CONNECTING_BIT);
        record_wifi_conn_info(INVALID_RSSI, INVALID_REASON);
        ret = true;
    }
    else
    {
        ret = false;
    }
    return ret;
}

esp_err_t wifi_set_sta_bssid(uint8_t bssid[6])
{
    memcpy(sta_config.sta.bssid, bssid, 6);
    sta_config.sta.bssid_set = 1;
    return esp_wifi_set_config(WIFI_IF_STA, &sta_config);
}

esp_err_t wifi_set_sta_ssid(uint8_t *ssid, int len)
{
    strncpy((char *)sta_config.sta.ssid, (char *)ssid, len);
    sta_config.sta.ssid[len] = '\0';
    return esp_wifi_set_config(WIFI_IF_STA, &sta_config);
}

esp_err_t wifi_set_sta_passwd(uint8_t *passwd, int len)
{
    strncpy((char *)sta_config.sta.password, (char *)passwd, len);
    sta_config.sta.password[len] = '\0';
    return esp_wifi_set_config(WIFI_IF_STA, &sta_config);
}

esp_err_t wifi_set_ap_ssid(uint8_t *ssid, int len)
{
    strncpy((char *)ap_config.ap.ssid, (char *)ssid, len);
    ap_config.ap.ssid[len] = '\0';
    ap_config.ap.ssid_len = len;
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

esp_err_t wifi_set_ap_passwd(uint8_t *passwd, int len)
{
    strncpy((char *)ap_config.ap.password, (char *)passwd, len);
    ap_config.ap.password[len] = '\0';
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

esp_err_t wifi_set_ap_max_conn(int max_conn_num)
{
    if (max_conn_num > 4)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ap_config.ap.max_connection = max_conn_num;
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

esp_err_t wifi_set_ap_auth(wifi_auth_mode_t mode)
{
    if (mode >= WIFI_AUTH_MAX)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ap_config.ap.authmode = mode;
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

esp_err_t wifi_set_ap_channel(uint8_t channel)
{
    if (channel > 13)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ap_config.ap.channel = channel;
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

void wifi_get_ssid_bssid(uint8_t bssid[6], uint8_t *ssid, int *len)
{
    if (bssid)
    {
        memcpy(bssid, gl_sta_bssid, sizeof(gl_sta_bssid));
    }

    if (ssid && len)
    {
        memcpy(ssid, gl_sta_ssid, gl_sta_ssid_len);
        *len = gl_sta_ssid_len;
    }
}

void wifi_get_ip_info(esp_netif_ip_info_t *ip)
{
    if (ip == NULL)
        return;
    *ip = ip_info;
}

int wifi_init()
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    return ESP_OK;
}

#pragma once

#include "esp_netif_types.h"
#include "esp_wifi_types.h"
#include "freertos/portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STA_START_BIT BIT0
#define CONNECTED_BIT BIT1
#define CONNECTING_BIT BIT2
#define GOT_IP_BIT BIT3
#define PASSWORD_ERROR BIT4

int wifi_init();

bool wifi_wait_event(uint32_t event, TickType_t xTicksToWait);

#define wifi_wait_connect(xTicksToWait) wifi_wait_event(CONNECTED_BIT, xTicksToWait)
#define wifi_wait_got_ip(xTicksToWait) wifi_wait_event(GOT_IP_BIT, xTicksToWait)
#define wifi_wait_sta_start(xTicksToWait) wifi_wait_event(STA_START_BIT, xTicksToWait)
#define wifi_is_goted_ip() wifi_wait_event(GOT_IP_BIT, 0)
#define wifi_is_connected(xTicksToWait) wifi_wait_event(CONNECTED_BIT, 0)
#define wifi_is_password_error() wifi_wait_event(PASSWORD_ERROR, 0)
#define wifi_is_connecting() wifi_wait_event(CONNECTING_BIT, 0)
#define wifi_sta_started(xTicksToWait) wifi_wait_event(STA_START_BIT, 0)

void record_wifi_conn_info(int rssi, uint8_t reason);
void wifi_connect(void);
bool wifi_reconnect(void);

esp_err_t wifi_set_sta_bssid(uint8_t bssid[6]);
esp_err_t wifi_set_sta_ssid(uint8_t *ssid, int ssid_len);
esp_err_t wifi_set_sta_passwd(uint8_t *passwd, int len);
esp_err_t wifi_set_ap_ssid(uint8_t *ssid, int len);
esp_err_t wifi_set_ap_passwd(uint8_t *passwd, int len);
esp_err_t wifi_set_ap_max_conn(int max_conn_num);
esp_err_t wifi_set_ap_auth(wifi_auth_mode_t mode);
esp_err_t wifi_set_ap_channel(uint8_t channel);
void wifi_get_ssid_bssid(uint8_t bssid[6], uint8_t *ssid, int *len);
void wifi_get_ip_info(esp_netif_ip_info_t *ip);

#ifdef __cplusplus
}
#endif
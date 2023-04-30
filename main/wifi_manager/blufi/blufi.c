#include "blufi_private.h"
#include "console/console.h"
#include "esp_blufi.h"
#include "esp_blufi_api.h"
#include "esp_bt.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_manager/wifi_manager.h"
#include <stdlib.h>
#include <string.h>

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param);

/* store the station info for send back to phone */
static QueueHandle_t blufi_event_queue;
static TaskHandle_t blufi_task_handle;
static bool ble_is_connected;
static wifi_sta_list_t gl_sta_list;
static esp_blufi_extra_info_t gl_sta_conn_info;

static esp_blufi_callbacks_t example_callbacks = {
    .event_cb = example_event_callback,
    .negotiate_data_handler = blufi_dh_negotiate_data_handler,
    .encrypt_func = blufi_aes_encrypt,
    .decrypt_func = blufi_aes_decrypt,
    .checksum_func = blufi_crc_checksum,
};

static int softap_get_current_connection_number(void)
{
    esp_err_t ret;
    ret = esp_wifi_ap_get_sta_list(&gl_sta_list);
    if (ret == ESP_OK)
    {
        return gl_sta_list.num;
    }

    return 0;
}

static void blufi_cmd_task(void *unused)
{
    char *cmd;
    while (true)
    {
        if (xQueueReceive(blufi_event_queue, &cmd, portMAX_DELAY) != pdPASS)
            continue;
        console_run_command(cmd);
        free(cmd);
    }
}

static void example_event_callback(esp_blufi_cb_event_t event, esp_blufi_cb_param_t *param)
{
    switch (event)
    {
    case ESP_BLUFI_EVENT_INIT_FINISH:
        BLUFI_INFO("BLUFI init finish");

        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_DEINIT_FINISH:
        BLUFI_INFO("BLUFI deinit finish");
        break;
    case ESP_BLUFI_EVENT_BLE_CONNECT:
        BLUFI_INFO("BLUFI ble connect");
        ble_is_connected = true;
        esp_blufi_adv_stop();
        blufi_security_init();
        break;
    case ESP_BLUFI_EVENT_BLE_DISCONNECT:
        BLUFI_INFO("BLUFI ble disconnect");
        ble_is_connected = false;
        blufi_security_deinit();
        esp_blufi_adv_start();
        break;
    case ESP_BLUFI_EVENT_SET_WIFI_OPMODE:
        BLUFI_INFO("BLUFI Set WIFI opmode %d", param->wifi_mode.op_mode);
        ESP_ERROR_CHECK(esp_wifi_set_mode(param->wifi_mode.op_mode));
        break;
    case ESP_BLUFI_EVENT_REQ_CONNECT_TO_AP:
        BLUFI_INFO("BLUFI requset wifi connect to AP");
        /* there is no wifi callback when the device has already connected to this wifi
        so disconnect wifi before connection.
        */
        esp_wifi_disconnect();
        wifi_connect();
        break;
    case ESP_BLUFI_EVENT_REQ_DISCONNECT_FROM_AP:
        BLUFI_INFO("BLUFI requset wifi disconnect from AP");
        esp_wifi_disconnect();
        break;
    case ESP_BLUFI_EVENT_REPORT_ERROR:
        BLUFI_ERROR("BLUFI report error, error code %d", param->report_error.state);
        esp_blufi_send_error_info(param->report_error.state);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_STATUS: {
        wifi_mode_t mode;
        esp_blufi_extra_info_t info;

        esp_wifi_get_mode(&mode);

        if (wifi_is_connected())
        {
            memset(&info, 0, sizeof(esp_blufi_extra_info_t));
            wifi_get_ssid_bssid(info.sta_bssid, info.sta_ssid, &info.sta_ssid_len);
            info.sta_bssid_set = true;
            esp_blufi_send_wifi_conn_report(mode, wifi_is_goted_ip() ? ESP_BLUFI_STA_CONN_SUCCESS : ESP_BLUFI_STA_NO_IP,
                                            softap_get_current_connection_number(), &info);
        }
        else if (wifi_is_connecting())
        {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONNECTING, softap_get_current_connection_number(),
                                            &gl_sta_conn_info);
        }
        else
        {
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_FAIL, softap_get_current_connection_number(),
                                            &gl_sta_conn_info);
        }
        BLUFI_INFO("BLUFI get wifi status from AP");

        break;
    }
    case ESP_BLUFI_EVENT_RECV_SLAVE_DISCONNECT_BLE:
        BLUFI_INFO("blufi close a gatt connection");
        esp_blufi_disconnect();
        break;
    case ESP_BLUFI_EVENT_DEAUTHENTICATE_STA:
        /* TODO */
        break;
    case ESP_BLUFI_EVENT_RECV_STA_BSSID:
        wifi_set_sta_bssid(param->sta_bssid.bssid);
        BLUFI_INFO("Recv STA BSSID %s", param->sta_bssid.bssid);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_SSID:
        wifi_set_sta_ssid(param->sta_ssid.ssid, param->sta_ssid.ssid_len);
        BLUFI_INFO("Recv STA SSID %s", param->sta_ssid.ssid);
        break;
    case ESP_BLUFI_EVENT_RECV_STA_PASSWD:
        wifi_set_sta_passwd(param->sta_passwd.passwd, param->sta_passwd.passwd_len);
        BLUFI_INFO("Recv STA PASSWORD %s", param->sta_passwd.passwd);
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_SSID:
        wifi_set_ap_ssid(param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        BLUFI_INFO("Recv SOFTAP SSID %s, ssid len %d", param->softap_ssid.ssid, param->softap_ssid.ssid_len);
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_PASSWD:
        wifi_set_ap_passwd(param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        BLUFI_INFO("Recv SOFTAP PASSWORD %s len = %d", param->softap_passwd.passwd, param->softap_passwd.passwd_len);
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_MAX_CONN_NUM:
        wifi_set_ap_max_conn(param->softap_max_conn_num.max_conn_num);
        BLUFI_INFO("Recv SOFTAP MAX CONN NUM %d", param->softap_max_conn_num.max_conn_num);
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_AUTH_MODE:
        wifi_set_ap_auth(param->softap_auth_mode.auth_mode);
        BLUFI_INFO("Recv SOFTAP AUTH MODE %d", param->softap_auth_mode.auth_mode);
        break;
    case ESP_BLUFI_EVENT_RECV_SOFTAP_CHANNEL:
        wifi_set_ap_channel(param->softap_channel.channel);
        BLUFI_INFO("Recv SOFTAP CHANNEL %d", param->softap_channel.channel);
        break;
    case ESP_BLUFI_EVENT_GET_WIFI_LIST: {
        wifi_scan_config_t scanConf = {.ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false};
        esp_err_t ret = esp_wifi_scan_start(&scanConf, true);
        if (ret != ESP_OK)
        {
            esp_blufi_send_error_info(ESP_BLUFI_WIFI_SCAN_FAIL);
        }
        break;
    }
    case ESP_BLUFI_EVENT_RECV_CUSTOM_DATA: {
        char *cmd = malloc(param->custom_data.data_len + 1);
        memcpy(cmd, param->custom_data.data, param->custom_data.data_len);
        cmd[param->custom_data.data_len] = 0;
        xQueueSend(blufi_event_queue, &cmd, 5);
        break;
    }
    case ESP_BLUFI_EVENT_RECV_USERNAME:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_CA_CERT:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_CLIENT_CERT:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_SERVER_CERT:
        /* Not handle currently */
        break;
    case ESP_BLUFI_EVENT_RECV_CLIENT_PRIV_KEY:
        /* Not handle currently */
        break;
        ;
    case ESP_BLUFI_EVENT_RECV_SERVER_PRIV_KEY:
        /* Not handle currently */
        break;
    default:
        break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint16_t apCount = 0;
    esp_wifi_scan_get_ap_num(&apCount);
    if (apCount == 0)
    {
        BLUFI_INFO("Nothing AP found");
        return;
    }
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * apCount);
    if (!ap_list)
    {
        BLUFI_ERROR("malloc error, ap_list is NULL");
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&apCount, ap_list));
    esp_blufi_ap_record_t *blufi_ap_list = (esp_blufi_ap_record_t *)malloc(apCount * sizeof(esp_blufi_ap_record_t));
    if (!blufi_ap_list)
    {
        if (ap_list)
        {
            free(ap_list);
        }
        BLUFI_ERROR("malloc error, blufi_ap_list is NULL");
        return;
    }
    for (int i = 0; i < apCount; ++i)
    {
        blufi_ap_list[i].rssi = ap_list[i].rssi;
        memcpy(blufi_ap_list[i].ssid, ap_list[i].ssid, sizeof(ap_list[i].ssid));
    }

    if (ble_is_connected == true)
    {
        esp_blufi_send_wifi_list(apCount, blufi_ap_list);
    }

    esp_wifi_scan_stop();
    free(ap_list);
    free(blufi_ap_list);
}

int blufi_init()
{
    blufi_event_queue = xQueueCreate(1, sizeof(char *));
    xTaskCreate(blufi_cmd_task, "blufi_cmd", 2048, NULL, 2, &blufi_task_handle);
    console_register_redirection(blufi_task_handle, (int (*)(const char *, uint32_t))esp_blufi_send_custom_data);

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    int ret = esp_bt_controller_init(&bt_cfg);
    if (ret)
    {
        BLUFI_ERROR("%s initialize bt controller failed: %s", __func__, esp_err_to_name(ret));
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret)
    {
        BLUFI_ERROR("%s enable bt controller failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ret = esp_blufi_host_and_cb_init(&example_callbacks);
    if (ret)
    {
        BLUFI_ERROR("%s initialise failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    BLUFI_INFO("BLUFI VERSION %04x", esp_blufi_get_version());
    return ret;
}
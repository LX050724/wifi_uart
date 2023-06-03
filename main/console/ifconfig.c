#include "console.h"
#include "esp_console.h"
#include "esp_netif_types.h"
#include "esp_sleep.h"
#include "lwip/inet.h"
#include "power/power.h"
#include "wifi_manager/wifi_manager.h"
#include <stdio.h>
#include <sys/unistd.h>

static int ifconfig_cmd_cb(int argc, char **argv)
{
    char ip[16], gw[16], mask[16];
    esp_netif_ip_info_t ip_info = {};
    wifi_get_ip_info(&ip_info);
    inet_ntoa_r(ip_info.ip.addr, ip, sizeof(ip));
    inet_ntoa_r(ip_info.gw.addr, gw, sizeof(gw));
    inet_ntoa_r(ip_info.netmask.addr, mask, sizeof(mask));
    console_printf("inet %s  netmask %s  gw %s\n", ip, mask, gw);
    return 0;
}

void register_ifconfig()
{
    const esp_console_cmd_t cmd = {
        .command = "ifconfig",
        .help = "查看网络信息",
        .hint = NULL,
        .func = ifconfig_cmd_cb,
        .argtable = NULL,
    };

    esp_console_cmd_register(&cmd);
}
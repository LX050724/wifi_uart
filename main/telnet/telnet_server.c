

#include "cc.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "wifi_manager/wifi_manager.h"
#include <assert.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <string.h>
#include <sys/_default_fcntl.h>
#include <sys/select.h>
#include <sys/unistd.h>

#define CLIENT_MAX 5

static const char *TAG = "telnet";

static TaskHandle_t telnet_server_task_handle;
static void telnet_server_task(void *arg);

// static TaskHandle_t telnet_work_task_handle;
// static void telnet_work_task(void *arg);

static int client_fds[CLIENT_MAX];

esp_err_t telnet_init()
{
    for (int i = 0; i < CLIENT_MAX; i++)
        client_fds[i] = -1;

    BaseType_t err = xTaskCreate(telnet_server_task, "telnet_srv", 4096, NULL, 1, &telnet_server_task_handle);
    if (err != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate telnet_srv failed %d %s", errno, strerror(errno));
        return ESP_FAIL;
    }
    return ESP_OK;
}

static int create_sockte()
{
    struct sockaddr_in servaddr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(23),
    };

    int sock_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        ESP_LOGE(TAG, "socket create failed %d %s", errno, strerror(errno));
        goto err;
    }

    int opval = 1;
    if (lwip_setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opval, sizeof(int)) == -1)
    {
        ESP_LOGE(TAG, "socket setsockopt failed %d %s", errno, strerror(errno));
        goto err;
    }

    if (lwip_bind(sock_fd, (const struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) == -1)
    {
        ESP_LOGE(TAG, "socket bind failed %d %s", errno, strerror(errno));
        goto err;
    }

    if (lwip_listen(sock_fd, CLIENT_MAX) == -1)
    {
        ESP_LOGE(TAG, "socket listen failed %d %s", errno, strerror(errno));
        goto err;
    }
    return sock_fd;
err:
    if (sock_fd > 0)
        lwip_close(sock_fd);
    return -1;
}

static void telnet_worker()
{
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    int sock_fd = create_sockte();
    ESP_LOGI(TAG, "create socket %d", sock_fd);

    fd_set rfds, efds;

    while (true)
    {
        FD_ZERO(&rfds);
        FD_ZERO(&efds);
        FD_SET(sock_fd, &rfds);
        FD_SET(sock_fd, &efds);

        int max_fd = sock_fd;
        for (int i = 0; i < CLIENT_MAX; i++)
        {
            if (client_fds[i] > max_fd)
                max_fd = client_fds[i];
            if (client_fds[i] != -1)
            {
                FD_SET(client_fds[i], &rfds);
                FD_SET(client_fds[i], &efds);
            }
        }

        int retval = lwip_select(max_fd + 1, &rfds, NULL, &efds, &tv);
        if (retval == -1)
        {
            ESP_LOGE(TAG, "select failed %d %s", errno, strerror(errno));
        }
        else if (retval)
        {
            if (FD_ISSET(sock_fd, &rfds))
            {
                struct sockaddr_in inaddr;
                socklen_t addrlen = sizeof(struct sockaddr_in);
                int fd = lwip_accept(sock_fd, (struct sockaddr *)&inaddr, &addrlen);
                if (fd != -1)
                {
                    char buf[16];
                    ESP_LOGI(TAG, "new connect in %s:%d %d", inet_ntoa_r(inaddr.sin_addr, buf, sizeof(buf)),
                             inaddr.sin_port, fd);
                    for (int i = 0; i < CLIENT_MAX; i++)
                    {
                        if (client_fds[i] == -1)
                        {
                            client_fds[i] = fd;
                            fd = 0;
                            break;
                        }
                    }

                    if (fd)
                    {
                        ESP_LOGE(TAG, "connection is full");
                        lwip_shutdown(fd, SHUT_RD);
                        lwip_close(fd);
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "accept failed %d %s", errno, strerror(errno));
                }
            }

            if (FD_ISSET(sock_fd, &efds))
            {
                ESP_LOGE(TAG, "sock_fd got error, shutdown");
                break;
            }

            for (int i = 0; i < CLIENT_MAX; i++)
            {
                uint8_t read_buf[256] = {};
                if (FD_ISSET(client_fds[i], &rfds))
                {
                    int rd_len = lwip_read(client_fds[i], read_buf, sizeof(read_buf));
                    if (rd_len == -1)
                    {
                        ESP_LOGI(TAG, "fd: %d disconnected", client_fds[i]);
                        lwip_shutdown(client_fds[i], SHUT_RD);
                        lwip_close(client_fds[i]);
                        client_fds[i] = -1;
                    }
                    else
                    {
                        /* TODO: Telnet协议处理 */
                    }
                }

                if (FD_ISSET(client_fds[i], &efds))
                {
                    ESP_LOGE(TAG, "fd %d error, close", client_fds[i]);
                    lwip_shutdown(client_fds[i], SHUT_RD);
                    lwip_close(client_fds[i]);
                    client_fds[i] = -1;
                }
            }
        }
    }

    for (int i = 0; i < CLIENT_MAX; i++)
    {
        if (client_fds[i] > -1)
        {
            lwip_shutdown(client_fds[i], SHUT_RD);
            lwip_close(client_fds[i]);
        }
        client_fds[i] = -1;
    }
    lwip_close(sock_fd);
}

static void telnet_server_task(void *arg)
{
    while (true)
    {
        wifi_wait_got_ip(portMAX_DELAY);
        telnet_worker();
    }
}

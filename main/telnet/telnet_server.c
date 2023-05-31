

#include "cc.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "events/events.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/sockets.h"
#include "usr_uart/usr_uart.h"
#include "wifi_manager/wifi_manager.h"
#include <assert.h>
#include <errno.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/_default_fcntl.h>
#include <sys/errno.h>
#include <sys/select.h>
#include <sys/unistd.h>

#define CLIENT_MAX 8

#define TELNET_PORT 23
#define TELNET_TX_BUF 1024

#define IAC 255           /* FF interpret as command: */
#define DONT 254          /* FE you are not to use option */
#define DO 253            /* FD please, you use option */
#define WONT 252          /* FC I won't use option */
#define WILL 251          /* FB I will use option */
#define SB 250            /* FA interpret as subnegotiation */
#define SE 240            /* F0 end sub negotiation */
#define NOP 241           /* F1 No Operation */
#define TELOPT_ECHO 1     /* 01 echo */
#define TELOPT_SGA 3      /* 03 suppress go ahead */
#define TELOPT_TTYPE 24   /* 18 terminal type */
#define TELOPT_NAWS 31    /* 1F window size */
#define TELOPT_BREAK 0xf3 /* F3 Break */

static const char *TAG = "telnet";

typedef enum
{
    FSM_IDLE,
    FSM_OPT,
    FSM_CMD,
    FSM_SUB_CMD,
    FSM_SUB_CMD_OPT,
} IAC_FSM;

typedef struct
{
    int fd;
    char ip_str[32];
    IAC_FSM fsm;
    int opt;
} TelnetConnect_t;

static TelnetConnect_t client_fds[CLIENT_MAX];
static char uart_rdbuf[1024];

static TaskHandle_t telnet_server_task_handle;
static void telnet_server_task(void *arg);

static TaskHandle_t uart_event_task_handle;
static void telnet_uart_event_task(void *arg);

static uint8_t telnet_proc_iac(TelnetConnect_t *connect, uint8_t data);
static void telnet_send_to_all(const void *data, size_t len);
static void telnet_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

static const uint8_t telnet_ctrl[] = {
    IAC, DO,   TELOPT_ECHO, //
    IAC, DO,   TELOPT_NAWS, //
    IAC, WILL, TELOPT_ECHO, //
    IAC, WILL, TELOPT_SGA,  //
};

esp_err_t telnet_init()
{
    for (int i = 0; i < CLIENT_MAX; i++)
    {
        client_fds[i].fd = -1;
    }
    vTaskSuspendAll();

    BaseType_t err = xTaskCreate(telnet_server_task, "telnet_srv", 4096, NULL, 1, &telnet_server_task_handle);
    if (err != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate telnet_srv failed %d %s", errno, strerror(errno));
        xTaskResumeAll();
        return ESP_FAIL;
    }

    err = xTaskCreate(telnet_uart_event_task, "uart_event_task", 2048, NULL, 1, &uart_event_task_handle);
    if (err != pdPASS)
    {
        vTaskDelete(telnet_server_task_handle);
        xTaskResumeAll();
        ESP_LOGE(TAG, "xTaskCreate telnet_srv failed %d %s", errno, strerror(errno));
        return ESP_FAIL;
    }

    xTaskResumeAll();

    esp_event_handler_register(APP_EVENTS, -1, telnet_event_handler, NULL);

    return ESP_OK;
}

static void telnet_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case APP_EVENT_POWER_DOWN:
        telnet_send_to_all(">>> Wireless serial: Power down <<<\r\n", 0);
        break;
    case APP_EVENT_POWER_ON:
        telnet_send_to_all(">>> Wireless serial: Power on <<<\r\n", 0);
        break;
    case APP_EVENT_POWER_LOW:
        telnet_send_to_all(">>> Wireless serial: Low power <<<\r\n", 0);
        break;
    }
}

static int create_sockte()
{
    struct sockaddr_in servaddr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family = AF_INET,
        .sin_port = htons(TELNET_PORT),
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
        ESP_LOGE(TAG, "socket setsockopt SO_REUSEADDR failed %d %s", errno, strerror(errno));
        goto err;
    }

    if (lwip_setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &opval, sizeof(int)) == -1)
    {
        ESP_LOGE(TAG, "socket setsockopt SO_KEEPALIVE failed %d %s", errno, strerror(errno));
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
            if (client_fds[i].fd > max_fd)
                max_fd = client_fds[i].fd;
            if (client_fds[i].fd != -1)
            {
                FD_SET(client_fds[i].fd, &rfds);
                FD_SET(client_fds[i].fd, &efds);
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
                    for (int i = 0; i < CLIENT_MAX; i++)
                    {
                        if (client_fds[i].fd == -1)
                        {
                            client_fds[i].fd = fd;
                            inet_ntoa_r(inaddr.sin_addr, client_fds[i].ip_str, sizeof(client_fds[i].ip_str));
                            lwip_send(fd, telnet_ctrl, sizeof(telnet_ctrl), MSG_DONTWAIT);
                            fd = 0;
                            break;
                        }
                    }

                    if (fd)
                    {
                        char buf[32];
                        ESP_LOGE(TAG, "connection is full, close %d:%s", fd,
                                 inet_ntoa_r(inaddr.sin_addr, buf, sizeof(buf)));
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
                TelnetConnect_t *client = &client_fds[i];
                if (FD_ISSET(client->fd, &rfds))
                {
                    int rd_len = lwip_recv(client->fd, read_buf, sizeof(read_buf), MSG_DONTWAIT);
                    if (rd_len <= 0)
                    {
                        if (errno == EWOULDBLOCK)
                        {
                            continue;
                        }
                        ESP_LOGI(TAG, "%d:%s disconnected %s", client->fd, client->ip_str, strerror(errno));
                        lwip_shutdown(client->fd, SHUT_RD);
                        lwip_close(client->fd);
                        client->fd = -1;
                    }
                    else
                    {
                        uint8_t *wp = read_buf;
                        int send_len = 0;
                        for (int j = 0; j < rd_len; j++)
                        {
                            uint8_t tmp = telnet_proc_iac(client, read_buf[j]);
                            if (tmp != 0xff)
                            {
                                *wp++ = tmp;
                                send_len++;
                            }
                        }

                        if (send_len)
                            uart_write_bytes(UART_NUM_1, read_buf, send_len);
                    }
                }

                if (FD_ISSET(client->fd, &efds))
                {
                    ESP_LOGE(TAG, "%d:%s error, close", client->fd, client->ip_str);
                    lwip_shutdown(client->fd, SHUT_RD);
                    lwip_close(client->fd);
                    client->fd = -1;
                }
            }
        }
    }

    for (int i = 0; i < CLIENT_MAX; i++)
    {
        if (client_fds[i].fd > -1)
        {
            lwip_shutdown(client_fds[i].fd, SHUT_RD);
            lwip_close(client_fds[i].fd);
        }
        client_fds[i].fsm = FSM_IDLE;
        client_fds[i].opt = 0;
        client_fds[i].fd = -1;
    }
    lwip_close(sock_fd);
}

static int telnet_uart_send_break()
{
    uint8_t data[] = {0};
    return uart_write_bytes_with_break(UART_NUM_1, data, sizeof(data), 128);
}

static void telnet_proc_cmd(TelnetConnect_t *connect, uint8_t op, uint8_t cmd)
{
    ESP_LOGI(TAG, "%d:%s Reveice IAC %02X %02X", connect->fd, connect->ip_str, op, cmd);
    if (op == TELOPT_BREAK)
    {
        ESP_LOGW(TAG, "%d:%s, send break signal", connect->fd, connect->ip_str);
        telnet_uart_send_break();
    }
}

static uint8_t telnet_proc_iac(TelnetConnect_t *connect, uint8_t data)
{
    switch (connect->fsm)
    {
    case FSM_IDLE:
        if (data == 0xff)
        {
            connect->fsm = FSM_OPT;
            return 0xff;
        }
        else
        {
            connect->fsm = FSM_IDLE;
            return data == 0 ? 0xff : data;
        }
    case FSM_OPT:
        switch (data)
        {
        case DONT:
        case DO:
        case WONT:
        case WILL:
            connect->fsm = FSM_CMD;
            connect->opt = data;
            return 0xff;
        case SB:
            connect->fsm = FSM_SUB_CMD;
            return 0xff;
        case TELOPT_BREAK:
            telnet_proc_cmd(connect, TELOPT_BREAK, 0);
            connect->fsm = FSM_IDLE;
            return 0xff;
        default:
            connect->fsm = FSM_IDLE;
            return 0xff;
        }
    case FSM_SUB_CMD:
        if (data == 0xff)
        {
            connect->fsm = FSM_SUB_CMD_OPT;
        }
        return 0xff;
    case FSM_SUB_CMD_OPT:
        connect->fsm = FSM_IDLE;
        return 0xff;
    case FSM_CMD:
        connect->fsm = FSM_IDLE;
        telnet_proc_cmd(connect, connect->opt, data);
        return 0xff;
    }
    return 0xff;
}

static void telnet_server_task(void *arg)
{
    while (true)
    {
        wifi_wait_got_ip(portMAX_DELAY);
        telnet_worker();
    }
}

static void telnet_send_to_all(const void *data, size_t len)
{
    if (len == 0)
        len = strlen(data);
    for (int i = 0; i < CLIENT_MAX; i++)
    {
        if (client_fds[i].fd != -1)
            lwip_send(client_fds[i].fd, data, len, MSG_DONTWAIT);
    }
}

static void telnet_uart_event_task(void *arg)
{
    QueueHandle_t uart_queue = uart_get_event_queue();
    uart_event_t event;
    for (;;)
    {
        // Waiting for UART event.
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY))
        {
            switch (event.type)
            {
            case UART_DATA:
                uart_read_bytes(UART_NUM_1, uart_rdbuf, event.size, portMAX_DELAY);
                telnet_send_to_all(uart_rdbuf, event.size);
                break;
            case UART_BREAK:
                ESP_LOGI(TAG, "uart rx break");
                break;
            case UART_PARITY_ERR:
                ESP_LOGI(TAG, "uart parity error");
                break;
            case UART_FRAME_ERR:
                ESP_LOGI(TAG, "uart frame error");
                break;
            default:
                ESP_LOGI(TAG, "uart event type: %d", event.type);
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

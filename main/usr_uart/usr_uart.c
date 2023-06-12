
#include "config/config.h"
#include "driver/uart.h"
#include "driver/uart_select.h"
#include "esp_err.h"
#include "esp_log.h"
#include "hal/gpio_types.h"

static QueueHandle_t uart_queue;

esp_err_t usr_uart_init()
{
    uart_config_t uart_config = {};
    conf_get_uart_param(&uart_config);
    uart_driver_install(UART_NUM_1, 1024, 1024, 20, &uart_queue, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, GPIO_NUM_5, GPIO_NUM_4, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    return ESP_OK;
}

QueueHandle_t uart_get_event_queue()
{
    return uart_queue;
}
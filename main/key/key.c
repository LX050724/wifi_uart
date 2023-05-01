
#include "key.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/gpio_hal.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

static TaskHandle_t key_scan_task_handel;
static void key_scan_task(void *arg);

static SemaphoreHandle_t mutex;

typedef struct
{
    TaskHandle_t handle;
    KeyEvent wait_event;
    TickType_t start_time;
    TickType_t wait_time;
    KeyEvent *ret;
} KeyEventWaiting_t;

#define WAITING_QUEUE_SIZE 16
static KeyEventWaiting_t waiting_queue[WAITING_QUEUE_SIZE];

esp_err_t key_init()
{
    gpio_config_t gpio_conf = {};
    gpio_conf.mode = GPIO_MODE_INPUT;
    gpio_conf.intr_type = GPIO_INTR_ANYEDGE;

    gpio_conf.pin_bit_mask = 1 << GPIO_NUM_0;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    gpio_config(&gpio_conf);

    gpio_conf.pin_bit_mask |= 1 << GPIO_NUM_9;
    gpio_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_conf.pull_down_en = GPIO_PULLUP_DISABLE;
    gpio_config(&gpio_conf);

    REG_SET_BIT(IO_MUX_GPIO0_REG, BIT(15));
    REG_SET_BIT(IO_MUX_GPIO9_REG, BIT(15));

    mutex = xSemaphoreCreateMutex();

    xTaskCreate(key_scan_task, "key_scan", 512, NULL, 2, &key_scan_task_handel);
    return ESP_OK;
}

static bool key_event_waiting_node_is_free(int i)
{
    /* 空节点 */
    if (waiting_queue[i].handle == NULL)
        return true;

    TickType_t tick_count = xTaskGetTickCount();

    /* 超时节点 */
    if (MIN(tick_count - waiting_queue[i].start_time, portMAX_DELAY - tick_count + waiting_queue[i].start_time) >
        waiting_queue[i].wait_time)
    {
        memset(&waiting_queue[i], 0, sizeof(KeyEventWaiting_t));
        return true;
    }

    return false;
}

static void key_send_event(KeyEvent event)
{
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (int i = 0; i < WAITING_QUEUE_SIZE; i++)
    {
        if (key_event_waiting_node_is_free(i))
            continue;
        if (waiting_queue[i].wait_event & event)
        {
            *waiting_queue[i].ret = event;
            xTaskNotifyGive(waiting_queue[i].handle);
            memset(&waiting_queue[i], 0, sizeof(KeyEventWaiting_t));
        }
    }
    xSemaphoreGive(mutex);
}

static void key_scan_task(void *arg)
{
    bool up_key = false;
    bool up_key_last = false;
    TickType_t up_key_pressed_time = 0;

    bool down_key = false;
    bool down_key_last = false;
    TickType_t down_key_pressed_time = 0;

    while (true)
    {
        up_key = key_up_button_pressed();
        down_key = key_down_button_pressed();
        if (up_key != up_key_last)
        {
            if (up_key)
            {
                /* 上按键按下 */
                up_key_pressed_time = 1;
            }
            else
            {
                /* 上按键抬起 */
                if (up_key_pressed_time < 70)
                    key_send_event(KEY_UP_SHORT_PRESSED);
                else if (up_key_pressed_time < 500)
                    key_send_event(KEY_UP_LONG_PRESSED);
                up_key_pressed_time = 0;
            }
        }
        else if (up_key)
        {
            /* 上按键持续按下 */
            up_key_pressed_time++;
        }

        if (down_key != down_key_last)
        {
            if (down_key)
            {
                /* 下按键按下 */
                down_key_pressed_time = 1;
            }
            else
            {
                /* 下按键抬起 */
                if (down_key_pressed_time < 70)
                    key_send_event(KEY_DOWN_SHORT_PRESSED);
                else if (down_key_pressed_time < 500)
                    key_send_event(KEY_DOWN_LONG_PRESSED);
                down_key_pressed_time = 0;
            }
        }
        else if (down_key)
        {
            /* 下按键持续按下 */
            down_key_pressed_time++;
        }

        up_key_last = up_key;
        down_key_last = down_key;
        vTaskDelay(1);
    }
}

KeyEvent key_wait_event(KeyEvent event, TickType_t xTicksToWait)
{
    TickType_t xPreviousWakeTime = xTaskGetTickCount();
    KeyEvent ret_event = 0;
    int i = 0;

    xSemaphoreTake(mutex, portMAX_DELAY);
    for (; i < WAITING_QUEUE_SIZE; i++)
    {
        if (!key_event_waiting_node_is_free(i))
            continue;
        waiting_queue[i].handle = xTaskGetCurrentTaskHandle();
        waiting_queue[i].start_time = xPreviousWakeTime;
        waiting_queue[i].wait_time = xTicksToWait;
        waiting_queue[i].wait_event = event;
        waiting_queue[i].ret = &ret_event;
        break;
    }
    xSemaphoreGive(mutex);

    if (i < WAITING_QUEUE_SIZE)
    {
        bool xShouldDelay = pdFALSE;
        TickType_t xConstTickCount = xTaskGetTickCount();
        TickType_t xTimeToWake = xPreviousWakeTime + xTicksToWait;

        if (xConstTickCount < xPreviousWakeTime)
        {
            xShouldDelay = ((xTimeToWake < xPreviousWakeTime) && (xTimeToWake > xConstTickCount));
        }
        else
        {
            xShouldDelay = ((xTimeToWake < xPreviousWakeTime) || (xTimeToWake > xConstTickCount));
        }
        if (xShouldDelay)
            ulTaskNotifyTake(pdTRUE, xTicksToWait);
    }

    return ret_event;
}

bool key_up_button_pressed()
{
    return !gpio_get_level(GPIO_NUM_9);
}

bool key_down_button_pressed()
{
    return gpio_get_level(GPIO_NUM_0);
}

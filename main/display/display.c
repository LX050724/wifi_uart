#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "key/key.h"
#include "lvgl.h"
#include <stdint.h>
#include <string.h>
#include <sys/cdefs.h>

static const char *TAG = "OLED";

#define I2C_HOST I2C_NUM_0

#define I2C_HW_ADDR 0x3C

#define LCD_H_RES 128
#define LCD_V_RES 32

#define LVGL_TICK_PERIOD_MS 2

typedef struct
{
    int head;
    uint8_t GRAM[LCD_V_RES / 8][LCD_H_RES];
} DisplayRAM;

DisplayRAM display_ram;
DisplayRAM display_ram2;

static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
static lv_disp_drv_t disp_drv;      // contains callback functions
static lv_indev_drv_t indev_drv;
static TaskHandle_t oled_task_handle;
static void display_task(void *param);

static void display_lvgl_set_px_cb(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
                                   lv_color_t color, lv_opa_t opa);
static void display_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void display_lvgl_key_scan(struct _lv_indev_drv_t *indev_drv, lv_indev_data_t *data);

static void display_lvgl_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area);
static void display_increase_lvgl_tick(void *arg);

static uint8_t CMD_Data[] = {
    0x00,                // MEM ADDR
    0xAE,                // 关闭显示
    0x00,                // 设置显示时的起始列地址低四位。0
    0x10,                // 设置显示时的起始列地址高四位。0
    0x40,                // 设置显示RAM显示起始行寄存器
    0xB0,                // 用于设置页地址，其低三位的值对应着GRAM的页地址。
    0x81, 0x8f,          // 设置对比度255
    0xA1,                // 列地址127被映射到SEG0
    0xA6,                // Set Normal/Inverse Display
    0xA8, LCD_V_RES - 1, // 设置多路复用率
    0xC8,                // 重新映射模式。扫描的COM COM0 (n - 1)
    0xD3, 0x00,          // Set Display Offset, 0
    0xD5, 0x80,          // Set Display Clock Divide Ratio/Oscillator Frequency
    0xD9, 0xF1,          // Set Pre-charge Period
    0xDA, 0x02,          // Set COM Pins Hardware Configuration
    0x8D, 0x14,          // DCDC ON
    0x20, 0x00,          // 设置寻址模式为水平模式
    0xb0,                // 设置行列指针位置0,0
    0x00, 0x10,          //
    0xAF,                // 开启显示
};

void display_init()
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_OLED_I2C_SDA,
        .scl_io_num = CONFIG_OLED_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0));

    i2c_master_write_to_device(I2C_HOST, I2C_HW_ADDR, CMD_Data, sizeof(CMD_Data), pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    display_ram.head = 0x40404040;
    lv_disp_draw_buf_init(&disp_buf, display_ram.GRAM, NULL, LCD_H_RES * LCD_V_RES);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;                      // 水平大小
    disp_drv.ver_res = LCD_V_RES;                      // 垂直大小
    disp_drv.full_refresh = 1;                         // 全屏刷新
    disp_drv.flush_cb = display_lvgl_flush_cb;         // 刷新函数
    disp_drv.draw_buf = &disp_buf;                     // 缓冲区
    disp_drv.rounder_cb = display_lvgl_rounder;        //
    disp_drv.set_px_cb = display_lvgl_set_px_cb;       //
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv); // 注册

    lv_indev_drv_init(&indev_drv);
    indev_drv.disp = disp;
    // indev_drv.long_press_time = 500;
    // indev_drv.long_press_repeat_time = 1000;
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = display_lvgl_key_scan;
    lv_indev_t *indev = lv_indev_drv_register(&indev_drv);

    lv_group_t *group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(indev, group);

    void main_page_init();
    main_page_init();

    // 创建2ms定时器作为时钟源
    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = &display_increase_lvgl_tick, .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Display LVGL Scroll Text");

    int ret = xTaskCreate(display_task, "lvgl", 8192, NULL, 5, &oled_task_handle);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "create lvgl thread failed");
    }
}

static void display_task(void *param)
{
    TickType_t PreviousWakeTime = xTaskGetTickCount();
    while (1)
    {
        lv_timer_handler();
        xTaskDelayUntil(&PreviousWakeTime, pdMS_TO_TICKS(30));
    }
    vTaskDelete(NULL);
}

static void display_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    i2c_master_write_to_device(I2C_HOST, I2C_HW_ADDR, ((uint8_t *)&display_ram) + 3, sizeof(display_ram) - 3,
                               pdMS_TO_TICKS(10));
    lv_disp_flush_ready(drv);
}

static IRAM_ATTR void display_lvgl_set_px_cb(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x,
                                             lv_coord_t y, lv_color_t color, lv_opa_t opa)
{
    uint8_t(*GRAM)[128] = (uint8_t(*)[128])buf;
    uint8_t bit = 1 << (y & 0x7);
    if (color.full == 0)
    {
        GRAM[y / 8][x] &= ~bit;
    }
    else
    {
        GRAM[y / 8][x] |= bit;
    }
}

static IRAM_ATTR void display_lvgl_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    area->y1 = area->y1 & (~0x7);
    area->y2 = area->y2 | 0x7;
}

static void display_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

// static void display_lvgl_key_scan(struct _lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
// {
//     data->state = key_up_button_pressed();
// }

static void display_lvgl_key_scan(struct _lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    static bool up_key = false;
    static bool up_key_last = false;
    static TickType_t up_key_pressed_time = 0;

    static bool down_key = false;
    static bool down_key_last = false;
    static TickType_t down_key_pressed_time = 0;

    up_key = key_up_button_pressed();
    down_key = key_down_button_pressed();
    data->state = LV_INDEV_STATE_RELEASED;
    data->key = 0;
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
            if (up_key_pressed_time < 20)
                data->key = LV_KEY_PREV;
            else if (up_key_pressed_time < 100)
                data->key = LV_KEY_ENTER;
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
            if (down_key_pressed_time < 20)
                data->key = LV_KEY_NEXT;
            else if (down_key_pressed_time < 100)
                data->key = LV_KEY_ESC;
            down_key_pressed_time = 0;
        }
    }
    else if (down_key)
    {
        /* 下按键持续按下 */
        down_key_pressed_time++;
    }

    if (data->key)
    {
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGI(TAG, "Key %d", (int)data->key);
    }
    up_key_last = up_key;
    down_key_last = down_key;
}

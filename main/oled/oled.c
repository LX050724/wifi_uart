#include "core/lv_disp.h"
#include "core/lv_obj.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal/lv_hal_disp.h"
#include "widgets/lv_label.h"
#include <sys/cdefs.h>

static const char *TAG = "OLED";

#define I2C_HOST I2C_NUM_0

#define I2C_HW_ADDR 0x3C

#define LCD_H_RES 128
#define LCD_V_RES 32

#define LVGL_TICK_PERIOD_MS 2

struct __packed
{
    uint8_t head;
    uint8_t GRAM[LCD_V_RES / 8][LCD_H_RES];
} display_ram;

static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
static lv_disp_drv_t disp_drv;      // contains callback functions
static TaskHandle_t oled_task_handle;
static void oled_task(void *param);

static void oled_lvgl_set_px_cb(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
                                lv_color_t color, lv_opa_t opa);
static void oled_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
static void oled_lvgl_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area);
static void oled_increase_lvgl_tick(void *arg);

uint8_t CMD_Data[] = {
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

void oled_init()
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

    display_ram.head = 0x40;
    lv_disp_draw_buf_init(&disp_buf, display_ram.GRAM, NULL, LCD_H_RES * LCD_V_RES);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;                      // 水平大小
    disp_drv.ver_res = LCD_V_RES;                      // 垂直大小
    disp_drv.full_refresh = 1;                         // 全屏刷新
    disp_drv.flush_cb = oled_lvgl_flush_cb;            // 刷新函数
    disp_drv.draw_buf = &disp_buf;                     // 缓冲区
    disp_drv.rounder_cb = oled_lvgl_rounder;           //
    disp_drv.set_px_cb = oled_lvgl_set_px_cb;          //
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv); // 注册

    // 创建2ms定时器作为时钟源
    ESP_LOGI(TAG, "Install LVGL tick timer");
    const esp_timer_create_args_t lvgl_tick_timer_args = {.callback = &oled_increase_lvgl_tick, .name = "lvgl_tick"};
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Display LVGL Scroll Text");
    // example_lvgl_demo_ui(disp);

    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, 150);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    int ret = xTaskCreate(oled_task, "lvgl", 2048, NULL, 5, &oled_task_handle);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "create lvgl thread failed");
    }
}

static void oled_task(void *param)
{
    TickType_t PreviousWakeTime = xTaskGetTickCount();
    while (1)
    {
        lv_timer_handler();
        xTaskDelayUntil(&PreviousWakeTime, pdMS_TO_TICKS(20));
    }
    vTaskDelete(NULL);
}

static void oled_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    i2c_master_write_to_device(I2C_HOST, I2C_HW_ADDR, (uint8_t *)&display_ram, sizeof(display_ram), pdMS_TO_TICKS(10));
    lv_disp_flush_ready(drv);
}

static void oled_lvgl_set_px_cb(lv_disp_drv_t *disp_drv, uint8_t *buf, lv_coord_t buf_w, lv_coord_t x, lv_coord_t y,
                                lv_color_t color, lv_opa_t opa)
{
    if (x > LCD_H_RES || y > LCD_V_RES)
    {
        return;
    }

    if ((color.full == 0) && (LV_OPA_TRANSP != opa))
    {
        buf[(y / 8) * 128 + x] |= 1 << (y & 0x07);
    }
    else
    {
        buf[(y / 8) * 128 + x] &= ~(1 << (y & 0x07));
    }
}

static void oled_lvgl_rounder(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    area->y1 = area->y1 & (~0x7);
    area->y2 = area->y2 | 0x7;
}

static void oled_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

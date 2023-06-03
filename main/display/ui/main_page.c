#include "config/config.h"
#include "core/lv_obj.h"
#include "core/lv_obj_style.h"
#include "esp_log.h"
#include "font/lv_font.h"
#include "lvgl.h"
#include "lwip/inet.h"
#include "misc/lv_style.h"
#include "misc/lv_timer.h"
#include "widgets/lv_label.h"
#include "wifi_manager/wifi_manager.h"

static void ip_label_timer_cb(lv_timer_t *timer)
{
    static esp_netif_ip_info_t ip_info_old;
    static char ip_str[16];
    esp_netif_ip_info_t ip_info;
    wifi_get_ip_info(&ip_info);
    if (ip_info.ip.addr != ip_info_old.ip.addr)
    {
        inet_ntoa_r(ip_info.ip.addr, ip_str, sizeof(ip_str));
        lv_label_set_text_static(timer->user_data, ip_str);
    }
}

void main_page_init()
{
    lv_group_t *grpup = lv_group_get_default();
    lv_obj_t *parent = lv_disp_get_scr_act(NULL);

    lv_obj_t *ip_label = lv_label_create(parent);
    lv_style_t front_style;
    lv_style_init(&front_style);
    lv_style_set_text_letter_space(&front_style, 1);
    lv_label_set_text_static(ip_label, "0.0.0.0");
    lv_obj_set_style_text_letter_space(ip_label, 1, 0);
    lv_obj_align(ip_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_group_add_obj(grpup, ip_label);

    lv_timer_create(ip_label_timer_cb, 500, ip_label);
    
    // lv_obj_t *lv_check_box = lv_checkbox_create(parent);
    // lv_obj_align(lv_check_box, LV_ALIGN_TOP_MID, 0, 0);
    // lv_obj_t *lv_check_box2 = lv_checkbox_create(parent);
    // lv_obj_align_to(lv_check_box2, lv_check_box, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    // lv_group_add_obj(grpup, lv_check_box);
    // lv_group_add_obj(grpup, lv_check_box2);
}
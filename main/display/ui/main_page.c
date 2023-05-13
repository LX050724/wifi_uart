#include "config/config.h"
#include "hal/lv_hal_disp.h"
#include "lvgl.h"

void main_page_init()
{
    lv_obj_t *scr = lv_disp_get_scr_act(lv_disp_get_default());
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_label_set_text(label, "Hello Espressif, Hello LVGL.");
    lv_obj_set_width(label, 150);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 0);

    // conf_get_dev_name(char *name, size_t len)
}
#include "config/config.h"
#include "lvgl.h"


void main_page_init()
{
    lv_group_t *grpup = lv_group_get_default();
    lv_obj_t *parent = lv_disp_get_scr_act(NULL);

    lv_obj_t *lv_check_box = lv_checkbox_create(parent);
    lv_obj_align(lv_check_box, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_t *lv_check_box2 = lv_checkbox_create(parent);
    lv_obj_align_to(lv_check_box2, lv_check_box, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

    lv_group_add_obj(grpup, lv_check_box);
    lv_group_add_obj(grpup, lv_check_box2);
}
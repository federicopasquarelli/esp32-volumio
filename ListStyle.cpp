#include "ListStyle.h"

void styleListRow(lv_obj_t* row, int32_t height) {
    lv_obj_set_height(row, height);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    // lv_list buttons are flex rows that put their children at the top; center them vertically.
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(row, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(row, 0, 0);
}

void styleListText(lv_obj_t* text) {
    lv_obj_set_style_bg_opa(text, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(text, 0, 0);
}

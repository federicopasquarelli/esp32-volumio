#include "ListStyle.h"
#include "CustomFonts.h"

lv_obj_t* createList(lv_obj_t* parent) {
    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_size(list, LV_PCT(100), LIST_BODY_H);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_HEADER_H);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_clip_corner(list, false, 0);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_text_color(list, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_ext_14, 0);
    return list;
}

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

lv_obj_t* createListHeader(lv_obj_t* parent, int32_t height) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), height);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_hor(bar, 8, 0);
    lv_obj_set_style_pad_column(bar, 8, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return bar;
}

lv_obj_t* addListTitle(lv_obj_t* header, const char* text) {
    lv_obj_t* label = lv_label_create(header);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xAAAAAA), 0);
    lv_label_set_text(label, text);
    return label;
}

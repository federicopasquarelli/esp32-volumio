#include "VolumioArt.h"
#include "DisplayConfig.h"

void setupAlbumArt(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* placeholder = lv_obj_create(parent);
    lv_obj_set_pos(placeholder, x, y);
    lv_obj_set_size(placeholder, w, h);
    lv_obj_set_style_radius(placeholder, 10, 0);
    lv_obj_set_style_bg_color(placeholder, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(placeholder, 0, 0);
    lv_obj_remove_flag(placeholder, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* icon = lv_label_create(placeholder);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(icon);
}

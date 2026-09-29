#include "PaginationNav.h"
#include "DisplayConfig.h"

lv_obj_t* createPagerButton(lv_obj_t* parent, int32_t width, int32_t height,
                             const char* symbol, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(parent);

    // A plain rectangle would read as more of a visual change than it's worth saving a line
    // over, so the corner radius is the one thing worth capturing from the theme before it's
    // stripped below -- everything else this button needs (colours per state) is set explicitly.
    int32_t radius = lv_obj_get_style_radius(b, LV_PART_MAIN);
    lv_obj_remove_style_all(b);

    // lv_obj_set_size() is itself LV_STYLE_WIDTH/HEIGHT under the hood (lv_obj_pos.c) -- it has to
    // come after remove_style_all(), not before, or the removal wipes it out too.
    lv_obj_set_size(b, width, height);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);

    // Enabled buttons are filled with the accent, disabled ones are empty, like the tab labels.
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x19A34A), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, lv_color_hex(0xAAAAAA), LV_STATE_DISABLED);
    lv_obj_set_style_border_width(b, 1, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(b, lv_color_hex(COLOR_ACCENT), LV_STATE_DISABLED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return b;
}

void setPagerEnabled(lv_obj_t* obj, bool enabled) {
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

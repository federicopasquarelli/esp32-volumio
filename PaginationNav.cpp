#include "PaginationNav.h"
#include "DisplayConfig.h"

lv_obj_t* createPagerButton(lv_obj_t* parent, int32_t width, int32_t height,
                             const char* symbol, lv_event_cb_t cb, void* user_data) {
    lv_obj_t* b = lv_button_create(parent);

    // Keep the theme's corner radius; every colour is set explicitly below.
    int32_t radius = lv_obj_get_style_radius(b, LV_PART_MAIN);
    lv_obj_remove_style_all(b);

    // The size is a style property too, so it has to be set after remove_style_all().
    lv_obj_set_size(b, width, height);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);

    // Enabled buttons are filled with the accent, disabled ones are outlined.
    lv_obj_set_style_bg_color(b, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x19A34A), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, lv_color_hex(0xAAAAAA), LV_STATE_DISABLED);
    lv_obj_set_style_border_width(b, 1, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(b, lv_color_hex(COLOR_ACCENT), LV_STATE_DISABLED);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return b;
}

void setPagerEnabled(lv_obj_t* obj, bool enabled) {
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

void formatPageLabel(char* buf, size_t bufsize, bool listing, int page, int page_size, int total_count) {
    if (!listing) {
        strlcpy(buf, "-", bufsize);
        return;
    }
    if (total_count >= 0) {
        int total_pages = (total_count + page_size - 1) / page_size;
        if (total_pages < 1) total_pages = 1;  // an empty list is still "page 1 of 1"
        snprintf(buf, bufsize, "%d/%d (%d)", page + 1, total_pages, total_count);
    } else {
        snprintf(buf, bufsize, "%d", page + 1);
    }
}

#include "QueueActionMenu.h"
#include "VolumioHandler.h"
#include "CustomFonts.h"

#define MENU_ROW_H 34
#define ACTION_UPDATE (QUEUE_CLEAR_AND_PLAY + 1)

struct ActionItem {
    char title[96];
    char uri[224];
    char service[16];
};

static ActionItem menu_item;    // the item the open menu is for
static ActionItem action_item;  // the item the running task works on
static QueueAction action_requested;
static volatile bool action_running = false;

static void copyItem(ActionItem& dst, const char* title, const char* uri, const char* service) {
    strlcpy(dst.title, title, sizeof(dst.title));
    strlcpy(dst.uri, uri, sizeof(dst.uri));
    strlcpy(dst.service, service, sizeof(dst.service));
}

static void actionTask(void*) {
    runQueueAction(action_requested, action_item.service, action_item.uri, action_item.title);
    action_running = false;
    vTaskDelete(NULL);
}

void startQueueAction(QueueAction action, const char* service, const char* uri, const char* title) {
    if (action_running) return;
    copyItem(action_item, title, uri, service);
    action_requested = action;
    action_running = true;
    if (xTaskCreatePinnedToCore(actionTask, "QueueAction", 8192, NULL, 1, NULL, 1) != pdPASS) {
        action_running = false;
    }
}

static void overlay_cb(lv_event_t* e) {
    // Only taps on the dimmed background close the menu, not taps bubbling up from the panel.
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) lv_obj_delete_async(lv_event_get_target_obj(e));
}

static void menu_action_cb(lv_event_t* e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    if (action == ACTION_UPDATE) {
        // Volumio has no REST route for a database update, so this one uses the WebSocket.
        updateFolder(menu_item.uri);
    } else {
        startQueueAction((QueueAction)action, menu_item.service, menu_item.uri, menu_item.title);
    }
    // The overlay is the panel's parent.
    lv_obj_t* overlay = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target_obj(e)));
    lv_obj_delete_async(overlay);
}

void showQueueActionMenu(const char* title, const char* uri, const char* service, bool with_update) {
    copyItem(menu_item, title, uri, service);

    lv_obj_t* overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(overlay, overlay_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* menu = lv_list_create(overlay);
    lv_obj_set_size(menu, 240, LV_SIZE_CONTENT);
    lv_obj_center(menu);
    // Jitter on the resistive touch would otherwise start a scroll and cancel the button click.
    lv_obj_remove_flag(menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(menu, 0, 0);
    lv_obj_set_style_pad_row(menu, 0, 0);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(menu, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(menu, &lv_font_montserrat_ext_14, 0);

    lv_obj_t* label = lv_list_add_text(menu, title);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, 224);

    static const char* icons[] = { LV_SYMBOL_PLAY, LV_SYMBOL_PLUS, LV_SYMBOL_TRASH, LV_SYMBOL_REFRESH };
    static const char* names[] = { "Play", "Add to queue", "Clear and play", "Update folder" };
    int count = with_update ? 4 : 3;
    for (int i = 0; i < count; i++) {
        lv_obj_t* b = lv_list_add_button(menu, icons[i], names[i]);
        lv_obj_set_height(b, MENU_ROW_H);
        lv_obj_set_style_pad_top(b, 0, 0);
        lv_obj_set_style_pad_bottom(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x111111), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x333333), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(b, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(0x222222), 0);
        lv_obj_add_event_cb(b, menu_action_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

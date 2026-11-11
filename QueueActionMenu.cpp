#include "QueueActionMenu.h"
#include "ListStyle.h"
#include "VolumioHandler.h"
#include "CustomFonts.h"

#define MENU_ROW_H 34
#define ACTION_UPDATE (QUEUE_CLEAR_AND_PLAY + 1)
// A saved playlist goes over the WebSocket by name instead (see showPlaylistActionMenu()).
#define ACTION_PLAYLIST_ADD (ACTION_UPDATE + 1)
#define ACTION_PLAYLIST_PLAY (ACTION_UPDATE + 2)

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
    } else if (action == ACTION_PLAYLIST_ADD) {
        enqueuePlaylist(menu_item.title);
    } else if (action == ACTION_PLAYLIST_PLAY) {
        playPlaylist(menu_item.title);
    } else {
        startQueueAction((QueueAction)action, menu_item.service, menu_item.uri, menu_item.title);
    }
    // The overlay is the panel's parent.
    lv_obj_t* overlay = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target_obj(e)));
    lv_obj_delete_async(overlay);
}

// Builds the menu with one row per entry of actions (QueueAction values or the ACTION_* above).
static void openMenu(const char* title, const char* uri, const char* service, const int* actions, int count) {
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
    // Black like every other list; a thin outline marks the panel's edge against the dimmed screen.
    lv_obj_set_style_bg_color(menu, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(menu, 1, 0);
    lv_obj_set_style_border_color(menu, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_color(menu, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(menu, &lv_font_montserrat_ext_14, 0);

    // The title is the only row with a fill, so it reads as the menu's header.
    lv_obj_t* label = lv_list_add_text(menu, title);
    styleListText(label);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_pad_ver(label, 8, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, LV_PCT(100));

    for (int i = 0; i < count; i++) {
        int a = actions[i];
        const char* icon;
        const char* name;
        switch (a) {
            case QUEUE_PLAY:           icon = LV_SYMBOL_PLAY;    name = "Play"; break;
            case QUEUE_ADD:
            case ACTION_PLAYLIST_ADD:  icon = LV_SYMBOL_PLUS;    name = "Add to queue"; break;
            case ACTION_UPDATE:        icon = LV_SYMBOL_REFRESH; name = "Update folder"; break;
            default:                   icon = LV_SYMBOL_TRASH;   name = "Clear and play"; break;
        }
        lv_obj_t* b = lv_list_add_button(menu, icon, name);
        styleListRow(b, MENU_ROW_H);
        lv_obj_add_event_cb(b, menu_action_cb, LV_EVENT_CLICKED, (void*)(intptr_t)a);
    }
}

void showQueueActionMenu(const char* title, const char* uri, const char* service, bool with_update) {
    static const int actions[] = { QUEUE_PLAY, QUEUE_ADD, QUEUE_CLEAR_AND_PLAY, ACTION_UPDATE };
    openMenu(title, uri, service, actions, with_update ? 4 : 3);
}

void showPlaylistActionMenu(const char* name) {
    // No "Play" (add, then play from there): Volumio's playlist events only append or replace.
    static const int actions[] = { ACTION_PLAYLIST_ADD, ACTION_PLAYLIST_PLAY };
    openMenu(name, "", "", actions, 2);
}

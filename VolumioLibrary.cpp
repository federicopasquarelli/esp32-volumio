#include "VolumioLibrary.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "VolumioHandler.h"
#include "CustomFonts.h"

#define LIB_ROOT_URI   "music-library"
#define LIB_PAGE_SIZE  4
#define LIB_ROW_H      34
#define LIB_MAX_DEPTH  12
#define LIB_URI_LEN    160
#define LIB_TITLE_LEN  64

struct LibItem {
    char title[LIB_TITLE_LEN];
    char uri[LIB_URI_LEN];
    char service[16];
    bool isFolder;
};

static lv_obj_t* list_library = NULL;
static lv_obj_t* tab_library = NULL;
static lv_obj_t* btn_up = NULL;
static lv_obj_t* btn_prev_page = NULL;
static lv_obj_t* btn_next_page = NULL;
static lv_obj_t* label_page = NULL;
static int page = 0;

// Only the page currently on screen is ever held in RAM -- Volumio's browse API takes
// offset/limit, so paging is a fresh HTTP request per page rather than one big fetch cached
// client-side (that used to reserve a 200-item array, ~49KB, permanently at boot).
static LibItem items[LIB_PAGE_SIZE];
static int item_count = 0;
// True if the last fetch returned a full page, i.e. there's probably a next page. Can be a false
// positive when the folder has exactly a multiple of LIB_PAGE_SIZE items (Next then loads an
// empty page) -- harmless, Prev still gets you back.
static bool has_more = false;

// URIs of the folders we entered, the last one is the folder being shown.
static char path_stack[LIB_MAX_DEPTH][LIB_URI_LEN];
static int path_depth = 0;
// Page shown in each folder we left, so going back returns to the same page.
static int page_stack[LIB_MAX_DEPTH];
static int pending_page = 0;

// Written by the fetch task, read by loopLibrary(). Only one fetch runs at a time.
static volatile bool fetching = false;
static volatile bool fetch_done = false;
static char fetch_uri[LIB_URI_LEN];
static int fetch_page = 0;
static char fetch_error[32];

// Kept alive between requests so we skip the TCP handshake each time.
static WiFiClient wifi_client;
static HTTPClient http;

static String urlEncode(const char* msg) {
    static const char hex[] = "0123456789ABCDEF";
    String out;
    out.reserve(strlen(msg) * 3);
    for (const char* p = msg; *p; p++) {
        unsigned char c = *p;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

static bool isFolderType(const char* type) {
    return strcmp(type, "song") != 0 && strcmp(type, "webradio") != 0 && strcmp(type, "track") != 0;
}

static void fetchTask(void*) {
    fetch_error[0] = '\0';

    String url = String("http://") + SECRET_VOLUMIO_HOST + ":" + SECRET_VOLUMIO_PORT +
                 "/api/v1/browse?uri=" + urlEncode(fetch_uri) +
                 "&offset=" + String(fetch_page * LIB_PAGE_SIZE) + "&limit=" + String(LIB_PAGE_SIZE);
    http.setReuse(true);
    http.setTimeout(8000);
    http.begin(wifi_client, url);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        strlcpy(fetch_error, "Request failed", sizeof(fetch_error));
    } else {
        JsonDocument filter;
        filter["navigation"]["lists"][0]["items"][0]["title"] = true;
        filter["navigation"]["lists"][0]["items"][0]["type"] = true;
        filter["navigation"]["lists"][0]["items"][0]["uri"] = true;
        filter["navigation"]["lists"][0]["items"][0]["service"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (err) {
            strlcpy(fetch_error, "Invalid response", sizeof(fetch_error));
        } else {
            item_count = 0;
            for (JsonObject list : doc["navigation"]["lists"].as<JsonArray>()) {
                for (JsonObject it : list["items"].as<JsonArray>()) {
                    if (item_count >= LIB_PAGE_SIZE) break;
                    LibItem& dst = items[item_count++];
                    strlcpy(dst.title, it["title"] | "", sizeof(dst.title));
                    strlcpy(dst.uri, it["uri"] | "", sizeof(dst.uri));
                    strlcpy(dst.service, it["service"] | "mpd", sizeof(dst.service));
                    dst.isFolder = isFolderType(it["type"] | "");
                }
            }
            has_more = (item_count == LIB_PAGE_SIZE);
        }
    }

    // With reuse enabled the connection stays open after end().
    http.end();
    fetch_done = true;
    vTaskDelete(NULL);
}

static void startFetch(const char* uri, int pageToFetch) {
    if (fetching) return;
    fetching = true;
    strlcpy(fetch_uri, uri, sizeof(fetch_uri));
    fetch_page = pageToFetch;
    xTaskCreatePinnedToCore(fetchTask, "LibFetch", 8192, NULL, 1, NULL, 1);
}

static void setEnabled(lv_obj_t* obj, bool enabled) {
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

// listing is false while loading or showing an error, when paging makes no sense. There's no
// running page total anymore (each page is its own fetch, not a slice of something already in
// full), so this just shows the current page number.
static void updateNav(bool listing) {
    char txt[8];
    if (listing) snprintf(txt, sizeof(txt), "%d", page + 1);
    else strlcpy(txt, "-", sizeof(txt));
    lv_label_set_text(label_page, txt);
    setEnabled(btn_up, path_depth > 1);
    setEnabled(btn_prev_page, listing && page > 0);
    setEnabled(btn_next_page, listing && has_more);
}

static void setStatus(const char* text) {
    lv_obj_clean(list_library);
    lv_list_add_text(list_library, text);
    updateNav(false);
}

static void enterFolder(const char* uri) {
    if (fetching || path_depth >= LIB_MAX_DEPTH) return;
    if (path_depth > 0) page_stack[path_depth - 1] = page;
    pending_page = 0;
    strlcpy(path_stack[path_depth], uri, LIB_URI_LEN);
    path_depth++;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(uri, pending_page);
}

static void goBack() {
    if (fetching || path_depth <= 1) return;
    path_depth--;
    pending_page = page_stack[path_depth - 1];
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

// Retries at whatever page the failed fetch was targeting -- pending_page still holds it,
// untouched since the fetch that set it never made it to the loopLibrary() success path.
static void retry_cb(lv_event_t*) {
    if (fetching || path_depth == 0) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

static void styleButton(lv_obj_t* btn) {
    lv_obj_set_height(btn, LIB_ROW_H);
    lv_obj_set_style_pad_top(btn, 0, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0);
}

// LVGL still sends CLICKED on release after a long press, so remember the press was consumed.
static bool long_press_handled = false;
static LibItem menu_item;
static volatile bool action_menu_running = false;
static int action_requested = 0;

static void overlay_cb(lv_event_t* e) {
    // Only taps on the dimmed background close the menu, not taps bubbling up from the panel.
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) lv_obj_delete_async(lv_event_get_target_obj(e));
}

enum FolderAction { ACTION_PLAY, ACTION_ADD_TO_QUEUE, ACTION_CLEAR_AND_PLAY, ACTION_UPDATE };

static String baseUrl() {
    return String("http://") + SECRET_VOLUMIO_HOST + ":" + SECRET_VOLUMIO_PORT + "/api/v1/";
}

static int postJson(const char* path, const JsonDocument& body) {
    WiFiClient client;
    HTTPClient req;
    req.begin(client, baseUrl() + path);
    req.setTimeout(8000);
    req.addHeader("Content-Type", "application/json");
    String payload;
    serializeJson(body, payload);
    int code = req.POST(payload);
    req.end();
    return code;
}

// Number of tracks in the queue, or -1 on failure.
static int queueLength() {
    WiFiClient client;
    HTTPClient req;
    req.begin(client, baseUrl() + "getQueue");
    req.setTimeout(8000);
    int len = -1;
    if (req.GET() == HTTP_CODE_OK) {
        JsonDocument filter;
        filter["queue"][0]["uri"] = true;
        JsonDocument doc;
        if (!deserializeJson(doc, req.getStream(), DeserializationOption::Filter(filter))) len = doc["queue"].size();
    }
    req.end();
    return len;
}

static void folderActionTask(void* pv) {
    LibItem* item = (LibItem*)pv;

    JsonDocument body;
    body["service"] = item->service;
    body["uri"] = item->uri;
    body["title"] = item->title;

    switch (action_requested) {
        case ACTION_ADD_TO_QUEUE:
            postJson("addToQueue", body);
            break;
        case ACTION_CLEAR_AND_PLAY: {
            // Sent both flat and wrapped in "item" so either body shape is understood.
            JsonDocument wrapped = body;
            wrapped["item"] = body;
            postJson("replaceAndPlay", wrapped);
            break;
        }
        case ACTION_PLAY: {
            // No REST "add and play": queue the folder, then play its first track.
            int first = queueLength();
            if (first >= 0 && postJson("addToQueue", body) > 0) {
                WiFiClient client;
                HTTPClient req;
                req.begin(client, baseUrl() + "commands/?cmd=play&N=" + String(first));
                req.setTimeout(8000);
                req.GET();
                req.end();
            }
            break;
        }
    }

    delete item;
    action_menu_running = false;
    vTaskDelete(NULL);
}

static void startAction(int action, const LibItem& item) {
    if (action_menu_running) return;
    action_requested = action;
    action_menu_running = true;
    xTaskCreatePinnedToCore(folderActionTask, "LibAction", 8192, new LibItem(item), 1, NULL, 1);
}

static void menu_action_cb(lv_event_t* e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    if (action == ACTION_UPDATE) {
        // Volumio has no REST route for a database update, so this one uses the WebSocket.
        updateFolder(menu_item.uri);
    } else {
        startAction(action, menu_item);
    }
    // The overlay is the panel's parent.
    lv_obj_t* overlay = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target_obj(e)));
    lv_obj_delete_async(overlay);
}

static void showContextMenu(const LibItem& item) {
    menu_item = item;

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

    lv_obj_t* title = lv_list_add_text(menu, item.title);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, 224);

    static const char* icons[] = { LV_SYMBOL_PLAY, LV_SYMBOL_PLUS, LV_SYMBOL_TRASH, LV_SYMBOL_REFRESH };
    static const char* names[] = { "Play", "Add to queue", "Clear and play", "Update folder" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* b = lv_list_add_button(menu, icons[i], names[i]);
        styleButton(b);
        lv_obj_add_event_cb(b, menu_action_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
}

static void item_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= item_count) return;

    if (code == LV_EVENT_PRESSED) {
        long_press_handled = false;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        long_press_handled = true;
        showContextMenu(items[idx]);
    } else if (code == LV_EVENT_CLICKED) {
        if (long_press_handled) long_press_handled = false;
        else enterFolder(items[idx].uri);
    }
}

static void track_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < item_count) startAction(ACTION_PLAY, items[idx]);
}

// items[] only ever holds the page just fetched (server-side offset/limit already did the
// slicing), so this just renders all of it.
static void showList() {
    lv_obj_clean(list_library);

    for (int i = 0; i < item_count; i++) {
        bool folder = items[i].isFolder;
        lv_obj_t* b = lv_list_add_button(list_library, folder ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_AUDIO, items[i].title);
        styleButton(b);
        if (folder) {
            lv_obj_add_event_cb(b, item_cb, LV_EVENT_ALL, (void*)(intptr_t)i);
        } else {
            lv_obj_add_event_cb(b, track_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
    }

    if (item_count == 0) lv_list_add_text(list_library, "Empty folder");
    updateNav(true);
}

static void up_cb(lv_event_t*) { goBack(); }

static void prev_page_cb(lv_event_t*) {
    if (fetching || page <= 0) return;
    pending_page = page - 1;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

static void next_page_cb(lv_event_t*) {
    if (fetching || !has_more) return;
    pending_page = page + 1;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

static lv_obj_t* addNavButton(lv_obj_t* bar, const char* symbol, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(bar);
    lv_obj_set_size(b, 70, LIB_ROW_H - 4);
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

void setupLibrary(lv_obj_t* parent_tab) {
    tab_library = parent_tab;

    lv_obj_remove_flag(tab_library, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab_library, 0, 0);

    list_library = lv_list_create(tab_library);
    lv_obj_set_size(list_library, LV_PCT(100), LIB_ROW_H * LIB_PAGE_SIZE);
    lv_obj_align(list_library, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(list_library, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(list_library, 0, 0);
    lv_obj_set_style_pad_row(list_library, 0, 0);
    lv_obj_set_style_bg_color(list_library, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(list_library, 0, 0);
    lv_obj_set_style_text_color(list_library, lv_color_hex(0xAAAAAA), 0);
    // Folder/track names come straight from file tags, which often have accented letters or
    // smart quotes the stock font doesn't have a glyph for -- see CustomFonts.h.
    lv_obj_set_style_text_font(list_library, &lv_font_montserrat_ext_14, 0);

    lv_obj_t* bar = lv_obj_create(tab_library);
    lv_obj_set_size(bar, LV_PCT(100), LIB_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_up = addNavButton(bar, LV_SYMBOL_UP, up_cb);
    btn_prev_page = addNavButton(bar, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = addNavButton(bar, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

void openLibraryRoot() {
    path_depth = 0;
    enterFolder(LIB_ROOT_URI);
}

static void showError(const char* error) {
    lv_obj_clean(list_library);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    lv_list_add_text(list_library, msg);

    lv_obj_t* retry = lv_list_add_button(list_library, LV_SYMBOL_REFRESH, "Retry");
    styleButton(retry);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, NULL);
    updateNav(false);
}

void loopLibrary() {
    if (!fetch_done) return;
    fetch_done = false;

    if (fetch_error[0]) {
        showError(fetch_error);
    } else {
        page = pending_page;
        showList();
    }
    fetching = false;
}

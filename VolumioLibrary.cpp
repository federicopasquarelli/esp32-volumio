#include "VolumioLibrary.h"
#include "ListStyle.h"
#include "DisplayConfig.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "VolumioApi.h"
#include "QueueActionMenu.h"

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

// Only the page on screen is held in RAM: browse takes offset/limit, so each page turn is a request.
static LibItem items[LIB_PAGE_SIZE];
static int item_count = 0;
// True if there's a next page. Volumio returns the folder's total as "count" when offset/limit
// are passed; without it (older versions) a full page is taken to mean there's more.
static bool has_more = false;
// The folder's total item count, or -1 if the response had no "count".
static int total_count = -1;

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

static bool isFolderType(const char* type) {
    return strcmp(type, "song") != 0 && strcmp(type, "webradio") != 0 && strcmp(type, "track") != 0;
}

static void fetchTask(void*) {
    fetch_error[0] = '\0';

    String url = volumioUrl("browse?uri=") + urlEncode(fetch_uri) +
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
        filter["navigation"]["lists"][0]["count"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (err) {
            strlcpy(fetch_error, "Invalid response", sizeof(fetch_error));
        } else {
            item_count = 0;
            total_count = -1;
            for (JsonObject list : doc["navigation"]["lists"].as<JsonArray>()) {
                if (list["count"].is<int>()) total_count = list["count"].as<int>();
                for (JsonObject it : list["items"].as<JsonArray>()) {
                    if (item_count >= LIB_PAGE_SIZE) break;
                    LibItem& dst = items[item_count++];
                    strlcpy(dst.title, it["title"] | "", sizeof(dst.title));
                    strlcpy(dst.uri, it["uri"] | "", sizeof(dst.uri));
                    strlcpy(dst.service, it["service"] | "mpd", sizeof(dst.service));
                    dst.isFolder = isFolderType(it["type"] | "");
                }
            }
            has_more = total_count >= 0
                ? (fetch_page + 1) * LIB_PAGE_SIZE < total_count
                : (item_count == LIB_PAGE_SIZE);  // no "count" in the response -- fall back to guessing
        }
    }

    // With reuse enabled the connection stays open after end().
    http.end();
    fetch_done = true;
    vTaskDelete(NULL);
}

static void showError(const char* error);  // defined further down, used by startFetch() below

static void startFetch(const char* uri, int pageToFetch) {
    if (fetching) return;
    fetching = true;
    strlcpy(fetch_uri, uri, sizeof(fetch_uri));
    fetch_page = pageToFetch;
    // On a fragmented heap the task's stack may not fit; without this, fetching stays true forever.
    if (xTaskCreatePinnedToCore(fetchTask, "LibFetch", 8192, NULL, 1, NULL, 1) != pdPASS) {
        fetching = false;
        showError("Out of memory, try again");
    }
}

// listing is false while loading or showing an error, when paging makes no sense.
static void updateNav(bool listing) {
    char txt[24];
    formatPageLabel(txt, sizeof(txt), listing, page, LIB_PAGE_SIZE, total_count);
    lv_label_set_text(label_page, txt);
    setPagerEnabled(btn_up, path_depth > 1);
    setPagerEnabled(btn_prev_page, listing && page > 0);
    setPagerEnabled(btn_next_page, listing && has_more);
}

static void setStatus(const char* text) {
    lv_obj_clean(list_library);
    styleListText(lv_list_add_text(list_library, text));
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

// Retries the page the failed fetch was for (still in pending_page).
static void retry_cb(lv_event_t*) {
    if (fetching || path_depth == 0) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

static void styleButton(lv_obj_t* btn) { styleListRow(btn, LIB_ROW_H); }

// LVGL still sends CLICKED on release after a long press, so remember the press was consumed.
static bool long_press_handled = false;

static void item_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= item_count) return;

    if (code == LV_EVENT_PRESSED) {
        long_press_handled = false;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        long_press_handled = true;
        showQueueActionMenu(items[idx].title, items[idx].uri, items[idx].service, true);
    } else if (code == LV_EVENT_CLICKED) {
        if (long_press_handled) long_press_handled = false;
        else enterFolder(items[idx].uri);
    }
}

static void track_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < item_count) startQueueAction(QUEUE_PLAY, items[idx].service, items[idx].uri, items[idx].title);
}

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

    if (item_count == 0) styleListText(lv_list_add_text(list_library, "Empty folder"));
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

void setupLibrary(lv_obj_t* parent_tab) {
    tab_library = parent_tab;
}

void buildLibrary(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    list_library = lv_list_create(parent);
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

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), LIB_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_up = createPagerButton(bar, 70, LIB_ROW_H - 4, LV_SYMBOL_UP, up_cb);
    btn_prev_page = createPagerButton(bar, 70, LIB_ROW_H - 4, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = createPagerButton(bar, 70, LIB_ROW_H - 4, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

void refreshLibrary() {
    if (path_depth == 0) {
        enterFolder(LIB_ROOT_URI);
    } else {
        pending_page = page;
        setStatus(LV_SYMBOL_REFRESH " Loading...");
        startFetch(path_stack[path_depth - 1], pending_page);
    }
}

void hideLibrary() {
    lv_obj_clean(tab_library);
    list_library = NULL;
    btn_up = NULL;
    btn_prev_page = NULL;
    btn_next_page = NULL;
    label_page = NULL;
}

static void showError(const char* error) {
    lv_obj_clean(list_library);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    styleListText(lv_list_add_text(list_library, msg));

    lv_obj_t* retry = lv_list_add_button(list_library, LV_SYMBOL_REFRESH, "Retry");
    styleButton(retry);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, NULL);
    updateNav(false);
}

void loopLibrary() {
    if (!fetch_done) return;
    fetch_done = false;

    if (!list_library) {
        // Hidden while the fetch was in flight: drop the result, refreshLibrary() fetches again.
        fetching = false;
        return;
    }

    if (fetch_error[0]) {
        showError(fetch_error);
    } else {
        page = pending_page;
        showList();
    }
    fetching = false;
}

#include "VolumioQueue.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include "VolumioHandler.h"
#include "CustomFonts.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define QUEUE_MAX_ITEMS 200
#define QUEUE_PAGE_SIZE 4
#define QUEUE_ROW_H     34
#define QUEUE_TITLE_LEN 96

static lv_obj_t* tab_queue = NULL;
static lv_obj_t* list_queue = NULL;
static lv_obj_t* btn_clear = NULL;
static lv_obj_t* btn_prev_page = NULL;
static lv_obj_t* btn_next_page = NULL;
static lv_obj_t* label_page = NULL;

static char (*titles)[QUEUE_TITLE_LEN] = NULL;
static int item_count = 0;
static int page = 0;

enum QueueOp { OP_REFRESH, OP_PLAY, OP_CLEAR };

// Written by the network task, read by loopQueue(). Only one operation runs at a time.
static volatile bool busy = false;
static volatile bool op_done = false;
static char op_error[32];

static String apiUrl(const char* path) {
    return String("http://") + SECRET_VOLUMIO_HOST + ":" + SECRET_VOLUMIO_PORT + "/api/v1/" + path;
}

static bool httpGet(const char* path) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, apiUrl(path));
    http.setTimeout(8000);
    int code = http.GET();
    http.end();
    return code == HTTP_CODE_OK;
}

static bool fetchQueue() {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, apiUrl("getQueue"));
    http.setTimeout(8000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        strlcpy(op_error, "Request failed", sizeof(op_error));
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["queue"][0]["name"] = true;
    filter["queue"][0]["title"] = true;
    filter["queue"][0]["artist"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        strlcpy(op_error, "Invalid response", sizeof(op_error));
        return false;
    }

    item_count = 0;
    for (JsonObject it : doc["queue"].as<JsonArray>()) {
        if (item_count >= QUEUE_MAX_ITEMS) break;
        const char* name = it["name"] | (it["title"] | "");
        const char* artist = it["artist"] | "";
        if (artist[0]) snprintf(titles[item_count], QUEUE_TITLE_LEN, "%s - %s", artist, name);
        else strlcpy(titles[item_count], name, QUEUE_TITLE_LEN);
        item_count++;
    }
    return true;
}

static void queueTask(void* pv) {
    int arg = (int)(intptr_t)pv;
    QueueOp op = (QueueOp)(arg & 0xF);
    int index = arg >> 4;
    op_error[0] = '\0';

    switch (op) {
        case OP_REFRESH:
            fetchQueue();
            break;
        case OP_PLAY: {
            char path[48];
            snprintf(path, sizeof(path), "commands/?cmd=play&N=%d", index);
            if (!httpGet(path)) strlcpy(op_error, "Play failed", sizeof(op_error));
            break;
        }
        case OP_CLEAR:
            if (httpGet("commands/?cmd=clearQueue")) fetchQueue();
            else strlcpy(op_error, "Clear failed", sizeof(op_error));
            break;
    }

    op_done = true;
    vTaskDelete(NULL);
}

static void startOp(QueueOp op, int index = 0) {
    if (busy) return;
    busy = true;
    xTaskCreatePinnedToCore(queueTask, "QueueOp", 8192, (void*)(intptr_t)((index << 4) | op), 1, NULL, 1);
}

static void setEnabled(lv_obj_t* obj, bool enabled) {
    if (enabled) lv_obj_remove_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static int pageCount() {
    int pages = (item_count + QUEUE_PAGE_SIZE - 1) / QUEUE_PAGE_SIZE;
    return pages > 0 ? pages : 1;
}

// listing is false while loading or showing an error, when paging makes no sense.
static void updateNav(bool listing) {
    int pages = pageCount();
    char txt[16];
    if (listing) snprintf(txt, sizeof(txt), "%d/%d", page + 1, pages);
    else strlcpy(txt, "-", sizeof(txt));
    lv_label_set_text(label_page, txt);
    setEnabled(btn_clear, listing && item_count > 0);
    setEnabled(btn_prev_page, listing && page > 0);
    setEnabled(btn_next_page, listing && page < pages - 1);
}

static void setStatus(const char* text) {
    lv_obj_clean(list_queue);
    lv_list_add_text(list_queue, text);
    updateNav(false);
}

static void styleButton(lv_obj_t* btn) {
    lv_obj_set_height(btn, QUEUE_ROW_H);
    lv_obj_set_style_pad_top(btn, 0, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0);
}

static void item_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (!busy && idx >= 0 && idx < item_count) startOp(OP_PLAY, idx);
}

static void showList();

static void remove_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (busy || idx < 0 || idx >= item_count) return;

    // Volumio has no REST route for this, so it goes over the WebSocket.
    removeFromQueue(idx);

    // Update the list locally so the following rows shift up without a reload.
    memmove(titles[idx], titles[idx + 1], (size_t)(item_count - idx - 1) * QUEUE_TITLE_LEN);
    item_count--;
    if (page >= pageCount()) page = pageCount() - 1;
    showList();
}

// Only the current page is turned into widgets, so a long queue costs no more than a short one.
static void showList() {
    lv_obj_clean(list_queue);

    int first = page * QUEUE_PAGE_SIZE;
    int last = min(first + QUEUE_PAGE_SIZE, item_count);
    for (int i = first; i < last; i++) {
        lv_obj_t* b = lv_list_add_button(list_queue, LV_SYMBOL_AUDIO, titles[i]);
        styleButton(b);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* rm = lv_button_create(b);
        lv_obj_set_size(rm, 30, QUEUE_ROW_H - 8);
        lv_obj_set_style_bg_color(rm, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_style_bg_color(rm, lv_color_hex(0x19A34A), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(rm, lv_color_hex(0xFFFFFF), 0);
        lv_obj_add_event_cb(rm, remove_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_t* l = lv_label_create(rm);
        lv_label_set_text(l, LV_SYMBOL_CLOSE);
        lv_obj_center(l);
    }

    if (item_count == 0) lv_list_add_text(list_queue, "Queue is empty");
    updateNav(true);
}

static void showError(const char* error) {
    lv_obj_clean(list_queue);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    lv_list_add_text(list_queue, msg);
    updateNav(false);
}

static void clear_cb(lv_event_t*) {
    if (busy) return;
    setStatus(LV_SYMBOL_REFRESH " Clearing...");
    startOp(OP_CLEAR);
}
static void prev_page_cb(lv_event_t*) { if (!busy && page > 0) { page--; showList(); } }
static void next_page_cb(lv_event_t*) { if (!busy && page < pageCount() - 1) { page++; showList(); } }

static lv_obj_t* addNavButton(lv_obj_t* bar, const char* symbol, lv_event_cb_t cb) {
    lv_obj_t* b = lv_button_create(bar);
    lv_obj_set_size(b, 70, QUEUE_ROW_H - 4);
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

void setupQueue(lv_obj_t* parent_tab) {
    tab_queue = parent_tab;
    titles = new char[QUEUE_MAX_ITEMS][QUEUE_TITLE_LEN];

    lv_obj_remove_flag(tab_queue, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(tab_queue, 0, 0);

    list_queue = lv_list_create(tab_queue);
    lv_obj_set_size(list_queue, LV_PCT(100), QUEUE_ROW_H * QUEUE_PAGE_SIZE);
    lv_obj_align(list_queue, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(list_queue, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(list_queue, 0, 0);
    lv_obj_set_style_pad_row(list_queue, 0, 0);
    lv_obj_set_style_bg_color(list_queue, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(list_queue, 0, 0);
    lv_obj_set_style_text_color(list_queue, lv_color_hex(0xAAAAAA), 0);
    // Track names come straight from file tags, which often have accented letters or smart
    // quotes the stock font doesn't have a glyph for -- see CustomFonts.h.
    lv_obj_set_style_text_font(list_queue, &lv_font_montserrat_ext_14, 0);

    lv_obj_t* bar = lv_obj_create(tab_queue);
    lv_obj_set_size(bar, LV_PCT(100), QUEUE_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_clear = addNavButton(bar, LV_SYMBOL_TRASH, clear_cb);
    btn_prev_page = addNavButton(bar, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = addNavButton(bar, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

void refreshQueue() {
    if (busy) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startOp(OP_REFRESH);
}

void loopQueue() {
    if (!op_done) return;
    op_done = false;

    if (op_error[0]) {
        showError(op_error);
    } else {
        // A play tap leaves the list as it is, everything else has new content.
        if (page >= pageCount()) page = pageCount() - 1;
        showList();
    }
    busy = false;
}

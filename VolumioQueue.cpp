#include "VolumioQueue.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include "VolumioHandler.h"
#include "CustomFonts.h"
#include "PaginationNav.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define QUEUE_PAGE_SIZE 4
#define QUEUE_ROW_H     34
#define QUEUE_TITLE_LEN 96

static lv_obj_t* tab_queue = NULL;
static lv_obj_t* list_queue = NULL;
static lv_obj_t* btn_clear = NULL;
static lv_obj_t* btn_prev_page = NULL;
static lv_obj_t* btn_next_page = NULL;
static lv_obj_t* label_page = NULL;

// Only the current page is kept: getQueue has no server-side paging and a long queue is far
// bigger than the heap, so it's streamed and every item outside the page is skipped.
static char titles[QUEUE_PAGE_SIZE][QUEUE_TITLE_LEN];
static int page_items = 0;
static int total_count = 0;
static int page = 0;
static int fetch_page = 0;

enum QueueOp { OP_REFRESH, OP_PLAY, OP_CLEAR, OP_REFRESH_AFTER_REMOVE };

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

static int nextNonSpace(Stream& s) {
    char c;
    while (s.readBytes(&c, 1) == 1) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return c;
    }
    return -1;
}

static int peekNonSpace(Stream& s) {
    unsigned long start = millis();
    while (millis() - start < 2000) {
        if (!s.available()) { delay(1); continue; }
        int c = s.peek();
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return c;
        s.read();
    }
    return -1;
}

static bool streamQueuePage(int wanted_page, int& total) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, apiUrl("getQueue"));
    http.setTimeout(8000);

    if (http.GET() != HTTP_CODE_OK) {
        strlcpy(op_error, "Request failed", sizeof(op_error));
        http.end();
        return false;
    }

    Stream& stream = http.getStream();
    stream.setTimeout(8000);
    if (!stream.find("\"queue\"") || !stream.find("[")) {
        strlcpy(op_error, "Invalid response", sizeof(op_error));
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["name"] = true;
    filter["title"] = true;
    filter["artist"] = true;

    int first = wanted_page * QUEUE_PAGE_SIZE;
    int idx = 0;
    page_items = 0;
    bool ok = true;

    stream.setTimeout(2000);
    while (true) {
        if (peekNonSpace(stream) == ']') break;

        JsonDocument doc;
        if (deserializeJson(doc, stream, DeserializationOption::Filter(filter))) { ok = false; break; }

        if (idx >= first && idx < first + QUEUE_PAGE_SIZE) {
            const char* name = doc["name"] | (doc["title"] | "");
            const char* artist = doc["artist"] | "";
            char* dst = titles[page_items++];
            if (artist[0]) snprintf(dst, QUEUE_TITLE_LEN, "%s - %s", artist, name);
            else strlcpy(dst, name, QUEUE_TITLE_LEN);
        }
        idx++;

        int sep = nextNonSpace(stream);
        if (sep == ']') break;
        if (sep != ',') { ok = false; break; }
    }
    http.end();

    if (!ok) {
        strlcpy(op_error, "Invalid response", sizeof(op_error));
        return false;
    }
    total = idx;
    return true;
}

// Fetches fetch_page; if the queue shrank below it, falls back to the new last page.
static bool fetchQueue() {
    int total = 0;
    if (!streamQueuePage(fetch_page, total)) return false;
    int last_page = total > 0 ? (total - 1) / QUEUE_PAGE_SIZE : 0;
    if (fetch_page > last_page) {
        fetch_page = last_page;
        if (!streamQueuePage(fetch_page, total)) return false;
    }
    total_count = total;
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
        case OP_REFRESH_AFTER_REMOVE:
            // The remove went out over the WebSocket; give Volumio a moment to apply it.
            vTaskDelay(pdMS_TO_TICKS(400));
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

static void showError(const char* error);

static void startOp(QueueOp op, int index = 0) {
    if (busy) return;
    busy = true;
    op_done = false;
    if (xTaskCreatePinnedToCore(queueTask, "QueueOp", 8192, (void*)(intptr_t)((index << 4) | op), 1, NULL, 1) != pdPASS) {
        busy = false;
        if (list_queue) showError("Out of memory");
    }
}

static int pageCount() {
    int pages = (total_count + QUEUE_PAGE_SIZE - 1) / QUEUE_PAGE_SIZE;
    return pages > 0 ? pages : 1;
}

// listing is false while loading or showing an error, when paging makes no sense.
static void updateNav(bool listing) {
    int pages = pageCount();
    char txt[24];
    formatPageLabel(txt, sizeof(txt), listing, page, QUEUE_PAGE_SIZE, total_count);
    lv_label_set_text(label_page, txt);
    setPagerEnabled(btn_clear, listing && total_count > 0);
    setPagerEnabled(btn_prev_page, listing && page > 0);
    setPagerEnabled(btn_next_page, listing && page < pages - 1);
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
    if (!busy && idx >= 0 && idx < total_count) startOp(OP_PLAY, idx);
}

static void showList();

static void remove_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    int slot = idx - page * QUEUE_PAGE_SIZE;
    if (busy || slot < 0 || slot >= page_items) return;

    // Volumio has no REST route for this, so it goes over the WebSocket.
    removeFromQueue(idx);

    // Drop the row locally right away, then reload the page to pull the next item up into it.
    memmove(titles[slot], titles[slot + 1], (size_t)(page_items - slot - 1) * QUEUE_TITLE_LEN);
    page_items--;
    total_count--;
    showList();
    fetch_page = page;
    startOp(OP_REFRESH_AFTER_REMOVE);
}

// Only the current page is turned into widgets, so a long queue costs no more than a short one.
static void showList() {
    lv_obj_clean(list_queue);

    int first = page * QUEUE_PAGE_SIZE;
    for (int i = first; i < first + page_items; i++) {
        lv_obj_t* b = lv_list_add_button(list_queue, LV_SYMBOL_AUDIO, titles[i - first]);
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

    if (total_count == 0) lv_list_add_text(list_queue, "Queue is empty");
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
    fetch_page = 0;
    startOp(OP_CLEAR);
}
static void loadPage(int target) {
    if (busy) return;
    fetch_page = target;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startOp(OP_REFRESH);
}

static void prev_page_cb(lv_event_t*) { if (page > 0) loadPage(page - 1); }
static void next_page_cb(lv_event_t*) { if (page < pageCount() - 1) loadPage(page + 1); }

void setupQueue(lv_obj_t* parent_tab) {
    tab_queue = parent_tab;
}

void buildQueue(lv_obj_t* parent) {
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    list_queue = lv_list_create(parent);
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

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), QUEUE_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_clear = createPagerButton(bar, 70, QUEUE_ROW_H - 4, LV_SYMBOL_TRASH, clear_cb);
    btn_prev_page = createPagerButton(bar, 70, QUEUE_ROW_H - 4, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = createPagerButton(bar, 70, QUEUE_ROW_H - 4, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

void refreshQueue() {
    loadPage(page);
}

void hideQueue() {
    lv_obj_clean(tab_queue);
    list_queue = NULL;
    btn_clear = NULL;
    btn_prev_page = NULL;
    btn_next_page = NULL;
    label_page = NULL;
}

void loopQueue() {
    if (!op_done) return;
    op_done = false;
    busy = false;

    if (!list_queue) {
        // Screen was hidden (its widgets torn down) while this op was in flight -- discard the
        // result instead of touching freed/NULL pointers. refreshQueue() starts a fresh one the
        // next time this screen is shown, so nothing is lost.
        return;
    }

    if (op_error[0]) {
        showError(op_error);
    } else {
        page = fetch_page;
        showList();
    }
}

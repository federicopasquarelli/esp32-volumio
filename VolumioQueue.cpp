#include "VolumioQueue.h"
#include "ListStyle.h"
#include "DisplayConfig.h"
#include "VolumioHandler.h"
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "VolumioApi.h"
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

enum QueueOp { OP_REFRESH, OP_PLAY, OP_CLEAR, OP_REFRESH_DELAYED };

// Written by the network task, read by loopQueue(). Only one operation runs at a time.
static volatile bool busy = false;
static volatile bool op_done = false;
static char op_error[32];

static bool httpGet(const char* path) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, volumioUrl(path));
    http.setTimeout(8000);
    int code = http.GET();
    http.end();
    return code == HTTP_CODE_OK;
}

static void collectTitle(JsonObject item, void*) {
    if (page_items >= QUEUE_PAGE_SIZE) return;
    const char* name = item["name"] | (item["title"] | "");
    const char* artist = item["artist"] | "";
    char* dst = titles[page_items++];
    if (artist[0]) snprintf(dst, QUEUE_TITLE_LEN, "%s - %s", artist, name);
    else strlcpy(dst, name, QUEUE_TITLE_LEN);
}

// Only the requested page's items are parsed; the rest of the queue is just counted.
static bool streamQueuePage(int wanted_page, int& total) {
    JsonDocument filter;
    filter["name"] = true;
    filter["title"] = true;
    filter["artist"] = true;

    page_items = 0;
    total = streamQueue(wanted_page * QUEUE_PAGE_SIZE, QUEUE_PAGE_SIZE, filter, collectTitle, NULL);
    if (total < 0) {
        strlcpy(op_error, "Request failed", sizeof(op_error));
        return false;
    }
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
        case OP_REFRESH_DELAYED:
            // A remove/move went out over the WebSocket; give Volumio a moment to apply it.
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
    styleListText(lv_list_add_text(list_queue, text));
    updateNav(false);
}

static void styleButton(lv_obj_t* btn) { styleListRow(btn, QUEUE_ROW_H); }

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
    startOp(OP_REFRESH_DELAYED);
}

// user_data is (index << 1) | down.
static void move_cb(lv_event_t* e) {
    int arg = (int)(intptr_t)lv_event_get_user_data(e);
    int from = arg >> 1;
    int to = (arg & 1) ? from + 1 : from - 1;
    int slot = from - page * QUEUE_PAGE_SIZE;
    if (busy || slot < 0 || slot >= page_items || to < 0 || to >= total_count) return;

    moveInQueue(from, to);

    // Swap the rows locally when both are on this page, then reload to pick up the real order
    // (and the item pulled in from the neighbouring page when the move crosses a page edge).
    int to_slot = to - page * QUEUE_PAGE_SIZE;
    if (to_slot >= 0 && to_slot < page_items) {
        char tmp[QUEUE_TITLE_LEN];
        memcpy(tmp, titles[slot], QUEUE_TITLE_LEN);
        memcpy(titles[slot], titles[to_slot], QUEUE_TITLE_LEN);
        memcpy(titles[to_slot], tmp, QUEUE_TITLE_LEN);
        showList();
    }
    fetch_page = page;
    startOp(OP_REFRESH_DELAYED);
}

// The ▲/▼/× buttons: 12 per page, so they're kept as light as possible in LVGL's pool, which ran out
// (UI frozen mid-redraw) when they were themed buttons with their own local styles. Each is one
// clickable label (no button + label pair) and all of them share three static styles. Labels get
// no theme styles, so there are no press transitions to pile up either (see CLAUDE.md, Memory).
#define ROW_BTN_W 30
#define ROW_BTN_H (QUEUE_ROW_H - 8)
static lv_style_t style_row_btn, style_row_btn_pressed, style_row_btn_disabled;

static void initRowButtonStyles() {
    static bool done = false;
    if (done) return;
    done = true;

    // Match the pager buttons' corner radius, which comes from the theme.
    lv_obj_t* probe = lv_button_create(lv_layer_sys());
    int32_t radius = lv_obj_get_style_radius(probe, LV_PART_MAIN);
    lv_obj_delete(probe);

    lv_style_init(&style_row_btn);
    lv_style_set_width(&style_row_btn, ROW_BTN_W);
    lv_style_set_height(&style_row_btn, ROW_BTN_H);
    lv_style_set_radius(&style_row_btn, radius);
    lv_style_set_bg_opa(&style_row_btn, LV_OPA_COVER);
    lv_style_set_bg_color(&style_row_btn, lv_color_hex(COLOR_ACCENT));
    lv_style_set_text_color(&style_row_btn, lv_color_hex(0xFFFFFF));
    lv_style_set_text_align(&style_row_btn, LV_TEXT_ALIGN_CENTER);
    // A label doesn't centre its text vertically: pad it down by half the spare height.
    lv_style_set_pad_top(&style_row_btn, (ROW_BTN_H - lv_font_get_line_height(&lv_font_montserrat_ext_14)) / 2);

    lv_style_init(&style_row_btn_pressed);
    lv_style_set_bg_color(&style_row_btn_pressed, lv_color_hex(0x19A34A));

    lv_style_init(&style_row_btn_disabled);
    lv_style_set_bg_opa(&style_row_btn_disabled, LV_OPA_TRANSP);
    lv_style_set_text_color(&style_row_btn_disabled, lv_color_hex(0x555555));
}

static lv_obj_t* rowButton(lv_obj_t* row, const char* symbol, lv_event_cb_t cb, int arg, bool enabled) {
    initRowButtonStyles();
    lv_obj_t* b = lv_label_create(row);
    lv_label_set_text_static(b, symbol);
    lv_obj_add_style(b, &style_row_btn, 0);
    lv_obj_add_style(b, &style_row_btn_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(b, &style_row_btn_disabled, LV_STATE_DISABLED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)(intptr_t)arg);
    if (!enabled) lv_obj_add_state(b, LV_STATE_DISABLED);
    return b;
}

// Only the current page is turned into widgets, so a long queue costs no more than a short one.
static void showList() {
    lv_obj_clean(list_queue);

    int first = page * QUEUE_PAGE_SIZE;
    for (int i = first; i < first + page_items; i++) {
        lv_obj_t* b = lv_list_add_button(list_queue, LV_SYMBOL_AUDIO, titles[i - first]);
        styleButton(b);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        rowButton(b, LV_SYMBOL_UP, move_cb, i << 1, i > 0);
        rowButton(b, LV_SYMBOL_DOWN, move_cb, (i << 1) | 1, i < total_count - 1);
        rowButton(b, LV_SYMBOL_CLOSE, remove_cb, i, true);
    }

    if (total_count == 0) styleListText(lv_list_add_text(list_queue, "Queue is empty"));
    updateNav(true);
}

static void showError(const char* error) {
    lv_obj_clean(list_queue);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    styleListText(lv_list_add_text(list_queue, msg));
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

    // Title row, the list and the page bar (see LIST_BODY_H).
    addListTitle(createListHeader(parent, LIST_HEADER_H), "Queue");

    list_queue = createList(parent);

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
        // Hidden while the op was in flight: drop the result, refreshQueue() fetches again.
        return;
    }

    if (op_error[0]) {
        showError(op_error);
    } else {
        page = fetch_page;
        showList();
    }
}

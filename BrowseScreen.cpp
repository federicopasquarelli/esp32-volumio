#include "BrowseScreen.h"
#include "ListStyle.h"
#include "DisplayConfig.h"
#include <ArduinoJson.h>
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "VolumioApi.h"
#include "QueueActionMenu.h"

#define BROWSE_ROW_H 34
// The path bar (createListHeader(): 8px padding and gap): Up button on the left, the path label
// filling the rest -- all of the width at the root, where there's no Up button.
#define PATH_BTN_W   46
#define PATH_PAD     8
#define PATH_LABEL_W_ROOT (SCREEN_WIDTH - 2 * PATH_PAD)
#define PATH_LABEL_W (PATH_LABEL_W_ROOT - PATH_BTN_W - PATH_PAD)

// Everything Volumio lists that isn't a playable item opens as a folder (folders, albums,
// radio categories, playlists...).
static bool isTrackType(const char* type) {
    return strcmp(type, "song") == 0 || strcmp(type, "webradio") == 0 || strcmp(type, "track") == 0;
}

void BrowseScreen::fetchTask(void* self) {
    static_cast<BrowseScreen*>(self)->runFetch();
    vTaskDelete(NULL);
}

void BrowseScreen::runFetch() {
    fetch_error[0] = '\0';

    String url = volumioUrl("browse?uri=") + urlEncode(fetch_uri) +
                 "&offset=" + String(fetch_page * BROWSE_PAGE_SIZE) + "&limit=" + String(BROWSE_PAGE_SIZE);
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
            for (JsonObject l : doc["navigation"]["lists"].as<JsonArray>()) {
                if (l["count"].is<int>()) total_count = l["count"].as<int>();
                for (JsonObject it : l["items"].as<JsonArray>()) {
                    if (item_count >= BROWSE_PAGE_SIZE) break;
                    Item& dst = items[item_count++];
                    const char* type = it["type"] | "";
                    strlcpy(dst.title, it["title"] | "", sizeof(dst.title));
                    strlcpy(dst.uri, it["uri"] | "", sizeof(dst.uri));
                    strlcpy(dst.service, it["service"] | "mpd", sizeof(dst.service));
                    dst.kind = isTrackType(type) ? KIND_TRACK
                             : strcmp(type, "playlist") == 0 ? KIND_PLAYLIST
                             : KIND_FOLDER;
                }
            }
            has_more = total_count >= 0
                ? (fetch_page + 1) * BROWSE_PAGE_SIZE < total_count
                : (item_count == BROWSE_PAGE_SIZE);  // no "count" in the response -- fall back to guessing
        }
    }

    // With reuse enabled the connection stays open after end().
    http.end();
    fetch_done = true;
}

void BrowseScreen::startFetch(const char* uri, int page_to_fetch) {
    if (fetching) return;
    fetching = true;
    strlcpy(fetch_uri, uri, sizeof(fetch_uri));
    fetch_page = page_to_fetch;
    // On a fragmented heap the task's stack may not fit; without this, fetching stays true forever.
    if (xTaskCreatePinnedToCore(fetchTask, "BrowseFetch", 8192, this, 1, NULL, 1) != pdPASS) {
        fetching = false;
        showError("Out of memory, try again");
    }
}

bool BrowseScreen::ensureStacks() {
    if (path_stack) return true;
    path_stack = (char (*)[BROWSE_URI_LEN])malloc((size_t)cfg.max_depth * BROWSE_URI_LEN);
    title_stack = (char (*)[BROWSE_TITLE_LEN])malloc((size_t)cfg.max_depth * BROWSE_TITLE_LEN);
    page_stack = (int*)malloc((size_t)cfg.max_depth * sizeof(int));
    if (path_stack && title_stack && page_stack) return true;
    free(path_stack);
    free(title_stack);
    free(page_stack);
    path_stack = NULL;
    title_stack = NULL;
    page_stack = NULL;
    return false;
}

// listing is false while loading or showing an error, when paging makes no sense.
void BrowseScreen::updateNav(bool listing) {
    char txt[24];
    formatPageLabel(txt, sizeof(txt), listing, page, BROWSE_PAGE_SIZE, total_count);
    lv_label_set_text(label_page, txt);
    setPagerEnabled(btn_prev_page, listing && page > 0);
    setPagerEnabled(btn_next_page, listing && has_more);
}

// "Library / Artist / Album". When that's wider than the label, leading folders are dropped
// ("… / Artist / Album") until it fits; a single folder name that still doesn't is cut with dots.
// Also shows the Up button below the root and removes it at the root, where there's no going back.
void BrowseScreen::updatePath() {
    if (!label_path) return;
    bool can_go_up = path_depth > 1;
    if (can_go_up) lv_obj_remove_flag(btn_up, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(btn_up, LV_OBJ_FLAG_HIDDEN);
    int32_t max_w = can_go_up ? PATH_LABEL_W : PATH_LABEL_W_ROOT;
    if (path_depth == 0) {
        lv_label_set_text(label_path, cfg.root_title);
        return;
    }

    // LVGL 9's text-measuring functions are private, so measure on the label itself.
    lv_label_set_long_mode(label_path, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(label_path, LV_SIZE_CONTENT);
    char buf[256];
    for (int first = 0; first < path_depth; first++) {
        buf[0] = '\0';
        if (first > 0) strlcat(buf, "\xE2\x80\xA6 / ", sizeof(buf));  // "… / "
        for (int i = first; i < path_depth; i++) {
            if (i > first) strlcat(buf, " / ", sizeof(buf));
            strlcat(buf, title_stack[i], sizeof(buf));
        }
        lv_label_set_text(label_path, buf);
        lv_obj_update_layout(label_path);
        if (lv_obj_get_width(label_path) <= max_w) break;
    }
    lv_obj_set_width(label_path, max_w);
    lv_label_set_long_mode(label_path, LV_LABEL_LONG_MODE_DOTS);
}

void BrowseScreen::setStatus(const char* text) {
    lv_obj_clean(list);
    styleListText(lv_list_add_text(list, text));
    updateNav(false);
}

void BrowseScreen::enterFolder(const char* uri, const char* title) {
    if (fetching || path_depth >= cfg.max_depth) return;
    if (!ensureStacks()) {
        showError("Out of memory, try again");
        return;
    }
    if (path_depth > 0) page_stack[path_depth - 1] = page;
    pending_page = 0;
    strlcpy(path_stack[path_depth], uri, BROWSE_URI_LEN);
    strlcpy(title_stack[path_depth], title, BROWSE_TITLE_LEN);
    path_depth++;
    updatePath();
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(uri, pending_page);
}

void BrowseScreen::goBack() {
    if (fetching || path_depth <= 1) return;
    path_depth--;
    updatePath();
    pending_page = page_stack[path_depth - 1];
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

void BrowseScreen::loadPage(int target) {
    if (fetching || path_depth == 0) return;
    pending_page = target;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(path_stack[path_depth - 1], pending_page);
}

// The event's user_data is the screen; a row's own user_data is its index on the page.
static int rowIndex(lv_event_t* e) {
    return (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
}

void BrowseScreen::item_cb(lv_event_t* e) {
    BrowseScreen* s = static_cast<BrowseScreen*>(lv_event_get_user_data(e));
    lv_event_code_t code = lv_event_get_code(e);
    int idx = rowIndex(e);
    if (idx < 0 || idx >= s->item_count) return;
    Item& it = s->items[idx];

    if (code == LV_EVENT_PRESSED) {
        s->long_press_handled = false;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        if (it.kind == KIND_PLAYLIST) {
            s->long_press_handled = true;
            showPlaylistActionMenu(it.title);
        } else if (s->cfg.queue_menu) {
            s->long_press_handled = true;
            showQueueActionMenu(it.title, it.uri, it.service, true);
        }
    } else if (code == LV_EVENT_CLICKED) {
        if (s->long_press_handled) s->long_press_handled = false;
        else s->enterFolder(it.uri, it.title);
    }
}

void BrowseScreen::track_cb(lv_event_t* e) {
    BrowseScreen* s = static_cast<BrowseScreen*>(lv_event_get_user_data(e));
    int idx = rowIndex(e);
    if (idx >= 0 && idx < s->item_count) {
        Item& it = s->items[idx];
        startQueueAction(QUEUE_PLAY, it.service, it.uri, it.title);
    }
}

// Retries the page the failed fetch was for (still in pending_page).
void BrowseScreen::retry_cb(lv_event_t* e) {
    BrowseScreen* s = static_cast<BrowseScreen*>(lv_event_get_user_data(e));
    if (s->path_depth == 0) s->refresh();  // the root itself never opened (out of memory)
    else s->loadPage(s->pending_page);
}

void BrowseScreen::up_cb(lv_event_t* e) {
    static_cast<BrowseScreen*>(lv_event_get_user_data(e))->goBack();
}

void BrowseScreen::prev_page_cb(lv_event_t* e) {
    BrowseScreen* s = static_cast<BrowseScreen*>(lv_event_get_user_data(e));
    if (s->page > 0) s->loadPage(s->page - 1);
}

void BrowseScreen::next_page_cb(lv_event_t* e) {
    BrowseScreen* s = static_cast<BrowseScreen*>(lv_event_get_user_data(e));
    if (s->has_more) s->loadPage(s->page + 1);
}

void BrowseScreen::showList() {
    lv_obj_clean(list);

    for (int i = 0; i < item_count; i++) {
        ItemKind kind = items[i].kind;
        const char* icon = kind == KIND_TRACK ? LV_SYMBOL_AUDIO
                         : kind == KIND_PLAYLIST ? LV_SYMBOL_LIST
                         : LV_SYMBOL_DIRECTORY;
        lv_obj_t* b = lv_list_add_button(list, icon, items[i].title);
        styleListRow(b, BROWSE_ROW_H);
        lv_obj_set_user_data(b, (void*)(intptr_t)i);
        if (kind == KIND_TRACK) lv_obj_add_event_cb(b, track_cb, LV_EVENT_CLICKED, this);
        else lv_obj_add_event_cb(b, item_cb, LV_EVENT_ALL, this);
    }

    if (item_count == 0) styleListText(lv_list_add_text(list, cfg.empty_text));
    updateNav(true);
}

void BrowseScreen::showError(const char* error) {
    lv_obj_clean(list);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    styleListText(lv_list_add_text(list, msg));

    lv_obj_t* retry = lv_list_add_button(list, LV_SYMBOL_REFRESH, "Retry");
    styleListRow(retry, BROWSE_ROW_H);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, this);
    updateNav(false);
}

void BrowseScreen::build(lv_obj_t* parent) {
    container = parent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Path bar on top: Up button (only below the root), then where you are.
    lv_obj_t* path_bar = createListHeader(parent, LIST_HEADER_H);
    btn_up = createPagerButton(path_bar, PATH_BTN_W, LIST_HEADER_H - 4, LV_SYMBOL_UP, up_cb, this);
    label_path = addListTitle(path_bar, cfg.root_title);
    updatePath();

    list = createList(parent);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), BROWSE_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_prev_page = createPagerButton(bar, 70, BROWSE_ROW_H - 4, LV_SYMBOL_LEFT, prev_page_cb, this);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = createPagerButton(bar, 70, BROWSE_ROW_H - 4, LV_SYMBOL_RIGHT, next_page_cb, this);
    updateNav(false);
}

void BrowseScreen::refresh() {
    if (path_depth == 0) enterFolder(cfg.root_uri, cfg.root_title);
    else loadPage(page);
}

void BrowseScreen::hide() {
    lv_obj_clean(container);
    list = NULL;
    btn_up = NULL;
    label_path = NULL;
    btn_prev_page = NULL;
    btn_next_page = NULL;
    label_page = NULL;
}

void BrowseScreen::loop() {
    if (!fetch_done) return;
    fetch_done = false;

    if (!list) {
        // Hidden while the fetch was in flight: drop the result, refresh() fetches again.
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

#include "VolumioArtists.h"
#include "ListStyle.h"
#include "DisplayConfig.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "VolumioArtistTracks.h"
#include "QueueActionMenu.h"

// A flat, paginated list of artists: /api/v1/browse on "artists://", with the same offset/limit
// and "count" as Library's "music-library". Tapping an artist opens VolumioArtistTracks.cpp.

#define ARTISTS_ROOT_URI  "artists://"
#define ARTISTS_PAGE_SIZE 4
#define ARTISTS_ROW_H     34
#define ARTISTS_TITLE_LEN 64
#define ARTISTS_URI_LEN   160

struct ArtistItem {
    char title[ARTISTS_TITLE_LEN];
    char uri[ARTISTS_URI_LEN];  // Used to open the track list and for the long-press actions below.
};

static lv_obj_t* tab_artists = NULL;
static lv_obj_t* list_artists = NULL;
static lv_obj_t* btn_prev_page = NULL;
static lv_obj_t* btn_next_page = NULL;
static lv_obj_t* label_page = NULL;
static int page = 0;
static int pending_page = 0;

// Only the page on screen is held in RAM.
static ArtistItem items[ARTISTS_PAGE_SIZE];
static int item_count = 0;
// True if there's a next page, from "count" (or a full page if the response had none).
static bool has_more = false;
static int total_count = -1;

// Written by the fetch task, read by loopArtists(). Only one fetch runs at a time.
static volatile bool fetching = false;
static volatile bool fetch_done = false;
static int fetch_page = 0;
static char fetch_error[32];

// Kept alive between requests so we skip the TCP handshake each time.
static WiFiClient wifi_client;
static HTTPClient http;

static void showError(const char* error);  // defined further down, used by startFetch() below

static void fetchTask(void*) {
    fetch_error[0] = '\0';

    String url = volumioUrl("browse?uri=") + urlEncode(ARTISTS_ROOT_URI) +
                 "&offset=" + String(fetch_page * ARTISTS_PAGE_SIZE) + "&limit=" + String(ARTISTS_PAGE_SIZE);
    http.setReuse(true);
    http.setTimeout(8000);
    http.begin(wifi_client, url);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        strlcpy(fetch_error, "Request failed", sizeof(fetch_error));
    } else {
        JsonDocument filter;
        filter["navigation"]["lists"][0]["items"][0]["title"] = true;
        filter["navigation"]["lists"][0]["items"][0]["uri"] = true;
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
                    if (item_count >= ARTISTS_PAGE_SIZE) break;
                    ArtistItem& dst = items[item_count++];
                    strlcpy(dst.title, it["title"] | "", sizeof(dst.title));
                    strlcpy(dst.uri, it["uri"] | "", sizeof(dst.uri));
                }
            }
            has_more = total_count >= 0
                ? (fetch_page + 1) * ARTISTS_PAGE_SIZE < total_count
                : (item_count == ARTISTS_PAGE_SIZE);  // no "count" in the response -- fall back to guessing
        }
    }

    // With reuse enabled the connection stays open after end().
    http.end();
    fetch_done = true;
    vTaskDelete(NULL);
}

static void startFetch(int pageToFetch) {
    if (fetching) return;
    fetching = true;
    fetch_page = pageToFetch;
    // On a fragmented heap the task's stack may not fit; without this, fetching stays true forever.
    if (xTaskCreatePinnedToCore(fetchTask, "ArtistsFetch", 8192, NULL, 1, NULL, 1) != pdPASS) {
        fetching = false;
        showError("Out of memory, try again");
    }
}

// listing is false while loading or showing an error, when paging makes no sense.
static void updateNav(bool listing) {
    char txt[24];
    formatPageLabel(txt, sizeof(txt), listing, page, ARTISTS_PAGE_SIZE, total_count);
    lv_label_set_text(label_page, txt);
    setPagerEnabled(btn_prev_page, listing && page > 0);
    setPagerEnabled(btn_next_page, listing && has_more);
}

static void setStatus(const char* text) {
    lv_obj_clean(list_artists);
    styleListText(lv_list_add_text(list_artists, text));
    updateNav(false);
}

// Retries the page the failed fetch was for (still in pending_page).
static void retry_cb(lv_event_t*) {
    if (fetching) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

static void styleButton(lv_obj_t* btn) { styleListRow(btn, ARTISTS_ROW_H); }

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
        // An artist's uri ("artists://<name>", as given, not re-encoded) queues all of their tracks.
        // Artist entries have no "service" field, hence "mpd".
        showQueueActionMenu(items[idx].title, items[idx].uri, "mpd", false);
    } else if (code == LV_EVENT_CLICKED) {
        if (long_press_handled) long_press_handled = false;
        else openArtistTracks(items[idx].uri, items[idx].title);
    }
}

static void showList() {
    lv_obj_clean(list_artists);

    for (int i = 0; i < item_count; i++) {
        lv_obj_t* b = lv_list_add_button(list_artists, LV_SYMBOL_DIRECTORY, items[i].title);
        styleButton(b);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }

    if (item_count == 0) styleListText(lv_list_add_text(list_artists, "No artists found"));
    updateNav(true);
}

static void prev_page_cb(lv_event_t*) {
    if (fetching || page <= 0) return;
    pending_page = page - 1;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

static void next_page_cb(lv_event_t*) {
    if (fetching || !has_more) return;
    pending_page = page + 1;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

void buildArtists(lv_obj_t* parent) {
    tab_artists = parent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    // Title row, the list and the page bar (see LIST_BODY_H).
    addListTitle(createListHeader(parent, LIST_HEADER_H), "Artists");

    list_artists = createList(parent);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), ARTISTS_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_prev_page = createPagerButton(bar, 70, ARTISTS_ROW_H - 4, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = createPagerButton(bar, 70, ARTISTS_ROW_H - 4, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

void refreshArtists() {
    pending_page = page;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

void hideArtists() {
    lv_obj_clean(tab_artists);
    list_artists = NULL;
    btn_prev_page = NULL;
    btn_next_page = NULL;
    label_page = NULL;
}

static void showError(const char* error) {
    lv_obj_clean(list_artists);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    styleListText(lv_list_add_text(list_artists, msg));

    lv_obj_t* retry = lv_list_add_button(list_artists, LV_SYMBOL_REFRESH, "Retry");
    styleButton(retry);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, NULL);
    updateNav(false);
}

void loopArtists() {
    if (!fetch_done) return;
    fetch_done = false;

    if (!list_artists) {
        // Hidden while the fetch was in flight: drop the result, refreshArtists() fetches again.
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

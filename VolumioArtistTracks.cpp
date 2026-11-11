#include "VolumioArtistTracks.h"
#include "ListStyle.h"
#include "DisplayConfig.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "UiHandler.h"
#include "VolumioApi.h"
#include "QueueActionMenu.h"

// Per-artist track list, opened by tapping an artist (VolumioArtists.cpp). Browsing
// "artists://<name>" returns two lists, "Albums (<name>)" and "Tracks (<name>)", each paged
// independently by offset/limit. Only the Tracks list is shown, found by its title rather than its
// position. Tapping a track plays it like a Library track.

#define TRACKS_PAGE_SIZE   4
#define TRACKS_ROW_H       34
#define TRACKS_TITLE_LEN   96
#define TRACKS_URI_LEN     220
#define TRACKS_SERVICE_LEN 16
#define ARTIST_URI_LEN     160
#define ARTIST_NAME_LEN    64

struct TrackItem {
    char title[TRACKS_TITLE_LEN];
    char uri[TRACKS_URI_LEN];
    char service[TRACKS_SERVICE_LEN];
};

static lv_obj_t* artists_screen_ref = NULL;  // where Back returns to
static lv_obj_t* tracks_screen = NULL;
static lv_obj_t* label_artist_name = NULL;
static lv_obj_t* list_tracks = NULL;
static lv_obj_t* btn_prev_page = NULL;
static lv_obj_t* btn_next_page = NULL;
static lv_obj_t* label_page = NULL;

static char artist_uri[ARTIST_URI_LEN] = "";
static char artist_name[ARTIST_NAME_LEN] = "";
static int page = 0;
static int pending_page = 0;

// Only the page on screen is held in RAM.
static TrackItem tracks[TRACKS_PAGE_SIZE];
static int track_count = 0;
static bool has_more = false;
static int total_count = -1;

// Written by the fetch task, read by loopArtistTracks(). Only one fetch runs at a time.
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

    // Not urlEncode()'d: Volumio already returns it encoded (see openArtistTracks()).
    String url = volumioUrl("browse?uri=") + artist_uri +
                 "&offset=" + String(fetch_page * TRACKS_PAGE_SIZE) + "&limit=" + String(TRACKS_PAGE_SIZE);
    http.setReuse(true);
    http.setTimeout(8000);
    http.begin(wifi_client, url);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        strlcpy(fetch_error, "Request failed", sizeof(fetch_error));
    } else {
        JsonDocument filter;
        filter["navigation"]["lists"][0]["title"] = true;
        filter["navigation"]["lists"][0]["items"][0]["title"] = true;
        filter["navigation"]["lists"][0]["items"][0]["uri"] = true;
        filter["navigation"]["lists"][0]["items"][0]["service"] = true;
        filter["navigation"]["lists"][0]["count"] = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
        if (err) {
            strlcpy(fetch_error, "Invalid response", sizeof(fetch_error));
        } else {
            track_count = 0;
            total_count = -1;
            for (JsonObject list : doc["navigation"]["lists"].as<JsonArray>()) {
                const char* list_title = list["title"] | "";
                if (strncmp(list_title, "Tracks", 6) != 0) continue;  // skip "Albums (<name>)"
                if (list["count"].is<int>()) total_count = list["count"].as<int>();
                for (JsonObject it : list["items"].as<JsonArray>()) {
                    if (track_count >= TRACKS_PAGE_SIZE) break;
                    TrackItem& dst = tracks[track_count++];
                    strlcpy(dst.title, it["title"] | "", sizeof(dst.title));
                    strlcpy(dst.uri, it["uri"] | "", sizeof(dst.uri));
                    strlcpy(dst.service, it["service"] | "mpd", sizeof(dst.service));
                }
            }
            has_more = total_count >= 0
                ? (fetch_page + 1) * TRACKS_PAGE_SIZE < total_count
                : (track_count == TRACKS_PAGE_SIZE);  // no "count" in the response -- fall back to guessing
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
    if (xTaskCreatePinnedToCore(fetchTask, "ArtistTracks", 8192, NULL, 1, NULL, 1) != pdPASS) {
        fetching = false;
        showError("Out of memory, try again");
    }
}

static void updateNav(bool listing) {
    char txt[24];
    formatPageLabel(txt, sizeof(txt), listing, page, TRACKS_PAGE_SIZE, total_count);
    lv_label_set_text(label_page, txt);
    setPagerEnabled(btn_prev_page, listing && page > 0);
    setPagerEnabled(btn_next_page, listing && has_more);
}

static void setStatus(const char* text) {
    lv_obj_clean(list_tracks);
    styleListText(lv_list_add_text(list_tracks, text));
    updateNav(false);
}

// Retries the page the failed fetch was for (still in pending_page).
static void retry_cb(lv_event_t*) {
    if (fetching) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

static void styleButton(lv_obj_t* btn) { styleListRow(btn, TRACKS_ROW_H); }

static void track_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < track_count) startQueueAction(QUEUE_PLAY, tracks[idx].service, tracks[idx].uri, tracks[idx].title);
}

static void showList() {
    lv_obj_clean(list_tracks);

    for (int i = 0; i < track_count; i++) {
        lv_obj_t* b = lv_list_add_button(list_tracks, LV_SYMBOL_AUDIO, tracks[i].title);
        styleButton(b);
        lv_obj_add_event_cb(b, track_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    if (track_count == 0) styleListText(lv_list_add_text(list_tracks, "No tracks found"));
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

static void back_cb(lv_event_t*) {
    showScreen(artists_screen_ref);
}

void setupArtistTracks(lv_obj_t* artistsScreen, lv_obj_t* tracksScreenParent) {
    artists_screen_ref = artistsScreen;
    tracks_screen = tracksScreenParent;
}

// The title row (Back + artist name), the track list and the Prev/Next bar (see LIST_BODY_H).
void buildArtistTracks(lv_obj_t* parent) {
    tracks_screen = parent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t* header = createListHeader(parent, LIST_HEADER_H);
    createPagerButton(header, 46, LIST_HEADER_H - 4, LV_SYMBOL_LEFT, back_cb);
    label_artist_name = addListTitle(header, "");
    lv_label_set_long_mode(label_artist_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label_artist_name, SCREEN_WIDTH - 2 * 8 - 46 - 8);

    list_tracks = createList(parent);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(bar, LV_PCT(100), TRACKS_ROW_H);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_prev_page = createPagerButton(bar, 70, TRACKS_ROW_H - 4, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = createPagerButton(bar, 70, TRACKS_ROW_H - 4, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

void refreshArtistTracks() {
    lv_label_set_text(label_artist_name, artist_name);
    pending_page = page;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

void hideArtistTracks() {
    lv_obj_clean(tracks_screen);
    label_artist_name = NULL;
    list_tracks = NULL;
    btn_prev_page = NULL;
    btn_next_page = NULL;
    label_page = NULL;
}

void openArtistTracks(const char* uri, const char* name) {
    strlcpy(artist_uri, uri, sizeof(artist_uri));
    strlcpy(artist_name, name, sizeof(artist_name));
    page = 0;
    showScreen(tracks_screen);
}

static void showError(const char* error) {
    lv_obj_clean(list_tracks);
    char msg[64];
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " %s", error);
    styleListText(lv_list_add_text(list_tracks, msg));

    lv_obj_t* retry = lv_list_add_button(list_tracks, LV_SYMBOL_REFRESH, "Retry");
    styleButton(retry);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, NULL);
    updateNav(false);
}

void loopArtistTracks() {
    if (!fetch_done) return;
    fetch_done = false;

    if (!list_tracks) {
        // Hidden while the fetch was in flight: drop the result, refreshArtistTracks() fetches again.
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

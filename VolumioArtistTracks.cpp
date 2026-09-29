#include "VolumioArtistTracks.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "UiHandler.h"

// Per-artist track list, opened by tapping a row on the Artists screen (VolumioArtists.cpp).
// Browsing "artists://<name>" (confirmed live) returns TWO separate lists in the same response --
// "Albums (<name>)" and "Tracks (<name>)" -- each with its own independent items/count, and
// offset/limit slices each list independently rather than treating them as one combined sequence
// (see VolumioArtists.cpp's file header). This screen only cares about the Tracks list -- Albums
// is ignored entirely -- picked out by its title starting with "Tracks" rather than by array
// position, in case Volumio ever reorders them.
//
// Each track item comes back with a normal "service"/"uri"/"title" identical in shape to a
// Library track (e.g. uri "music-library/USB/.../song.flac"), so tapping one to play reuses the
// same addToQueue + play-Nth pattern VolumioLibrary.cpp's ACTION_PLAY does.

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

// Only the page currently on screen is ever held in RAM, same reasoning as Library's items[].
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

    // artist_uri is used as-is, NOT urlEncode()'d -- see this file's header comment / the .h's
    // openArtistTracks() comment for why re-encoding it would corrupt it.
    String url = String("http://") + SECRET_VOLUMIO_HOST + ":" + SECRET_VOLUMIO_PORT +
                 "/api/v1/browse?uri=" + artist_uri +
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
    // Return value checked -- see CLAUDE.md item 25's follow-up: an unchecked task-creation
    // failure here would leave `fetching` stuck true forever with nothing to ever clear it.
    if (xTaskCreatePinnedToCore(fetchTask, "ArtistTracks", 8192, NULL, 1, NULL, 1) != pdPASS) {
        fetching = false;
        showError("Out of memory, try again");
    }
}

// --- Playing a track (addToQueue + play-Nth, same shape as VolumioLibrary.cpp's ACTION_PLAY) ---

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

static volatile bool play_running = false;
static TrackItem play_item;  // copied before the task starts -- tracks[] may get refetched/paged

static void playTask(void*) {
    JsonDocument body;
    body["service"] = play_item.service;
    body["uri"] = play_item.uri;
    body["title"] = play_item.title;

    // No REST "add and play": queue the track, then play it by its (new) position.
    int first = queueLength();
    if (first >= 0 && postJson("addToQueue", body) > 0) {
        WiFiClient client;
        HTTPClient req;
        req.begin(client, baseUrl() + "commands/?cmd=play&N=" + String(first));
        req.setTimeout(8000);
        req.GET();
        req.end();
    }

    play_running = false;
    vTaskDelete(NULL);
}

static void playTrack(const TrackItem& item) {
    if (play_running) return;
    play_item = item;
    play_running = true;
    if (xTaskCreatePinnedToCore(playTask, "ArtistPlay", 8192, NULL, 1, NULL, 1) != pdPASS) {
        play_running = false;
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
    lv_list_add_text(list_tracks, text);
    updateNav(false);
}

// Retries at whatever page the failed fetch was targeting -- pending_page still holds it.
static void retry_cb(lv_event_t*) {
    if (fetching) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

static void styleButton(lv_obj_t* btn) {
    lv_obj_set_height(btn, TRACKS_ROW_H);
    lv_obj_set_style_pad_top(btn, 0, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0);
}

static void track_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && idx < track_count) playTrack(tracks[idx]);
}

// tracks[] only ever holds the page just fetched (server-side offset/limit already did the
// slicing), so this just renders all of it.
static void showList() {
    lv_obj_clean(list_tracks);

    for (int i = 0; i < track_count; i++) {
        lv_obj_t* b = lv_list_add_button(list_tracks, LV_SYMBOL_AUDIO, tracks[i].title);
        styleButton(b);
        lv_obj_add_event_cb(b, track_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }

    if (track_count == 0) lv_list_add_text(list_tracks, "No tracks found");
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

// on_build: constructs the widget tree -- a compact header row (Back + artist name, same height
// as a list row so the math below is exact), the track list, and the same Prev/Next bar every
// other paginated screen has. 34 (header) + 34*4 (list) + 34 (nav bar) = 204 = the full content
// height below the top bar.
void buildArtistTracks(lv_obj_t* parent) {
    tracks_screen = parent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    lv_obj_t* btn_back = lv_button_create(parent);
    lv_obj_set_size(btn_back, 50, TRACKS_ROW_H - 4);
    lv_obj_set_pos(btn_back, 4, 2);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x222222), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* back_icon = lv_label_create(btn_back);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_center(back_icon);

    label_artist_name = lv_label_create(parent);
    lv_label_set_long_mode(label_artist_name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label_artist_name, 250);
    lv_obj_set_pos(label_artist_name, 60, 8);
    lv_obj_set_style_text_font(label_artist_name, &lv_font_montserrat_ext_18, 0);
    lv_obj_set_style_text_color(label_artist_name, lv_color_hex(0xFFFFFF), 0);

    list_tracks = lv_list_create(parent);
    lv_obj_set_pos(list_tracks, 0, TRACKS_ROW_H);
    lv_obj_set_size(list_tracks, LV_PCT(100), TRACKS_ROW_H * TRACKS_PAGE_SIZE);
    lv_obj_remove_flag(list_tracks, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(list_tracks, 0, 0);
    lv_obj_set_style_pad_row(list_tracks, 0, 0);
    lv_obj_set_style_bg_color(list_tracks, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(list_tracks, 0, 0);
    lv_obj_set_style_text_color(list_tracks, lv_color_hex(0xAAAAAA), 0);
    // Track names come straight from file tags, which often have accented letters or smart
    // quotes the stock font doesn't have a glyph for -- see CustomFonts.h.
    lv_obj_set_style_text_font(list_tracks, &lv_font_montserrat_ext_14, 0);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_pos(bar, 0, TRACKS_ROW_H + TRACKS_ROW_H * TRACKS_PAGE_SIZE);
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

// on_show: (re)loads the current artist's page.
void refreshArtistTracks() {
    lv_label_set_text(label_artist_name, artist_name);
    pending_page = page;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

// on_hide: frees this screen's widgets back to LVGL's pool.
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
    lv_list_add_text(list_tracks, msg);

    lv_obj_t* retry = lv_list_add_button(list_tracks, LV_SYMBOL_REFRESH, "Retry");
    styleButton(retry);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, NULL);
    updateNav(false);
}

void loopArtistTracks() {
    if (!fetch_done) return;
    fetch_done = false;

    if (!list_tracks) {
        // Screen was hidden (its widgets torn down) while this fetch was in flight -- discard;
        // refreshArtistTracks() starts a fresh one the next time this screen is shown.
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

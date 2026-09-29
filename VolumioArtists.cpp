#include "VolumioArtists.h"
#include "arduino_secrets.h"
#include "DisplayConfig.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "CustomFonts.h"
#include "PaginationNav.h"
#include "VolumioArtistTracks.h"

// Same /api/v1/browse endpoint VolumioLibrary.cpp uses for "music-library", just rooted at
// "artists://" instead -- confirmed live against volumio.local that it takes the exact same
// offset/limit and returns the exact same "count" (real total, not just this page's size) that
// VolumioLibrary.cpp's pagination already relies on. So this screen is a flat, paginated list of
// artist names, built the same way, and lazy-built/freed on show/hide like every other screen
// (see CLAUDE.md item 29).
//
// Tapping an artist opens VolumioArtistTracks.cpp's screen (their track list) -- see that file
// for why it's tracks only, not a full albums+tracks browser: browsing "artists://<name>" comes
// back as TWO separate lists in the same response, each with its own independent items/count,
// rather than the single list this pager (and Library's) assumes.

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

// Only the page currently on screen is ever held in RAM, same reasoning as Library's items[].
static ArtistItem items[ARTISTS_PAGE_SIZE];
static int item_count = 0;
// True if there's a next page, from Volumio's own "count" (real total) -- falls back to the
// old "was the page full?" guess only if a response is ever missing "count".
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

static void showError(const char* error);  // defined further down, used by startFetch() below

static void fetchTask(void*) {
    fetch_error[0] = '\0';

    String url = String("http://") + SECRET_VOLUMIO_HOST + ":" + SECRET_VOLUMIO_PORT +
                 "/api/v1/browse?uri=" + urlEncode(ARTISTS_ROOT_URI) +
                 "&offset=" + String(fetch_page * ARTISTS_PAGE_SIZE) + "&limit=" + String(ARTISTS_PAGE_SIZE);
    http.setReuse(true);
    http.setTimeout(8000);
    http.begin(wifi_client, url);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        strlcpy(fetch_error, "Request failed", sizeof(fetch_error));
    } else {
        // artists:// at the top level is a single list (unlike browsing into one artist, see the
        // file header comment), so this filter/parse is the same shape as Library's.
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
    // Return value checked -- see CLAUDE.md item 25's follow-up: an unchecked task-creation
    // failure here would leave `fetching` stuck true forever with nothing to ever clear it.
    if (xTaskCreatePinnedToCore(fetchTask, "ArtistsFetch", 8192, NULL, 1, NULL, 1) != pdPASS) {
        fetching = false;
        showError("Out of memory, try again");
    }
}

// listing is false while loading or showing an error, when paging makes no sense. Shows
// "current/total (item count)" when the last fetch's response included Volumio's real total
// count, otherwise just the current page number -- see PaginationNav.cpp's formatPageLabel(),
// shared by every paginated screen.
static void updateNav(bool listing) {
    char txt[24];
    formatPageLabel(txt, sizeof(txt), listing, page, ARTISTS_PAGE_SIZE, total_count);
    lv_label_set_text(label_page, txt);
    setPagerEnabled(btn_prev_page, listing && page > 0);
    setPagerEnabled(btn_next_page, listing && has_more);
}

static void setStatus(const char* text) {
    lv_obj_clean(list_artists);
    lv_list_add_text(list_artists, text);
    updateNav(false);
}

// Retries at whatever page the failed fetch was targeting -- pending_page still holds it.
static void retry_cb(lv_event_t*) {
    if (fetching) return;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

static void styleButton(lv_obj_t* btn) {
    lv_obj_set_height(btn, ARTISTS_ROW_H);
    lv_obj_set_style_pad_top(btn, 0, 0);
    lv_obj_set_style_pad_bottom(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0);
}

// LVGL still sends CLICKED on release after a long press, so remember the press was consumed --
// same pattern VolumioLibrary.cpp's item_cb() uses for its folder rows.
static bool long_press_handled = false;
static ArtistItem menu_item;
static volatile bool action_menu_running = false;
static int action_requested = 0;

static void overlay_cb(lv_event_t* e) {
    // Only taps on the dimmed background close the menu, not taps bubbling up from the panel.
    if (lv_event_get_target_obj(e) == lv_event_get_current_target_obj(e)) lv_obj_delete_async(lv_event_get_target_obj(e));
}

// Same three as VolumioLibrary.cpp's folder actions, minus "Update folder": an artist here is a
// virtual grouping across (possibly many) real folders, not one Volumio can rescan as a unit.
enum ArtistAction { ACTION_PLAY, ACTION_ADD_TO_QUEUE, ACTION_CLEAR_AND_PLAY };

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

// Confirmed live against volumio.local: addToQueue with an artist's own uri
// ("artists://<name>", exactly as given by the artists:// list -- see this file's header comment
// about not re-encoding it) queues every one of that artist's tracks, the same way a folder's uri
// does in VolumioLibrary.cpp. "service" is hardcoded "mpd" here (artist entries don't carry their
// own service field the way folder/track items do -- there was nothing to read it from).
static void artistActionTask(void* pv) {
    ArtistItem* item = (ArtistItem*)pv;

    JsonDocument body;
    body["service"] = "mpd";
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
            // No REST "add and play": queue the artist's tracks, then play the first one added.
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

static void startAction(int action, const ArtistItem& item) {
    if (action_menu_running) return;
    action_requested = action;
    action_menu_running = true;
    if (xTaskCreatePinnedToCore(artistActionTask, "ArtistAction", 8192, new ArtistItem(item), 1, NULL, 1) != pdPASS) {
        action_menu_running = false;
    }
}

static void menu_action_cb(lv_event_t* e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    startAction(action, menu_item);
    // The overlay is the panel's parent.
    lv_obj_t* overlay = lv_obj_get_parent(lv_obj_get_parent(lv_event_get_target_obj(e)));
    lv_obj_delete_async(overlay);
}

static void showContextMenu(const ArtistItem& item) {
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

    static const char* icons[] = { LV_SYMBOL_PLAY, LV_SYMBOL_PLUS, LV_SYMBOL_TRASH };
    static const char* names[] = { "Play", "Add to queue", "Clear and play" };
    for (int i = 0; i < 3; i++) {
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
        else openArtistTracks(items[idx].uri, items[idx].title);
    }
}

// items[] only ever holds the page just fetched (server-side offset/limit already did the
// slicing), so this just renders all of it.
static void showList() {
    lv_obj_clean(list_artists);

    for (int i = 0; i < item_count; i++) {
        lv_obj_t* b = lv_list_add_button(list_artists, LV_SYMBOL_DIRECTORY, items[i].title);
        styleButton(b);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_ALL, (void*)(intptr_t)i);
    }

    if (item_count == 0) lv_list_add_text(list_artists, "No artists found");
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

// on_build: constructs the widget tree. Mirrors VolumioLibrary.cpp's buildLibrary() minus the
// "Up" button (this is a single flat level, nowhere to go back to).
void buildArtists(lv_obj_t* parent) {
    tab_artists = parent;
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(parent, 0, 0);

    list_artists = lv_list_create(parent);
    lv_obj_set_size(list_artists, LV_PCT(100), ARTISTS_ROW_H * ARTISTS_PAGE_SIZE);
    lv_obj_align(list_artists, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(list_artists, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(list_artists, 0, 0);
    lv_obj_set_style_pad_row(list_artists, 0, 0);
    lv_obj_set_style_bg_color(list_artists, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(list_artists, 0, 0);
    lv_obj_set_style_text_color(list_artists, lv_color_hex(0xAAAAAA), 0);
    // Artist names come straight from file tags, which often have accented letters the stock font
    // has no glyph for -- see CustomFonts.h.
    lv_obj_set_style_text_font(list_artists, &lv_font_montserrat_ext_14, 0);

    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), ARTISTS_ROW_H);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // No "Up" button -- unlike Library, this is a single flat level with nowhere to go back to.
    btn_prev_page = createPagerButton(bar, 70, ARTISTS_ROW_H - 4, LV_SYMBOL_LEFT, prev_page_cb);
    label_page = lv_label_create(bar);
    lv_obj_set_style_text_color(label_page, lv_color_hex(0xFFFFFF), 0);
    btn_next_page = createPagerButton(bar, 70, ARTISTS_ROW_H - 4, LV_SYMBOL_RIGHT, next_page_cb);
    updateNav(false);
}

// on_show: re-fetches the remembered page (page 0 the first time ever) -- items[] and the list
// widgets were both wiped on hide, so this repopulates the freshly-rebuilt (empty) list.
void refreshArtists() {
    pending_page = page;
    setStatus(LV_SYMBOL_REFRESH " Loading...");
    startFetch(pending_page);
}

// on_hide: frees this screen's widgets back to LVGL's pool. `page` is deliberately left alone --
// refreshArtists() uses it to rebuild the same view next time.
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
    lv_list_add_text(list_artists, msg);

    lv_obj_t* retry = lv_list_add_button(list_artists, LV_SYMBOL_REFRESH, "Retry");
    styleButton(retry);
    lv_obj_add_event_cb(retry, retry_cb, LV_EVENT_CLICKED, NULL);
    updateNav(false);
}

void loopArtists() {
    if (!fetch_done) return;
    fetch_done = false;

    if (!list_artists) {
        // Screen was hidden (its widgets torn down) while this fetch was in flight -- discard;
        // refreshArtists() starts a fresh one the next time this screen is shown.
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

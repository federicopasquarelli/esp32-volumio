#ifndef BROWSE_SCREEN_H
#define BROWSE_SCREEN_H

#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>

// A paginated folder browser over /api/v1/browse, starting at a fixed root URI: Library
// ("music-library"), Playlists ("playlists") and Web Radio ("radio") are each one instance.
// The current path sits above the list, next to the Up button; the page buttons are below it.
// Tap a folder to open it, tap a track or station to play it. Long-press a playlist for
// Add to queue / Clear and play; long-press any other folder for the queue menu if
// queue_menu is set.
//
// Each instance keeps its own folder and page across hides, so reopening a screen returns to
// the same place. build()/refresh()/hide() are addScreen()'s on_build/on_show/on_hide (wrap
// them in captureless lambdas), loop() goes in the sketch's loop().
struct BrowseConfig {
    const char* root_uri;
    const char* root_title;  // first segment of the path shown above the list
    int max_depth;           // folders deep, root included
    const char* empty_text;  // shown for a folder with no items
    bool queue_menu;         // long-press on a folder opens the queue menu (with "Update folder")
};

#define BROWSE_PAGE_SIZE 4
#define BROWSE_URI_LEN   160
#define BROWSE_TITLE_LEN 64

class BrowseScreen {
public:
    explicit BrowseScreen(const BrowseConfig& cfg) : cfg(cfg) {}
    void build(lv_obj_t* parent);
    // Opens the root the first time, then the folder and page shown when it was last left.
    void refresh();
    // Frees the widgets; the current folder and page are kept.
    void hide();
    // Applies a finished fetch to the UI.
    void loop();

private:
    enum ItemKind : uint8_t { KIND_FOLDER, KIND_PLAYLIST, KIND_TRACK };
    struct Item {
        char title[BROWSE_TITLE_LEN];
        char uri[BROWSE_URI_LEN];
        char service[16];
        ItemKind kind;
    };

    const BrowseConfig cfg;

    lv_obj_t* container = NULL;
    lv_obj_t* list = NULL;
    lv_obj_t* btn_up = NULL;
    lv_obj_t* label_path = NULL;
    lv_obj_t* btn_prev_page = NULL;
    lv_obj_t* btn_next_page = NULL;
    lv_obj_t* label_page = NULL;
    int page = 0;

    // Only the page on screen is held in RAM: browse takes offset/limit, so each page turn is a
    // request.
    Item items[BROWSE_PAGE_SIZE];
    int item_count = 0;
    // True if there's a next page. Volumio returns the folder's total as "count" when
    // offset/limit are passed; without it a full page is taken to mean there's more.
    bool has_more = false;
    int total_count = -1;  // -1 if the response had no "count"

    // URIs and titles of the folders entered, the last one is the folder shown. Heap-allocated
    // on first use (max_depth * ~220 bytes): static DRAM is nearly full, see CLAUDE.md.
    char (*path_stack)[BROWSE_URI_LEN] = NULL;
    char (*title_stack)[BROWSE_TITLE_LEN] = NULL;
    int* page_stack = NULL;  // the page shown in each folder left, to return to it
    int path_depth = 0;
    int pending_page = 0;

    // Written by the fetch task, read by loop(). Only one fetch runs at a time per instance.
    volatile bool fetching = false;
    volatile bool fetch_done = false;
    char fetch_uri[BROWSE_URI_LEN];
    int fetch_page = 0;
    char fetch_error[32];

    // LVGL still sends CLICKED on release after a long press, so remember the press was consumed.
    bool long_press_handled = false;

    // Kept alive between requests so the TCP handshake is skipped each time.
    WiFiClient wifi_client;
    HTTPClient http;

    static void fetchTask(void* self);
    void runFetch();
    void startFetch(const char* uri, int page_to_fetch);
    bool ensureStacks();
    void updateNav(bool listing);
    void setStatus(const char* text);
    void showList();
    void showError(const char* error);
    void updatePath();
    void enterFolder(const char* uri, const char* title);
    void goBack();
    void loadPage(int target);

    static void item_cb(lv_event_t* e);
    static void track_cb(lv_event_t* e);
    static void retry_cb(lv_event_t* e);
    static void up_cb(lv_event_t* e);
    static void prev_page_cb(lv_event_t* e);
    static void next_page_cb(lv_event_t* e);
};

#endif

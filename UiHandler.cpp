#include "UiHandler.h"
#include "DisplayConfig.h"
#include "VolumioHandler.h"
#include "CustomFonts.h"
#include <time.h>

#define MAX_SCREENS 6

static lv_obj_t *label_top, *cont_player, *btn_menu, *menu_list, *menu_grid, *label_title, *label_artist, *label_album;
static lv_obj_t *confirm_layer, *confirm_title, *confirm_note, *confirm_ok_btn, *confirm_ok_label;
static bool (*confirm_action)() = NULL;
static lv_obj_t *label_elapsed, *label_volume;
static lv_obj_t *btn_play, *btn_label, *btn_prev, *btn_next, *btn_shuffle, *btn_repeat;
static lv_obj_t *slider_volume;
static lv_obj_t* cont_offline = NULL;
static bool offline_shown = false;
static bool currentShuffle = false;
static bool currentRepeat = false;
static bool currentRepeatSingle = false;

// The elapsed time ticks locally once a second, rather than only redrawing on a pushState
// (Volumio doesn't push one continuously during playback, e.g. it doesn't push one every
// second, so waiting on the websocket alone made the label look frozen). Whenever a pushState
// does arrive, base_elapsed_sec/base_elapsed_at_ms are resynced to Volumio's authoritative
// value, correcting for local drift and for seeks/track changes.
static int base_elapsed_sec = 0;
static unsigned long base_elapsed_at_ms = 0;
static int cur_duration_sec = 0;
static bool cur_is_playing = false;

struct ScreenEntry { lv_obj_t* obj; void (*on_show)(void); };
static ScreenEntry screens[MAX_SCREENS];
static int screen_count = 0;

// Active buttons are filled with the accent, inactive ones are empty, like the tab labels used to be.
static void set_btn_active_style(lv_obj_t* btn) {
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_ACCENT), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x19A34A), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(btn, 0, 0);
}

static void set_btn_inactive_style(lv_obj_t* btn) {
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COLOR_ACCENT), 0);
}

// Dim outline, no fill: what play/prev/next look like while AirPlay is the source (Volumio has
// nothing to control there -- the sending device is in charge). Set once, then it's just a matter
// of adding/removing LV_STATE_DISABLED.
static void set_btn_disabled_style(lv_obj_t* btn) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x555555), LV_STATE_DISABLED);
    lv_obj_set_style_border_width(btn, 1, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x333333), LV_STATE_DISABLED);
}

// True while Volumio's source is AirPlay. The callbacks check it themselves rather than trusting
// LV_STATE_DISABLED alone to swallow the tap.
static bool player_airplay = false;

static void play_cb(lv_event_t * e) { if (!player_airplay) togglePlayback(); }
static void prev_cb(lv_event_t * e) { if (!player_airplay) prevTrack(); }
static void next_cb(lv_event_t * e) { if (!player_airplay) nextTrack(); }

static void shuffle_cb(lv_event_t * e) { setShuffle(!currentShuffle); }

static void repeat_cb(lv_event_t * e) {
    if (!currentRepeat) {
        // Was OFF, go to Repeat All
        setRepeatMode(true, false);
    } else if (!currentRepeatSingle) {
        // Was Repeat All, go to Repeat Single
        setRepeatMode(true, true);
    } else {
        // Was Repeat Single, go to OFF
        setRepeatMode(false, false);
    }
}

static void volume_cb(lv_event_t * e) {
    int v = lv_slider_get_value(lv_event_get_target_obj(e));
    setVolume(v);
    lv_label_set_text_fmt(label_volume, "%d", v);
}

// mm:ss (zero-padded, e.g. "03:04"), or h:mm:ss once it runs an hour long.
static void formatTime(char* out, size_t out_len, int totalSec) {
    if (totalSec < 0) totalSec = 0;
    int h = totalSec / 3600, m = (totalSec % 3600) / 60, s = totalSec % 60;
    if (h > 0) snprintf(out, out_len, "%d:%02d:%02d", h, m, s);
    else snprintf(out, out_len, "%02d:%02d", m, s);
}

// Redraws the "elapsed / duration" label. Called every loop() iteration, not on any timer of
// its own: elapsed is recomputed fresh each time from real milliseconds since the last
// pushState, so the displayed second changes exactly when it should, with nothing to
// accidentally phase-lock against another timer (which a periodic "tick every ~1000ms" was
// doing, even with distinct variables — they kept resetting to the same millis() together).
static int last_displayed_elapsed = -1;
static void refreshPlaybackClock() {
    if (!label_elapsed) return;

    int elapsed = base_elapsed_sec;
    if (cur_is_playing) elapsed += (millis() - base_elapsed_at_ms) / 1000;
    if (elapsed > cur_duration_sec) elapsed = cur_duration_sec;
    if (elapsed == last_displayed_elapsed) return;  // avoid redrawing every 5ms for nothing
    last_displayed_elapsed = elapsed;

    char elapsed_buf[16], duration_buf[16], time_buf[36];
    formatTime(elapsed_buf, sizeof(elapsed_buf), elapsed);
    formatTime(duration_buf, sizeof(duration_buf), cur_duration_sec);
    snprintf(time_buf, sizeof(time_buf), "%s / %s", elapsed_buf, duration_buf);
    lv_label_set_text(label_elapsed, time_buf);
}

// Hides every registered screen and the player, then shows just the target and runs its
// on_show callback, if it has one. Also closes the dropdown menu, in case it was left open.
void showScreen(lv_obj_t* target) {
    lv_obj_add_flag(cont_player, LV_OBJ_FLAG_HIDDEN);
    void (*on_show)(void) = NULL;
    for (int i = 0; i < screen_count; i++) {
        lv_obj_add_flag(screens[i].obj, LV_OBJ_FLAG_HIDDEN);
        if (screens[i].obj == target) on_show = screens[i].on_show;
    }
    lv_obj_remove_flag(target, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(menu_list, LV_OBJ_FLAG_HIDDEN);
    if (on_show) on_show();
}

static void menu_open_cb(lv_event_t*) {
    // Screens are created after the menu, so bring it back above whichever one is showing.
    lv_obj_move_foreground(menu_list);
    lv_obj_remove_flag(menu_list, LV_OBJ_FLAG_HIDDEN);
}

// Just hides it: nothing underneath was ever switched away from, so there's nothing to restore
// (and no screen's on_show to re-fire, which is why this is an overlay and not a screen).
static void menu_close_cb(lv_event_t*) {
    lv_obj_add_flag(menu_list, LV_OBJ_FLAG_HIDDEN);
}

static void menu_item_cb(lv_event_t* e) {
    showScreen((lv_obj_t*)lv_event_get_user_data(e));
}

// One big tile in the menu grid, navigating to target when tapped. Used both for the Player entry
// and for every screen registered via addScreen(). Two columns of 146px fill the grid's 300px of
// usable width; four tiles fill its two rows, and any more just make it scroll. The two system
// actions sit in their own row below the grid (see addMenuAction()) so they stay last no matter
// how many screens get registered after them.
static void addMenuEntry(const char* name, lv_obj_t* target) {
    lv_obj_t* tile = lv_button_create(menu_grid);
    lv_obj_set_size(tile, 146, 56);
    set_btn_inactive_style(tile);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_add_event_cb(tile, menu_item_cb, LV_EVENT_CLICKED, target);
    lv_obj_t* tile_label = lv_label_create(tile);
    lv_obj_set_style_text_font(tile_label, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(tile_label, name);
    lv_obj_center(tile_label);
}

// --- Volumio system actions (restart / shut down), behind a confirmation ---
// Shutting the Pi down can't be undone from here (it has to be switched back on by hand), so
// neither action fires from the tile itself: the tile only opens a confirm dialog.

struct MenuAction { const char* title; const char* note; const char* confirm; bool (*fn)(); };
static const MenuAction restart_action = {
    "Restart Volumio?", "The device reboots and is unreachable for a minute or so.", "Restart", restartVolumio };
static const MenuAction shutdown_action = {
    "Shut down Volumio?", "It has to be switched back on by hand.", "Shut down", shutdownVolumio };

static void confirm_cancel_cb(lv_event_t*) {
    lv_obj_add_flag(confirm_layer, LV_OBJ_FLAG_HIDDEN);
}

static void confirm_ok_cb(lv_event_t*) {
    if (confirm_action && confirm_action()) {
        lv_obj_add_flag(confirm_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_list, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Not even sent: the websocket isn't connected. Say so and leave only Cancel.
        lv_label_set_text(confirm_title, "Volumio is unreachable");
        lv_label_set_text(confirm_note, "Nothing was sent.");
        lv_obj_add_flag(confirm_ok_btn, LV_OBJ_FLAG_HIDDEN);
    }
}

static void action_tile_cb(lv_event_t* e) {
    const MenuAction* a = (const MenuAction*)lv_event_get_user_data(e);
    confirm_action = a->fn;
    lv_label_set_text(confirm_title, a->title);
    lv_label_set_text(confirm_note, a->note);
    lv_label_set_text(confirm_ok_label, a->confirm);
    lv_obj_remove_flag(confirm_ok_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(confirm_layer);
    lv_obj_remove_flag(confirm_layer, LV_OBJ_FLAG_HIDDEN);
}

// A tile in the row along the bottom of the menu. Grey outline instead of the accent the
// navigation tiles use, so the destructive ones don't read as just another destination.
static void addMenuAction(const char* label, int x, const MenuAction* action) {
    lv_obj_t* tile = lv_button_create(menu_list);
    lv_obj_set_size(tile, 146, 56);
    lv_obj_set_pos(tile, x, 174);
    set_btn_inactive_style(tile);
    lv_obj_set_style_border_color(tile, lv_color_hex(0x555555), 0);
    lv_obj_set_style_radius(tile, 12, 0);
    lv_obj_add_event_cb(tile, action_tile_cb, LV_EVENT_CLICKED, (void*)action);
    lv_obj_t* tile_label = lv_label_create(tile);
    lv_obj_set_style_text_font(tile_label, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(tile_label, label);
    lv_obj_center(tile_label);
}

void setupUI() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    label_top = lv_label_create(scr);
    lv_obj_set_style_text_font(label_top, &lv_font_montserrat_ext_18, 0);
    lv_obj_set_style_text_color(label_top, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_top, LV_ALIGN_TOP_LEFT, 10, 5);

    // Elapsed / duration, top center, between the clock and the dropdown toggle. Lives in the
    // shared header (like the clock) rather than the player screen's own content, so it's
    // visible from Library/Queue too.
    label_elapsed = lv_label_create(scr);
    lv_obj_set_style_text_color(label_elapsed, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(label_elapsed, LV_ALIGN_TOP_MID, 0, 8);

    // Menu button, top right, level with the time. Always visible, on every screen, so you can
    // jump straight from e.g. Library to Queue without detouring through the player first.
    btn_menu = lv_button_create(scr);
    lv_obj_set_size(btn_menu, 46, 28);
    lv_obj_align(btn_menu, LV_ALIGN_TOP_RIGHT, -10, 4);
    set_btn_inactive_style(btn_menu);
    lv_obj_add_event_cb(btn_menu, menu_open_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* menu_icon = lv_label_create(btn_menu);
    lv_obj_set_style_text_font(menu_icon, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(menu_icon, LV_SYMBOL_LIST);
    lv_obj_center(menu_icon);

    // Menu: a full-screen overlay (covering the header too) with a title, a close button in the
    // same top-right spot as the button that opens it, and a grid of big tiles -- one per screen
    // registered with addScreen(), plus "Player" itself. Hidden until btn_menu is tapped.
    menu_list = lv_obj_create(scr);
    lv_obj_set_pos(menu_list, 0, 0);
    lv_obj_set_size(menu_list, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(menu_list, lv_color_hex(0x000000), 0);
    lv_obj_set_style_radius(menu_list, 0, 0);
    lv_obj_set_style_border_width(menu_list, 0, 0);
    lv_obj_set_style_pad_all(menu_list, 0, 0);
    lv_obj_remove_flag(menu_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(menu_list, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* menu_title = lv_label_create(menu_list);
    lv_obj_set_style_text_font(menu_title, &lv_font_montserrat_ext_18, 0);
    lv_obj_set_style_text_color(menu_title, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(menu_title, "Menu");
    lv_obj_align(menu_title, LV_ALIGN_TOP_LEFT, 10, 5);

    lv_obj_t* btn_close = lv_button_create(menu_list);
    lv_obj_set_size(btn_close, 46, 28);
    lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, -10, 4);
    set_btn_inactive_style(btn_close);
    lv_obj_add_event_cb(btn_close, menu_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* close_icon = lv_label_create(btn_close);
    lv_obj_set_style_text_font(close_icon, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
    lv_obj_center(close_icon);

    menu_grid = lv_obj_create(menu_list);
    lv_obj_set_pos(menu_grid, 0, TOP_BAR_H);
    lv_obj_set_size(menu_grid, SCREEN_WIDTH, 138);  // two rows of tiles; the actions row starts right below
    lv_obj_set_style_bg_opa(menu_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(menu_grid, 0, 0);
    lv_obj_set_style_border_width(menu_grid, 0, 0);
    lv_obj_set_style_pad_left(menu_grid, 10, 0);
    lv_obj_set_style_pad_right(menu_grid, 10, 0);
    lv_obj_set_style_pad_top(menu_grid, 6, 0);
    lv_obj_set_style_pad_bottom(menu_grid, 0, 0);
    lv_obj_set_style_pad_row(menu_grid, 8, 0);
    lv_obj_set_style_pad_column(menu_grid, 8, 0);
    lv_obj_set_scroll_dir(menu_grid, LV_DIR_VER);  // only matters once there are more than 4 tiles
    lv_obj_set_flex_flow(menu_grid, LV_FLEX_FLOW_ROW_WRAP);

    addMenuAction(LV_SYMBOL_REFRESH "  Restart", 10, &restart_action);
    addMenuAction(LV_SYMBOL_POWER "  Shut down", 164, &shutdown_action);

    // Confirm dialog, over the whole menu: a dimmed backdrop that also swallows taps on the tiles
    // behind it, and a centered panel with a title, a one-line consequence, Cancel and the action.
    confirm_layer = lv_obj_create(menu_list);
    lv_obj_set_pos(confirm_layer, 0, 0);
    lv_obj_set_size(confirm_layer, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(confirm_layer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(confirm_layer, LV_OPA_80, 0);
    lv_obj_set_style_radius(confirm_layer, 0, 0);
    lv_obj_set_style_border_width(confirm_layer, 0, 0);
    lv_obj_set_style_pad_all(confirm_layer, 0, 0);
    lv_obj_remove_flag(confirm_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(confirm_layer, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* panel = lv_obj_create(confirm_layer);
    lv_obj_set_size(panel, 270, 150);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    confirm_title = lv_label_create(panel);
    lv_obj_set_width(confirm_title, 240);
    lv_obj_set_style_text_align(confirm_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(confirm_title, &lv_font_montserrat_ext_18, 0);
    lv_obj_set_style_text_color(confirm_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(confirm_title, LV_ALIGN_TOP_MID, 0, 14);

    confirm_note = lv_label_create(panel);
    lv_obj_set_width(confirm_note, 240);
    lv_obj_set_style_text_align(confirm_note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(confirm_note, &lv_font_montserrat_ext_14, 0);
    lv_obj_set_style_text_color(confirm_note, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(confirm_note, LV_ALIGN_TOP_MID, 0, 46);

    lv_obj_t* btn_cancel = lv_button_create(panel);
    lv_obj_set_size(btn_cancel, 114, 38);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 12, -12);
    set_btn_inactive_style(btn_cancel);
    lv_obj_add_event_cb(btn_cancel, confirm_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* cancel_label = lv_label_create(btn_cancel);
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_ext_18, 0);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_center(cancel_label);

    confirm_ok_btn = lv_button_create(panel);
    lv_obj_set_size(confirm_ok_btn, 114, 38);
    lv_obj_align(confirm_ok_btn, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    set_btn_active_style(confirm_ok_btn);
    lv_obj_add_event_cb(confirm_ok_btn, confirm_ok_cb, LV_EVENT_CLICKED, NULL);
    confirm_ok_label = lv_label_create(confirm_ok_btn);
    lv_obj_set_style_text_font(confirm_ok_label, &lv_font_montserrat_ext_18, 0);
    lv_obj_center(confirm_ok_label);

    // Player screen. Unlike the pages from addScreen(), it's shown by default and has no
    // back button or menu entry of its own.
    cont_player = lv_obj_create(scr);
    lv_obj_set_pos(cont_player, 0, TOP_BAR_H);
    lv_obj_set_size(cont_player, SCREEN_WIDTH, SCREEN_HEIGHT - TOP_BAR_H);
    lv_obj_set_style_bg_color(cont_player, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_player, 0, 0);
    lv_obj_set_style_pad_all(cont_player, 0, 0);
    lv_obj_remove_flag(cont_player, LV_OBJ_FLAG_SCROLLABLE);

    // "Volumio is unreachable" notice. Covers the whole player area rather than sitting in a free
    // corner: once the connection has dropped, whatever controls/track info were showing are
    // stale and their buttons would silently do nothing, and a plain opaque cover also swallows
    // taps on them. The header (and so the dropdown) stays outside it. Hidden until
    // setVolumioOffline(true).
    cont_offline = lv_obj_create(cont_player);
    lv_obj_set_size(cont_offline, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(cont_offline, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(cont_offline, 0, 0);
    lv_obj_set_style_pad_all(cont_offline, 0, 0);
    lv_obj_remove_flag(cont_offline, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* offline_label = lv_label_create(cont_offline);
    lv_label_set_text(offline_label, LV_SYMBOL_WARNING "  Volumio is unreachable");
    lv_obj_set_style_text_font(offline_label, &lv_font_montserrat_ext_18, 0);
    lv_obj_set_style_text_color(offline_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_center(offline_label);
    lv_obj_add_flag(cont_offline, LV_OBJ_FLAG_HIDDEN);

    // Player's own dropdown entry, now that there's no dedicated Back button to return to it.
    addMenuEntry("Player", cont_player);
}

void setVolumioOffline(bool offline) {
    if (!cont_offline || offline == offline_shown) return;
    offline_shown = offline;
    if (offline) {
        // The player's own widgets get created after this one (on the first pushState), so on a
        // later drop they'd otherwise sit on top of it.
        lv_obj_move_foreground(cont_offline);
        lv_obj_remove_flag(cont_offline, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(cont_offline, LV_OBJ_FLAG_HIDDEN);
    }
}

void updateTime() {
    struct tm ti;
    if (getLocalTime(&ti) && ti.tm_year > 100) {
        char s[16]; strftime(s, sizeof(s), "%H:%M:%S", &ti);
        lv_label_set_text(label_top, s);
    }
}

static lv_obj_t* createScreen(void (*on_show)(void)) {
    // Plain content filling the space below the top bar, same geometry the player screen uses.
    lv_obj_t* screen = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(screen, 0, TOP_BAR_H);
    lv_obj_set_size(screen, SCREEN_WIDTH, SCREEN_HEIGHT - TOP_BAR_H);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_HIDDEN);

    if (screen_count < MAX_SCREENS) screens[screen_count++] = { screen, on_show };
    return screen;
}

lv_obj_t* addScreen(const char* name, void (*on_show)(void)) {
    // The dropdown (see addMenuEntry) is how every one of these is reached.
    lv_obj_t* screen = createScreen(on_show);
    addMenuEntry(name, screen);
    return screen;
}

lv_obj_t* addHiddenScreen(void (*on_show)(void)) {
    return createScreen(on_show);
}

void updateVolumioUI(const char* title, const char* artist, const char* album, bool airplay, bool isPlaying, bool shuffle, bool repeat, bool repeatSingle, int elapsedSec, int durationSec) {

    currentShuffle = shuffle;
    currentRepeat = repeat;
    currentRepeatSingle = repeatSingle;

    if (!label_title) {
        lv_obj_t* t1 = cont_player;

        // Track info, top of the screen, left-aligned (no album art box anymore -- see
        // CLAUDE.md item 8/17, tried and reverted twice).
        lv_obj_t* now_playing = lv_label_create(t1);
        lv_label_set_text(now_playing, "NOW PLAYING");
        lv_obj_set_style_text_font(now_playing, &lv_font_montserrat_ext_12, 0);
        lv_obj_set_style_text_color(now_playing, lv_color_hex(COLOR_ACCENT), 0);
        lv_obj_set_pos(now_playing, 8, 4);

        label_title = lv_label_create(t1);
        lv_label_set_long_mode(label_title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_width(label_title, 296);
        lv_obj_set_style_text_font(label_title, &lv_font_montserrat_ext_18, 0);
        lv_obj_set_style_text_color(label_title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_pos(label_title, 8, 20);

        label_artist = lv_label_create(t1);
        lv_label_set_long_mode(label_artist, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_width(label_artist, 296);
        lv_obj_set_style_text_font(label_artist, &lv_font_montserrat_ext_14, 0);
        lv_obj_set_style_text_color(label_artist, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_pos(label_artist, 8, 42);

        label_album = lv_label_create(t1);
        lv_label_set_long_mode(label_album, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_set_width(label_album, 296);
        lv_obj_set_style_text_font(label_album, &lv_font_montserrat_ext_12, 0);
        lv_obj_set_style_text_color(label_album, lv_color_hex(0x888888), 0);
        lv_obj_set_pos(label_album, 8, 60);

        // Transport row: shuffle, prev, play/pause (big, round), next, centered as a group.
        btn_shuffle = lv_button_create(t1);
        lv_obj_set_size(btn_shuffle, 34, 34);
        lv_obj_set_style_radius(btn_shuffle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(btn_shuffle, 31, 107);
        lv_obj_add_event_cb(btn_shuffle, shuffle_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_shuffle), LV_SYMBOL_SHUFFLE);
        lv_obj_center(lv_obj_get_child(btn_shuffle, 0));
        set_btn_inactive_style(btn_shuffle);

        btn_prev = lv_button_create(t1);
        lv_obj_set_size(btn_prev, 46, 46);
        lv_obj_set_style_radius(btn_prev, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(btn_prev, 75, 101);
        lv_obj_add_event_cb(btn_prev, prev_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_prev), LV_SYMBOL_PREV);
        lv_obj_center(lv_obj_get_child(btn_prev, 0));
        set_btn_active_style(btn_prev);
        set_btn_disabled_style(btn_prev);

        btn_play = lv_button_create(t1);
        lv_obj_set_size(btn_play, 64, 64);
        lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(btn_play, 129, 92);
        lv_obj_add_event_cb(btn_play, play_cb, LV_EVENT_CLICKED, NULL);
        btn_label = lv_label_create(btn_play);
        lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_ext_18, 0);
        lv_obj_center(btn_label);
        set_btn_active_style(btn_play);
        set_btn_disabled_style(btn_play);

        btn_next = lv_button_create(t1);
        lv_obj_set_size(btn_next, 46, 46);
        lv_obj_set_style_radius(btn_next, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(btn_next, 201, 101);
        lv_obj_add_event_cb(btn_next, next_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_next), LV_SYMBOL_NEXT);
        lv_obj_center(lv_obj_get_child(btn_next, 0));
        set_btn_active_style(btn_next);
        set_btn_disabled_style(btn_next);

        btn_repeat = lv_button_create(t1);
        lv_obj_set_size(btn_repeat, 34, 34);
        lv_obj_set_style_radius(btn_repeat, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(btn_repeat, 255, 107);
        lv_obj_add_event_cb(btn_repeat, repeat_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_repeat), LV_SYMBOL_REFRESH);
        lv_obj_center(lv_obj_get_child(btn_repeat, 0));
        // Style will be set dynamically below
        set_btn_inactive_style(btn_repeat);

        // Volume, bottom of the screen.
        lv_obj_t* vol_icon = lv_label_create(t1);
        lv_label_set_text(vol_icon, LV_SYMBOL_VOLUME_MAX);
        lv_obj_set_style_text_color(vol_icon, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_pos(vol_icon, 8, 173);

        slider_volume = lv_slider_create(t1);
        lv_obj_set_size(slider_volume, 246, 6);
        lv_obj_set_pos(slider_volume, 30, 177);
        lv_slider_set_range(slider_volume, 0, 100);
        lv_obj_add_event_cb(slider_volume, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

        label_volume = lv_label_create(t1);
        lv_obj_set_style_text_color(label_volume, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_pos(label_volume, 284, 173);
    }

    // On AirPlay Volumio sends empty title/artist/album, and there's nothing to control from here.
    player_airplay = airplay;
    lv_obj_t* transport[] = { btn_prev, btn_play, btn_next };
    for (lv_obj_t* b : transport) {
        if (airplay) lv_obj_add_state(b, LV_STATE_DISABLED);
        else lv_obj_remove_state(b, LV_STATE_DISABLED);
    }

    lv_label_set_text(label_title, title);
    lv_label_set_text(label_artist, airplay ? "Airplay" : artist);
    lv_label_set_text(label_album, album);
    lv_label_set_text(btn_label, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    base_elapsed_sec = elapsedSec;
    base_elapsed_at_ms = millis();
    cur_duration_sec = durationSec;
    cur_is_playing = isPlaying;
    refreshPlaybackClock();

    // Update Shuffle button state
    if (shuffle) {
        set_btn_active_style(btn_shuffle);
    } else {
        set_btn_inactive_style(btn_shuffle);
    }

    // Update Repeat button state
    lv_obj_t* repeat_label = lv_obj_get_child(btn_repeat, 0);
    if (repeatSingle) {
        lv_label_set_text(repeat_label, LV_SYMBOL_REFRESH " 1");
        set_btn_active_style(btn_repeat);
    } else if (repeat) {
        lv_label_set_text(repeat_label, LV_SYMBOL_REFRESH);
        set_btn_active_style(btn_repeat);
    } else {
        lv_label_set_text(repeat_label, LV_SYMBOL_REFRESH);
        set_btn_inactive_style(btn_repeat);
    }
}

void updateVolumeUI(int volume) {
    if (slider_volume) {
        lv_slider_set_value(slider_volume, volume, LV_ANIM_OFF);
        lv_label_set_text_fmt(label_volume, "%d", volume);
    }
}

void tickPlaybackClock() {
    refreshPlaybackClock();
}

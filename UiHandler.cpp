#include "UiHandler.h"
#include "DisplayConfig.h"
#include "VolumioHandler.h"
#include <time.h>

static lv_obj_t *label_top, *tabview, *label_title, *label_artist, *label_album;
static lv_obj_t *btn_play, *btn_label, *btn_prev, *btn_next, *btn_repeat, *btn_shuffle;
static lv_obj_t *slider_volume, *btn_mute;
static bool isMuted = false;
static bool currentRepeat = false;
static bool currentRepeatSingle = false;
static bool currentShuffle = false;


// Helper to set button style consistently
static void set_btn_green_style(lv_obj_t* btn) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1DB954), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x19A34A), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1DB954), LV_STATE_FOCUSED);
}

static void set_btn_default_style(lv_obj_t* btn) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x444444), LV_STATE_FOCUSED);
}

static void play_cb(lv_event_t * e) { togglePlayback(); }
static void prev_cb(lv_event_t * e) { prevTrack(); }
static void next_cb(lv_event_t * e) { nextTrack(); }

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

static void shuffle_cb(lv_event_t * e) { setShuffle(!currentShuffle); }

static void volume_cb(lv_event_t * e) { setVolume(lv_slider_get_value(lv_event_get_target(e))); }
static void mute_cb(lv_event_t * e) {
    if (isMuted) {
        unmute();
        isMuted = false;
        set_btn_green_style(btn_mute);
        lv_label_set_text(lv_obj_get_child(btn_mute, 0), LV_SYMBOL_VOLUME_MAX);
    } else {
        mute();
        isMuted = true;
        set_btn_default_style(btn_mute);
        lv_label_set_text(lv_obj_get_child(btn_mute, 0), LV_SYMBOL_MUTE);
    }
}

void setupUI() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    label_top = lv_label_create(scr);
    lv_obj_set_style_text_font(label_top, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label_top, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label_top, LV_ALIGN_TOP_LEFT, 10, 5);
    
    tabview = lv_tabview_create(scr, LV_DIR_TOP, 40);
    lv_obj_set_pos(tabview, 0, 30);
    lv_obj_set_size(tabview, SCREEN_WIDTH, SCREEN_HEIGHT - 30);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), 0);
    
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x000000), 0);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xAAAAAA), LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x444444), LV_PART_ITEMS | LV_STATE_CHECKED);
}

void updateTime() {
    struct tm ti;
    if (getLocalTime(&ti) && ti.tm_year > 100) {
        char s[64]; strftime(s, 64, "%A, %B %d  -  %H:%M:%S", &ti);
        lv_label_set_text(label_top, s);
    }
}

lv_obj_t* addTab(const char* name) {
    lv_obj_t* t = lv_tabview_add_tab(tabview, name);
    lv_obj_set_style_bg_color(t, lv_color_hex(0x000000), 0);
    return t;
}


void updateVolumioUI(const char* title, const char* artist, const char* album, bool isPlaying, bool repeat, bool shuffle, bool repeatSingle) {

    currentRepeat = repeat;
    currentRepeatSingle = repeatSingle;
    currentShuffle = shuffle;

    if (!label_title) {
        lv_obj_t* t1 = lv_obj_get_child(lv_tabview_get_content(tabview), 0);
        label_title = lv_label_create(t1);
        lv_label_set_long_mode(label_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(label_title, 280);
        lv_obj_set_style_text_font(label_title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(label_title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(label_title, LV_ALIGN_TOP_LEFT, 0, 0);
        
        label_artist = lv_label_create(t1);
        lv_label_set_long_mode(label_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(label_artist, 280);
        lv_obj_set_style_text_font(label_artist, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label_artist, lv_color_hex(0xCCCCCC), 0);
        lv_obj_align(label_artist, LV_ALIGN_TOP_LEFT, 0, 25);
        
        label_album = lv_label_create(t1);
        lv_label_set_long_mode(label_album, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_width(label_album, 280);
        lv_obj_set_style_text_font(label_album, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(label_album, lv_color_hex(0x888888), 0);
        lv_obj_align(label_album, LV_ALIGN_TOP_LEFT, 0, 45);

        btn_prev = lv_btn_create(t1);
        lv_obj_set_size(btn_prev, 40, 40);
        lv_obj_align(btn_prev, LV_ALIGN_TOP_LEFT, 0, 70);
        lv_obj_add_event_cb(btn_prev, prev_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_prev), LV_SYMBOL_PREV);
        lv_obj_center(lv_obj_get_child(btn_prev, 0));
        set_btn_green_style(btn_prev);

        btn_play = lv_btn_create(t1);
        lv_obj_set_size(btn_play, 40, 40);
        lv_obj_align_to(btn_play, btn_prev, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_play, play_cb, LV_EVENT_CLICKED, NULL);
        btn_label = lv_label_create(btn_play);
        lv_obj_center(btn_label);
        set_btn_green_style(btn_play);

        btn_next = lv_btn_create(t1);
        lv_obj_set_size(btn_next, 40, 40);
        lv_obj_align_to(btn_next, btn_play, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_next, next_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_next), LV_SYMBOL_NEXT);
        lv_obj_center(lv_obj_get_child(btn_next, 0));
        set_btn_green_style(btn_next);

        btn_repeat = lv_btn_create(t1);
        lv_obj_set_size(btn_repeat, 40, 40);
        lv_obj_align_to(btn_repeat, btn_next, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_repeat, repeat_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_repeat), LV_SYMBOL_REFRESH);
        lv_obj_center(lv_obj_get_child(btn_repeat, 0));
        // Style will be set dynamically below
        set_btn_green_style(btn_repeat);

        btn_shuffle = lv_btn_create(t1);
        lv_obj_set_size(btn_shuffle, 40, 40);
        lv_obj_align_to(btn_shuffle, btn_repeat, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_shuffle, shuffle_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_shuffle), LV_SYMBOL_SHUFFLE);
        lv_obj_center(lv_obj_get_child(btn_shuffle, 0));
        // Style will be set dynamically below
        set_btn_green_style(btn_shuffle);

        btn_mute = lv_btn_create(t1);
        lv_obj_set_size(btn_mute, 40, 40);
        lv_obj_align_to(btn_mute, btn_shuffle, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_mute, mute_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_mute), isMuted ? LV_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX);
        lv_obj_center(lv_obj_get_child(btn_mute, 0));
        set_btn_green_style(btn_mute);

        slider_volume = lv_slider_create(t1);
        lv_obj_set_size(slider_volume, 250, 15);
        lv_obj_align(slider_volume, LV_ALIGN_TOP_LEFT, 0, 130);
        lv_slider_set_range(slider_volume, 0, 100);
        lv_obj_add_event_cb(slider_volume, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
    
    lv_label_set_text(label_title, title);
    lv_label_set_text(label_artist, artist);
    lv_label_set_text(label_album, album);
    lv_label_set_text(btn_label, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    
    // Update Repeat button state
    lv_obj_t* repeat_label = lv_obj_get_child(btn_repeat, 0);
    if (repeatSingle) {
        lv_label_set_text(repeat_label, LV_SYMBOL_REFRESH " 1"); 
        set_btn_green_style(btn_repeat);
    } else if (repeat) {
        lv_label_set_text(repeat_label, LV_SYMBOL_REFRESH);
        set_btn_green_style(btn_repeat);
    } else {
        lv_label_set_text(repeat_label, LV_SYMBOL_REFRESH);
        set_btn_default_style(btn_repeat);
    }
    
    // Update Shuffle button state
    if (shuffle) {
        set_btn_green_style(btn_shuffle);
    } else {
        set_btn_default_style(btn_shuffle);
    }
}

void updateVolumeUI(int volume) {
    if (slider_volume) {
        lv_slider_set_value(slider_volume, volume, LV_ANIM_OFF);
    }
}

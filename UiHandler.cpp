#include "UiHandler.h"
#include "DisplayConfig.h"
#include "VolumioHandler.h"
#include <time.h>
static lv_obj_t *label_top, *tabview, *label_title, *label_artist, *label_album, *btn_play, *btn_label, *btn_prev, *btn_next, *slider_volume, *btn_mute;
static bool isMuted = false;
static void play_cb(lv_event_t * e) { togglePlayback(); }
static void prev_cb(lv_event_t * e) { prevTrack(); }
static void next_cb(lv_event_t * e) { nextTrack(); }
static void volume_cb(lv_event_t * e) { setVolume(lv_slider_get_value(lv_event_get_target(e))); }
static void mute_cb(lv_event_t * e) {
    if (isMuted) unmute();
    else mute();
    isMuted = !isMuted;
    lv_label_set_text(lv_obj_get_child(btn_mute, 0), isMuted ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_MUTE);
}
void setupUI() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0); // Black background
    
    label_top = lv_label_create(scr);
    lv_obj_set_style_text_font(label_top, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(label_top, lv_color_hex(0xFFFFFF), 0); // White text
    lv_obj_align(label_top, LV_ALIGN_TOP_LEFT, 10, 5);
    
    tabview = lv_tabview_create(scr, LV_DIR_TOP, 40);
    lv_obj_set_pos(tabview, 0, 30);
    lv_obj_set_size(tabview, SCREEN_WIDTH, SCREEN_HEIGHT - 30);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0x000000), 0); // Black tab content background
    
    // Set style for tab buttons
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x000000), 0); // Black tab background
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xAAAAAA), LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_btns, lv_color_hex(0xFFFFFF), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x444444), LV_PART_ITEMS | LV_STATE_CHECKED); // Selected tab color
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
void updateVolumioUI(const char* title, const char* artist, const char* album, bool isPlaying) {
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

        btn_play = lv_btn_create(t1);
        lv_obj_set_size(btn_play, 40, 40);
        lv_obj_align_to(btn_play, btn_prev, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_play, play_cb, LV_EVENT_CLICKED, NULL);
        btn_label = lv_label_create(btn_play);
        lv_obj_center(btn_label);

        btn_next = lv_btn_create(t1);
        lv_obj_set_size(btn_next, 40, 40);
        lv_obj_align_to(btn_next, btn_play, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(btn_next, next_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_next), LV_SYMBOL_NEXT);
        lv_obj_center(lv_obj_get_child(btn_next, 0));

        slider_volume = lv_slider_create(t1);
        lv_obj_set_size(slider_volume, 250, 15);
        lv_obj_align(slider_volume, LV_ALIGN_TOP_LEFT, 0, 130);

        lv_slider_set_range(slider_volume, 0, 100);
        lv_obj_add_event_cb(slider_volume, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
        btn_mute = lv_btn_create(t1);
        lv_obj_set_size(btn_mute, 30, 30);
        lv_obj_align_to(btn_mute, slider_volume, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
        lv_obj_add_event_cb(btn_mute, mute_cb, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_mute), LV_SYMBOL_MUTE);
        lv_obj_center(lv_obj_get_child(btn_mute, 0));
    }
    lv_label_set_text(label_title, title);
    lv_label_set_text(label_artist, artist);
    lv_label_set_text(label_album, album);
    lv_label_set_text(btn_label, isPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}
void updateVolumeUI(int volume) {
    if (slider_volume) {
        lv_slider_set_value(slider_volume, volume, LV_ANIM_OFF);
    }
}

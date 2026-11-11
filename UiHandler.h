#ifndef UI_HANDLER_H
#define UI_HANDLER_H
#include <lvgl.h>
void setupUI();
void updateTime();
// Creates a full-screen page below the header, with a tile in the menu. Returns its (empty)
// container.
//
// on_build(parent) runs when the page is shown after being hidden (and the first time): build its
// widgets into parent. on_show runs every time it's shown, after on_build: load its data.
// on_hide runs when navigating away: lv_obj_clean(parent) and null your lv_obj_t* pointers, so
// LVGL's small memory pool only ever holds the visible page. Keep small state (current page,
// folder) so on_show can rebuild the same view. With on_build == NULL the page is never rebuilt.
// icon is the tile's 32px icon and must be in lv_font_montserrat_ext_32's subset (CustomFonts.h).
lv_obj_t* addScreen(const char* name, const char* icon, void (*on_build)(lv_obj_t* parent) = NULL,
                     void (*on_show)(void) = NULL, void (*on_hide)(void) = NULL);
// Like addScreen(), but with no menu tile: a sub-page opened from another screen via showScreen().
lv_obj_t* addHiddenScreen(void (*on_build)(lv_obj_t* parent) = NULL,
                           void (*on_show)(void) = NULL, void (*on_hide)(void) = NULL);
// Adds the System tile (Update library / Restart / Shut down behind it) as the menu grid's last
// tile. Call exactly once, after every addScreen() the sketch registers.
void addSystemMenuActions();
// Switches to the given screen (any addScreen()/addHiddenScreen() result, or the player): runs
// the current screen's on_hide, the target's on_build/on_show, and closes the menu.
void showScreen(lv_obj_t* target);
// Back to the player, closing the menu, its confirm dialog and any long-press menu.
void showPlayer();
// airplay: Volumio's source is AirPlay -- play/prev/next are disabled and the artist reads "Airplay".
void updateVolumioUI(const char* title, const char* artist, const char* album, bool airplay, bool isPlaying, bool shuffle, bool repeat, bool repeatSingle, int elapsedSec, int durationSec);
void updateVolumeUI(int volume);
// Shows/hides the "Volumio is unreachable" notice covering the player screen. Cheap to call every
// loop() iteration -- it no-ops unless the state actually changes.
void setVolumioOffline(bool offline);
// Sets the text under the player's loader (shown until the first pushState).
void setLoadingStatus(const char* text);
// Redraws the elapsed-time label from the time since the last pushState. Call every loop()
// iteration; it no-ops unless the displayed second changed.
void tickPlaybackClock();
#endif

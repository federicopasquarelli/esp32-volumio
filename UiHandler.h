#ifndef UI_HANDLER_H
#define UI_HANDLER_H
#include <lvgl.h>
void setupUI();
void updateTime();
// Creates a full-screen page reachable from the top-right dropdown (which also always has a
// "Player" entry to get back). on_show, if given, runs each time the page is switched to (e.g.
// to refresh its data). Returns the page's content area, sized like the old tabs were.
lv_obj_t* addScreen(const char* name, void (*on_show)(void) = NULL);
// Like addScreen(), but with no dropdown entry: for a sub-page only reachable from another screen
// (via showScreen() below) rather than from the menu, e.g. a per-item detail view.
lv_obj_t* addHiddenScreen(void (*on_show)(void) = NULL);
// Switches to the given screen (any addScreen()/addHiddenScreen() result, or the player) the same
// way the dropdown does: hides the others, closes the dropdown, runs its on_show.
void showScreen(lv_obj_t* target);
// airplay: Volumio's source is AirPlay -- play/prev/next are disabled and the artist reads "Airplay".
void updateVolumioUI(const char* title, const char* artist, const char* album, bool airplay, bool isPlaying, bool shuffle, bool repeat, bool repeatSingle, int elapsedSec, int durationSec);
void updateVolumeUI(int volume);
// Shows/hides the "Volumio is unreachable" notice covering the player screen. Cheap to call every
// loop() iteration -- it no-ops unless the state actually changes.
void setVolumioOffline(bool offline);
// Redraws the elapsed-time label from real time elapsed since the last pushState. Call every
// loop() iteration, not on a timer (it no-ops unless the displayed second actually changed) —
// a no-op until the first pushState has arrived either way.
void tickPlaybackClock();
#endif

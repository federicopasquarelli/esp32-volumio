#ifndef UI_HANDLER_H
#define UI_HANDLER_H
#include <lvgl.h>
void setupUI();
void updateTime();
// Creates a full-screen page reachable from the top-right dropdown (which also always has a
// "Player" entry to get back). Returns the page's content area (empty until on_build runs),
// sized like the old tabs were.
//
// on_build(parent), if given, runs the first time this page is shown -- build its widget tree
// into `parent` here, instead of eagerly at setup() time, so a screen nobody visits never costs
// any of LVGL's tight static memory pool. on_show, if given, runs every time the page is shown
// (after on_build, the first time) -- (re)load its data here. on_hide, if given, runs when
// navigating away from this page: destroy what on_build created (lv_obj_clean(parent) + null out
// your own lv_obj_t* pointers) so the memory is freed until the page is shown again; keep any
// small logical state (current position, fetched data) so on_show can rebuild the same view
// instead of resetting to a default. Passing on_build == NULL keeps the old always-built
// behavior for a screen that doesn't need this.
lv_obj_t* addScreen(const char* name, void (*on_build)(lv_obj_t* parent) = NULL,
                     void (*on_show)(void) = NULL, void (*on_hide)(void) = NULL);
// Like addScreen(), but with no dropdown entry: for a sub-page only reachable from another screen
// (via showScreen() below) rather than from the menu, e.g. a per-item detail view.
lv_obj_t* addHiddenScreen(void (*on_build)(lv_obj_t* parent) = NULL,
                           void (*on_show)(void) = NULL, void (*on_hide)(void) = NULL);
// Adds the Restart/Shut down tiles to the end of the dropdown menu grid. Call exactly once, after
// every addScreen()/addHiddenScreen() the sketch will ever register, so these two land after all
// of them and have to be scrolled to like any other overflow tile, instead of sitting in their
// own fixed row below the grid.
void addSystemMenuActions();
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

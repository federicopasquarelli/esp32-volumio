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
// icon is the menu tile's 32px icon: it must be in lv_font_montserrat_ext_32's subset
// (CustomFonts.h).
lv_obj_t* addScreen(const char* name, const char* icon, void (*on_build)(lv_obj_t* parent) = NULL,
                     void (*on_show)(void) = NULL, void (*on_hide)(void) = NULL);
// Like addScreen(), but with no dropdown entry: for a sub-page only reachable from another screen
// (via showScreen() below) rather than from the menu, e.g. a per-item detail view.
lv_obj_t* addHiddenScreen(void (*on_build)(lv_obj_t* parent) = NULL,
                           void (*on_show)(void) = NULL, void (*on_hide)(void) = NULL);
// Adds the System tile (Update library / Restart / Shut down behind it) as the menu grid's last
// tile. Call exactly once, after every addScreen() the sketch registers.
void addSystemMenuActions();
// Switches to the given screen (any addScreen()/addHiddenScreen() result, or the player) the same
// way the dropdown does: hides the others, closes the dropdown, runs its on_show.
void showScreen(lv_obj_t* target);
// Back to the player, closing the menu, its confirm dialog and any long-press menu.
void showPlayer();
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

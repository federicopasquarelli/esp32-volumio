#ifndef UI_HANDLER_H
#define UI_HANDLER_H
#include <lvgl.h>
void setupUI();
void updateTime();
// Creates a full-screen page reachable from the top-right menu, with its own "Back" button
// that returns to the player screen. on_show, if given, runs each time the page is switched to
// (e.g. to refresh its data). Returns the page's content area, sized like the old tabs were.
lv_obj_t* addScreen(const char* name, void (*on_show)(void) = NULL);
void updateVolumioUI(const char* title, const char* artist, const char* album, bool isPlaying, bool shuffle, bool repeat, bool repeatSingle, int elapsedSec, int durationSec);
void updateVolumeUI(int volume);
// Ticks the elapsed-time label forward by however long it's been since the last pushState.
// Call once a second; a no-op until the first pushState has arrived.
void tickPlaybackClock();
#endif

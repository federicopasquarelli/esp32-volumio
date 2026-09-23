#ifndef UI_HANDLER_H
#define UI_HANDLER_H
#include <lvgl.h>
void setupUI();
void updateTime();
// Creates a full-screen page reachable from the top-right dropdown (which also always has a
// "Player" entry to get back). on_show, if given, runs each time the page is switched to (e.g.
// to refresh its data). Returns the page's content area, sized like the old tabs were.
lv_obj_t* addScreen(const char* name, void (*on_show)(void) = NULL);
void updateVolumioUI(const char* title, const char* artist, const char* album, bool isPlaying, bool shuffle, bool repeat, bool repeatSingle, int elapsedSec, int durationSec);
void updateVolumeUI(int volume);
// Redraws the elapsed-time label from real time elapsed since the last pushState. Call every
// loop() iteration, not on a timer (it no-ops unless the displayed second actually changed) —
// a no-op until the first pushState has arrived either way.
void tickPlaybackClock();
#endif

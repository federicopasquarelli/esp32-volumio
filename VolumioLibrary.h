#ifndef VOLUMIO_LIBRARY_H
#define VOLUMIO_LIBRARY_H

#include <lvgl.h>

// Registers the Library screen's container. Its widgets are no longer built here -- wire
// buildLibrary()/refreshLibrary()/hideLibrary() as addScreen()'s on_build/on_show/on_hide (see
// arduino32.ino) so they're built and fetched only the first time the screen is actually shown.
void setupLibrary(lv_obj_t* parent_tab);
// Builds the Library screen's widgets into parent. Wire as addScreen()'s on_build.
void buildLibrary(lv_obj_t* parent);
// Loads the root of the music library the first time ever this screen is shown, or re-fetches
// the remembered folder/page (to repopulate the just-rebuilt list) on a later visit. Wire as
// addScreen()'s on_show.
void refreshLibrary();
// Tears the Library screen's widgets back down and frees them from LVGL's memory pool -- the
// current folder/page are kept (tiny, not LVGL objects), so refreshLibrary() can rebuild the same
// view next time instead of resetting to the root. Wire as addScreen()'s on_hide.
void hideLibrary();
// Applies finished network results to the UI. Call from loop().
void loopLibrary();

#endif

#ifndef VOLUMIO_LIBRARY_H
#define VOLUMIO_LIBRARY_H

#include <lvgl.h>

// Builds the folder list inside the given tab.
void setupLibrary(lv_obj_t* parent_tab);
// Applies finished network results to the UI. Call from loop().
void loopLibrary();
// Loads the root of the music library. Call once WiFi is up.
void openLibraryRoot();

#endif

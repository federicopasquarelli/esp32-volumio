#ifndef VOLUMIO_LIBRARY_H
#define VOLUMIO_LIBRARY_H

#include <lvgl.h>

// Library browser. setupLibrary() takes addScreen()'s container; buildLibrary()/refreshLibrary()/
// hideLibrary() are its on_build/on_show/on_hide.
void setupLibrary(lv_obj_t* parent_tab);
void buildLibrary(lv_obj_t* parent);
// Opens the library root the first time, then the folder and page shown when it was last left.
void refreshLibrary();
// Frees the widgets; the current folder and page are kept.
void hideLibrary();
// Applies finished network results to the UI. Call from loop().
void loopLibrary();

#endif

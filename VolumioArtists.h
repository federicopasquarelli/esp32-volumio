#ifndef VOLUMIO_ARTISTS_H
#define VOLUMIO_ARTISTS_H

#include <lvgl.h>

// Builds the (currently empty) Artists screen's placeholder content into parent. Wire as
// addScreen()'s on_build (see arduino32.ino) -- no separate setup step is needed, this screen has
// no state of its own beyond its container.
void buildArtists(lv_obj_t* parent);
// Frees the placeholder label back to LVGL's pool. Wire as addScreen()'s on_hide.
void hideArtists();

#endif

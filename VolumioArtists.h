#ifndef VOLUMIO_ARTISTS_H
#define VOLUMIO_ARTISTS_H

#include <lvgl.h>

// Paginated list of every artist. buildArtists()/refreshArtists()/hideArtists() are addScreen()'s
// on_build/on_show/on_hide. Tap an artist for their tracks, long-press to play or queue them all.
void buildArtists(lv_obj_t* parent);
// Loads the page shown when the screen was last left (the first page the first time).
void refreshArtists();
// Frees the widgets; the current page is kept.
void hideArtists();
// Applies finished network results to the UI. Call from loop().
void loopArtists();

#endif

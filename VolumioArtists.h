#ifndef VOLUMIO_ARTISTS_H
#define VOLUMIO_ARTISTS_H

#include <lvgl.h>

// Builds the Artists screen's widgets into parent: a paginated list of every artist in Volumio's
// library. Browses "artists://" the same way VolumioLibrary.cpp browses "music-library" -- same
// /api/v1/browse endpoint, same offset/limit/count-based pagination (confirmed live: artists://
// supports it identically). Tapping an artist opens VolumioArtistTracks.cpp's screen (their track
// list). Lazy-built/freed like every other screen (see CLAUDE.md item 29) -- wire as addScreen()'s
// on_build.
void buildArtists(lv_obj_t* parent);
// Loads the remembered page (page 0 the first time ever) -- re-fetched fresh on every show,
// including a revisit after the widgets were torn down, so the list always reflects the current
// library. Wire as addScreen()'s on_show.
void refreshArtists();
// Tears the Artists screen's widgets back down and frees them from LVGL's memory pool. The
// current page is kept (tiny, not an LVGL object) so refreshArtists() rebuilds the same view next
// time instead of resetting to page 1. Wire as addScreen()'s on_hide.
void hideArtists();
// Applies finished network results to the UI. Call from loop().
void loopArtists();

#endif

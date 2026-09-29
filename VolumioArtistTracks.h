#ifndef VOLUMIO_ARTIST_TRACKS_H
#define VOLUMIO_ARTIST_TRACKS_H

#include <lvgl.h>

// Registers the Artists screen (to return to on Back) and the per-artist track list's container
// (a hidden screen, opened by tapping an artist -- see openArtistTracks()).
void setupArtistTracks(lv_obj_t* artistsScreen, lv_obj_t* tracksScreen);
// Builds this screen's widgets into parent. Lazy-built/freed like every other screen (see
// CLAUDE.md item 29). Wire as addHiddenScreen()'s on_build.
void buildArtistTracks(lv_obj_t* parent);
// Loads the current artist's tracks (page 0, or the remembered page if this exact screen instance
// was hidden and reopened without a new openArtistTracks() call in between). Wire as
// addHiddenScreen()'s on_show.
void refreshArtistTracks();
// Tears this screen's widgets back down and frees them from LVGL's memory pool. Wire as
// addHiddenScreen()'s on_hide.
void hideArtistTracks();
// Applies finished network results to the UI. Call from loop().
void loopArtistTracks();
// Opens this screen for the given artist. uri must be exactly what Volumio returned for that
// artist in the Artists list (VolumioArtists.cpp) -- already percent-encoded where Volumio itself
// encodes it (e.g. spaces as %20), so it's used as-is in the browse request rather than
// re-encoded (confirmed live: re-encoding an already-encoded uri double-encodes it and breaks the
// request). name is shown as the screen's header. Always starts at page 0, since opening this is
// a fresh navigation to whichever artist was just tapped, not a return to a remembered position.
void openArtistTracks(const char* uri, const char* name);

#endif

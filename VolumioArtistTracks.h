#ifndef VOLUMIO_ARTIST_TRACKS_H
#define VOLUMIO_ARTIST_TRACKS_H

#include <lvgl.h>

// Registers the Artists screen (to return to on Back) and the per-artist track list's container
// (a hidden screen, opened by tapping an artist -- see openArtistTracks()).
void setupArtistTracks(lv_obj_t* artistsScreen, lv_obj_t* tracksScreen);
// addHiddenScreen()'s on_build/on_show/on_hide.
void buildArtistTracks(lv_obj_t* parent);
void refreshArtistTracks();
void hideArtistTracks();
// Applies finished network results to the UI. Call from loop().
void loopArtistTracks();
// Opens this screen on page 1 for the given artist. uri must be exactly what the Artists list
// returned: Volumio already percent-encodes it (spaces as %20), so encoding it again breaks the
// request. name is shown in the header.
void openArtistTracks(const char* uri, const char* name);

#endif

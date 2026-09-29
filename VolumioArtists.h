#ifndef VOLUMIO_ARTISTS_H
#define VOLUMIO_ARTISTS_H

#include <lvgl.h>

// Builds the (currently empty) Artists screen inside the given container. Reachable from the
// dropdown menu like Library/Queue/Lights, via addScreen().
void setupArtists(lv_obj_t* parent);

#endif

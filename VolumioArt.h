#ifndef VOLUMIO_ART_H
#define VOLUMIO_ART_H

#include <lvgl.h>

// Creates a static placeholder box in place of real album art. Real-artwork fetching was tried
// and reverted — see git history / conversation notes if picking this back up: Volumio's
// /tinyart endpoint (local files) and its plugin CDN URLs (Spotify etc, via HTTPS + a JPEG
// decoder) both worked in isolation, but reserving the buffers they needed left too little heap
// for WiFi's own connection-time allocations, and broke the device's ability to connect to WiFi
// at all. Needs a real fix to that memory budget before trying again, not just retried as-is.
void setupAlbumArt(lv_obj_t* parent, int x, int y, int w, int h);

#endif

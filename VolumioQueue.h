#ifndef VOLUMIO_QUEUE_H
#define VOLUMIO_QUEUE_H

#include <lvgl.h>

// Registers the Queue screen's container. Its widgets are no longer built here -- wire
// buildQueue()/refreshQueue()/hideQueue() as addScreen()'s on_build/on_show/on_hide (see
// arduino32.ino) so they're built and fetched only the first time the screen is actually shown.
void setupQueue(lv_obj_t* parent_tab);
// Builds the Queue screen's widgets into parent. Wire as addScreen()'s on_build.
void buildQueue(lv_obj_t* parent);
// Applies finished network results to the UI. Call from loop().
void loopQueue();
// Reloads the current queue page from Volumio. Wire as addScreen()'s on_show.
void refreshQueue();
// Tears the Queue screen's widgets back down and frees them from LVGL's memory pool. Wire as
// addScreen()'s on_hide.
void hideQueue();

#endif

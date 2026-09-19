#ifndef VOLUMIO_QUEUE_H
#define VOLUMIO_QUEUE_H

#include <lvgl.h>

// Builds the queue list inside the given tab. The queue reloads whenever the tab is shown.
void setupQueue(lv_obj_t* parent_tab);
// Applies finished network results to the UI. Call from loop().
void loopQueue();
// Reloads the queue from Volumio. Call once WiFi is up.
void refreshQueue();

#endif

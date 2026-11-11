#ifndef VOLUMIO_QUEUE_H
#define VOLUMIO_QUEUE_H

#include <lvgl.h>

// Queue viewer. setupQueue() takes addScreen()'s container; buildQueue()/refreshQueue()/
// hideQueue() are its on_build/on_show/on_hide.
void setupQueue(lv_obj_t* parent_tab);
void buildQueue(lv_obj_t* parent);
// Reloads the current page.
void refreshQueue();
void hideQueue();
// Applies finished network results to the UI. Call from loop().
void loopQueue();

#endif

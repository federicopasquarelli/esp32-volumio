#ifndef TUYA_LIGHTS_H
#define TUYA_LIGHTS_H

#include <lvgl.h>

// Builds the lights screen inside the given container, reached from the dropdown menu.
void setupTuyaLights(lv_obj_t* parent);
// Applies finished network results to the UI. Call from loop().
void loopTuyaLights();
// Authenticates against the Tuya Cloud API (if we don't already hold a live token). Runs on a
// background task, wired as the screen's on_show callback so it fires every time you open it.
void refreshTuyaLights();

#endif

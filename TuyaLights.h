#ifndef TUYA_LIGHTS_H
#define TUYA_LIGHTS_H

#include <lvgl.h>

// Builds the lights screen inside the given container, reached from the dropdown menu.
void setupTuyaLights(lv_obj_t* parent);
// Applies finished network results to the UI. Call from loop().
void loopTuyaLights();
// Reloads the device list (and each one's on/off state) from the Tuya Cloud API. Runs on a
// background task, wired as the screen's on_show callback so it fires every time you open it.
void refreshTuyaLights();
// Pre-authenticates against the Tuya Cloud API in the background, so the first time the Lights
// screen is opened it only pays for the device list/status calls, not a login too. Call once,
// after WiFi/NTP are up. Safe to call even if the Lights screen is opened before this finishes --
// that just queues the device list refresh to run right after.
void startTuyaAuth();

#endif
